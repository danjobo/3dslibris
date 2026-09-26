/*
    3dslibris - note_editor.h

    Edits highlight notes with the system software keyboard (swkbd).

    The keyboard is a library applet: launching it runs the APT ONSUSPEND /
    ONRESTORE hooks, so the app takes the normal resume path afterwards and
    the caller must redraw both screens.
*/

#pragma once

#include <string>

namespace note_editor {

// Maximum note length in characters (the keyboard enforces it).
static const int kMaxNoteChars = 500;

// Shows the keyboard pre-filled with initial. Returns true and stores the
// UTF-8 result in out when the user confirms; false when cancelled.
bool Edit(const std::string &initial, std::string *out);

} // namespace note_editor
