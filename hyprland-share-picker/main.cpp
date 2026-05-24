#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <hyprtoolkit/types/FontTypes.hpp>
#include <hyprtoolkit/element/Element.hpp>
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
#include <hyprutils/cli/Logger.hpp>
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

enum ESourceType {
    MONITOR = 0,
    WINDOW,
    WORKSPACE,
    REGION,
    UNKNOWN,
};

constexpr std::string enumToString(ESourceType g) {
    switch (g) {
        case ESourceType::MONITOR   : return "screen"; 
        case ESourceType::WINDOW    : return "window";
        case ESourceType::REGION    : return "region";
        case ESourceType::WORKSPACE : return "workspace";
        default                     : return "unknown";
    }
}

struct SSourceEntry {
    ESourceType type;
    std::string name;

    //for window and workspace
    std::string        clazz;
    unsigned long long id = 0;

    //for monitor
    int64_t x = 0;
    int64_t y = 0;
    int64_t width = 0;
    int64_t height = 0;
};

//global states
static SP<CColumnLayoutElement> sourcesLayout;
bool restoreToken = false;
ESourceType currentTabEnum = MONITOR;
std::vector<SSourceEntry> monitors;

static std::vector<SSourceEntry> getMonitors(const std::string& MONITORLISTSTR) {
    std::vector<SSourceEntry> result;

    std::stringstream stream(MONITORLISTSTR);
    std::string line;

    while(std::getline(stream, line)) {
        if(line.starts_with("Monitor ")) {
            SSourceEntry newEntry;

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
    REGIONSTR.erase(std::remove(REGIONSTR.begin(), REGIONSTR.end(), '\n'), REGIONSTR.end());

    std::stringstream stream(REGIONSTR);

    std::string screenName;
    int64_t globalX = 0;
    int64_t globalY = 0;
    int64_t width   = 0;
    int64_t height  = 0;

    stream >> screenName >> globalX >> globalY >> width >> height;

    if (screenName.empty()) {
        g_logger.log(Hyprutils::CLI::LOG_ERR, "Failed parsing slurp output");
        return "";
    }

    auto monitor = std::find_if(monitors.begin(), monitors.end(),
        [&](const auto& m) { return m.name == screenName; });

    if (monitor == monitors.end()) {
        g_logger.log(Hyprutils::CLI::LOG_ERR, std::format("Monitor {} not found", screenName));
        return "";
    }

    // Convert global coords -> monitor-local coords
    const auto localX = globalX - monitor->x;
    const auto localY = globalY - monitor->y;

    const auto result = std::format( "{}@{},{},{},{}", screenName, localX, localY, width, height);

    g_logger.log(Hyprutils::CLI::LOG_DEBUG, std::format("getRegion return region:{}", result));

    return result;
}

std::vector<SSourceEntry> getWorkspaces(const std::string& env) {
    std::vector<SSourceEntry> result;

    if (env.empty())
        return result;

    std::string rolling = env;
    while(!rolling.empty()) {
        const auto IDSEPPOS = rolling.find("[WI>]");
        const auto IDSTR    = rolling.substr(0, IDSEPPOS);

        const auto NAMESEPPOS = rolling.find("[WN>]");
        const auto NAMESTR    = rolling.substr(IDSEPPOS + 5, NAMESEPPOS - IDSEPPOS - 5);

        try {
            SSourceEntry workspaceEntry;
            workspaceEntry.name = NAMESTR;
            workspaceEntry.id = std::stoll(IDSTR);
            result.push_back(workspaceEntry);
        } catch (std::exception& e) {
            // silent err
        }

        rolling = rolling.substr(NAMESEPPOS + 5);
    }

    return result;
}

std::vector<SSourceEntry> getWindows(const std::string& env) {
    std::vector<SSourceEntry> result;

    if (env.empty())
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
            SSourceEntry windowEntry;
            windowEntry.name = TITLESTR;
            windowEntry.clazz = CLASSSTR;
            windowEntry.id = std::stoll(IDSTR);
            result.push_back(windowEntry);
        } catch (std::exception& e) {
            // silent err
        }

        rolling = rolling.substr(WINDOWSEPPOS + 5);
    }

    return result;
}

