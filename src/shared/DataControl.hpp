#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../includes.hpp"

class CCWlSeat;
class CCExtDataControlManagerV1;
class CCZwlrDataControlManagerV1;

// A compositor's clipboard through one of the data control protocols
// (ext-data-control-v1, or the older wlr-data-control-unstable-v1 which is the same
// thing under another name), so the portal doesn't care which one it got.

// The selection some client holds, as announced by the compositor.
class IDataControlOffer {
  public:
    virtual ~IDataControlOffer() = default;

    // Ask the owner to write the selection in this format to `fd`.
    virtual void             receive(const std::string& mimeType, int fd) = 0;
    virtual void             destroy()                                    = 0;

    std::vector<std::string> m_mimeTypes;
};

// A selection of our own.
class IDataControlSource {
  public:
    virtual ~IDataControlSource() = default;

    virtual void offer(const std::string& mimeType) = 0;
    virtual void destroy()                          = 0;
};

class IDataControl {
  public:
    virtual ~IDataControl() = default;

    // `onSend` gets the format someone wants and the fd to write it to, which it owns.
    // `onCancelled` is called when another client takes the selection from this source.
    using SendFn      = std::function<void(const std::string& mimeType, int fd)>;
    using CancelledFn = std::function<void(const IDataControlSource* source)>;

    virtual SP<IDataControlSource> createSource(SendFn onSend, CancelledFn onCancelled) = 0;
    // Make `source` the selection, or clear it when null.
    virtual void setSelection(IDataControlSource* source) = 0;

    // The selection changed: null when there is none. Owns the offer.
    std::function<void(SP<IDataControlOffer> offer)> m_onSelection;
    // The compositor won't send anything more.
    std::function<void()> m_onFinished;
};

std::unique_ptr<IDataControl> makeDataControl(SP<CCExtDataControlManagerV1> manager, SP<CCWlSeat> seat);
std::unique_ptr<IDataControl> makeDataControl(SP<CCZwlrDataControlManagerV1> manager, SP<CCWlSeat> seat);
