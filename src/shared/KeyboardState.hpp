#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>
#include <xkbcommon/xkbcommon.h>

// XKB counts key downs, including repeats. Track the actual held keys so a
// duplicate press cannot leave a modifier stuck, and teardown can release them.
class CKeyboardState {
  public:
    explicit CKeyboardState(xkb_keymap* keymap);

    xkb_state*            get() const;
    bool                  key(uint32_t code, bool pressed);
    std::vector<uint32_t> releaseAll();
    // Modifiers to drop from the current state so `code`, sent with `levelMods` added,
    // resolves to `sym` in `layout`. A level's mask selects it from a clean state, so a
    // locked Caps or a held Shift can otherwise flip what the client sees.
    xkb_mod_mask_t conflictingMods(uint32_t code, xkb_keysym_t sym, xkb_mod_mask_t levelMods, xkb_layout_index_t layout) const;
    // key(), for a key sent with `levelMods` added, `dropped` removed and `layout` locked,
    // so its XKB action (e.g. Caps_Lock on a Shift level) is the one the emitted event
    // selects. Afterwards only the real held modifiers stay depressed.
    bool key(uint32_t code, bool pressed, xkb_mod_mask_t levelMods, xkb_mod_mask_t dropped, xkb_layout_index_t layout);

  private:
    std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> m_state{nullptr, xkb_state_unref};
    std::unordered_set<uint32_t>                           m_pressed;
};
