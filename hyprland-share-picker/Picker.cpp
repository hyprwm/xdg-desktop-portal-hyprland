#include "Picker.hpp"

#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/types/SizeType.hpp>
#include <hyprtoolkit/types/FontTypes.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <hyprutils/os/Process.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace Hyprutils::Math;
using namespace Hyprutils::OS;
using namespace Hyprtoolkit;

namespace {

    constexpr const char* GEOMETRY_PATH = "/tmp/hypr/hyprland-share-picker.conf";

    std::string           execAndGet(const std::string& cmd) {
        CProcess proc("/bin/sh", {"-c", cmd});
        if (!proc.runSync())
            return {};
        return proc.stdOut();
    }

    std::string tabLabel(uint8_t t) {
        switch (t) {
            case 0: return "Screens";
            case 1: return "Windows";
            case 2: return "Region";
        }
        return "?";
    }

}

CPicker::CPicker(bool allowTokenByDefault) : m_restoreToken(allowTokenByDefault) {
    m_backend = IBackend::create();
}

CPicker::~CPicker() {
    if (m_window)
        m_window->close();
}

int CPicker::run() {
    if (!m_backend) {
        std::cerr << "[picker] failed to create hyprtoolkit backend\n";
        return 1;
    }

    loadGeometry();
    build();

    if (!m_window) {
        std::cerr << "[picker] failed to create window\n";
        return 1;
    }

    switchTab(TAB_SCREENS);
    m_window->open();
    m_backend->enterLoop();
    return 0;
}

void CPicker::loadGeometry() {
    std::ifstream f(GEOMETRY_PATH);
    if (!f.good())
        return;

    std::string line;
    while (std::getline(f, line)) {
        const auto EQ = line.find('=');
        if (EQ == std::string::npos)
            continue;
        const auto KEY = line.substr(0, EQ);
        const auto VAL = line.substr(EQ + 1);
        try {
            if (KEY == "width") {
                const auto W = std::stoi(VAL);
                if (W >= 480 && W <= 4096)
                    m_logicalSize.x = W;
            } else if (KEY == "height") {
                const auto H = std::stoi(VAL);
                if (H >= 360 && H <= 4096)
                    m_logicalSize.y = H;
            }
        } catch (...) { /* skip */
        }
    }
}

void CPicker::saveGeometry() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(GEOMETRY_PATH).parent_path(), ec);

    // atomic write: dump to a tmpfile next to the target, then rename. avoids
    // half-written files if the process dies mid-write.
    const std::string TMP = std::string{GEOMETRY_PATH} + ".tmp";
    {
        std::ofstream f(TMP, std::ios::trunc);
        if (!f.good())
            return;
        f << "width=" << static_cast<int>(m_logicalSize.x) << "\n";
        f << "height=" << static_cast<int>(m_logicalSize.y) << "\n";
    }
    fs::rename(TMP, GEOMETRY_PATH, ec);
    if (ec)
        fs::remove(TMP, ec);
}

void CPicker::build() {
    m_window = CWindowBuilder::begin()->preferredSize(m_logicalSize)->minSize({480, 360})->appTitle("Select what to share")->appClass("hyprland-share-picker")->commence();

    m_window->m_rootElement->addChild(CRectangleBuilder::begin()
                                          ->color([bk = m_backend] { return bk->getPalette()->m_colors.background; })
                                          ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})
                                          ->commence());

    // tab row: anchored to top
    m_tabRow = CRowLayoutBuilder::begin()->gap(6)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.96F, 40.F}})->commence();
    m_tabRow->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
    m_tabRow->setPositionFlag(IElement::HT_POSITION_FLAG_TOP, true);
    m_tabRow->setPositionFlag(IElement::HT_POSITION_FLAG_HCENTER, true);
    m_tabRow->setMargin(8);
    m_window->m_rootElement->addChild(m_tabRow);
    buildTabBar(m_tabRow);

    // footer: anchored to bottom
    auto footerBg = CRectangleBuilder::begin()
                        ->color([bk = m_backend] { return bk->getPalette()->m_colors.alternateBase; })
                        ->rounding(8)
                        ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.96F, 40.F}})
                        ->commence();
    footerBg->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
    footerBg->setPositionFlag(IElement::HT_POSITION_FLAG_BOTTOM, true);
    footerBg->setPositionFlag(IElement::HT_POSITION_FLAG_HCENTER, true);
    footerBg->setMargin(8);
    m_window->m_rootElement->addChild(footerBg);

    auto footer = CRowLayoutBuilder::begin()->gap(8)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->commence();
    footer->setMargin(8);
    footerBg->addChild(footer);

    // source list between
    auto scrollBg = CRectangleBuilder::begin()
                        ->color([bk = m_backend] { return bk->getPalette()->m_colors.base; })
                        ->rounding(8)
                        ->borderColor([bk = m_backend] { return bk->getPalette()->m_colors.alternateBase; })
                        ->borderThickness(1)
                        ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.96F, 0.78F}})
                        ->commence();
    scrollBg->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
    scrollBg->setPositionFlag(IElement::HT_POSITION_FLAG_CENTER, true);
    m_window->m_rootElement->addChild(scrollBg);

    auto scroll = CScrollAreaBuilder::begin()->scrollY(true)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})->commence();
    scrollBg->addChild(scroll);

    m_sourcesList = CColumnLayoutBuilder::begin()->gap(6)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.F, 0.F}})->commence();
    m_sourcesList->setMargin(8);
    scroll->addChild(m_sourcesList);

    m_restoreCheckbox = CCheckboxBuilder::begin()
                            ->toggled(m_restoreToken)
                            ->onToggled([this](SP<CCheckboxElement>, bool state) { m_restoreToken = state; })
                            ->size({CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {20.F, 1.F}})
                            ->commence();
    footer->addChild(m_restoreCheckbox);

    auto restoreLabel = CTextBuilder::begin()
                            ->text("Allow restore session")
                            ->color([bk = m_backend] { return bk->getPalette()->m_colors.text; })
                            ->fontSize({CFontSize::HT_FONT_TEXT})
                            ->size({CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_PERCENT, {1.F, 1.F}})
                            ->commence();
    footer->addChild(restoreLabel);

    // events
    m_resizeListener = m_window->m_events.resized.listen([this](Vector2D size) { m_logicalSize = size; });
    m_closeListener  = m_window->m_events.closeRequest.listen([this] { quit(); });
}

