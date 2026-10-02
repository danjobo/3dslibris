#include "settings/prefs_input_utils.h"

#include "shared/orientation_utils.h"

namespace prefs_input_utils {

bool ShouldReturnFromPrefs(uint32_t keys, bool book_context,
                           uint32_t b_key, uint32_t select_key,
                           uint32_t y_key, uint32_t start_key) {
  if (book_context)
    return (keys & (b_key | select_key | y_key)) != 0;
  return (keys & (b_key | select_key | y_key | start_key)) != 0;
}

bool ShouldRedrawPrefsAfterOverlayInput(bool prefs_dirty,
                                        bool prefs_mode_active) {
  return prefs_dirty && prefs_mode_active;
}

DpadListKeys DpadListKeysFor(unsigned char orientation, uint32_t dleft,
                             uint32_t dright) {
  DpadListKeys keys;
  if (orientation == orientation_utils::ORIENT_TURNED_LEFT) {
    keys.previous = dright;
    keys.next = dleft;
  } else {
    keys.previous = dleft;
    keys.next = dright;
  }
  return keys;
}

} // namespace prefs_input_utils
