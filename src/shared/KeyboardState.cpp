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

xkb_mod_mask_t CKeyboardState::conflictingMods(uint32_t code, xkb_keysym_t sym, xkb_mod_mask_t levelMods, xkb_layout_index_t layout) const {
    if (!m_state)
        return 0;

    const std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> SCRATCH{xkb_state_new(xkb_state_get_keymap(m_state.get())), xkb_state_unref};
    if (!SCRATCH)
        return 0;

    const auto DEPRESSED = xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_DEPRESSED);
    const auto LATCHED   = xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_LATCHED);
    const auto LOCKED    = xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_LOCKED);

    // Keep as much of the real state as still yields the keysym: all of it, then
    // without latches and locks, then nothing but the level's own modifiers.
    for (const auto DROPPED : {xkb_mod_mask_t{0}, LATCHED | LOCKED, DEPRESSED | LATCHED | LOCKED}) {
        xkb_state_update_mask(SCRATCH.get(), (DEPRESSED & ~DROPPED) | levelMods, LATCHED & ~DROPPED, LOCKED & ~DROPPED, 0, 0, layout);
        if (xkb_state_key_get_one_sym(SCRATCH.get(), code + 8) == sym)
            return DROPPED;
    }

    return DEPRESSED | LATCHED | LOCKED;
}
