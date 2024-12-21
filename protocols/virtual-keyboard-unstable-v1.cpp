// Generated with hyprwayland-scanner 0.4.2. Made with vaxry's keyboard and ❤️.
// virtual_keyboard_unstable_v1

/*
 This protocol's authors' copyright notice is:


    Copyright © 2008-2011  Kristian Høgsberg
    Copyright © 2010-2013  Intel Corporation
    Copyright © 2012-2013  Collabora, Ltd.
    Copyright © 2018       Purism SPC

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice (including the next
    paragraph) shall be included in all copies or substantial portions of the
    Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
  
*/

#define private public
#define HYPRWAYLAND_SCANNER_NO_INTERFACES
#include "virtual-keyboard-unstable-v1.hpp"
#undef private
#define F std::function

static const wl_interface* dummyTypes[] = { nullptr };

// Reference all other interfaces.
// The reason why this is in snake is to
// be able to cooperate with existing
// wayland_scanner interfaces (they are interop)
extern const wl_interface zwp_virtual_keyboard_v1_interface;
extern const wl_interface zwp_virtual_keyboard_manager_v1_interface;
extern const wl_interface wl_seat_interface;

static const void* _CCZwpVirtualKeyboardV1VTable[] = {
};

void CCZwpVirtualKeyboardV1::sendKeymap(uint32_t format, int32_t fd, uint32_t size) {
    if (!pResource)
        return;

    auto proxy = wl_proxy_marshal_flags((wl_proxy*)pResource, 0, nullptr, wl_proxy_get_version((wl_proxy*)pResource), 0, format, fd, size);
    proxy;
}

void CCZwpVirtualKeyboardV1::sendKey(uint32_t time, uint32_t key, uint32_t state) {
    if (!pResource)
        return;

    auto proxy = wl_proxy_marshal_flags((wl_proxy*)pResource, 1, nullptr, wl_proxy_get_version((wl_proxy*)pResource), 0, time, key, state);
    proxy;
}

void CCZwpVirtualKeyboardV1::sendModifiers(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    if (!pResource)
        return;

    auto proxy = wl_proxy_marshal_flags((wl_proxy*)pResource, 2, nullptr, wl_proxy_get_version((wl_proxy*)pResource), 0, mods_depressed, mods_latched, mods_locked, group);
    proxy;
}

void CCZwpVirtualKeyboardV1::sendDestroy() {
    if (!pResource)
        return;
    destroyed = true;

    auto proxy = wl_proxy_marshal_flags((wl_proxy*)pResource, 3, nullptr, wl_proxy_get_version((wl_proxy*)pResource), 1);
    proxy;
}
static const wl_interface* _CZwpVirtualKeyboardV1KeymapTypes[] = {
    nullptr,
    nullptr,
    nullptr,
};
static const wl_interface* _CZwpVirtualKeyboardV1KeyTypes[] = {
    nullptr,
    nullptr,
    nullptr,
};
static const wl_interface* _CZwpVirtualKeyboardV1ModifiersTypes[] = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

static const wl_message _CZwpVirtualKeyboardV1Requests[] = {
    { "keymap", "uhu", _CZwpVirtualKeyboardV1KeymapTypes + 0},
    { "key", "uuu", _CZwpVirtualKeyboardV1KeyTypes + 0},
    { "modifiers", "uuuu", _CZwpVirtualKeyboardV1ModifiersTypes + 0},
    { "destroy", "1", dummyTypes + 0},
};

const wl_interface zwp_virtual_keyboard_v1_interface = {
    "zwp_virtual_keyboard_v1", 1,
    4, _CZwpVirtualKeyboardV1Requests,
    0, nullptr,
};

CCZwpVirtualKeyboardV1::CCZwpVirtualKeyboardV1(wl_resource* resource) {
    pResource = resource;

    if (!pResource)
        return;

    wl_proxy_add_listener(pResource, (void (**)(void))&_CCZwpVirtualKeyboardV1VTable, this);
}

CCZwpVirtualKeyboardV1::~CCZwpVirtualKeyboardV1() {
    if (!destroyed)
        sendDestroy();
}

static const void* _CCZwpVirtualKeyboardManagerV1VTable[] = {
};

wl_proxy* CCZwpVirtualKeyboardManagerV1::sendCreateVirtualKeyboard(wl_resource* seat) {
    if (!pResource)
        return nullptr;

    auto proxy = wl_proxy_marshal_flags((wl_proxy*)pResource, 0, &zwp_virtual_keyboard_v1_interface, wl_proxy_get_version((wl_proxy*)pResource), 0, seat);

    return proxy;
}
static const wl_interface* _CZwpVirtualKeyboardManagerV1CreateVirtualKeyboardTypes[] = {
    &wl_seat_interface,
    &zwp_virtual_keyboard_v1_interface,
};

static const wl_message _CZwpVirtualKeyboardManagerV1Requests[] = {
    { "create_virtual_keyboard", "on", _CZwpVirtualKeyboardManagerV1CreateVirtualKeyboardTypes + 0},
};

const wl_interface zwp_virtual_keyboard_manager_v1_interface = {
    "zwp_virtual_keyboard_manager_v1", 1,
    1, _CZwpVirtualKeyboardManagerV1Requests,
    0, nullptr,
};

CCZwpVirtualKeyboardManagerV1::CCZwpVirtualKeyboardManagerV1(wl_resource* resource) {
    pResource = resource;

    if (!pResource)
        return;

    wl_proxy_add_listener(pResource, (void (**)(void))&_CCZwpVirtualKeyboardManagerV1VTable, this);
}

CCZwpVirtualKeyboardManagerV1::~CCZwpVirtualKeyboardManagerV1() {
    if (!destroyed)
        wl_proxy_destroy(pResource);
}

#undef F
