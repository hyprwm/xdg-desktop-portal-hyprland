#include "GlobalShortcuts.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"

// wayland

static void handleActivated(void* data, hyprland_global_shortcut_v1* hyprland_global_shortcut_v1, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    const auto PKEYBIND = (SKeybind*)data;

    g_pPortalManager->m_sPortals.globalShortcuts->onActivated(PKEYBIND, ((uint64_t)tv_sec_hi << 32) | (uint64_t)(tv_sec_lo));
}

static void handleDeactivated(void* data, hyprland_global_shortcut_v1* hyprland_global_shortcut_v1, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    const auto PKEYBIND = (SKeybind*)data;

    g_pPortalManager->m_sPortals.globalShortcuts->onDeactivated(PKEYBIND, ((uint64_t)tv_sec_hi << 32) | (uint64_t)(tv_sec_lo));
}

static const hyprland_global_shortcut_v1_listener shortcutListener = {
    .pressed  = handleActivated,
    .released = handleDeactivated,
};

//

CGlobalShortcutsPortal::SSession* CGlobalShortcutsPortal::getSession(sdbus::ObjectPath& path) {
    for (auto& s : m_vSessions) {
        if (s->sessionHandle == path)
            return s.get();
    }

    return nullptr;
}

void CGlobalShortcutsPortal::onCreateSession(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle, sessionHandle;

    call >> requestHandle;
    call >> sessionHandle;

    std::string appID;
    call >> appID;

    Debug::log(LOG, "[globalshortcuts] New session:");
    Debug::log(LOG, "[globalshortcuts]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[globalshortcuts]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[globalshortcuts]  | appid: {}", appID);

    const auto PSESSION = m_vSessions.emplace_back(std::make_unique<SSession>(appID, requestHandle, sessionHandle)).get();

    // create objects
    PSESSION->session            = createDBusSession(sessionHandle);
    PSESSION->session->onDestroy = [PSESSION]() { PSESSION->session.release(); };
    PSESSION->request            = createDBusRequest(requestHandle);
    PSESSION->request->onDestroy = [PSESSION]() { PSESSION->request.release(); };

    std::unordered_map<std::string, sdbus::Variant> opts;
    call >> opts;

    for (auto& [k, v] : opts) {
        if (k == "shortcuts") {
            PSESSION->registered = true;

            std::vector<sdbus::Struct<std::string, std::unordered_map<std::string, sdbus::Variant>>> shortcuts;

            shortcuts = v.get<std::vector<sdbus::Struct<std::string, std::unordered_map<std::string, sdbus::Variant>>>>();

            for (auto& s : shortcuts) {
                const auto PSHORTCUT = PSESSION->keybinds.emplace_back(std::make_unique<SKeybind>()).get();

                PSHORTCUT->id = s.get<0>();

                std::unordered_map<std::string, sdbus::Variant> data = s.get<1>();

                for (auto& [k, v] : data) {
                    if (k == "description") {
                        PSHORTCUT->description = v.get<std::string>();
                    } else {
                        Debug::log(LOG, "[globalshortcuts] unknown shortcut data type {}", k);
                    }
                }

                PSHORTCUT->shortcut =
                    hyprland_global_shortcuts_manager_v1_register_shortcut(m_sState.manager, PSHORTCUT->id.c_str(), PSESSION->appid.c_str(), PSHORTCUT->description.c_str(), "");
                hyprland_global_shortcut_v1_add_listener(PSHORTCUT->shortcut, &shortcutListener, PSHORTCUT);

                PSHORTCUT->session = PSESSION;
            }

            Debug::log(LOG, "[globalshortcuts] registered {} shortcuts", shortcuts.size());
        }
    }

    auto reply = call.createReply();
    reply << (uint32_t)0;
    reply << std::unordered_map<std::string, sdbus::Variant>{};
    reply.send();
}

