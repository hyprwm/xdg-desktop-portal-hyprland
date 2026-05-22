#include "Element.hpp"
#include "cli/Logger.hpp"
#include <cstdint>
#include <functional>
#include <hyprtoolkit/types/FontTypes.hpp>
#include <hyprtoolkit/types/SizeType.hpp>
#include <hyprtoolkit/core/LogTypes.hpp>
#include <pixman-1/pixman.h>
#include <hyprtoolkit/core/CoreMacros.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprtoolkit/core/Backend.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <string>
#include <string_view>


using namespace Hyprutils::Memory;
using namespace Hyprutils::Math;
using namespace Hyprtoolkit;

#define SP CSharedPointer
#define WP CWeakPointer
#define UP CUniquePointer

static SP<IBackend> backend;
static SP<CColumnLayoutElement> sourcesLayout;
Hyprutils::CLI::CLogger g_logger;

enum ESourceGroups {
    MONITOR = 0,
    WINDOW,
    REGION,
    WORKSPACE,
    UNKNOWN,
};

std::string enumToString(ESourceGroups g) {
    switch (g) {
        case ESourceGroups::MONITOR   : return "Monitor";
        case ESourceGroups::WINDOW    : return "Window";
        case ESourceGroups::REGION    : return "Region";
        case ESourceGroups::WORKSPACE : return "Workspace";
        default                       : return "Unknown";
    }
}

struct SWindowEntry {
    std::string        name;
    std::string        clazz;
    unsigned long long id = 0;
};

struct SWorkspaceEntry {
    std::string name;
    int64_t id;
};

struct SMonitorEntry {
    std::string name;
    int64_t width;
    int64_t height;
};

struct SRegion {
    int64_t x;
    int64_t y;
    int64_t size_x;
    int64_t size_y;
};

std::vector<SWorkspaceEntry> getWorkspaces(const char* env) {
    std::vector<SWorkspaceEntry> result;

    if (!env)
        return result;

    std::string rolling = env;
    while(!rolling.empty()) {
        const auto IDSEPPOS = rolling.find("[WI>]");
        const auto IDSTR    = rolling.substr(0, IDSEPPOS);

        const auto NAMESEPPOS = rolling.find("[WN>]");
        const auto NAMESTR    = rolling.substr(IDSEPPOS + 5, NAMESEPPOS - IDSEPPOS - 5);

        try {
            result.push_back({NAMESTR, std::stoll(IDSTR)});
        } catch (std::exception& e) {
            // silent err
        }

        rolling = rolling.substr(NAMESEPPOS + 5);
    }

    return result;
}

std::vector<SWindowEntry> getWindows(const char* env) {
    std::vector<SWindowEntry> result;

    if (!env)
        return result;

    std::string rolling = env;

    while (!rolling.empty()) {
        // ID
        const auto IDSEPPOS = rolling.find("[HC>]");
        const auto IDSTR    = rolling.substr(0, IDSEPPOS);

        // class
        const auto CLASSSEPPOS = rolling.find("[HT>]");
        const auto CLASSSTR    = rolling.substr(IDSEPPOS + 5, CLASSSEPPOS - IDSEPPOS - 5);

        // title
        const auto TITLESEPPOS = rolling.find("[HE>]");
        const auto TITLESTR    = rolling.substr(CLASSSEPPOS + 5, TITLESEPPOS - 5 - CLASSSEPPOS);

        // window address
        const auto WINDOWSEPPOS = rolling.find("[HA>]");
        const auto WINDOWADDR = rolling.substr(TITLESEPPOS + 5, WINDOWSEPPOS - 5 - TITLESEPPOS);

        try {
            result.push_back({TITLESTR, CLASSSTR, std::stoull(IDSTR)});
        } catch (std::exception& e) {
            // silent err
        }

        rolling = rolling.substr(WINDOWSEPPOS + 5);
    }

    return result;
}

ESourceGroups currentTabEnum = WINDOW;
std::vector<SMonitorEntry>   monitors;
std::vector<SWindowEntry>    windows;
std::vector<SWorkspaceEntry> workspaces;

