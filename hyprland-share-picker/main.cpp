#include "Picker.hpp"
#include "PickerData.hpp"

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/window/Window.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace Hyprtoolkit;
using namespace Hyprutils::Memory;

static int runRemoteDesktopConsent(const CSharedPointer<IBackend>& backend) {
    const auto WINDOW = CWindowBuilder::begin()
                            ->type(HT_WINDOW_TOPLEVEL)
                            ->preferredSize({600, 240})
                            ->minSize({500, 240})
                            ->appTitle("Allow remote control?")
                            ->appClass("hyprland-share-picker")
                            ->commence();
    if (!WINDOW) {
        std::cerr << "[XDPH_PICKER_ERROR] Failed to create the consent window\n";
        backend->destroy();
        return 1;
    }

    bool       authorized = false;
    const auto CLOSE      = [&] {
        WINDOW->close();
        backend->destroy();
    };
    WINDOW->m_events.closeRequest.listenStatic(CLOSE);

    const auto FULL_SIZE  = [] { return CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}}; };
    const auto BACKGROUND = CRectangleBuilder::begin()->color([backend] { return backend->getPalette()->m_colors.background; })->size(FULL_SIZE())->commence();
    WINDOW->m_rootElement->addChild(BACKGROUND);
    const auto LAYOUT = CColumnLayoutBuilder::begin()->size(FULL_SIZE())->gap(16)->commence();
    LAYOUT->setMargin(20);
    BACKGROUND->addChild(LAYOUT);

    const char* APPID = std::getenv("XDPH_REMOTE_DESKTOP_APP_ID");
    const auto  APP   = escapeMarkup(APPID && *APPID ? APPID : "An application");
    LAYOUT->addChild(
        CTextBuilder::begin()->text(APP + " wants to control your pointer and keyboard.")->color([backend] { return backend->getPalette()->m_colors.text; })->commence());

    const auto ACTIONS = CRowLayoutBuilder::begin()->gap(8)->commence();
    ACTIONS->addChild(CButtonBuilder::begin()->label("Deny")->onMainClick([&](CSharedPointer<CButtonElement>) { CLOSE(); })->commence());
    ACTIONS->addChild(CButtonBuilder::begin()
                          ->label("Allow")
                          ->onMainClick([&](CSharedPointer<CButtonElement>) {
                              authorized = true;
                              CLOSE();
                          })
                          ->commence());
    LAYOUT->addChild(ACTIONS);

    WINDOW->open();
    backend->enterLoop();
    if (!authorized)
        return 1;

    std::cout << "[AUTHORIZED]\n";
    return 0;
}

int main(int argc, char** argv) {
    setenv("HT_QUIET", "1", 0);

    bool allowTokenByDefault = false;
    bool remoteDesktop       = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--allow-token")
            allowTokenByDefault = true;
        else if (std::string_view{argv[i]} == "--remote-desktop")
            remoteDesktop = true;
    }

    const auto BACKEND = Hyprtoolkit::IBackend::create();
    if (!BACKEND) {
        std::cerr << "[XDPH_PICKER_ERROR] Failed to initialize Hyprtoolkit\n";
        return 1;
    }

    BACKEND->setLogFn([](Hyprtoolkit::eLogLevel, const std::string& message) { std::cerr << "[hyprtoolkit] " << message << '\n'; });
    if (remoteDesktop)
        return runRemoteDesktopConsent(BACKEND);

    const auto WINDOW_LIST       = std::getenv("XDPH_WINDOW_SHARING_LIST");
    const auto OUTPUT_LIST       = std::getenv("XDPH_OUTPUT_SHARING_LIST");
    const auto WORKSPACE_LIST    = std::getenv("XDPH_WORKSPACE_SHARING_LIST_V2");
    const auto WORKSPACE_LIST_V3 = std::getenv("XDPH_WORKSPACE_SHARING_LIST_V3");

    CPicker    picker(BACKEND, parseOutputList(OUTPUT_LIST ? OUTPUT_LIST : ""), parseWindowList(WINDOW_LIST ? WINDOW_LIST : ""),
                      WORKSPACE_LIST_V3 ? parseWorkspaceListV3(WORKSPACE_LIST_V3) : parseWorkspaceList(WORKSPACE_LIST ? WORKSPACE_LIST : ""), allowTokenByDefault);
    if (!picker.initialize()) {
        std::cerr << "[XDPH_PICKER_ERROR] Failed to create the picker window\n";
        BACKEND->destroy();
        return 1;
    }

    picker.run();
    return 0;
}