void CPicker::buildTabBar(const SP<IElement>& parent) {
    parent->clearChildren();
    for (uint8_t t = TAB_SCREENS; t <= TAB_REGION; ++t) {
        const bool ACTIVE = (t == m_activeTab);
        auto       btn    = CButtonBuilder::begin()
                                ->label(tabLabel(t))
                                ->accent(ACTIVE)
                                ->onMainClick([this, t](SP<CButtonElement>) { switchTab(static_cast<eSourceTab>(t)); })
                                ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.F / 3.F - 0.01F, 1.F}})
                                ->fontSize({CFontSize::HT_FONT_TEXT})
                                ->commence();
        parent->addChild(btn);
    }
}

void CPicker::switchTab(eSourceTab tab) {
    m_activeTab = tab;
    buildTabBar(m_tabRow);
    m_sourcesList->clearChildren();
    switch (tab) {
        case TAB_SCREENS: populateScreens(); break;
        case TAB_WINDOWS: populateWindows(); break;
        case TAB_REGION: populateRegion(); break;
    }
}

void CPicker::populateScreens() {
    m_monitors = Picker::parseMonitors(execAndGet("hyprctl monitors"));

    if (m_monitors.empty()) {
        auto t = CTextBuilder::begin()->text("No screens available.")->color([bk = m_backend] { return bk->getPalette()->m_colors.text; })->commence();
        m_sourcesList->addChild(t);
        return;
    }

    for (const auto& m : m_monitors) {
        const auto LABEL = m.name + " (" + std::to_string(m.width) + "x" + std::to_string(m.height) + ")";
        const auto NAME  = m.name;
        auto       btn   = CButtonBuilder::begin()
                               ->label(std::string{LABEL})
                               ->onMainClick([this, NAME](SP<CButtonElement>) { emitAndQuit("screen", NAME); })
                               ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 40.F}})
                               ->fontSize({CFontSize::HT_FONT_TEXT})
                               ->commence();
        m_sourcesList->addChild(btn);
    }
}

void CPicker::populateWindows() {
    const char* ENV     = getenv("XDPH_WINDOW_SHARING_LIST");
    const auto  WINDOWS = Picker::parseWindowList(ENV ? std::string{ENV} : std::string{});

    if (WINDOWS.empty()) {
        auto t = CTextBuilder::begin()->text("No shareable windows.")->color([bk = m_backend] { return bk->getPalette()->m_colors.text; })->commence();
        m_sourcesList->addChild(t);
        return;
    }

    for (const auto& w : WINDOWS) {
        const auto LABEL = w.clazz + ": " + w.title;
        const auto ID    = std::to_string(w.id);
        auto       btn   = CButtonBuilder::begin()
                               ->label(std::string{LABEL})
                               ->onMainClick([this, ID](SP<CButtonElement>) { emitAndQuit("window", ID); })
                               ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 40.F}})
                               ->fontSize({CFontSize::HT_FONT_TEXT})
                               ->commence();
        m_sourcesList->addChild(btn);
    }
}

void CPicker::populateRegion() {
    auto btn = CButtonBuilder::begin()
                   ->label("Select Region with slurp")
                   ->accent(true)
                   ->onMainClick([this](SP<CButtonElement>) {
                       // populate monitors first so we can convert global coords to local
                       if (m_monitors.empty())
                           m_monitors = Picker::parseMonitors(execAndGet("hyprctl monitors"));

                       // hide the picker so it doesn't overlap slurp
                       if (m_window)
                           m_window->close();

                       const auto OUT = execAndGet("slurp -f \"%o %x %y %w %h\"");
                       if (OUT.empty()) {
                           if (m_window)
                               m_window->open();
                           return;
                       }

                       const auto DETAIL = Picker::parseRegion(OUT, m_monitors);
                       if (!DETAIL) {
                           if (m_window)
                               m_window->open();
                           return;
                       }

                       emitAndQuit("region", *DETAIL);
                   })
                   ->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 60.F}})
                   ->fontSize({CFontSize::HT_FONT_TEXT})
                   ->commence();
    m_sourcesList->addChild(btn);
}

void CPicker::emitAndQuit(const std::string& kind, const std::string& detail) {
    const std::string FLAGS = m_restoreToken ? "r" : "";
    std::cout << "[SELECTION]" << FLAGS << "/" << kind << ":" << detail << "\n" << std::flush;
    saveGeometry();
    quit();
}

void CPicker::quit() {
    // idempotent: safe to call multiple times (close-request, signal, selection emit)
    if (m_window) {
        m_window->close();
        m_window.reset();
    }
    if (m_backend) {
        m_backend->destroy();
        m_backend.reset();
    }
}
