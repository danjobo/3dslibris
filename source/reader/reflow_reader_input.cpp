/*
    3dslibris - reflow_reader_input.cpp
    New 3DS reader module by Rigle.

    Summary:
    - Input handling for reflowable books (EPUB/FB2/MOBI/TXT etc.) in reading mode.
    - Extracted from reader/app_book.cpp HandleEventInBook reflowable branch.
    - Inline link interaction helpers (EnterInlineLinkFocus, TryFollowTouchLink,
      etc.) are private to this translation unit.
*/

#include "reader/reflow_reader_input.h"

#include <3ds.h>
#include <algorithm>
#include <string.h>

#include "app/app.h"
#include "book/book.h"
#include "book/page.h"
#include "reader/book_page_nav.h"
#include "reader/inline_link_utils.h"
#include "reader/note_editor.h"
#include "reader/page_repeat_utils.h"
#include "reader/text_selection_utils.h"
#include "ui/button.h"
#include "settings/prefs.h"
#include "shared/app_flow_utils.h"
#include "shared/orientation_utils.h"
#include "ui/text.h"
#include "ui/ui_button_skin.h"

namespace {

static const uint64_t kInlineLinkHoldThresholdMs = 350;
static const uint64_t kPageRepeatInitialDelayMs = 400;
static const uint64_t kPageRepeatIntervalMs = 150;
static const int kInlineLinkSecondScreenYOffset = 420;
static const int kTouchLinkMinHitPx = 12;
static const int kTouchLinkPadPx = 4;

static const std::vector<Page::InlineLinkRenderEntry> *
CurrentPageInlineLinks(Book *book) {
  if (!book || book->GetPageCount() == 0 || book->IsFixedLayout())
    return NULL;
  Page *page = book->GetPage();
  if (!page)
    return NULL;
  return &page->GetRenderedInlineLinks();
}

static bool CurrentPageHasInlineLinks(Book *book) {
  const std::vector<Page::InlineLinkRenderEntry> *links =
      CurrentPageInlineLinks(book);
  return links && !links->empty();
}

static void ExitInlineLinkFocus(App *app, Book *book) {
  if (book)
    book->ClearFocusedInlineLink();
  if (app)
    app->SetInlineLinkFocusActive(false);
}

static bool EnterInlineLinkFocus(App *app, Book *book, Text *ts) {
  if (!app || !book || !ts)
    return false;
  const std::vector<Page::InlineLinkRenderEntry> *links =
      CurrentPageInlineLinks(book);
  if (!links || links->empty())
    return false;
  int focus_index = book->GetFocusedInlineLinkIndex();
  if (focus_index < 0 || focus_index >= (int)links->size())
    focus_index = 0;
  book->SetFocusedInlineLinkIndex(focus_index);
  app->SetInlineLinkFocusActive(true);
  book_nav::DrawPage(book, ts);
  return true;
}

static size_t InlineLinkCountForPage(Book *book, int page_index) {
  if (!book || book->IsFixedLayout() || page_index < 0 ||
      page_index >= book->GetPageCount())
    return 0;
  Page *page = book->GetPage(page_index);
  return page ? page->GetInlineLinkCount() : 0;
}

static bool MoveInlineLinkFocusSequential(Book *book, Text *ts, int direction) {
  if (!book || !ts || direction == 0 || book->IsFixedLayout())
    return false;
  const std::vector<Page::InlineLinkRenderEntry> *links =
      CurrentPageInlineLinks(book);
  if (!links || links->empty())
    return false;

  int current_index = book->GetFocusedInlineLinkIndex();
  if (current_index < 0 || current_index >= (int)links->size())
    current_index = 0;

  if (direction > 0 && current_index + 1 < (int)links->size()) {
    book->SetFocusedInlineLinkIndex(current_index + 1);
    book_nav::DrawPage(book, ts);
    return true;
  }
  if (direction < 0 && current_index > 0) {
    book->SetFocusedInlineLinkIndex(current_index - 1);
    book_nav::DrawPage(book, ts);
    return true;
  }

  const int page_count = (int)book->GetPageCount();
  const int current_page = book->GetPosition();
  if (direction > 0) {
    for (int page_index = current_page + 1; page_index < page_count; ++page_index) {
      const size_t link_count = InlineLinkCountForPage(book, page_index);
      if (link_count == 0)
        continue;
      book->SetPosition(page_index);
      book->SetFocusedInlineLinkIndex(0);
      book_nav::DrawPage(book, ts);
      return true;
    }
    return false;
  }

  for (int page_index = current_page - 1; page_index >= 0; --page_index) {
    const size_t link_count = InlineLinkCountForPage(book, page_index);
    if (link_count == 0)
      continue;
    book->SetPosition(page_index);
    book->SetFocusedInlineLinkIndex((int)link_count - 1);
    book_nav::DrawPage(book, ts);
    return true;
  }
  return false;
}

static bool FollowFocusedInlineLink(Book *book, Text *ts) {
  if (!book || !ts)
    return false;
  const std::vector<Page::InlineLinkRenderEntry> *links =
      CurrentPageInlineLinks(book);
  if (!links || links->empty())
    return false;
  const int focus_index = book->GetFocusedInlineLinkIndex();
  if (focus_index < 0 || focus_index >= (int)links->size())
    return false;
  const uint16_t href_id = (*links)[(size_t)focus_index].href_id;
  const std::string *href = book->GetInlineLinkHref(href_id);
  if (!href || href->empty())
    return false;
  uint16_t target_page = 0;
  bool found_anchor = book->FindChapterAnchorPage(*href, &target_page);
  bool found_doc = false;
  if (!found_anchor)
    found_doc = book->FindChapterDocStartPage(*href, &target_page);
  if (!found_anchor && !found_doc)
    return false;
  book->ClearFocusedInlineLink();
  return book_nav::SetPage(book, ts, target_page);
}

static bool TryFollowTouchLink(Book *book, Text *ts, int tx, int ty) {
  if (!book || !ts)
    return false;
  const std::vector<Page::InlineLinkRenderEntry> *links =
      CurrentPageInlineLinks(book);
  if (!links || links->empty())
    return false;

  int best_index = -1;
  for (int i = 0; i < (int)links->size(); ++i) {
    const Page::InlineLinkRenderEntry &entry = (*links)[(size_t)i];
    if (entry.screen_index != 1)
      continue;

    inline_link_utils::LinkRect r = entry.bounds;
    const int w = r.x1 - r.x0;
    const int h = r.y1 - r.y0;
    const int cx = r.x0 + w / 2;
    const int cy = r.y0 + h / 2;
    if (w < kTouchLinkMinHitPx) {
      r.x0 = cx - kTouchLinkMinHitPx / 2;
      r.x1 = cx + kTouchLinkMinHitPx / 2;
    }
    if (h < kTouchLinkMinHitPx) {
      r.y0 = cy - kTouchLinkMinHitPx / 2;
      r.y1 = cy + kTouchLinkMinHitPx / 2;
    }
    r.x0 -= kTouchLinkPadPx;
    r.y0 -= kTouchLinkPadPx;
    r.x1 += kTouchLinkPadPx;
    r.y1 += kTouchLinkPadPx;

    if (tx >= r.x0 && tx < r.x1 && ty >= r.y0 && ty < r.y1) {
      best_index = i;
      break;
    }
  }

  if (best_index < 0)
    return false;

  book->SetFocusedInlineLinkIndex(best_index);
  return FollowFocusedInlineLink(book, ts);
}

// ---------------------------------------------------------------------------
// Text selection mode (highlights and notes).
//
// Hold X to enter. The D-pad moves a word cursor (touch drag also selects on
// the touch screen); A marks the start, A again opens the action popup.
// ---------------------------------------------------------------------------

using text_selection_utils::SelectionPopup;
using text_selection_utils::TextSelectionState;
using text_selection_utils::WordBox;

static const uint64_t kSelectionHoldThresholdMs = 400;
static const int kTouchWordPadPx = 6;
static const int kPopupOptionCount = 3;
static const int kPopupButtonW = 220;
static const int kPopupButtonH = 36;
static const int kPopupButtonX = 10;
static const int kPopupButtonY0 = 90;
static const int kPopupButtonStride = 42;

static const std::vector<WordBox> *CurrentPageWords(Book *book) {
  if (!book || book->GetPageCount() == 0 || book->IsFixedLayout())
    return NULL;
  Page *page = book->GetPage();
  return page ? &page->GetRenderedWords() : NULL;
}

static const char *PopupLabel(SelectionPopup popup, int index) {
  static const char *kNew[kPopupOptionCount] = {"Highlight",
                                                "Highlight + note", "Cancel"};
  static const char *kExisting[kPopupOptionCount] = {
      "Edit note", "Delete highlight", "Cancel"};
  if (index < 0 || index >= kPopupOptionCount)
    return "";
  return popup == SelectionPopup::ExistingHighlight ? kExisting[index]
                                                    : kNew[index];
}

static void LayoutPopupButton(Button *button, int index) {
  button->Init();
  button->Resize(kPopupButtonW, kPopupButtonH);
  button->Move(kPopupButtonX, kPopupButtonY0 + index * kPopupButtonStride);
}

static void DrawSelectionPopup(App &app, Text *ts) {
  const TextSelectionState &sel = app.MutableTextSelection();
  if (sel.popup == SelectionPopup::None || !ts)
    return;
  for (int i = 0; i < kPopupOptionCount; i++) {
    Button button(ts);
    LayoutPopupButton(&button, i);
    button.SetLabel1(PopupLabel(sel.popup, i));
    button.Draw(ts->screenright, i == sel.popup_index);
  }
  ts->MarkScreenDirty(ts->screenright);
}

static int PopupOptionAt(Text *ts, int x, int y) {
  for (int i = 0; i < kPopupOptionCount; i++) {
    Button button(ts);
    LayoutPopupButton(&button, i);
    if (button.EnclosesPoint((u16)x, (u16)y))
      return i;
  }
  return -1;
}

// Selection feedback is drawn on a copy of the rendered page instead of
// re-rendering it: a full page draw is far too slow on Old 3DS to follow the
// D-pad or a stylus drag. Saved highlights are part of the copy.
static std::vector<u16> s_snapshot_left;
static std::vector<u16> s_snapshot_right;

static const u16 kSelectionTintLight = 0xAE7F; // light blue
static const u16 kSelectionTintDark = 0x2A1F;  // blue
static const u16 kCursorColor = 0xF800;        // red underline

static bool IsDarkTheme(Text *ts) {
  const int mode = ts->GetColorMode();
  return mode == 1 || mode == 4 || mode == 5;
}

static size_t ScreenBufferPixels(Text *ts) {
  return (size_t)ts->BufferStride() * (size_t)ts->BufferStride();
}

static void TakePageSnapshot(Text *ts) {
  const size_t pixels = ScreenBufferPixels(ts);
  s_snapshot_left.assign(ts->screenleft, ts->screenleft + pixels);
  s_snapshot_right.assign(ts->screenright, ts->screenright + pixels);
}

static void ReleasePageSnapshot() {
  std::vector<u16>().swap(s_snapshot_left);
  std::vector<u16>().swap(s_snapshot_right);
}

static void RestorePageSnapshot(Text *ts) {
  const size_t pixels = ScreenBufferPixels(ts);
  if (s_snapshot_left.size() != pixels || s_snapshot_right.size() != pixels)
    return;
  memcpy(ts->screenleft, s_snapshot_left.data(), pixels * sizeof(u16));
  memcpy(ts->screenright, s_snapshot_right.data(), pixels * sizeof(u16));
}

// Buffer holding a reading screen (0 or 1) for the book's orientation.
static u16 *ReadingScreenBuffer(Book *book, Text *ts, int screen_index) {
  const bool first_is_left = orientation_utils::FirstScreenIsLeft(
      (unsigned char)book->GetOrientation());
  const bool left = (screen_index == 0) == first_is_left;
  return left ? ts->screenleft : ts->screenright;
}

static void TintBufferRect(Text *ts, u16 *buf, bool is_left, int x0, int y0,
                           int x1, int y1, u16 tint, bool dark) {
  const int stride = ts->BufferStride();
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(ts->LogicalWidthFor(is_left), x1);
  y1 = std::min(ts->LogicalHeightFor(is_left), y1);
  for (int y = y0; y < y1; y++) {
    u16 *row = buf + y * stride;
    for (int x = x0; x < x1; x++)
      row[x] = text_selection_utils::TintPixel565(row[x], tint, dark);
  }
}

static void FillBufferRect(Text *ts, u16 *buf, bool is_left, int x0, int y0,
                           int x1, int y1, u16 color) {
  const int stride = ts->BufferStride();
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(ts->LogicalWidthFor(is_left), x1);
  y1 = std::min(ts->LogicalHeightFor(is_left), y1);
  for (int y = y0; y < y1; y++) {
    u16 *row = buf + y * stride;
    for (int x = x0; x < x1; x++)
      row[x] = color;
  }
}

static void TintSelectedWords(Book *book, Text *ts,
                              const std::vector<WordBox> &words, int first,
                              int last) {
  const u16 tint = IsDarkTheme(ts) ? kSelectionTintDark : kSelectionTintLight;
  const bool dark = IsDarkTheme(ts);
  for (int i = first; i <= last && i < (int)words.size(); i++) {
    const WordBox &w = words[(size_t)i];
    u16 *buf = ReadingScreenBuffer(book, ts, w.screen_index);
    const bool is_left = buf == ts->screenleft;
    int x1 = w.bounds.x1;
    // Also tint the gap to the next selected word on the same line.
    if (i < last && i + 1 < (int)words.size()) {
      const WordBox &next = words[(size_t)i + 1];
      if (next.screen_index == w.screen_index && next.bounds.y0 == w.bounds.y0 &&
          next.bounds.x0 > w.bounds.x1)
        x1 = next.bounds.x0;
    }
    TintBufferRect(ts, buf, is_left, w.bounds.x0, w.bounds.y0, x1,
                   w.bounds.y1, tint, dark);
  }
}

static void DrawCursorUnderline(Book *book, Text *ts, const WordBox &w) {
  u16 *buf = ReadingScreenBuffer(book, ts, w.screen_index);
  FillBufferRect(ts, buf, buf == ts->screenleft, w.bounds.x0, w.bounds.y1,
                 w.bounds.x1, w.bounds.y1 + 2, kCursorColor);
}

static text_selection_utils::MirrorMap TopMirrorMap(Text *ts) {
  return text_selection_utils::BuildMirrorMap(
      ts->LogicalWidthFor(true), ts->LogicalHeightFor(true),
      ts->LogicalWidthFor(false), ts->LogicalHeightFor(false));
}

// Paints a scaled copy of the top screen onto the touch screen.
static void DrawTopMirror(Text *ts) {
  const text_selection_utils::MirrorMap map = TopMirrorMap(ts);
  if (map.draw_w <= 0 || map.draw_h <= 0)
    return;
  const int stride = ts->BufferStride();
  const u16 *src = ts->screenleft;
  u16 *dst = ts->screenright;
  const u16 background = src[0];
  FillBufferRect(ts, dst, false, 0, 0, map.dst_w, map.dst_h, background);
  std::vector<int> src_x((size_t)map.draw_w);
  for (int x = 0; x < map.draw_w; x++)
    src_x[(size_t)x] = (int)((long)x * map.src_w / map.draw_w);
  for (int y = 0; y < map.draw_h; y++) {
    const int sy = (int)((long)y * map.src_h / map.draw_h);
    const u16 *src_row = src + sy * stride;
    u16 *dst_row = dst + (y + map.off_y) * stride + map.off_x;
    for (int x = 0; x < map.draw_w; x++)
      dst_row[x] = src_row[src_x[(size_t)x]];
  }
}

// Repaints selection feedback from the page snapshot: tinted range, cursor
// underline, optional top-screen mirror, and the action popup.
static void RedrawSelection(App &app, Book *book, Text *ts) {
  TextSelectionState &sel = app.MutableTextSelection();
  RestorePageSnapshot(ts);
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (words && !words->empty() && sel.cursor >= 0 &&
      sel.cursor < (int)words->size()) {
    const int first = sel.anchor >= 0 ? std::min(sel.anchor, sel.cursor)
                                      : sel.cursor;
    const int last = sel.anchor >= 0 ? std::max(sel.anchor, sel.cursor)
                                     : sel.cursor;
    TintSelectedWords(book, ts, *words, first, last);
    DrawCursorUnderline(book, ts, (*words)[(size_t)sel.cursor]);
  }
  if (sel.mirror_top)
    DrawTopMirror(ts);
  DrawSelectionPopup(app, ts);
  ts->MarkScreenDirty(ts->screenleft);
  ts->MarkScreenDirty(ts->screenright);
  // The status bar is drawn over the page; the snapshot predates it.
  app.RequestStatusRedraw();
}

static void ExitSelectionMode(App &app, Book *book, Text *ts) {
  app.MutableTextSelection().ResetSelection();
  ReleasePageSnapshot();
  if (book) {
    book->SetWordCaptureEnabled(false);
    book_nav::DrawPage(book, ts);
  }
  app.RequestStatusRedraw();
}

static bool EnterSelectionMode(App &app, Book *book, Text *ts) {
  if (!book || !ts || !book->SupportsAnnotations())
    return false;
  TextSelectionState &sel = app.MutableTextSelection();
  sel.ResetSelection();
  sel.active = true;
  book->SetWordCaptureEnabled(true);
  // One full draw records the word boxes; everything after works on a copy.
  book_nav::DrawPage(book, ts);
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words || words->empty()) {
    ExitSelectionMode(app, book, ts);
    app.PrintStatus("No text to select on this page");
    return false;
  }
  TakePageSnapshot(ts);
  sel.cursor = 0;
  RedrawSelection(app, book, ts);
  return true;
}

