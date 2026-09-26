#include "reader/text_selection_utils.h"

#include "test_assert.h"

#include <vector>

using text_selection_utils::WordBox;

namespace {

WordBox Word(int begin, int end, int screen, int x0, int y0, int x1, int y1) {
  WordBox w;
  w.buf_begin = begin;
  w.buf_end = end;
  w.screen_index = (uint8_t)screen;
  w.bounds.x0 = x0;
  w.bounds.y0 = y0;
  w.bounds.x1 = x1;
  w.bounds.y1 = y1;
  return w;
}

// Two lines on the first screen, one line on the second.
std::vector<WordBox> SampleWords() {
  std::vector<WordBox> words;
  words.push_back(Word(0, 3, 0, 10, 10, 40, 24));    // 0: line 1
  words.push_back(Word(4, 9, 0, 50, 10, 100, 24));   // 1: line 1
  words.push_back(Word(10, 12, 0, 10, 30, 30, 44));  // 2: line 2
  words.push_back(Word(13, 20, 0, 60, 30, 120, 44)); // 3: line 2
  words.push_back(Word(21, 25, 1, 10, 10, 60, 24));  // 4: second screen
  return words;
}

void TestStepWord() {
  test::ExpectEq("next", text_selection_utils::StepWord(5, 1, 1), 2);
  test::ExpectEq("prev", text_selection_utils::StepWord(5, 1, -1), 0);
  test::ExpectEq("clamp end", text_selection_utils::StepWord(5, 4, 1), 4);
  test::ExpectEq("clamp start", text_selection_utils::StepWord(5, 0, -1), 0);
  test::ExpectEq("empty", text_selection_utils::StepWord(0, 0, 1), -1);
}

void TestVerticalNeighbor() {
  const std::vector<WordBox> words = SampleWords();
  test::ExpectEq("down from line 1", text_selection_utils::VerticalNeighbor(
                                         words, 1, true),
                 3);
  test::ExpectEq("up from line 2",
                 text_selection_utils::VerticalNeighbor(words, 2, false), 0);
  test::ExpectEq("down crosses to second screen",
                 text_selection_utils::VerticalNeighbor(words, 2, true), 4);
  test::ExpectEq("up crosses back",
                 text_selection_utils::VerticalNeighbor(words, 4, false), 2);
  test::ExpectEq("nothing above",
                 text_selection_utils::VerticalNeighbor(words, 0, false), -1);
  test::ExpectEq("bad index",
                 text_selection_utils::VerticalNeighbor(words, 9, true), -1);
}

void TestWordAtPoint() {
  const std::vector<WordBox> words = SampleWords();
  test::ExpectEq("direct hit",
                 text_selection_utils::WordAtPoint(words, 0, 70, 15, 2), 1);
  test::ExpectEq("padding hit",
                 text_selection_utils::WordAtPoint(words, 0, 42, 15, 3), 0);
  test::ExpectEq("gap picks closest on line",
                 text_selection_utils::WordAtPoint(words, 0, 47, 15, 0), 1);
  test::ExpectEq("screen filter",
                 text_selection_utils::WordAtPoint(words, 1, 20, 15, 2), 4);
  test::ExpectEq("no line there",
                 text_selection_utils::WordAtPoint(words, 0, 20, 200, 2), -1);
}

void TestSelectionBufRange() {
  const std::vector<WordBox> words = SampleWords();
  int begin = 0, end = 0;
  test::ExpectTrue("forward", text_selection_utils::SelectionBufRange(
                                  words, 1, 3, &begin, &end));
  test::ExpectEq("forward begin", begin, 4);
  test::ExpectEq("forward end", end, 20);
  test::ExpectTrue("backward", text_selection_utils::SelectionBufRange(
                                   words, 3, 1, &begin, &end));
  test::ExpectEq("backward begin", begin, 4);
  test::ExpectTrue("cursor only", text_selection_utils::SelectionBufRange(
                                      words, -1, 2, &begin, &end));
  test::ExpectEq("cursor begin", begin, 10);
  test::ExpectEq("cursor end", end, 12);
  test::ExpectFalse("out of range", text_selection_utils::SelectionBufRange(
                                        words, 0, 7, &begin, &end));
}

void TestPopupIndex() {
  test::ExpectEq("down", text_selection_utils::StepPopupIndex(3, 0, 1), 1);
  test::ExpectEq("wrap down", text_selection_utils::StepPopupIndex(3, 2, 1), 0);
  test::ExpectEq("wrap up", text_selection_utils::StepPopupIndex(3, 0, -1), 2);
}

void TestResetSelection() {
  text_selection_utils::TextSelectionState state;
  state.active = true;
  state.anchor = 3;
  state.popup = text_selection_utils::SelectionPopup::NewSelection;
  state.x_hold_armed = true;
  state.ResetSelection();
  test::ExpectFalse("inactive", state.active);
  test::ExpectEq("anchor cleared", state.anchor, -1);
  test::ExpectTrue("popup cleared",
                   state.popup == text_selection_utils::SelectionPopup::None);
  test::ExpectTrue("hold state untouched", state.x_hold_armed);
}

void TestPhysicalToScreenDirection() {
  using text_selection_utils::PhysicalToScreenDirection;
  using text_selection_utils::ScreenDirection;
  const unsigned char kLeft = 0, kRight = 1, kLandscape = 2;
  // Landscape: no rotation.
  test::ExpectTrue("landscape left",
                   PhysicalToScreenDirection(kLandscape, false, false, true,
                                             false) == ScreenDirection::Left);
  // Turned left: console top points to the reader's left.
  test::ExpectTrue("turned-left up is page left",
                   PhysicalToScreenDirection(kLeft, true, false, false,
                                             false) == ScreenDirection::Left);
  test::ExpectTrue("turned-left down is page right",
                   PhysicalToScreenDirection(kLeft, false, true, false,
                                             false) == ScreenDirection::Right);
  test::ExpectTrue("turned-left left is page down",
                   PhysicalToScreenDirection(kLeft, false, false, true,
                                             false) == ScreenDirection::Down);
  test::ExpectTrue("turned-left right is page up",
                   PhysicalToScreenDirection(kLeft, false, false, false,
                                             true) == ScreenDirection::Up);
  // Turned right mirrors it.
  test::ExpectTrue("turned-right up is page right",
                   PhysicalToScreenDirection(kRight, true, false, false,
                                             false) == ScreenDirection::Right);
  test::ExpectTrue("turned-right left is page up",
                   PhysicalToScreenDirection(kRight, false, false, true,
                                             false) == ScreenDirection::Up);
  test::ExpectTrue("nothing pressed",
                   PhysicalToScreenDirection(kLeft, false, false, false,
                                             false) == ScreenDirection::None);
}

void TestTouchScreenIndex() {
  test::ExpectEq("turned left", text_selection_utils::TouchScreenIndex(0), 1);
  test::ExpectEq("turned right", text_selection_utils::TouchScreenIndex(1), 0);
  test::ExpectEq("landscape", text_selection_utils::TouchScreenIndex(2), 1);
}

void TestTintPixel() {
  using text_selection_utils::TintPixel565;
  // Light theme multiply: white takes the tint, black stays black.
  test::ExpectEqU("white becomes tint", TintPixel565(0xFFFF, 0xFF71, false),
                  0xFF71);
  test::ExpectEqU("black stays black", TintPixel565(0x0000, 0xFF71, false),
                  0x0000);
  // Dark theme blend: halfway between pixel and tint.
  test::ExpectEqU("dark blend", TintPixel565(0x0000, 0xF81F, true),
                  (15u << 11) | 15u);
}

void TestMirrorMap() {
  // Portrait: 240x400 top screen onto the 240x320 touch screen.
  text_selection_utils::MirrorMap portrait =
      text_selection_utils::BuildMirrorMap(240, 400, 240, 320);
  test::ExpectEq("portrait draw w", portrait.draw_w, 192);
  test::ExpectEq("portrait draw h", portrait.draw_h, 320);
  test::ExpectEq("portrait off x", portrait.off_x, 24);
  test::ExpectEq("portrait off y", portrait.off_y, 0);
  int sx = 0, sy = 0;
  test::ExpectTrue("inside", text_selection_utils::MirrorDstToSrc(
                                 portrait, 24 + 96, 160, &sx, &sy));
  test::ExpectEq("center x", sx, 120);
  test::ExpectEq("center y", sy, 200);
  test::ExpectFalse("left margin", text_selection_utils::MirrorDstToSrc(
                                       portrait, 10, 160, &sx, &sy));

  // Landscape: 400x240 onto 320x240.
  text_selection_utils::MirrorMap landscape =
      text_selection_utils::BuildMirrorMap(400, 240, 320, 240);
  test::ExpectEq("landscape draw w", landscape.draw_w, 320);
  test::ExpectEq("landscape draw h", landscape.draw_h, 192);
  test::ExpectEq("landscape off y", landscape.off_y, 24);
  test::ExpectTrue("corner", text_selection_utils::MirrorDstToSrc(
                                 landscape, 319, 24 + 191, &sx, &sy));
  test::ExpectTrue("corner in range", sx < 400 && sy < 240);
}

} // namespace

int main() {
  TestPhysicalToScreenDirection();
  TestTouchScreenIndex();
  TestTintPixel();
  TestMirrorMap();
  TestStepWord();
  TestVerticalNeighbor();
  TestWordAtPoint();
  TestSelectionBufRange();
  TestPopupIndex();
  TestResetSelection();
  return 0;
}
