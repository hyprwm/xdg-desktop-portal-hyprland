#include "Settings.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sstream>
#include <cmath>

CSettingsPortal::CSettingsPortal() {
    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);

    m_pObject
        ->addVTable(
            sdbus::registerMethod("ReadAll").implementedAs([this](std::vector<std::string> namespaces) {
                return ReadAll(namespaces);
            }),
            sdbus::registerMethod("Read").implementedAs([this](std::string ns, std::string key) {
                return Read(ns, key);
            }),
            sdbus::registerSignal("SettingChanged").withParameters<std::string, std::string, sdbus::Variant>(),
            sdbus::registerProperty("version").withGetter([]() { return uint32_t{1}; }))
        .forInterface(INTERFACE_NAME);

    Debug::log(LOG, "[settings] Portal registered");

    // Initialize settings from Hyprland
    connectToHyprlandIPC();
    updateColorScheme();
    updateAccentColor();
}

std::unordered_map<std::string, sdbus::Variant> CSettingsPortal::ReadAll(std::vector<std::string> namespaces) {
    Debug::log(LOG, "[settings] ReadAll called");
    
    std::unordered_map<std::string, sdbus::Variant> result;
    
    // Check if org.freedesktop.appearance is requested or if no specific namespaces
    bool includeAppearance = namespaces.empty();
    for (const auto& ns : namespaces) {
        if (ns == "org.freedesktop.appearance") {
            includeAppearance = true;
            break;
        }
    }
    
    if (includeAppearance) {
        std::unordered_map<std::string, sdbus::Variant> appearance;
        
        // Add color-scheme
        appearance["color-scheme"] = sdbus::Variant(m_colorScheme);
        
        // Add accent-color if available
        if (m_accentColor.has_value()) {
            auto [r, g, b] = m_accentColor.value();
            std::tuple<double, double, double> color{r, g, b};
            appearance["accent-color"] = sdbus::Variant(color);
        }
        
        result["org.freedesktop.appearance"] = sdbus::Variant(appearance);
    }
    
    return result;
}

sdbus::Variant CSettingsPortal::Read(std::string ns, std::string key) {
    Debug::log(LOG, "[settings] Read called for {}:{}", ns, key);
    
    if (ns == "org.freedesktop.appearance") {
        if (key == "color-scheme") {
            return sdbus::Variant(m_colorScheme);
        } else if (key == "accent-color" && m_accentColor.has_value()) {
            auto [r, g, b] = m_accentColor.value();
            std::tuple<double, double, double> color{r, g, b};
            return sdbus::Variant(color);
        }
    }
    
    // Return empty variant for unknown settings
    return sdbus::Variant();
}

void CSettingsPortal::connectToHyprlandIPC() {
    // Hyprland IPC is initialized through the main connection
    // We'll use the existing connection from PortalManager
}

std::string CSettingsPortal::queryHyprlandConfig(const std::string& query) {
    // Connect to Hyprland IPC socket
    const char* his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!his) {
        Debug::log(WARN, "[settings] HYPRLAND_INSTANCE_SIGNATURE not set");
        return "";
    }
    
    std::string socketPath = "/tmp/hypr/" + std::string(his) + "/.socket.sock";
    
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        Debug::log(ERR, "[settings] Failed to create socket");
        return "";
    }
    
    sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, socketPath.c_str());
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Debug::log(ERR, "[settings] Failed to connect to Hyprland IPC");
        close(sockfd);
        return "";
    }
    
    // Send query
    if (write(sockfd, query.c_str(), query.length()) < 0) {
        Debug::log(ERR, "[settings] Failed to write to socket");
        close(sockfd);
        return "";
    }
    
    // Read response
    char buffer[8192] = {0};
    ssize_t bytesRead = read(sockfd, buffer, sizeof(buffer) - 1);
    close(sockfd);
    
    if (bytesRead < 0) {
        Debug::log(ERR, "[settings] Failed to read from socket");
        return "";
    }
    
    return std::string(buffer, bytesRead);
}

