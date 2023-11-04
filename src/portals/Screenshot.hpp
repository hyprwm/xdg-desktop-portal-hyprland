#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include <protocols/wlr-screencopy-unstable-v1-protocol.h>

class CScreenshotPortal {
  public:
    CScreenshotPortal();

    void onScreenshot(sdbus::MethodCall& call);
    void onPickColor(sdbus::MethodCall& call);

  private:
    std::unique_ptr<sdbus::IObject> m_pObject;

    const std::string INTERFACE_NAME = "org.freedesktop.impl.portal.Screenshot";
    const std::string OBJECT_PATH    = "/org/freedesktop/portal/desktop";
};