static uint64_t HighlightUnderCursor(Book *book,
                                     const TextSelectionState &sel) {
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words || sel.cursor < 0 || sel.cursor >= (int)words->size())
    return 0;
  return book->FindAnnotationAt(book->GetPosition(),
                                (*words)[(size_t)sel.cursor].buf_begin);
}

static void OpenPopupForSelection(App &app, Book *book, Text *ts) {
  TextSelectionState &sel = app.MutableTextSelection();
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words)
    return;
  const bool single_word = sel.anchor < 0 || sel.anchor == sel.cursor;
  const uint64_t existing = single_word ? HighlightUnderCursor(book, sel) : 0;
  sel.popup_index = 0;
  if (existing) {
    sel.popup = SelectionPopup::ExistingHighlight;
    sel.popup_annotation_id = existing;
  } else if (text_selection_utils::SelectionBufRange(
                 *words, sel.anchor, sel.cursor, &sel.popup_buf_begin,
                 &sel.popup_buf_end)) {
    sel.popup = SelectionPopup::NewSelection;
  } else {
    return;
  }
  RedrawSelection(app, book, ts);
}

static void ClosePopup(App &app, Book *book, Text *ts) {
  TextSelectionState &sel = app.MutableTextSelection();
  sel.popup = SelectionPopup::None;
  sel.popup_index = 0;
  sel.popup_annotation_id = 0;
  RedrawSelection(app, book, ts);
}