void CGlobalShortcutsPortal::onBindShortcuts(sdbus::MethodCall& call) {
    sdbus::ObjectPath sessionHandle, requestHandle;
    call >> requestHandle;
    call >> sessionHandle;

    Debug::log(LOG, "[globalshortcuts] Bind keys:");
    Debug::log(LOG, "[globalshortcuts]  | {}", sessionHandle.c_str());

    std::vector<sdbus::Struct<std::string, std::unordered_map<std::string, sdbus::Variant>>> shortcuts;
    std::vector<sdbus::Struct<std::string, std::unordered_map<std::string, sdbus::Variant>>> shortcutsToReturn;
    call >> shortcuts;

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[globalshortcuts] No session?");
        return;
    }

    PSESSION->registered = true;

    for (auto& s : shortcuts) {
        const auto PSHORTCUT = PSESSION->keybinds.emplace_back(std::make_unique<SKeybind>()).get();

        PSHORTCUT->id = s.get<0>();

        std::unordered_map<std::string, sdbus::Variant> data = s.get<1>();

        for (auto& [k, v] : data) {
            if (k == "description") {
                PSHORTCUT->description = v.get<std::string>();
            } else {
                Debug::log(LOG, "[globalshortcuts] unknown shortcut data type {}", k);
            }
        }

        PSHORTCUT->shortcut =
            hyprland_global_shortcuts_manager_v1_register_shortcut(m_sState.manager, PSHORTCUT->id.c_str(), PSESSION->appid.c_str(), PSHORTCUT->description.c_str(), "");
        hyprland_global_shortcut_v1_add_listener(PSHORTCUT->shortcut, &shortcutListener, PSHORTCUT);

        PSHORTCUT->session = PSESSION;

        std::unordered_map<std::string, sdbus::Variant> shortcutData;
        shortcutData["description"]         = PSHORTCUT->description;
        shortcutData["trigger_description"] = "";
        shortcutsToReturn.push_back({PSHORTCUT->id, shortcutData});
    }

    Debug::log(LOG, "[globalshortcuts] registered {} shortcuts", shortcuts.size());

    auto                                            reply = call.createReply();

    std::unordered_map<std::string, sdbus::Variant> data;
    data["shortcuts"] = shortcutsToReturn;

    reply << (uint32_t)0;
    reply << data;
    reply.send();
}

void CGlobalShortcutsPortal::onListShortcuts(sdbus::MethodCall& call) {
    sdbus::ObjectPath sessionHandle, requestHandle;
    call >> requestHandle;
    call >> sessionHandle;

    Debug::log(LOG, "[globalshortcuts] List keys:");
    Debug::log(LOG, "[globalshortcuts]  | {}", sessionHandle.c_str());

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[globalshortcuts] No session?");
        return;
    }

    std::vector<sdbus::Struct<std::string, std::unordered_map<std::string, sdbus::Variant>>> shortcuts;

    for (auto& s : PSESSION->keybinds) {
        std::unordered_map<std::string, sdbus::Variant> opts;
        opts["description"]         = s->description;
        opts["trigger_description"] = "";
        shortcuts.push_back({s->id, opts});
    }

    auto                                            reply = call.createReply();

    std::unordered_map<std::string, sdbus::Variant> data;
    data["shortcuts"] = shortcuts;

    reply << (uint32_t)0;
    reply << data;
    reply.send();
}

CGlobalShortcutsPortal::CGlobalShortcutsPortal(hyprland_global_shortcuts_manager_v1* mgr) {
    m_sState.manager = mgr;

    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject->registerMethod(INTERFACE_NAME, "CreateSession", "oosa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onCreateSession(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "BindShortcuts", "ooa(sa{sv})sa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onBindShortcuts(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "ListShortcuts", "oo", "ua{sv}", [&](sdbus::MethodCall c) { onListShortcuts(c); });
    m_pObject->registerSignal(INTERFACE_NAME, "Activated", "osta{sv}");
    m_pObject->registerSignal(INTERFACE_NAME, "Deactivated", "osta{sv}");
    m_pObject->registerSignal(INTERFACE_NAME, "ShortcutsChanged", "oa(sa{sv})");

    m_pObject->finishRegistration();

    Debug::log(LOG, "[globalshortcuts] registered");
}

void CGlobalShortcutsPortal::onActivated(SKeybind* pKeybind, uint64_t time) {
    const auto PSESSION = (CGlobalShortcutsPortal::SSession*)pKeybind->session;

    Debug::log(TRACE, "[gs] Session {} called activated on {}", (void*)PSESSION, pKeybind->id);

    auto signal = m_pObject->createSignal(INTERFACE_NAME, "Activated");
    signal << PSESSION->sessionHandle;
    signal << pKeybind->id;
    signal << time;
    signal << std::unordered_map<std::string, sdbus::Variant>{};

    m_pObject->emitSignal(signal);
}

void CGlobalShortcutsPortal::onDeactivated(SKeybind* pKeybind, uint64_t time) {
    const auto PSESSION = (CGlobalShortcutsPortal::SSession*)pKeybind->session;

    Debug::log(TRACE, "[gs] Session {} called deactivated on {}", (void*)PSESSION, pKeybind->id);

    auto signal = m_pObject->createSignal(INTERFACE_NAME, "Deactivated");
    signal << PSESSION->sessionHandle;
    signal << pKeybind->id;
    signal << time;
    signal << std::unordered_map<std::string, sdbus::Variant>{};

    m_pObject->emitSignal(signal);
}