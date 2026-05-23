#include "Element.hpp"
#include "cli/Logger.hpp"
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <hyprtoolkit/types/FontTypes.hpp>
#include <hyprtoolkit/types/SizeType.hpp>
#include <hyprtoolkit/core/LogTypes.hpp>
#include <hyprtoolkit/core/Output.hpp>
#include <iostream>
#include <pixman-1/pixman.h>
#include <hyprtoolkit/core/CoreMacros.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprtoolkit/core/Backend.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Checkbox.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprutils/os/Process.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <vector>


using namespace Hyprutils::Memory;
using namespace Hyprutils::Math;
using namespace Hyprutils::OS;
using namespace Hyprtoolkit;

#define SP CSharedPointer
#define WP CWeakPointer
#define UP CUniquePointer

static SP<IBackend> backend;
static SP<IWindow> picker;

Hyprutils::CLI::CLogger g_logger;

std::string execAndGet(const char* cmd) {
    std::string command = cmd + std::string{" 2>&1"};
    CProcess proc("/bin/sh", {"-c", cmd});

    if (!proc.runSync())
        return "error";

    return proc.stdOut();
}

enum ESourceGroups {
    MONITOR = 0,
    WINDOW,
    WORKSPACE,
    REGION,
    UNKNOWN,
};

std::string enumToString(ESourceGroups g) {
    switch (g) {
        case ESourceGroups::MONITOR   : return "screen"; 
        case ESourceGroups::WINDOW    : return "window";
        case ESourceGroups::REGION    : return "region";
        case ESourceGroups::WORKSPACE : return "workspace";
        default                       : return "unknown";
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
    int64_t id;
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
};

struct SRegion {
    std::string monitor;
    int64_t x;
    int64_t y;
    int64_t w;
    int64_t h;
};

//globals
static SP<CColumnLayoutElement> sourcesLayout;
//static SP<CCheckboxElement> restoreToken;
bool restoreToken = false;
ESourceGroups currentTabEnum = MONITOR;
std::vector<SMonitorEntry>   monitors;
std::vector<SWindowEntry>    windows;
std::vector<SWorkspaceEntry> workspaces;

static std::vector<SMonitorEntry> getMonitors(const std::string& MONITORLISTSTR) {
    std::vector<SMonitorEntry> result;

    std::stringstream stream(MONITORLISTSTR);
    std::string line;

    while(std::getline(stream, line)) {
        if(line.starts_with("Monitor ")) {
            SMonitorEntry newEntry;

            const auto NAME_START = std::string("Monitor ").length();
            const auto NAME_END   = line.find(" (");
            newEntry.name = line.substr(NAME_START, NAME_END - NAME_START);


            const auto ID_BEGIN = line.find("(ID ");
            const auto ID_END   = line.find("):");
            newEntry.id = std::stoll(line.substr(ID_BEGIN + 4, ID_END - (ID_BEGIN + 4) ));

            //Next line for width + height
            std::getline(stream, line);
            static const std::regex pattern( R"(\s*(\d+)x(\d+)@([\d.]+)\s+at\s+(-?\d+)x(-?\d+))");
            std::smatch match;

            if (!std::regex_match(line, match, pattern)) {
                return result;
            }

            newEntry.width = std::stoll(match[1]);
            newEntry.height = std::stoll(match[2]);
            newEntry.x = std::stoll(match[4]);
            newEntry.y = std::stoll(match[5]);

            result.push_back(newEntry);
        }
    }

    return result;
}

std::string getRegionDetail(std::string& REGIONSTR) {

    SRegion region;
    std::string result; 

    REGIONSTR.erase( std::remove(REGIONSTR.begin(), REGIONSTR.end(), '\n'), REGIONSTR.end());

    std::stringstream stream(REGIONSTR);
    std::string screenName;

    int64_t globalX = 0;
    int64_t globalY = 0;
    stream >> screenName >> globalX >> globalY >> region.w >> region.h;

    if(screenName.empty()) {
        g_logger.log( Hyprutils::CLI::LOG_ERR, "Failed parsing slurp output");
        return result;
    }

    auto monitor = std::find_if( monitors.begin(), monitors.end(), [&](const auto& m) { return m.name == screenName; });

    if(monitor == monitors.end()) {
        g_logger.log( Hyprutils::CLI::LOG_ERR, std::format( "Monitor {} not found", screenName));
        return result;
    }

    // Convert global coords -> monitor-local coords
    region.x = globalX - monitor->x;
    region.y = globalY - monitor->y;

    region.monitor = screenName;

    g_logger.log( Hyprutils::CLI::LOG_DEBUG, std::format( "getRegion return region:{}@{},{},{},{}", region.monitor, region.x, region.y, region.w, region.h));

    result = std::format( "{}@{},{},{},{}", region.monitor, region.x, region.y, region.w, region.h);
    return result;
}


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

static void onSelectSource(const std::string& detail) {
    //The libhyprtoolkit.so has not expose the CCheckboxElement::state() yet
    //const std::string restoreSession = restoreToken->state() ? "r" : "";
    const std::string restoreSession = restoreToken ? "r" : "";
    const std::string selection = std::format("[SELECTION]{}/{}:{}\n", restoreSession, enumToString(currentTabEnum), detail);
    std::cout << selection;
    picker->close();
    backend->destroy();
}

static void changeTab(ESourceGroups sourceGroup, bool forceRefresh = false)  {
    if(sourceGroup == UNKNOWN || !sourcesLayout)
        return;

    currentTabEnum = sourceGroup;
    switch(currentTabEnum) {
        case MONITOR: {
            if(!monitors.empty() && !forceRefresh)
                return;

            sourcesLayout->clearChildren();

            std::string MONITORLISTSTR = execAndGet("hyprctl monitors");

            if(MONITORLISTSTR.empty()) {
                g_logger.log(Hyprutils::CLI::LOG_WARN, "NO MONITOR FOUND");
                return;
            }

            //we can use the backend->getOutputs() but IOutput does not expose size,..
            monitors = getMonitors(MONITORLISTSTR);
            
            for(auto& m : monitors) {
                auto detail = m.name;
                auto entry = CButtonBuilder::begin()
                    ->label(std::format( "{} (ID {}): {}x{}", m.name, m.id, m.width, m.height))
                    ->onMainClick([detail](SP<CButtonElement> el){ onSelectSource(detail); })
                    ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.99f, 60.f} })
                    ->fontSize(CFontSize::HT_FONT_SMALL)
                    ->commence();

                sourcesLayout->addChild(entry);
            }
            return;
        }

        case REGION: {
            sourcesLayout->clearChildren();
            auto entry = CButtonBuilder::begin()
                    ->label("Select Region")
                    ->onMainClick([](SP<CButtonElement> el){
                        auto REGIONSTR = execAndGet("slurp -f \"%o %x %y %w %h\"");
                        std::string regionDetail = getRegionDetail(REGIONSTR);

                        if(REGIONSTR.empty())
                            return;

                        onSelectSource(regionDetail); 
                    })
                    ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.99f, 60.f} })
                    ->fontSize(CFontSize::HT_FONT_SMALL)
                    ->commence();

                sourcesLayout->addChild(entry);

            return;
        }    

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
                auto detail = std::to_string(w.id);
                auto entry = CButtonBuilder::begin()
                    ->label(std::string(w.name))
                    ->onMainClick([detail](SP<CButtonElement> el){ onSelectSource(detail); })
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
                auto detail = w.name;
                auto entry = CButtonBuilder::begin()
                    ->label(std::string(w.name))
                    ->onMainClick([detail](SP<CButtonElement> el){ onSelectSource(detail); })
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

