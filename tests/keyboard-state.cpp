#include "../src/shared/KeyboardState.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <linux/input-event-codes.h>

static void require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

int main() {
    const std::unique_ptr<xkb_context, decltype(&xkb_context_unref)> CONTEXT{xkb_context_new(XKB_CONTEXT_NO_FLAGS), xkb_context_unref};
    require(CONTEXT != nullptr, "create XKB context");
    xkb_rule_names names{};
    names.layout = "fr";
    const std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> KEYMAP{xkb_keymap_new_from_names(CONTEXT.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS), xkb_keymap_unref};
    require(KEYMAP != nullptr, "load French keymap");
    CKeyboardState keyboard{KEYMAP.get()};
    require(keyboard.get() != nullptr, "create keyboard state");

    require(xkb_state_key_get_utf32(keyboard.get(), KEY_Q + 8) == 'a', "use compositor's AZERTY layout");
    require(xkb_state_key_get_utf32(keyboard.get(), KEY_2 + 8) == U'é', "preserve accented characters");
    keyboard.key(KEY_RIGHTALT, true);
    require(xkb_state_key_get_utf32(keyboard.get(), KEY_0 + 8) == '@', "AltGr uses the keymap's modifier");
    keyboard.key(KEY_RIGHTALT, false);

    keyboard.key(KEY_LEFTSHIFT, true);
    keyboard.key(KEY_LEFTSHIFT, true);
    keyboard.key(KEY_LEFTSHIFT, false);
    require(xkb_state_serialize_mods(keyboard.get(), XKB_STATE_MODS_DEPRESSED) == 0, "duplicate press must not leave Shift stuck");
    require(!keyboard.key(KEY_LEFTSHIFT, false), "ignore releases for keys that are not held");

    keyboard.key(KEY_LEFTCTRL, true);
    keyboard.key(KEY_RIGHTCTRL, true);
    keyboard.key(KEY_LEFTCTRL, false);
    require(xkb_state_mod_name_is_active(keyboard.get(), XKB_MOD_NAME_CTRL, XKB_STATE_MODS_DEPRESSED) == 1, "right Ctrl stays held after left Ctrl release");
    keyboard.key(KEY_RIGHTCTRL, false);
    require(xkb_state_serialize_mods(keyboard.get(), XKB_STATE_MODS_DEPRESSED) == 0, "both Ctrl keys released");

    for (int cycle = 0; cycle < 100; ++cycle) {
        keyboard.key(KEY_LEFTCTRL, true);
        keyboard.key(KEY_RIGHTALT, true);
        keyboard.key(KEY_Q, true);
        auto released = keyboard.releaseAll();
        std::ranges::sort(released);
        require(released == std::vector<uint32_t>{KEY_Q, KEY_LEFTCTRL, KEY_RIGHTALT}, "cleanup returns every held key for virtual key-up events");
        require(keyboard.releaseAll().empty(), "cleanup is idempotent");
        require(xkb_state_serialize_mods(keyboard.get(), XKB_STATE_MODS_DEPRESSED) == 0, "cleanup clears depressed modifiers");
        require(xkb_state_key_get_utf32(keyboard.get(), KEY_Q + 8) == 'a', "resume without stale modifiers");
        require(xkb_state_serialize_layout(keyboard.get(), XKB_STATE_LAYOUT_EFFECTIVE) == 0, "cleanup preserves the active layout");
    }

    keyboard.key(KEY_CAPSLOCK, true);
    keyboard.key(KEY_CAPSLOCK, false);
    const auto LOCKED = xkb_state_serialize_mods(keyboard.get(), XKB_STATE_MODS_LOCKED);
    require(LOCKED != 0, "Caps Lock enabled");
    keyboard.key(KEY_LEFTSHIFT, true);
    keyboard.releaseAll();
    require(xkb_state_serialize_mods(keyboard.get(), XKB_STATE_MODS_LOCKED) == LOCKED, "cleanup preserves lock state");
    std::cout << "PASS: AZERTY, AltGr, duplicate presses, both Ctrl keys, lock state, 100 release/resume cycles\n";
}
