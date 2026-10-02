#pragma once

#include <stdint.h>

namespace prefs_input_utils {

bool ShouldReturnFromPrefs(uint32_t keys, bool book_context,
                           uint32_t b_key, uint32_t select_key,
                           uint32_t y_key, uint32_t start_key);

bool ShouldRedrawPrefsAfterOverlayInput(bool prefs_dirty,
                                        bool prefs_mode_active);

// D-pad keys that move to the previous and next settings entry. Held like a
// book, the D-pad turns with the screen: turned left, its right arrow points
// up the list; turned right, its left arrow does. Landscape keeps left and
// right, since up and down change values there.
struct DpadListKeys {
  uint32_t previous;
  uint32_t next;
};
DpadListKeys DpadListKeysFor(unsigned char orientation, uint32_t dleft,
                             uint32_t dright);

} // namespace prefs_input_utils
