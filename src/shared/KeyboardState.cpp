#include "KeyboardState.hpp"

CKeyboardState::CKeyboardState(xkb_keymap* keymap) : m_state(xkb_state_new(keymap), xkb_state_unref) {
    ;
}

xkb_state* CKeyboardState::get() const {
    return m_state.get();
}

bool CKeyboardState::key(uint32_t code, bool pressed) {
    if (!m_state)
        return false;

    const bool CHANGED = pressed ? m_pressed.insert(code).second : m_pressed.erase(code) != 0;
    if (!CHANGED)
        return false;

    xkb_state_update_key(m_state.get(), code + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    return true;
}

std::vector<uint32_t> CKeyboardState::releaseAll() {
    std::vector<uint32_t> keys(m_pressed.begin(), m_pressed.end());
    for (const auto KEY : keys)
        key(KEY, false);
    return keys;
}
