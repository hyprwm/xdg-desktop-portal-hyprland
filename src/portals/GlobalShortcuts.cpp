#include "GlobalShortcuts.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"

SKeybind::SKeybind(SP<CCHyprlandGlobalShortcutV1> shortcut_) : shortcut(shortcut_) {
    shortcut->setPressed([this](CCHyprlandGlobalShortcutV1* r, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
        g_pPortalManager->m_sPortals.globalShortcuts->onActivated(this, ((uint64_t)tv_sec_hi << 32) | (uint64_t)(tv_sec_lo));
    });
    shortcut->setReleased([this](CCHyprlandGlobalShortcutV1* r, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
        g_pPortalManager->m_sPortals.globalShortcuts->onDeactivated(this, ((uint64_t)tv_sec_hi << 32) | (uint64_t)(tv_sec_lo));
    });
}

//

CGlobalShortcutsPortal::SSession* CGlobalShortcutsPortal::getSession(sdbus::ObjectPath& path) {
    for (auto& s : m_vSessions) {
        if (s->sessionHandle == path)
            return s.get();
    }

    return nullptr;
}

SKeybind* CGlobalShortcutsPortal::getShortcutById(const std::string& appID, const std::string& shortcutId) {
    for (auto& s : m_vSessions) {
        if (s->appid != appID)
            continue;

        for (auto& keybind : s->keybinds) {
            if (keybind->id == shortcutId)
                return keybind.get();
        }
    }

    return nullptr;
}

SKeybind* CGlobalShortcutsPortal::registerShortcut(SSession* session, const DBusShortcut& shortcut) {
    std::string                                     id   = shortcut.get<0>();
    std::unordered_map<std::string, sdbus::Variant> data = shortcut.get<1>();
    std::string                                     description;

    for (auto& [k, v] : data) {
        if (k == "description")
            description = v.get<std::string>();
        else
            Debug::log(LOG, "[globalshortcuts] unknown shortcut data type {}", k);
    }

    auto* PSHORTCUT = getShortcutById(session->appid, id);
    if (PSHORTCUT)
        Debug::log(WARN, "[globalshortcuts] shortcut {} already registered for appid {}", id, session->appid);
    else {
        PSHORTCUT = session->keybinds
                        .emplace_back(std::make_unique<SKeybind>(
                            makeShared<CCHyprlandGlobalShortcutV1>(m_sState.manager->sendRegisterShortcut(id.c_str(), session->appid.c_str(), description.c_str(), ""))))
                        .get();
    }

    PSHORTCUT->id          = std::move(id);
    PSHORTCUT->description = std::move(description);
    PSHORTCUT->session     = session;

    return PSHORTCUT;
}

dbUasv CGlobalShortcutsPortal::onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                               std::unordered_map<std::string, sdbus::Variant> opts) {
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

    for (auto& [k, v] : opts) {
        if (k == "shortcuts") {
            PSESSION->registered = true;

            std::vector<DBusShortcut> shortcuts = v.get<std::vector<DBusShortcut>>();

            for (auto& s : shortcuts) {
                registerShortcut(PSESSION, s);
            }

            Debug::log(LOG, "[globalshortcuts] registered {} shortcuts", shortcuts.size());
        }
    }

    return {0, {}};
}

dbUasv CGlobalShortcutsPortal::onBindShortcuts(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::vector<DBusShortcut> shortcuts, std::string appID,
                                               std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[globalshortcuts] Bind keys:");
    Debug::log(LOG, "[globalshortcuts]  | {}", sessionHandle.c_str());

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[globalshortcuts] No session?");
        return {1, {}};
    }

    std::vector<DBusShortcut> shortcutsToReturn;

    PSESSION->registered = true;

    for (auto& s : shortcuts) {
        const auto*                                     PSHORTCUT = registerShortcut(PSESSION, s);

        std::unordered_map<std::string, sdbus::Variant> shortcutData;
        shortcutData["description"]         = sdbus::Variant{PSHORTCUT->description};
        shortcutData["trigger_description"] = sdbus::Variant{""};
        shortcutsToReturn.push_back({PSHORTCUT->id, shortcutData});
    }

    Debug::log(LOG, "[globalshortcuts] registered {} shortcuts", shortcuts.size());

    std::unordered_map<std::string, sdbus::Variant> data;
    data["shortcuts"] = sdbus::Variant{shortcutsToReturn};

    return {0, data};
}