static void RunPopupOption(App &app, Book *book, Text *ts, int option) {
  TextSelectionState &sel = app.MutableTextSelection();
  const int page = book->GetPosition();
  if (sel.popup == SelectionPopup::NewSelection) {
    if (option == 0) {
      if (!book->AddAnnotationFromPageRange(page, sel.popup_buf_begin,
                                            sel.popup_buf_end, ""))
        app.PrintStatus("Nothing to highlight");
    } else if (option == 1) {
      std::string note;
      if (note_editor::Edit("", &note) &&
          !book->AddAnnotationFromPageRange(page, sel.popup_buf_begin,
                                            sel.popup_buf_end, note))
        app.PrintStatus("Nothing to highlight");
      // The keyboard applet replaced both screens.
      ts->MarkAllScreensDirty();
    } else {
      // Cancel keeps selection mode open so the range can be adjusted.
      sel.anchor = -1;
      ClosePopup(app, book, ts);
      return;
    }
  } else if (sel.popup == SelectionPopup::ExistingHighlight) {
    const uint64_t id = sel.popup_annotation_id;
    if (option == 0) {
      const Annotation *a = book->FindAnnotation(id);
      std::string note;
      if (a && note_editor::Edit(a->note, &note))
        book->SetAnnotationNote(id, note);
      ts->MarkAllScreensDirty();
    } else if (option == 1) {
      book->RemoveAnnotation(id);
    } else {
      ClosePopup(app, book, ts);
      return;
    }
  }
  ExitSelectionMode(app, book, ts);
}

