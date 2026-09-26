#include "reader/note_editor.h"

#include <3ds.h>
#include <string.h>

namespace note_editor {

bool Edit(const std::string &initial, std::string *out) {
  if (!out)
    return false;

  // UTF-8 needs up to 4 bytes per character, plus the terminator.
  static char buffer[kMaxNoteChars * 4 + 1];
  memset(buffer, 0, sizeof(buffer));

  SwkbdState swkbd;
  swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, kMaxNoteChars);
  swkbdSetHintText(&swkbd, "Note");
  swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
  swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Save", true);
  swkbdSetFeatures(&swkbd, SWKBD_MULTILINE | SWKBD_DARKEN_TOP_SCREEN);
  swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
  if (!initial.empty())
    swkbdSetInitialText(&swkbd, initial.c_str());

  const SwkbdButton button = swkbdInputText(&swkbd, buffer, sizeof(buffer));
  if (button != SWKBD_BUTTON_CONFIRM)
    return false;
  *out = buffer;
  return true;
}

} // namespace note_editor
