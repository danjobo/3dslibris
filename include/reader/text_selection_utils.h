/*
    3dslibris - text_selection_utils.h

    Pure helpers for the reader's text selection mode (highlights/notes):
    word boxes recorded while a page is drawn, cursor movement between them,
    touch hit-testing, and the selected buffer range.
*/

#pragma once

#include <stdint.h>
#include <vector>

#include "reader/inline_link_utils.h"

namespace text_selection_utils {

// One drawn word. buf_begin/buf_end are page-buffer indices of its glyphs;
// screen_index is 0 for the first screen in reading order, 1 for the second.
struct WordBox {
  int buf_begin;
  int buf_end;
  uint8_t screen_index;
  inline_link_utils::LinkRect bounds;
};

enum class SelectionPopup : uint8_t {
  None = 0,
  NewSelection,      // Highlight / Highlight + note / Cancel
  ExistingHighlight, // Edit note / Delete / Cancel
};

// Direction on the page, as the reader sees it.
enum class ScreenDirection : uint8_t { None = 0, Left, Right, Up, Down };

struct TextSelectionState {
  bool active = false;
  int anchor = -1; // word index where the selection started, -1 if unset
  int cursor = 0;  // word index under the cursor
  bool touch_dragging = false;
  // Show a scaled copy of the top screen on the touch screen so its words
  // can be selected by touch.
  bool mirror_top = false;
  ScreenDirection repeat_direction = ScreenDirection::None;
  uint64_t repeat_next_ms = 0;

  SelectionPopup popup = SelectionPopup::None;
  int popup_index = 0;
  uint64_t popup_annotation_id = 0;
  int popup_buf_begin = -1;
  int popup_buf_end = -1;

  bool x_hold_armed = false;
  bool x_hold_consumed = false;
  uint64_t x_hold_started_ms = 0;

  void ResetSelection() {
    active = false;
    anchor = -1;
    cursor = 0;
    touch_dragging = false;
    mirror_top = false;
    repeat_direction = ScreenDirection::None;
    repeat_next_ms = 0;
    popup = SelectionPopup::None;
    popup_index = 0;
    popup_annotation_id = 0;
    popup_buf_begin = -1;
    popup_buf_end = -1;
  }
};

// Moves one word forward (direction > 0) or back, clamped to the list.
int StepWord(int word_count, int current, int direction);

// Nearest word above (down == false) or below the current one, treating the
// second screen as sitting below the first. Returns -1 if there is none.
int VerticalNeighbor(const std::vector<WordBox> &words, int current,
                     bool down);

// Word on the given screen containing (x, y), padded by pad_px for fingers.
// Falls back to the nearest word on the same line. Returns -1 if none.
int WordAtPoint(const std::vector<WordBox> &words, uint8_t screen_index, int x,
                int y, int pad_px);

// Page-buffer range covered by words [min(a, b), max(a, b)].
bool SelectionBufRange(const std::vector<WordBox> &words, int a, int b,
                       int *buf_begin, int *buf_end);

// Index of the popup option chosen by moving up/down, wrapping around.
int StepPopupIndex(int option_count, int current, int direction);

// Converts pressed physical D-pad/Circle Pad directions into a direction on
// the page. In the portrait orientations the console is held rotated, so
// e.g. physical Left means "down the page" when turned left. First match wins
// in the order up, down, left, right.
ScreenDirection PhysicalToScreenDirection(unsigned char orientation, bool up,
                                          bool down, bool left, bool right);

// Reading-screen index (0 or 1) shown on the touch screen.
uint8_t TouchScreenIndex(unsigned char orientation);

// Tints one RGB565 pixel for a selection: multiply on light themes (text
// stays dark), 50% blend on dark themes (text stays light).
uint16_t TintPixel565(uint16_t pixel, uint16_t tint, bool dark_theme);

// Fitting the top screen, scaled and centered, onto the touch screen.
struct MirrorMap {
  int src_w, src_h;   // top screen logical size
  int dst_w, dst_h;   // touch screen logical size
  int draw_w, draw_h; // scaled image size
  int off_x, off_y;   // image offset on the touch screen
};
MirrorMap BuildMirrorMap(int src_w, int src_h, int dst_w, int dst_h);
// Maps a touch-screen point to the top screen; false outside the image.
bool MirrorDstToSrc(const MirrorMap &map, int dx, int dy, int *sx, int *sy);

} // namespace text_selection_utils