static void MoveCursorTo(App &app, Book *book, Text *ts, int word) {
  TextSelectionState &sel = app.MutableTextSelection();
  if (word < 0 || word == sel.cursor)
    return;
  sel.cursor = word;
  RedrawSelection(app, book, ts);
}

static const uint64_t kCursorRepeatDelayMs = 350;
static const uint64_t kCursorRepeatIntervalMs = 80;

// Direction on the page currently pressed (new press or held), mapped from
// the physical D-pad / Circle Pad through the reading orientation.
static text_selection_utils::ScreenDirection PressedDirection(App &app,
                                                              Book *book,
                                                              uint32_t bits) {
  return text_selection_utils::PhysicalToScreenDirection(
      (unsigned char)book->GetOrientation(),
      (bits & (app.key.dup | app.key.up)) != 0,
      (bits & (app.key.ddown | app.key.down)) != 0,
      (bits & (app.key.dleft | app.key.left)) != 0,
      (bits & (app.key.dright | app.key.right)) != 0);
}

static int CursorTarget(const std::vector<WordBox> &words, int cursor,
                        text_selection_utils::ScreenDirection dir) {
  using text_selection_utils::ScreenDirection;
  switch (dir) {
  case ScreenDirection::Left:
    return text_selection_utils::StepWord((int)words.size(), cursor, -1);
  case ScreenDirection::Right:
    return text_selection_utils::StepWord((int)words.size(), cursor, 1);
  case ScreenDirection::Up:
    return text_selection_utils::VerticalNeighbor(words, cursor, false);
  case ScreenDirection::Down:
    return text_selection_utils::VerticalNeighbor(words, cursor, true);
  default:
    return -1;
  }
}