std::tuple<double, double, double> CSettingsPortal::parseColorString(const std::string& colorStr) {
    // Parse color in format "rgba(rrggbbaa)" or "0xaarrggbb"
    double r = 0, g = 0, b = 0;
    
    if (colorStr.find("rgba(") == 0 || colorStr.find("rgb(") == 0) {
        // Parse rgba(rrggbbaa) format
        size_t start = colorStr.find('(') + 1;
        size_t end = colorStr.find(')');
        if (start != std::string::npos && end != std::string::npos) {
            std::string hex = colorStr.substr(start, end - start);
            if (hex.length() >= 6) {
                uint32_t color = std::stoul(hex.substr(0, 6), nullptr, 16);
                r = ((color >> 16) & 0xFF) / 255.0;
                g = ((color >> 8) & 0xFF) / 255.0;
                b = (color & 0xFF) / 255.0;
            }
        }
    } else if (colorStr.find("0x") == 0) {
        // Parse 0xaarrggbb format
        if (colorStr.length() >= 10) {
            uint32_t color = std::stoul(colorStr.substr(4, 6), nullptr, 16);
            r = ((color >> 16) & 0xFF) / 255.0;
            g = ((color >> 8) & 0xFF) / 255.0;
            b = (color & 0xFF) / 255.0;
        }
    }
    
    return {r, g, b};
}

void CSettingsPortal::updateColorScheme() {
    // Query Hyprland for theme preference
    // For now, we'll check if the user has dark decorations
    std::string config = queryHyprlandConfig("j/getoption decoration:dim_inactive");
    
    // Simple heuristic: if dim_inactive is enabled, assume dark theme preference
    // You could also check other settings or wallpaper brightness
    uint32_t oldScheme = m_colorScheme;
    
    // Check for dark theme indicators in config
    std::string generalConfig = queryHyprlandConfig("j/getoption general:col.active_border");
    if (generalConfig.find("\"str\":") != std::string::npos) {
        // Extract color value
        size_t pos = generalConfig.find("\"str\":\"");
        if (pos != std::string::npos) {
            pos += 7;
            size_t endPos = generalConfig.find("\"", pos);
            if (endPos != std::string::npos) {
                std::string colorValue = generalConfig.substr(pos, endPos - pos);
                // Simple heuristic: darker colors suggest dark theme
                auto [r, g, b] = parseColorString(colorValue);
                double brightness = (r + g + b) / 3.0;
                m_colorScheme = (brightness < 0.5) ? 1 : 2; // 1 = dark, 2 = light
            }
        }
    }
    
    if (oldScheme != m_colorScheme) {
        // Emit signal for color scheme change
        m_pObject->emitSignal(INTERFACE_NAME, "SettingChanged")
            .onInterface(INTERFACE_NAME)
            .withArguments("org.freedesktop.appearance", "color-scheme", sdbus::Variant(m_colorScheme));
        
        Debug::log(LOG, "[settings] Color scheme updated to {}", m_colorScheme);
    }
}

void CSettingsPortal::updateAccentColor() {
    // Query Hyprland for active border color
    std::string response = queryHyprlandConfig("j/getoption general:col.active_border");
    
    if (response.empty()) {
        Debug::log(WARN, "[settings] Failed to get active border color");
        return;
    }
    
    // Parse JSON response to get color value
    // Looking for: {"str":"rgba(33ccffee) rgba(00ff99ee) 45deg","value":"..."}
    size_t pos = response.find("\"str\":\"");
    if (pos == std::string::npos) {
        Debug::log(WARN, "[settings] Failed to parse active border color response");
        return;
    }
    
    pos += 7; // Move past "str":"
    size_t endPos = response.find("\"", pos);
    if (endPos == std::string::npos) {
        Debug::log(WARN, "[settings] Failed to find end of color string");
        return;
    }
    
    std::string colorStr = response.substr(pos, endPos - pos);
    
    // Handle gradient colors - just take the first one
    size_t spacePos = colorStr.find(' ');
    if (spacePos != std::string::npos) {
        colorStr = colorStr.substr(0, spacePos);
    }
    
    auto oldAccent = m_accentColor;
    auto [r, g, b] = parseColorString(colorStr);
    
    // Only update if we got valid colors
    if (r > 0 || g > 0 || b > 0) {
        m_accentColor = {r, g, b};
        
        if (!oldAccent.has_value() || oldAccent.value() != m_accentColor.value()) {
            // Emit signal for accent color change
            std::tuple<double, double, double> color{r, g, b};
            m_pObject->emitSignal(INTERFACE_NAME, "SettingChanged")
                .onInterface(INTERFACE_NAME)
                .withArguments("org.freedesktop.appearance", "accent-color", sdbus::Variant(color));
            
            Debug::log(LOG, "[settings] Accent color updated to ({:.2f}, {:.2f}, {:.2f})", r, g, b);
        }
    }
}