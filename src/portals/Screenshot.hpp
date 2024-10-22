#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "../dbusDefines.hpp"

class CScreenshotPortal {
  public:
    CScreenshotPortal();

    dbUasv onScreenshot(sdbus::ObjectPath requestHandle, std::string appID, std::string parentWindow, std::unordered_map<std::string, sdbus::Variant> options);
    dbUasv onPickColor(sdbus::ObjectPath requestHandle, std::string appID, std::string parentWindow, std::unordered_map<std::string, sdbus::Variant> options);

  private:
    std::unique_ptr<sdbus::IObject> m_pObject;

    const sdbus::InterfaceName      INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.Screenshot"};
    const sdbus::ObjectPath         OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};