// Word under a touch point, looking through the top-screen mirror if shown.
static int TouchedWord(App &app, Book *book, Text *ts,
                       const std::vector<WordBox> &words,
                       const FrameInput &input) {
  const TextSelectionState &sel = app.MutableTextSelection();
  const touchPosition mapped = app.MapTouch(input);
  const unsigned char orientation = (unsigned char)book->GetOrientation();
  const uint8_t touch_index = text_selection_utils::TouchScreenIndex(orientation);
  if (!sel.mirror_top)
    return text_selection_utils::WordAtPoint(words, touch_index, mapped.px,
                                             mapped.py, kTouchWordPadPx);
  const text_selection_utils::MirrorMap map = TopMirrorMap(ts);
  int sx = 0;
  int sy = 0;
  if (!text_selection_utils::MirrorDstToSrc(map, mapped.px, mapped.py, &sx,
                                            &sy))
    return -1;
  // Scale the finger padding up to top-screen pixels.
  const int pad = map.draw_h > 0 ? kTouchWordPadPx * map.src_h / map.draw_h
                                 : kTouchWordPadPx;
  return text_selection_utils::WordAtPoint(words, (uint8_t)(1 - touch_index),
                                           sx, sy, pad);
}

// Handles one frame of input while selection mode is active. Always
// consumes the input.
static bool HandleSelectionInput(App &app, Book *book, Text *ts,
                                 const FrameInput &input) {
  using text_selection_utils::ScreenDirection;
  TextSelectionState &sel = app.MutableTextSelection();
  const uint32_t keys = input.keys_down;
  const uint32_t held = input.keys_held;
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words || words->empty()) {
    ExitSelectionMode(app, book, ts);
    return true;
  }
  const int word_count = (int)words->size();
  const ScreenDirection pressed = PressedDirection(app, book, keys);

  if (sel.popup != SelectionPopup::None) {
    if (keys & app.key.a) {
      RunPopupOption(app, book, ts, sel.popup_index);
    } else if (keys & app.key.b) {
      ClosePopup(app, book, ts);
    } else if (pressed == ScreenDirection::Up ||
               pressed == ScreenDirection::Down) {
      sel.popup_index = text_selection_utils::StepPopupIndex(
          kPopupOptionCount, sel.popup_index,
          pressed == ScreenDirection::Up ? -1 : 1);
      RedrawSelection(app, book, ts);
    } else if (keys & KEY_TOUCH) {
      const touchPosition mapped = app.MapTouch(input);
      const int option = PopupOptionAt(ts, mapped.px, mapped.py);
      if (option >= 0) {
        sel.popup_index = option;
        RunPopupOption(app, book, ts, option);
      }
    }
    return true;
  }

  // Touch drag: select from the touched word to the word under the finger.
  if (keys & KEY_TOUCH) {
    const int word = TouchedWord(app, book, ts, *words, input);
    if (word >= 0) {
      sel.anchor = word;
      sel.cursor = word;
      sel.touch_dragging = true;
      RedrawSelection(app, book, ts);
    }
    return true;
  }
  if (sel.touch_dragging) {
    if (held & KEY_TOUCH) {
      MoveCursorTo(app, book, ts, TouchedWord(app, book, ts, *words, input));
    } else {
      sel.touch_dragging = false;
      OpenPopupForSelection(app, book, ts);
    }
    return true;
  }

  // Cursor movement, repeating while a direction is held.
  const ScreenDirection held_dir = PressedDirection(app, book, held);
  const uint64_t now_ms = input.timestamp_ms;
  if (pressed != ScreenDirection::None) {
    sel.repeat_direction = pressed;
    sel.repeat_next_ms = now_ms + kCursorRepeatDelayMs;
    MoveCursorTo(app, book, ts, CursorTarget(*words, sel.cursor, pressed));
    return true;
  }
  if (held_dir == ScreenDirection::None || held_dir != sel.repeat_direction) {
    sel.repeat_direction = ScreenDirection::None;
  } else if (now_ms >= sel.repeat_next_ms) {
    sel.repeat_next_ms = now_ms + kCursorRepeatIntervalMs;
    MoveCursorTo(app, book, ts, CursorTarget(*words, sel.cursor, held_dir));
    return true;
  }

  if (keys & app.key.a) {
    if (sel.anchor < 0 && !HighlightUnderCursor(book, sel)) {
      sel.anchor = sel.cursor;
      RedrawSelection(app, book, ts);
    } else {
      OpenPopupForSelection(app, book, ts);
    }
  } else if (keys & app.key.b) {
    if (sel.anchor >= 0) {
      sel.anchor = -1;
      RedrawSelection(app, book, ts);
    } else {
      ExitSelectionMode(app, book, ts);
    }
  } else if (keys & app.key.y) {
    // Show the top screen on the touch screen so its words can be touched.
    sel.mirror_top = !sel.mirror_top;
    RedrawSelection(app, book, ts);
  } else if (keys & app.key.l) {
    MoveCursorTo(app, book, ts, 0);
  } else if (keys & app.key.r) {
    MoveCursorTo(app, book, ts, word_count - 1);
  } else if (keys & (app.key.start | app.key.select | app.key.x)) {
    ExitSelectionMode(app, book, ts);
  }
  return true;
}

} // namespace

