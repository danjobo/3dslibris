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

struct TextSelectionState {
  bool active = false;
  int anchor = -1; // word index where the selection started, -1 if unset
  int cursor = 0;  // word index under the cursor
  bool touch_dragging = false;

  SelectionPopup popup = SelectionPopup::None;
  int popup_index = 0;
  uint32_t popup_annotation_id = 0;
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

} // namespace text_selection_utils