SP<IWindow> windowLayout() {
    auto window = CWindowBuilder::begin() ->preferredSize({720, 650})
        ->minSize({650, 600})
        ->appTitle("Select what to share")
        ->appClass("hyprland-share-picker")
        ->commence();

    auto rect1 = CRectangleBuilder::begin()
        ->color([] { return CHyprColor{0xFF1f898c}; })
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, Vector2D{1.f, 0.08f} })
        ->rounding(5)
        ->commence();

    auto rect2 = CRectangleBuilder::begin()
        ->color([] { return CHyprColor{0xFF1f898c}; })
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, Vector2D{1.f, 0.5f} })
        ->rounding(5)
        ->commence();
    rect2->setGrow(true);

    // background
    window->m_rootElement->addChild( CRectangleBuilder::begin() ->color([] { return CHyprColor{0xFFd3edee}; }) ->commence());

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
            ->onMainClick([sourceGroup](SP<CButtonElement> el){ changeTab(static_cast<ESourceGroups>(sourceGroup), true); })
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

    auto checkBoxesLayout = CRowLayoutBuilder::begin()
        ->gap(5)
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.f, 20.f} })
        ->commence();

    auto restoreTokenCheckBox = CCheckboxBuilder::begin()
        ->onToggled([](SP<CCheckboxElement> el, bool state){ restoreToken = state;})
        ->size({ CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_PERCENT, {1.f, 1.f} })
        ->toggled(false)->commence();

    auto restoreTokenText = CTextBuilder::begin()
        ->text("Allow restore session")
        ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.f, 1.f} })
        ->fontSize(CFontSize::HT_FONT_SMALL)
        ->color([] { return CHyprColor{0xFF1f898c}; })
        ->commence();

    window->m_rootElement->addChild(mainLayout);

    mainLayout->setMargin(10.f);
    mainLayout->addChild(rect1);
    mainLayout->addChild(rect2);
    mainLayout->addChild(checkBoxesLayout);

    rect1->addChild(tabButtons);

    rect2->addChild(scroll);
    scroll->addChild(sourceButtons);

    checkBoxesLayout->addChild(restoreTokenCheckBox);
    checkBoxesLayout->addChild(restoreTokenText);

    return window;
}

int main(int argc, char** argv, char** envp) {
    backend = IBackend::create();

    picker = windowLayout();
    refreshTabs();

    picker->m_events.closeRequest.listenStatic([p = WP<IWindow>{picker}] { p->close(); backend->destroy(); });

    picker->open();
    backend->enterLoop();

    return 0;
}
