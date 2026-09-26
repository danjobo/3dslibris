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

} // namespace

int main() {
  TestStepWord();
  TestVerticalNeighbor();
  TestWordAtPoint();
  TestSelectionBufRange();
  TestPopupIndex();
  TestResetSelection();
  return 0;
}