dbUasv CGlobalShortcutsPortal::onListShortcuts(sdbus::ObjectPath sessionHandle, sdbus::ObjectPath requestHandle) {
    Debug::log(LOG, "[globalshortcuts] List keys:");
    Debug::log(LOG, "[globalshortcuts]  | {}", sessionHandle.c_str());

    const auto PSESSION = getSession(sessionHandle);

    if (!PSESSION) {
        Debug::log(ERR, "[globalshortcuts] No session?");
        return {1, {}};
    }

    std::vector<DBusShortcut> shortcuts;

    for (auto& s : PSESSION->keybinds) {
        std::unordered_map<std::string, sdbus::Variant> opts;
        opts["description"]         = sdbus::Variant{s->description};
        opts["trigger_description"] = sdbus::Variant{""};
        shortcuts.push_back({s->id, opts});
    }

    std::unordered_map<std::string, sdbus::Variant> data;
    data["shortcuts"] = sdbus::Variant{shortcuts};

    return {0, data};
}

CGlobalShortcutsPortal::CGlobalShortcutsPortal(SP<CCHyprlandGlobalShortcutsManagerV1> mgr) {
    m_sState.manager = mgr;

    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject
        ->addVTable(sdbus::registerMethod("CreateSession")
                        .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                            return onCreateSession(o1, o2, s, m);
                        }),
                    sdbus::registerMethod("BindShortcuts")
                        .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::vector<DBusShortcut> v1, std::string s1,
                                              std::unordered_map<std::string, sdbus::Variant> m2) { return onBindShortcuts(o1, o2, v1, s1, m2); }),
                    sdbus::registerMethod("ListShortcuts").implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2) { return onListShortcuts(o1, o2); }),
                    sdbus::registerSignal("Activated").withParameters<sdbus::ObjectPath, std::string, uint64_t, std::unordered_map<std::string, sdbus::Variant>>(),
                    sdbus::registerSignal("Deactivated").withParameters<sdbus::ObjectPath, std::string, uint64_t, std::unordered_map<std::string, sdbus::Variant>>(),
                    sdbus::registerSignal("ShortcutsChanged").withParameters<sdbus::ObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, sdbus::Variant>>>())
        .forInterface(INTERFACE_NAME);

    Debug::log(LOG, "[globalshortcuts] registered");
}

void CGlobalShortcutsPortal::onActivated(SKeybind* pKeybind, uint64_t time) {
    const auto PSESSION = (CGlobalShortcutsPortal::SSession*)pKeybind->session;

    Debug::log(TRACE, "[gs] Session {} called activated on {}", PSESSION->sessionHandle.c_str(), pKeybind->id);

    m_pObject->emitSignal("Activated").onInterface(INTERFACE_NAME).withArguments(PSESSION->sessionHandle, pKeybind->id, time, std::unordered_map<std::string, sdbus::Variant>{});
}

void CGlobalShortcutsPortal::onDeactivated(SKeybind* pKeybind, uint64_t time) {
    const auto PSESSION = (CGlobalShortcutsPortal::SSession*)pKeybind->session;

    Debug::log(TRACE, "[gs] Session {} called deactivated on {}", PSESSION->sessionHandle.c_str(), pKeybind->id);

    m_pObject->emitSignal("Deactivated").onInterface(INTERFACE_NAME).withArguments(PSESSION->sessionHandle, pKeybind->id, time, std::unordered_map<std::string, sdbus::Variant>{});
}