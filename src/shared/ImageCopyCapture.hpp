#pragma once

#include "../includes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include <sys/types.h>

class CCExtImageCopyCaptureManagerV1;
class CCExtImageCaptureSourceV1;
class CCExtImageCopyCaptureSessionV1;
class CCExtImageCopyCaptureFrameV1;
class CCWlBuffer;

class CImageCopyCapture {
  public:
    CImageCopyCapture(SP<CCExtImageCopyCaptureManagerV1> manager, SP<CCExtImageCaptureSourceV1> source, bool paintCursors);
    ~CImageCopyCapture();

    CImageCopyCapture(const CImageCopyCapture&)            = delete;
    CImageCopyCapture& operator=(const CImageCopyCapture&) = delete;

    struct SDmabufFormat {
        uint32_t              format = 0;
        std::vector<uint64_t> modifiers;
    };

    struct SConstraints {
        uint32_t                   width = 0, height = 0;
        std::vector<uint32_t>      shmFormats;
        std::vector<SDmabufFormat> dmabufFormats;
        std::optional<dev_t>       device;
    };

    struct SDamage {
        int32_t x = 0, y = 0, width = 0, height = 0;
    };

    struct SFrame {
        uint32_t               transform   = 0;
        uint64_t               timestampNs = 0;
        std::array<SDamage, 4> damage      = {};
        size_t                 damageCount = 0;
    };

    enum eFailure {
        FAILURE_UNKNOWN,
        FAILURE_BUFFER_CONSTRAINTS,
        FAILURE_STOPPED,
    };

    std::function<void()>              onConstraints, onStopped;
    std::function<void(const SFrame&)> onReady;
    std::function<void(eFailure)>      onFailed;

    const SConstraints&                constraints() const;
    bool                               ready() const;
    bool                               pending() const;
    bool                               stopped() const;
    bool                               capture(SP<CCWlBuffer> buffer);
    void                               cancelFrame();
    void                               stop();

  private:
    SP<CCExtImageCaptureSourceV1>      m_source;
    SP<CCExtImageCopyCaptureSessionV1> m_session;
    SP<CCExtImageCopyCaptureFrameV1>   m_frame;
    SP<CCWlBuffer>                     m_buffer;
    SConstraints                       m_constraints, m_pendingConstraints;
    SFrame                             m_frameData;
    bool                               m_ready              = false;
    bool                               m_stopped            = false;
    bool                               m_invalidConstraints = false;
    bool                               m_damageOverflow     = false;
};