static void changeTab(ESourceGroups sourceGroup, bool forceRefresh = false)  {
    if(sourceGroup == UNKNOWN || !sourcesLayout)
        return;

    currentTabEnum = sourceGroup;
    switch(currentTabEnum) {
        case MONITOR:
            if(!monitors.empty())
                return;

            //get list of monitors and commence sourcesTab
            return;

        case WINDOW: {
            if(!windows.empty() && !forceRefresh)
                return;

            sourcesLayout->clearChildren();

            const char*  WINDOWLISTSTR = getenv("XDPH_WINDOW_SHARING_LIST");
            if(!WINDOWLISTSTR) {
                g_logger.log(Hyprutils::CLI::LOG_ERR, "No Windows");
                return;
            }


            windows = getWindows(WINDOWLISTSTR);

            for(auto& w : windows) {
                auto entry = CButtonBuilder::begin()
                    ->label(std::string(w.name))
                    ->onMainClick([](SP<CButtonElement> el){ /**TODO: onSelecSource**/ })
                    ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.99f, 60.f} })
                    ->fontSize(CFontSize::HT_FONT_SMALL)
                    ->commence();

                sourcesLayout->addChild(entry);
            }
            return;
        }

        case WORKSPACE: {
            if(!workspaces.empty() && !forceRefresh)
                return;

            sourcesLayout->clearChildren();

            const char*  WORKSPACELISTSTR = getenv("XDPH_WORKSPACE_SHARING_LIST");
            if(!WORKSPACELISTSTR) {
                g_logger.log(Hyprutils::CLI::LOG_ERR, "No Windows");
                return;
            }

            workspaces = getWorkspaces(WORKSPACELISTSTR);
            for(auto& w : workspaces) {
                g_logger.log(Hyprutils::CLI::LOG_ERR, std::string_view(w.name));
                auto entry = CButtonBuilder::begin()
                    ->label(std::string(w.name))
                    ->onMainClick([](SP<CButtonElement> el){ /**TODO: onSelecSource**/ })
                    ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.99f, 60.f} })
                    ->fontSize(CFontSize::HT_FONT_SMALL)
                    ->commence();

                sourcesLayout->addChild(entry);
            }
            return;
        }

        default: 
            return;
    }
}

static void refreshTabs() {
    switch(currentTabEnum) {
        case MONITOR:
            monitors.clear();
            break;
        case WINDOW:
            windows.clear();
            break;
        case REGION:
            break; 
        case WORKSPACE:
            workspaces.clear();
            break;
        default:
            break;
    }
    changeTab(currentTabEnum, true);
}

static void onSelectSource(std::string sourceString) {
}


SP<IWindow> windowLayout() {
    auto window = CWindowBuilder::begin() ->preferredSize({720, 650})
        ->minSize({650, 600})
        ->appTitle("Select what to share")
        ->appClass("hyprland-share-picker")
        ->commence();

    auto rect1 = CRectangleBuilder::begin()
        ->color([] { return CHyprColor{0x547A95FF}; })
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, Vector2D{1.f, 0.08f} })
        ->rounding(5)
        ->commence();

    auto rect2 = CRectangleBuilder::begin()
        ->color([] { return CHyprColor{0x547A95FF}; })
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, Vector2D{1.f, 0.5f} })
        ->rounding(5)
        ->commence();
    rect2->setGrow(true);

    // background
    window->m_rootElement->addChild( CRectangleBuilder::begin() ->color([] { return CHyprColor{0xE8EDF2FF}; }) ->commence());

    // main layout
    auto mainLayout = CColumnLayoutBuilder::begin()
        ->gap(5)
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, Vector2D{1.f, 1.f} })
        ->commence();
    mainLayout->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
    mainLayout->setPositionFlag(IElement::HT_POSITION_FLAG_VCENTER, true);

    auto tabButtons = CRowLayoutBuilder::begin()
        ->gap(5)
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, Vector2D{1.f, 1.f} })
        ->commence();

    tabButtons->setPositionMode(IElement::HT_POSITION_ABSOLUTE);
    tabButtons->setPositionFlag(IElement::HT_POSITION_FLAG_VCENTER, true);
    tabButtons->setMargin(5.f);

    for (int sourceGroup = ESourceGroups::MONITOR; sourceGroup != ESourceGroups::UNKNOWN; sourceGroup++) {
        auto tabButton = CButtonBuilder::begin()
            ->label(enumToString(static_cast<ESourceGroups>(sourceGroup)))
            ->onMainClick([sourceGroup](SP<CButtonElement> el){ changeTab(static_cast<ESourceGroups>(sourceGroup)); })
            ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.18f, 0.85f} })
            ->fontSize(CFontSize::HT_FONT_SMALL)
            ->commence();
        tabButtons->addChild(tabButton);
    }

    // Sources layout
    auto scroll = CScrollAreaBuilder::begin()->scrollY(true)->size({CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.f, 1.f}})->commence();
    auto sourceButtons = CColumnLayoutBuilder::begin()
        ->gap(5)
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.f, 0.f} })
        ->commence();
    sourceButtons->setMargin(5.f);
    sourcesLayout = sourceButtons;

    window->m_rootElement->addChild(mainLayout);

    mainLayout->setMargin(10.f);
    mainLayout->addChild(rect1);
    mainLayout->addChild(rect2);
    rect1->addChild(tabButtons);
    rect2->addChild(scroll);
    scroll->addChild(sourceButtons);

    return window;
}

int main(int argc, char** argv, char** envp) {
    backend = IBackend::create();

    auto window = windowLayout();
    refreshTabs();

    window->open();

    backend->enterLoop();

    window->close();
}
