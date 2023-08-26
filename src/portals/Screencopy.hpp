#pragma once

#include <protocols/wlr-screencopy-unstable-v1-protocol.h>
#include <sdbus-c++/sdbus-c++.h>
#include "../shared/ScreencopyShared.hpp"

enum cursorModes
{
    HIDDEN   = 1,
    EMBEDDED = 2,
    METADATA = 4,
};

enum sourceTypes
{
    MONITOR = 1,
    WINDOW  = 2,
    VIRTUAL = 4,
};

class CScreencopyPortal {
  public:
    CScreencopyPortal(zwlr_screencopy_manager_v1*);

    void onCreateSession(sdbus::MethodCall& call);
    void onSelectSources(sdbus::MethodCall& call);
    void onStart(sdbus::MethodCall& call);

    struct SSession {
        std::string                     appid;
        sdbus::ObjectPath               requestHandle, sessionHandle;
        uint32_t                        cursorMode  = HIDDEN;
        uint32_t                        persistMode = 0;

        std::unique_ptr<sdbus::IObject> request, session;
        SSelectionData                  selection;

        void                            onCloseRequest(sdbus::MethodCall&);
        void                            onCloseSession(sdbus::MethodCall&);
    };

  private:
    std::unique_ptr<sdbus::IObject>        m_pObject;

    std::vector<std::unique_ptr<SSession>> m_vSessions;

    SSession*                              getSession(sdbus::ObjectPath& path);

    const std::string                      INTERFACE_NAME = "org.freedesktop.impl.portal.ScreenCast";
    const std::string                      OBJECT_PATH    = "/org/freedesktop/portal/desktop";
};