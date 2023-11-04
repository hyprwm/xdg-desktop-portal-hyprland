#include "Screenshot.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"
#include <regex>

CScreenshotPortal::CScreenshotPortal() {
    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject->registerMethod(INTERFACE_NAME, "Screenshot", "ossa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onScreenshot(c); });
    m_pObject->registerMethod(INTERFACE_NAME, "PickColor", "ossa{sv}", "ua{sv}", [&](sdbus::MethodCall c) { onPickColor(c); });

    m_pObject->registerProperty(INTERFACE_NAME, "version", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint32_t)(2); });

    m_pObject->finishRegistration();

    Debug::log(LOG, "[screenshot] init successful");
}

void CScreenshotPortal::onScreenshot(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle;
    call >> requestHandle;

    std::string appID;
    call >> appID;

    std::string parentWindow;
    call >> parentWindow;

    std::unordered_map<std::string, sdbus::Variant> options;
    call >> options;

    Debug::log(LOG, "[screenshot] New screenshot request:");
    Debug::log(LOG, "[screenshot]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screenshot]  | appid: {}", appID);

    bool isInteractive = options.count("interactive") && options["interactive"].get<bool>();

    // make screenshot
    const std::string FILE_PATH = "/tmp/xdph_screenshot.png";
    const std::string SNAP_CMD  = "grim " + FILE_PATH;
    const std::string SNAP_INTERACTIVE_CMD = "grim -g \"$(slurp)\" " + FILE_PATH;

    std::unordered_map<std::string, sdbus::Variant> results;
    results["uri"] = "file://" + FILE_PATH;

    remove(FILE_PATH.c_str());

    if (isInteractive) {
        execAndGet(SNAP_INTERACTIVE_CMD.c_str());
    } else {
        execAndGet(SNAP_CMD.c_str());
    }

    uint32_t responseCode = fileExists(FILE_PATH) ? 0 : 1;

    auto reply = call.createReply();
    reply << responseCode;
    reply << results;
    reply.send();
}

void CScreenshotPortal::onPickColor(sdbus::MethodCall& call) {
    sdbus::ObjectPath requestHandle;
    call >> requestHandle;

    std::string appID;
    call >> appID;

    std::string parentWindow;
    call >> parentWindow;

    Debug::log(LOG, "[screenshot] New PickColor request:");
    Debug::log(LOG, "[screenshot]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[screenshot]  | appid: {}", appID);

    const std::string PICK_COLOR_CMD = "grim -g \"$(slurp -p)\" -t ppm -";

    std::string ppmColor = execAndGet(PICK_COLOR_CMD.c_str());

    // unify whitespace
    ppmColor = std::regex_replace(ppmColor, std::regex("\\s+"), std::string(" "));

    // check if we got a 1x1 PPM Image
    if (!ppmColor.starts_with("P6 1 1 ")) {
        auto reply = call.createReply();
        reply << (uint32_t)1;
        reply.send();
        return;
    }

    // convert it to a rgb value
    std::string maxValString = ppmColor.substr(7, ppmColor.size());
    maxValString = maxValString.substr(0, maxValString.find(' '));
    uint32_t maxVal = std::stoi(maxValString);

    double r, g, b;

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

    auto reply = call.createReply();

    std::unordered_map<std::string, sdbus::Variant> results;
    results["color"] = sdbus::Struct(std::tuple {r, g, b});

    reply << (uint32_t)0;
    reply << results;
    reply.send();
}