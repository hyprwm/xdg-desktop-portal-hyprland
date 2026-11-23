#pragma once

#include "../includes.hpp"
#include "../shared/FileChooserData.hpp"

#include <hyprutils/eventLoop/EventMask.hpp>
#include <sdbus-c++/sdbus-c++.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Hyprutils::EventLoop {
    class IFDSource;
}

struct SDBusRequest;

// Runs hyprland-file-picker (or a custom picker) for each request. Replies are asynchronous,
// so the portal keeps serving everything else while a dialog is open.
class CFileChooserPortal {
  public:
    CFileChooserPortal();
    ~CFileChooserPortal();

    // filechooser:custom_picker_binary, or hyprland-file-picker
    static std::string pickerBinary();

  private:
    using Result = sdbus::Result<uint32_t, std::unordered_map<std::string, sdbus::Variant>>;

    struct SPendingRequest {
        SFileChooserRequest                 request;
        Result                              result;
        std::unique_ptr<SDBusRequest>       dbusRequest;
        SP<Hyprutils::EventLoop::IFDSource> output;
        std::string                         stdOut;
        pid_t                               pid  = 0;
        bool                                done = false;
    };

    void                             onCall(eFileChooserMethod method, Result&& result, const sdbus::ObjectPath& handle, const std::string& appID, std::string title,
                                            const std::unordered_map<std::string, sdbus::Variant>& options);
    void                             onOutput(const WP<SPendingRequest>& pending, Hyprutils::EventLoop::FdEventMask events);
    void                             onClose(const WP<SPendingRequest>& pending);
    void                             respond(const SP<SPendingRequest>& pending);
    void                             stopPicker(const SP<SPendingRequest>& pending);
    void                             complete(const SP<SPendingRequest>& pending, uint32_t response, std::unordered_map<std::string, sdbus::Variant> results);

    std::unique_ptr<sdbus::IObject>  m_pObject;
    std::vector<SP<SPendingRequest>> m_vRequests;

    const sdbus::InterfaceName       INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.FileChooser"};
    const sdbus::ObjectPath          OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};
