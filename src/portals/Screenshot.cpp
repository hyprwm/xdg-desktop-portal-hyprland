#include "Screenshot.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

#include <regex>
#include <filesystem>

std::string lastScreenshot;

//
static dbUasv pickHyprPicker(sdbus::ObjectPath requestHandle, std::string appID, std::string parentWindow, std::unordered_map<std::string, sdbus::Variant> options) {
    const std::string HYPRPICKER_CMD = "hyprpicker --format=rgb --no-fancy";
    std::string       rgbColor       = execAndGet(HYPRPICKER_CMD.c_str());

    if (rgbColor.size() > 12) {
        Debug::log(ERR, "hyprpicker returned strange output: " + rgbColor);
        return {1, {}};
    }

    std::array<uint8_t, 3> colors{0, 0, 0};

    try {
        for (uint8_t i = 0; i < 2; i++) {
            uint64_t next = rgbColor.find(' ');

            if (next == std::string::npos) {
                Debug::log(ERR, "hyprpicker returned strange output: " + rgbColor);
                return {1, {}};
            }

            colors[i] = std::stoi(rgbColor.substr(0, next));
            rgbColor  = rgbColor.substr(next + 1, rgbColor.size() - next);
        }
        colors[2] = std::stoi(rgbColor);
    } catch (...) {
        Debug::log(ERR, "Reading RGB values from hyprpicker failed. This is likely a string to integer error.");
        return {1, {}};
    }

    auto [r, g, b] = colors;
    std::unordered_map<std::string, sdbus::Variant> results;
    results["color"] = sdbus::Variant{sdbus::Struct<double, double, double>(r / 255.0, g / 255.0, b / 255.0)};

    return {0, results};
}

static dbUasv pickSlurp(sdbus::ObjectPath requestHandle, std::string appID, std::string parentWindow, std::unordered_map<std::string, sdbus::Variant> options) {
    const std::string PICK_COLOR_CMD = "grim -g \"$(slurp -p)\" -t ppm -";
    std::string       ppmColor       = execAndGet(PICK_COLOR_CMD.c_str());

    // unify whitespace
    ppmColor = std::regex_replace(ppmColor, std::regex("\\s+"), std::string(" "));

    // check if we got a 1x1 PPM Image
    if (!ppmColor.starts_with("P6 1 1 ")) {
        Debug::log(ERR, "grim did not return a PPM Image for us.");
        return {1, {}};
    }

    // convert it to a rgb value
    try {
        std::string maxValString = ppmColor.substr(7, ppmColor.size());
        maxValString             = maxValString.substr(0, maxValString.find(' '));
        uint32_t maxVal          = std::stoi(maxValString);

        double   r, g, b;

        // 1 byte per triplet
        if (maxVal < 256) {
            std::string byteString = ppmColor.substr(11, 14);

            r = (uint8_t)byteString[0] / (maxVal * 1.0);
            g = (uint8_t)byteString[1] / (maxVal * 1.0);
            b = (uint8_t)byteString[2] / (maxVal * 1.0);
        } else {
            // 2 byte per triplet (MSB first)
            std::string byteString = ppmColor.substr(11, 17);

            r = ((byteString[0] << 8) | byteString[1]) / (maxVal * 1.0);
            g = ((byteString[2] << 8) | byteString[3]) / (maxVal * 1.0);
            b = ((byteString[4] << 8) | byteString[5]) / (maxVal * 1.0);
        }

        std::unordered_map<std::string, sdbus::Variant> results;
        results["color"] = sdbus::Variant{sdbus::Struct<double, double, double>(r, g, b)};

        return {0, results};
    } catch (...) { Debug::log(ERR, "Converting PPM to RGB failed. This is likely a string to integer error."); }

    return {1, {}};
}

CScreenshotPortal::CScreenshotPortal() {
    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject
        ->addVTable(
            sdbus::registerMethod("Screenshot").implementedAs([this](sdbus::ObjectPath o, std::string s1, std::string s2, std::unordered_map<std::string, sdbus::Variant> m) {
                return onScreenshot(o, s1, s2, m);
            }),
            sdbus::registerMethod("PickColor").implementedAs([this](sdbus::ObjectPath o, std::string s1, std::string s2, std::unordered_map<std::string, sdbus::Variant> m) {
                return onPickColor(o, s1, s2, m);
            }),
            sdbus::registerProperty("version").withGetter([]() { return uint32_t{2}; }))
        .forInterface(INTERFACE_NAME);

    Debug::log(LOG, "[screenshot] init successful");
}

dbUasv CScreenshotPortal::onScreenshot(sdbus::ObjectPath requestHandle, std::string appID, std::string parentWindow, std::unordered_map<std::string, sdbus::Variant> options) {

    Debug::log(LOG, "[screenshot] New screenshot request:");
    Debug::log(LOG, "[screenshot]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screenshot]  | appid: {}", appID);

    bool isInteractive = options.count("interactive") && options["interactive"].get<bool>() && inShellPath("slurp");

    // make screenshot

    const auto RUNTIME_DIR = getenv("XDG_RUNTIME_DIR");
    srand(time(nullptr));

    const std::string                               HYPR_DIR             = RUNTIME_DIR ? std::string{RUNTIME_DIR} + "/hypr/" : "/tmp/hypr/";
    const std::string                               SNAP_FILE            = std::format("xdph_screenshot_{:x}.png", rand()); // rand() is good enough
    const std::string                               FILE_PATH            = HYPR_DIR + SNAP_FILE;
    const std::string                               SNAP_CMD             = "grim '" + FILE_PATH + "'";
    const std::string                               SNAP_INTERACTIVE_CMD = "grim -g \"$(slurp)\" '" + FILE_PATH + "'";

    std::unordered_map<std::string, sdbus::Variant> results;
    results["uri"] = sdbus::Variant{"file://" + FILE_PATH};

    std::filesystem::remove(FILE_PATH);
    std::filesystem::create_directory(HYPR_DIR);

    // remove last screenshot. This could cause issues if the app hasn't read the screenshot back yet, but oh well.
    if (!lastScreenshot.empty())
        std::filesystem::remove(lastScreenshot);
    lastScreenshot = FILE_PATH;

    if (isInteractive)
        execAndGet(SNAP_INTERACTIVE_CMD.c_str());
    else
        execAndGet(SNAP_CMD.c_str());

    uint32_t responseCode = std::filesystem::exists(FILE_PATH) ? 0 : 1;

    return {responseCode, results};
}

dbUasv CScreenshotPortal::onPickColor(sdbus::ObjectPath requestHandle, std::string appID, std::string parentWindow, std::unordered_map<std::string, sdbus::Variant> options) {

    Debug::log(LOG, "[screenshot] New PickColor request:");
    Debug::log(LOG, "[screenshot]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screenshot]  | appid: {}", appID);

    bool hyprPickerInstalled = inShellPath("hyprpicker");
    bool slurpInstalled      = inShellPath("slurp");

    if (!slurpInstalled && !hyprPickerInstalled) {
        Debug::log(ERR, "Neither slurp nor hyprpicker found. We can't pick colors.");
        return {1, {}};
    }

    // use hyprpicker if installed, slurp as fallback
    if (hyprPickerInstalled)
        return pickHyprPicker(requestHandle, appID, parentWindow, options);
    else
        return pickSlurp(requestHandle, appID, parentWindow, options);
}
