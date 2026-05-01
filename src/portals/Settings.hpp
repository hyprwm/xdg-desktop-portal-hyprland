#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "../dbusDefines.hpp"
#include <optional>
#include <string>
#include <tuple>

class CSettingsPortal {
  public:
    CSettingsPortal();

    // Portal methods
    std::unordered_map<std::string, sdbus::Variant> ReadAll(std::vector<std::string> namespaces);
    sdbus::Variant                                  Read(std::string ns, std::string key);

    // Update settings from Hyprland
    void updateColorScheme();
    void updateAccentColor();

  private:
    std::unique_ptr<sdbus::IObject> m_pObject;

    // Current settings values
    uint32_t                     m_colorScheme = 0; // 0: no preference, 1: dark, 2: light
    std::optional<std::tuple<double, double, double>> m_accentColor; // RGB values 0.0-1.0

    // Helper methods
    void                         connectToHyprlandIPC();
    std::string                  queryHyprlandConfig(const std::string& query);
    std::tuple<double, double, double> parseColorString(const std::string& colorStr);
    
    const sdbus::InterfaceName  INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.Settings"};
    const sdbus::ObjectPath      OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};