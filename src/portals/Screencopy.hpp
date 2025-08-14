#pragma once

#include "wlr-screencopy-unstable-v1.hpp"
#include "hyprland-toplevel-export-v1.hpp"
#include <hyprutils/memory/UniquePtr.hpp>
#include <hyprutils/memory/WeakPtr.hpp>
#include <sdbus-c++/sdbus-c++.h>
#include "../shared/ScreencopyShared.hpp"
#include <gbm.h>
#include "../shared/Session.hpp"
#include "../dbusDefines.hpp"
#include <chrono>

enum cursorModes {
    HIDDEN   = 1,
    EMBEDDED = 2,
    METADATA = 4,
};

enum sourceTypes {
    MONITOR = 1,
    WINDOW  = 2,
    VIRTUAL = 4,
};

enum frameStatus {
    FRAME_NONE = 0,
    FRAME_QUEUED,
    FRAME_READY,
    FRAME_FAILED,
    FRAME_RENEG,
};

struct pw_context;
struct pw_core;
struct pw_stream;
struct pw_buffer;

struct SBuffer {
    bool           isDMABUF = false;
    uint32_t       w = 0, h = 0, fmt = 0;
    int            planeCount = 0;

    int            fd[4];
    uint32_t       size[4], stride[4], offset[4];

    gbm_bo*        bo = nullptr;

    SP<CCWlBuffer> wlBuffer = nullptr;
    pw_buffer*     pwBuffer = nullptr;
};

class CPipewireConnection;

class CScreencopyPortal {
  public:
    CScreencopyPortal(SP<CCZwlrScreencopyManagerV1>);

    void   appendToplevelExport(SP<CCHyprlandToplevelExportManagerV1>);

    dbUasv onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv onSelectSources(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts);
    dbUasv onStart(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::string parentWindow,
                   std::unordered_map<std::string, sdbus::Variant> opts);

    struct SSession {
        std::string                               appid;
        sdbus::ObjectPath                         requestHandle, sessionHandle;
        uint32_t                                  cursorMode  = HIDDEN;
        uint32_t                                  persistMode = 0;

        std::unique_ptr<SDBusRequest>             request;
        std::unique_ptr<SDBusSession>             session;
        SSelectionData                            selection;
        Hyprutils::Memory::CWeakPointer<SSession> self;

        void                                      startCopy();
        void                                      initCallbacks();

        struct {
            bool                                  active              = false;
            SP<CCZwlrScreencopyFrameV1>           frameCallback       = nullptr;
            SP<CCHyprlandToplevelExportFrameV1>   windowFrameCallback = nullptr;
            frameStatus                           status              = FRAME_NONE;
            uint64_t                              tvSec               = 0;
            uint32_t                              tvNsec              = 0;
            uint64_t                              tvTimestampNs       = 0;
            uint32_t                              nodeID              = 0;
            uint32_t                              framerate           = 60;
            wl_output_transform                   transform           = WL_OUTPUT_TRANSFORM_NORMAL;
            std::chrono::system_clock::time_point begunFrame          = std::chrono::system_clock::now();
            uint32_t                              copyRetries         = 0;

            struct {
                uint32_t w = 0, h = 0, size = 0, stride = 0, fmt = 0;
            } frameInfoSHM;

            struct {
                uint32_t w = 0, h = 0, fmt = 0;
            } frameInfoDMA;

            struct {
                uint32_t x = 0, y = 0, w = 0, h = 0;
            } damage[4];
            uint32_t damageCount = 0;
        } sharingData;

        void onCloseRequest(sdbus::MethodCall&);
        void onCloseSession(sdbus::MethodCall&);
    };

    void                                 startFrameCopy(SSession* pSession);
    void                                 queueNextShareFrame(SSession* pSession);
    bool                                 hasToplevelCapabilities();

    std::unique_ptr<CPipewireConnection> m_pPipewire;

  private:
    std::unique_ptr<sdbus::IObject>                          m_pObject;

    std::vector<Hyprutils::Memory::CUniquePointer<SSession>> m_vSessions;

    SSession*                                                getSession(sdbus::ObjectPath& path);
    void                                                     startSharing(SSession* pSession);

    struct {
        SP<CCZwlrScreencopyManagerV1>         screencopy = nullptr;
        SP<CCHyprlandToplevelExportManagerV1> toplevel   = nullptr;
    } m_sState;

    const sdbus::InterfaceName INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.ScreenCast"};
    const sdbus::ObjectPath    OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};

    friend struct SSession;
};

class CPipewireConnection {
  public:
    CPipewireConnection();
    ~CPipewireConnection();

    bool good();

    void createStream(CScreencopyPortal::SSession* pSession);
    void destroyStream(CScreencopyPortal::SSession* pSession);

    void enqueue(CScreencopyPortal::SSession* pSession);
    void dequeue(CScreencopyPortal::SSession* pSession);

    struct SPWStream {
        CScreencopyPortal::SSession*          pSession    = nullptr;
        pw_stream*                            stream      = nullptr;
        bool                                  streamState = false;
        spa_hook                              streamListener;
        SBuffer*                              currentPWBuffer = nullptr;
        spa_video_info_raw                    pwVideoInfo;
        uint32_t                              seq   = 0;
        bool                                  isDMA = false;

        std::vector<std::unique_ptr<SBuffer>> buffers;
    };

    std::unique_ptr<SBuffer> createBuffer(SPWStream* pStream, bool dmabuf);
    SPWStream*               streamFromSession(CScreencopyPortal::SSession* pSession);
    void                     removeSessionFrameCallbacks(CScreencopyPortal::SSession* pSession);
    uint32_t                 buildFormatsFor(spa_pod_builder* b[2], const spa_pod* params[2], SPWStream* stream);
    void                     updateStreamParam(SPWStream* pStream);

  private:
    std::vector<std::unique_ptr<SPWStream>> m_vStreams;

    bool                                    buildModListFor(SPWStream* stream, uint32_t drmFmt, uint64_t** mods, uint32_t* modCount);

    pw_context*                             m_pContext = nullptr;
    pw_core*                                m_pCore    = nullptr;
};