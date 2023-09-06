#pragma once

#include <string>
#include <cstdint>
extern "C" {
#include <spa/pod/builder.h>

#undef SPA_VERSION_POD_BUILDER_CALLBACKS
#define SPA_VERSION_POD_BUILDER_CALLBACKS .version = 0
#include <spa/buffer/meta.h>
#include <spa/utils/result.h>
#include <spa/param/props.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/dynamic.h>
#undef SPA_VERSION_POD_BUILDER_CALLBACKS
#define SPA_VERSION_POD_BUILDER_CALLBACKS 0
}
#include <wayland-client.h>

#define XDPH_PWR_BUFFERS     4
#define XDPH_PWR_BUFFERS_MIN 2
#define XDPH_PWR_ALIGN       16

enum eSelectionType
{
    TYPE_INVALID = -1,
    TYPE_OUTPUT  = 0,
    TYPE_WINDOW,
    TYPE_GEOMETRY,
    TYPE_WORKSPACE,
};

struct zwlr_foreign_toplevel_handle_v1;

struct SSelectionData {
    eSelectionType                   type = TYPE_INVALID;
    std::string                      output;
    zwlr_foreign_toplevel_handle_v1* windowHandle = nullptr;
    uint32_t                         x = 0, y = 0, w = 0, h = 0; // for TYPE_GEOMETRY
    bool                             allowToken = false;
};

struct wl_buffer;

SSelectionData   promptForScreencopySelection();
uint32_t         drmFourccFromSHM(wl_shm_format format);
spa_video_format pwFromDrmFourcc(uint32_t format);
wl_shm_format    wlSHMFromDrmFourcc(uint32_t format);
spa_video_format pwStripAlpha(spa_video_format format);
std::string      getRandName(std::string prefix);
spa_pod*         build_format(spa_pod_builder* b, spa_video_format format, uint32_t width, uint32_t height, uint32_t framerate, uint64_t* modifiers, int modifier_count);
spa_pod*         fixate_format(spa_pod_builder* b, spa_video_format format, uint32_t width, uint32_t height, uint32_t framerate, uint64_t* modifier);
spa_pod*         build_buffer(spa_pod_builder* b, uint32_t blocks, uint32_t size, uint32_t stride, uint32_t datatype);
int              anonymous_shm_open();
wl_buffer*       import_wl_shm_buffer(int fd, wl_shm_format fmt, int width, int height, int stride);