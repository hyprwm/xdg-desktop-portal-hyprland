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

  private:
    std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> m_state{nullptr, xkb_state_unref};
    std::unordered_set<uint32_t>                           m_pressed;
};