namespace reflow_input {

bool HandleInBook(App &app, Book *book, Text *ts, Prefs * /*prefs*/,
                  const FrameInput &input, uint16_t *pagecurrent,
                  uint16_t *pagecount, const ReaderControls &ctrl) {
  const uint32_t keys = input.keys_down;
  const uint32_t held = input.keys_held;
  bool status_dirty = false;
  const bool has_inline_links = CurrentPageHasInlineLinks(book);
  const uint64_t now_ms = input.timestamp_ms;
  TextSelectionState &selection = app.MutableTextSelection();

  if (selection.active) {
    app.ResetPageRepeat();
    HandleSelectionInput(app, book, ts, input);
    return true;
  }

  // X: short press cycles the colour theme (on release), hold enters text
  // selection for highlights and notes.
  if (!app.IsInlineLinkFocusActive() && (keys & app.key.x)) {
    selection.x_hold_armed = true;
    selection.x_hold_consumed = false;
    selection.x_hold_started_ms = now_ms;
  }
  if (selection.x_hold_armed && (held & app.key.x) &&
      !selection.x_hold_consumed &&
      now_ms >= selection.x_hold_started_ms + kSelectionHoldThresholdMs) {
    selection.x_hold_consumed = true;
    app.ResetPageRepeat();
    if (EnterSelectionMode(app, book, ts))
      return true;
  }
  if (selection.x_hold_armed && !(held & app.key.x)) {
    const bool consumed = selection.x_hold_consumed;
    selection.x_hold_armed = false;
    selection.x_hold_consumed = false;
    selection.x_hold_started_ms = 0;
    if (!consumed && !app.IsInlineLinkFocusActive()) {
      int mode = ts->GetColorMode();
      int next = (mode + 1) % 6;
      app.colorMode = next;
      ts->SetColorMode(next);
      UiButtonSkin_SetColorMode(next);
      ts->MarkAllScreensDirty();
      book_nav::DrawPage(book, ts);
      status_dirty = true;
    }
  }

  if (!app.IsInlineLinkFocusActive() && (keys & app.key.y)) {
    app.SetInlineLinkHoldArmed(true);
    app.SetInlineLinkHoldConsumed(false);
    app.SetInlineLinkHoldStartedAtMs(now_ms);
  }

  if (!app.IsInlineLinkFocusActive() && app.IsInlineLinkHoldArmed() &&
      (held & app.key.y) && !app.IsInlineLinkHoldConsumed() &&
      has_inline_links &&
      now_ms >= app.GetInlineLinkHoldStartedAtMs() + kInlineLinkHoldThresholdMs) {
    if (EnterInlineLinkFocus(&app, book, ts)) {
      app.SetInlineLinkHoldConsumed(true);
      status_dirty = true;
    }
  }

  if (app.IsInlineLinkFocusActive()) {
    if (keys & (app.key.b | app.key.y)) {
      ExitInlineLinkFocus(&app, book);
      book_nav::DrawPage(book, ts);
      status_dirty = true;
    } else if (keys & app.key.a) {
      if (FollowFocusedInlineLink(book, ts)) {
        app.SetInlineLinkFocusActive(false);
        status_dirty = true;
      }
    } else if (keys & ctrl.link_next) {
      if (MoveInlineLinkFocusSequential(book, ts, 1))
        status_dirty = true;
    } else if (keys & ctrl.link_prev) {
      if (MoveInlineLinkFocusSequential(book, ts, -1))
        status_dirty = true;
    } else if (keys & ctrl.back_to_library) {
      ExitInlineLinkFocus(&app, book);
      app.ShowLibraryView();
    } else if (keys & ctrl.open_settings) {
      ExitInlineLinkFocus(&app, book);
      app.ShowSettingsView(true);
      app.prefs->Write();
    }
  } else {
    // D-pad up/down support page repeat. Circle Pad repeat is opt-in because
    // analog drift can otherwise trigger fast accidental page turns.
    const bool circle_repeat_enabled =
        app.prefs.get() && app.prefs->circle_pad_page_turn;
    const uint32_t repeat_next_keys =
        reader_input_utils::ReflowablePageRepeatKeys(
            app.key.down, app.key.ddown, circle_repeat_enabled);
    const uint32_t repeat_prev_keys =
        reader_input_utils::ReflowablePageRepeatKeys(
            app.key.up, app.key.dup, circle_repeat_enabled);
    const uint32_t repeat_keys = repeat_next_keys | repeat_prev_keys;
    const uint32_t non_repeat_held = held & ~repeat_keys;
    const uint32_t non_repeat_keys = keys & ~repeat_keys;
    bool repeat_next = false;
    bool repeat_prev = false;
    if (non_repeat_held == 0 && non_repeat_keys == 0 &&
        (held & repeat_next_keys)) {
      repeat_next = app.ShouldFirePageRepeat(
          reader::PAGE_REPEAT_NEXT, (keys & repeat_next_keys) != 0,
          (held & repeat_next_keys) != 0, now_ms,
          kPageRepeatInitialDelayMs, kPageRepeatIntervalMs);
    } else if (non_repeat_held == 0 && non_repeat_keys == 0 &&
               (held & repeat_prev_keys)) {
      repeat_prev = app.ShouldFirePageRepeat(
          reader::PAGE_REPEAT_PREVIOUS, (keys & repeat_prev_keys) != 0,
          (held & repeat_prev_keys) != 0, now_ms,
          kPageRepeatInitialDelayMs, kPageRepeatIntervalMs);
    } else if ((held & repeat_keys) == 0 || non_repeat_held != 0) {
      app.ResetPageRepeat();
    }

    if ((keys & ctrl.page_next) || repeat_next) {
      if (!book_nav::AdvancePage(book, ts, pagecurrent, pagecount, &status_dirty))
        app.ResetPageRepeat();
    } else if ((keys & ctrl.page_prev) || repeat_prev) {
      if (book_nav::TurnPage(book, ts, pagecurrent, *pagecount, -1))
        status_dirty = true;
      else
        app.ResetPageRepeat();
    } else if (keys & app.key.x) {
      // x: colour cycle on release / selection on hold, handled above
    } else if (keys & app.key.y) {
      // y without hold: consumed by hold-arm; no-op here
    } else if (keys & KEY_TOUCH) {
      app.ResetPageRepeat();
      touchPosition mapped = app.MapTouch(input);
      if (TryFollowTouchLink(book, ts, (int)mapped.px, (int)mapped.py)) {
        status_dirty = true;
      } else {
        const bool forward_zone =
            ((int)mapped.px >= ts->LogicalWidth() / 2);
        if (!forward_zone) {
          if (book_nav::TurnPage(book, ts, pagecurrent, *pagecount, -1))
            status_dirty = true;
        } else {
          book_nav::AdvancePage(book, ts, pagecurrent, pagecount, &status_dirty);
        }
      }
    } else if (keys & ctrl.back_to_library) {
      app.ResetPageRepeat();
      app.ShowLibraryView();
    } else if (keys & ctrl.open_settings) {
      app.ResetPageRepeat();
      app.ShowSettingsView(true);
      app.prefs->Write();
    } else if (keys & (ctrl.bookmark_prev | ctrl.bookmark_next)) {
      app.ResetPageRepeat();
      app_flow_utils::BookmarkJumpResult jump = app_flow_utils::FindBookmarkJumpTarget(
          book->GetBookmarks(), book->GetPosition(),
          (keys & ctrl.bookmark_prev)
              ? app_flow_utils::BookmarkJumpDirection::Previous
              : app_flow_utils::BookmarkJumpDirection::Next);
      if (jump.found) {
        book->SetPosition(jump.page);
        book_nav::DrawPage(book, ts);
        status_dirty = true;
      }
    }
  } // end else (reflowable input)

  if (app.IsInlineLinkHoldArmed() && !(held & app.key.y)) {
    const bool consumed = app.IsInlineLinkHoldConsumed();
    app.SetInlineLinkHoldArmed(false);
    app.SetInlineLinkHoldStartedAtMs(0);
    app.SetInlineLinkHoldConsumed(false);
    if (!consumed && !app.IsInlineLinkFocusActive())
      app.ToggleBookmark();
  }

  return status_dirty;
}

} // namespace reflow_input