std::string getLabel(ESourceType type, SSourceEntry entry) {
    switch (type) {
        case ESourceType::MONITOR:   return std::format( "{} (ID {}): {}x{}", entry.name, entry.id, entry.width, entry.height);
        case ESourceType::WINDOW:    return std::format("{}: {}", entry.clazz, entry.name);
        case ESourceType::WORKSPACE: return std::format("{}: {}", entry.id, entry.name); 
        default: return {};
    }
}

std::string getDetail(ESourceType type, SSourceEntry entry) {
    switch (type) {
        case ESourceType::MONITOR:   return entry.name;
        case ESourceType::WINDOW:    return std::to_string(entry.id);
        case ESourceType::WORKSPACE: return entry.name; 
        default: return {};
    }
}

using GETSOURCES_FUNC = std::vector<SSourceEntry>(*)(const std::string&);
GETSOURCES_FUNC getSourceFunction(ESourceType type) {
    switch (type) {
        case ESourceType::MONITOR:   return getMonitors;
        case ESourceType::WORKSPACE: return getWorkspaces;
        case ESourceType::WINDOW:    return getWindows;
        default: return nullptr;
    }
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

static void changeTab(ESourceType sourceGroup)  {
    if(sourceGroup == UNKNOWN || sourceGroup == currentTabEnum || !sourcesLayout)
        return;

    currentTabEnum = sourceGroup;

    std::string SOURCELISTSTR = {};
    sourcesLayout->clearChildren();

    switch(currentTabEnum) {
        case REGION: {
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

        case MONITOR: {
            SOURCELISTSTR = execAndGet("hyprctl monitors");
            break;
        }

        case WINDOW: {
            const char* WINDOWLISTSTR = getenv("XDPH_WINDOW_SHARING_LIST");
            if(!WINDOWLISTSTR) 
                break;

            SOURCELISTSTR = WINDOWLISTSTR;
            break;
        }

        case WORKSPACE: {
            const char* WORKSPACELISTSTR = getenv("XDPH_WORKSPACE_SHARING_LIST");
            if(!WORKSPACELISTSTR) 
                break;

            SOURCELISTSTR = WORKSPACELISTSTR;
            break;
        }
        default: 
            return;
    }

    if(SOURCELISTSTR.empty()) {
        g_logger.log(Hyprutils::CLI::LOG_WARN, std::format("No {}", enumToString(currentTabEnum)));
        auto null = CNullBuilder::begin()->commence();
        sourcesLayout->addChild(null);
        return;
    }

    auto getSourcesFn = getSourceFunction(currentTabEnum);
    auto sources = getSourcesFn(SOURCELISTSTR);

    //Because region depends on the monitor, we have to update the monitors
    monitors = currentTabEnum == MONITOR ? sources : monitors;

    for(auto& s : sources) {
        auto detail = getDetail(currentTabEnum, s);
        auto entry = CButtonBuilder::begin()
            ->label(getLabel(currentTabEnum, s))
            ->onMainClick([detail](SP<CButtonElement> el){ onSelectSource(detail); })
            ->size({ CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.99f, 60.f} })
            ->fontSize(CFontSize::HT_FONT_SMALL)
            ->commence();

        sourcesLayout->addChild(entry);
    }
    return;

}

//Note: this will change the global sourcesLayout as well
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

    for (int sourceType = ESourceType::MONITOR; sourceType != ESourceType::UNKNOWN; sourceType++) {
        if(sourceType == WORKSPACE) 
            continue; //the portal has not support workspace sharing yet
        
        auto tabButton = CButtonBuilder::begin()
            ->label(enumToString(static_cast<ESourceType>(sourceType)))
            ->onMainClick([sourceType](SP<CButtonElement> el){ changeTab(static_cast<ESourceType>(sourceType)); })
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
    changeTab(currentTabEnum);

    picker->m_events.closeRequest.listenStatic([p = WP<IWindow>{picker}] { p->close(); backend->destroy(); });

    picker->open();
    backend->enterLoop();

    return 0;
}
