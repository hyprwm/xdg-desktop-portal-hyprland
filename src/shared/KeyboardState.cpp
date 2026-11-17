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

bool CKeyboardState::key(uint32_t code, bool pressed, xkb_mod_mask_t levelMods, xkb_mod_mask_t dropped, xkb_layout_index_t layout) {
    if (!m_state || pressed == m_pressed.contains(code))
        return false;

    const auto DEPRESSED    = xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_DEPRESSED);
    const auto LATCHED      = xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_LATCHED);
    const auto LOCKED       = xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_LOCKED);
    const auto BASEGROUP    = xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_DEPRESSED);
    const auto LATCHEDGROUP = xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_LATCHED);
    const auto LOCKEDGROUP  = xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_LOCKED);
    const auto GROUP        = layout == XKB_LAYOUT_INVALID ? xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_EFFECTIVE) : layout;

    // Run the action in the emitted state. xkb keeps per-key action state across mask
    // updates, so a lock toggled here is released correctly by the matching key-up.
    xkb_state_update_mask(m_state.get(), (DEPRESSED & ~dropped) | levelMods, LATCHED & ~dropped, LOCKED & ~dropped, BASEGROUP, LATCHEDGROUP, GROUP);
    key(code, pressed);

    // Keep what the action changed, but put back the real modifiers the level overrode.
    const auto SYNTHETIC = levelMods & ~DEPRESSED;
    const auto NEWGROUP  = xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_LOCKED);
    xkb_state_update_mask(m_state.get(), (xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_DEPRESSED) & ~SYNTHETIC) | (DEPRESSED & dropped),
                          xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_LATCHED), xkb_state_serialize_mods(m_state.get(), XKB_STATE_MODS_LOCKED) | (LOCKED & dropped),
                          BASEGROUP, xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_LATCHED), NEWGROUP == GROUP ? LOCKEDGROUP : NEWGROUP);
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
    const auto PRODUCES = [&](xkb_mod_mask_t dropped) {
        xkb_state_update_mask(SCRATCH.get(), (DEPRESSED & ~dropped) | levelMods, LATCHED & ~dropped, LOCKED & ~dropped, 0, 0, layout);
        return xkb_state_key_get_one_sym(SCRATCH.get(), code + 8) == sym;
    };

    for (auto dropped : {xkb_mod_mask_t{0}, LATCHED | LOCKED, DEPRESSED | LATCHED | LOCKED}) {
        if (!PRODUCES(dropped))
            continue;

        // Hand back every modifier the keysym doesn't need gone, so a held Ctrl
        // survives dropping the Shift that would capitalize it.
        for (xkb_mod_mask_t bit = 1; bit && bit <= dropped; bit <<= 1) {
            if ((dropped & bit) && PRODUCES(dropped & ~bit))
                dropped &= ~bit;
        }
        return dropped;
    }

    return DEPRESSED | LATCHED | LOCKED;
}
