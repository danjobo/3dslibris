/*
    3dslibris - reflow_reader_input.cpp
    New 3DS reader module by Rigle.

    Summary:
    - Input handling for reflowable books (EPUB/FB2/MOBI/TXT etc.) in reading mode.
    - Extracted from reader/app_book.cpp HandleEventInBook reflowable branch.
    - Inline links are followed by touch, or from the word lookup popup
      (hold Y on a word that is part of a link).
    - Text selection (hold X: highlights and notes) and word lookup (hold
      Y: dictionary and online definitions) share the word cursor below.
*/

#include "reader/reflow_reader_input.h"

#include <3ds.h>
#include <algorithm>
#include <string.h>
#include <string>

#include "app/app.h"
#include "book/annotation_text_utils.h"
#include "book/book.h"
#include "book/highlight_color_utils.h"
#include "book/page.h"
#include "dictionary/dictionary_set.h"
#include "dictionary/web_lookup.h"
#include "dictionary/word_lookup_utils.h"
#include "reader/book_page_nav.h"
#include "reader/inline_link_utils.h"
#include "reader/note_editor.h"
#include "reader/page_repeat_utils.h"
#include "reader/text_selection_utils.h"
#include "reader/word_lookup_panel.h"
#include "ui/button.h"
#include "settings/prefs.h"
#include "shared/app_flow_utils.h"
#include "shared/orientation_utils.h"
#include "shared/path_constants.h"
#include "ui/text.h"
#include "ui/ui_button_skin.h"

namespace {

static const uint64_t kPageRepeatInitialDelayMs = 400;
static const uint64_t kPageRepeatIntervalMs = 150;
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
//
// Hold Y for word lookup: the same cursor picks one word, and A (or a tap)
// opens Dictionary / Look up online / Follow link. Definitions show in a
// scrolling panel on the touch screen.
// ---------------------------------------------------------------------------

using text_selection_utils::SelectionPopup;
using text_selection_utils::TextSelectionState;
using text_selection_utils::WordBox;

static const uint64_t kSelectionHoldThresholdMs = 400;
static const int kTouchWordPadPx = 6;
static const int kMaxPopupOptions = 5;
static const int kPopupButtonW = 220;
static const int kPopupButtonH = 32;
static const int kPopupButtonX = 10;
// Five rows fit the 240px-tall touch screen in landscape too.
static const int kPopupButtonY0 = 44;
static const int kPopupButtonStride = 37;
// The new-selection row that adds or shows a character.
static const int kCharacterOption = 3;
// Longer selections aren't offered as a character name.
static const size_t kMaxCharacterNameChars = 60;

static const std::vector<WordBox> *CurrentPageWords(Book *book) {
  if (!book || book->GetPageCount() == 0 || book->IsFixedLayout())
    return NULL;
  Page *page = book->GetPage();
  return page ? &page->GetRenderedWords() : NULL;
}

// The popup row that picks the highlight color, -1 if there is none.
static int ColorOption(SelectionPopup popup) {
  if (popup == SelectionPopup::WordLookup)
    return -1;
  return popup == SelectionPopup::ExistingHighlight ? 1 : 2;
}

enum class LookupAction : uint8_t { Dictionary, Online, FollowLink, Cancel };

static int PopupOptionCount(const TextSelectionState &sel) {
  if (sel.popup == SelectionPopup::WordLookup)
    return sel.popup_link >= 0 ? 4 : 3;
  if (sel.popup == SelectionPopup::ExistingHighlight)
    return 4;
  return kMaxPopupOptions;
}

static bool CharacterNameFits(const std::string &name) {
  return !name.empty() &&
         annotation_text_utils::Utf8ToCodepoints(name).size() <=
             kMaxCharacterNameChars;
}

static std::string CharacterLabel(const TextSelectionState &sel) {
  if (!CharacterNameFits(sel.popup_word))
    return "Character: select just a name";
  if (!sel.popup_character_id)
    return "Add as character";
  std::vector<uint32_t> name =
      annotation_text_utils::Utf8ToCodepoints(sel.popup_word);
  std::string shown = annotation_text_utils::CodepointsToUtf8(
      name, 0, std::min(name.size(), (size_t)16));
  if (name.size() > 16)
    shown += "...";
  return "Mentions of " + shown;
}

static LookupAction LookupActionAt(const TextSelectionState &sel, int index) {
  if (index == 0)
    return LookupAction::Dictionary;
  if (index == 1)
    return LookupAction::Online;
  if (index == 2 && sel.popup_link >= 0)
    return LookupAction::FollowLink;
  return LookupAction::Cancel;
}

static std::string PopupLabel(const TextSelectionState &sel, int index) {
  static const char *kNew[kMaxPopupOptions] = {"Highlight", "Highlight + note",
                                               NULL, NULL, "Cancel"};
  static const char *kExisting[kMaxPopupOptions] = {
      "Edit note", NULL, "Delete highlight", "Cancel", NULL};
  if (index < 0 || index >= PopupOptionCount(sel))
    return "";
  if (sel.popup == SelectionPopup::WordLookup) {
    switch (LookupActionAt(sel, index)) {
    case LookupAction::Dictionary:
      return "Dictionary";
    case LookupAction::Online:
      return "Look up online";
    case LookupAction::FollowLink:
      return "Follow link";
    default:
      return "Cancel";
    }
  }
  if (sel.popup == SelectionPopup::NewSelection && index == kCharacterOption)
    return CharacterLabel(sel);
  if (index == ColorOption(sel.popup))
    return std::string("Color: ") + highlight_color_utils::Name(sel.popup_color) +
           "  < >";
  return sel.popup == SelectionPopup::ExistingHighlight ? kExisting[index]
                                                        : kNew[index];
}

static void LayoutPopupButton(Button *button, int index) {
  button->Init();
  button->Resize(kPopupButtonW, kPopupButtonH);
  button->Move(kPopupButtonX, kPopupButtonY0 + index * kPopupButtonStride);
}

static bool IsDarkTheme(Text *ts);
static void FillBufferRect(Text *ts, u16 *buf, bool is_left, int x0, int y0,
                           int x1, int y1, u16 color);

static void DrawSelectionPopup(App &app, Text *ts) {
  const TextSelectionState &sel = app.MutableTextSelection();
  if (sel.popup == SelectionPopup::None || !ts)
    return;
  if (sel.popup == SelectionPopup::WordLookup && !sel.popup_word.empty()) {
    // The word being looked up, above the buttons.
    const int saved_style = ts->GetStyle();
    u16 *saved_screen = ts->GetScreen();
    ts->SetScreen(ts->screenright);
    ts->SetStyle(TEXT_STYLE_BROWSER);
    const int x1 = kPopupButtonX + kPopupButtonW;
    const int y1 = kPopupButtonY0 - 6;
    const int y0 = y1 - (int)ts->GetHeight() - 10;
    FillBufferRect(ts, ts->screenright, false, kPopupButtonX, y0, x1, y1,
                   ts->GetBgColor());
    ts->SetPen(kPopupButtonX + 6, (u16)(y1 - 6));
    ts->PrintString(("\"" + sel.popup_word + "\"").c_str());
    ts->SetStyle(saved_style);
    ts->SetScreen(saved_screen);
  }
  for (int i = 0; i < PopupOptionCount(sel); i++) {
    Button button(ts);
    LayoutPopupButton(&button, i);
    const std::string label = PopupLabel(sel, i);
    button.SetLabel1(label.c_str());
    button.Draw(ts->screenright, i == sel.popup_index);
    if (i == ColorOption(sel.popup)) {
      // A swatch of the color as it looks behind text in this theme.
      const int y0 = kPopupButtonY0 + i * kPopupButtonStride;
      const int x1 = kPopupButtonX + kPopupButtonW - 8;
      FillBufferRect(ts, ts->screenright, false, x1 - 34, y0 + 8, x1,
                     y0 + kPopupButtonH - 8,
                     highlight_color_utils::Tint(sel.popup_color,
                                                 IsDarkTheme(ts)));
    }
  }
  ts->MarkScreenDirty(ts->screenright);
}

static int PopupOptionAt(const TextSelectionState &sel, Text *ts, int x,
                         int y) {
  for (int i = 0; i < PopupOptionCount(sel); i++) {
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
  if (word_lookup_panel::IsVisible())
    word_lookup_panel::Draw(ts);
  else
    DrawSelectionPopup(app, ts);
  ts->MarkScreenDirty(ts->screenleft);
  ts->MarkScreenDirty(ts->screenright);
  // The status bar is drawn over the page; the snapshot predates it.
  app.RequestStatusRedraw();
}

// What the lookup panel shows, so A can switch to the other source.
enum class PanelSource : uint8_t { Dictionary, Online };
// Waiting for the "Looking up..." panel to be on screen before the
// (blocking) lookup runs on the next frame.
enum class PendingLookup : uint8_t { None, Dictionary, Online };
static PendingLookup s_pending_lookup = PendingLookup::None;
static PanelSource s_panel_source = PanelSource::Dictionary;
// A finger on the lookup panel: where it went down and the first line shown
// then. It scrolls with the finger once it has moved kPanelDragSlopPx; a
// tap without moving pages up or down instead.
static const int kPanelDragSlopPx = 6;
static bool s_panel_drag_active = false;
static bool s_panel_drag_moved = false;
static int s_panel_drag_start_y = 0;
static int s_panel_drag_start_top = 0;

static void ExitSelectionMode(App &app, Book *book, Text *ts) {
  app.MutableTextSelection().ResetSelection();
  word_lookup_panel::Hide();
  s_pending_lookup = PendingLookup::None;
  ReleasePageSnapshot();
  if (book) {
    book->SetWordCaptureEnabled(false);
    book_nav::DrawPage(book, ts);
  }
  app.RequestStatusRedraw();
}

static bool EnterSelectionMode(App &app, Book *book, Text *ts, bool lookup) {
  if (!book || !ts || !book->SupportsAnnotations())
    return false;
  TextSelectionState &sel = app.MutableTextSelection();
  sel.ResetSelection();
  word_lookup_panel::Hide();
  s_pending_lookup = PendingLookup::None;
  sel.active = true;
  sel.lookup = lookup;
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

// Redraws the current page with word capture on and snapshots it (after a
// page change or a highlight color change). False if it has no words.
static bool RecapturePage(Book *book, Text *ts) {
  book_nav::DrawPage(book, ts);
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words || words->empty())
    return false;
  TakePageSnapshot(ts);
  return true;
}

// The cursor is on the last word with a selection running: keep the
// selection's start and continue it on the next page.
static bool CarrySelectionToNextPage(App &app, Book *book, Text *ts) {
  TextSelectionState &sel = app.MutableTextSelection();
  const std::vector<WordBox> *words = CurrentPageWords(book);
  const int page = book->GetPosition();
  if (!words || sel.anchor < 0 || sel.anchor >= (int)words->size() ||
      sel.carried_page >= 0 || page + 1 >= (int)book->GetPageCount())
    return false;
  const int start = (*words)[(size_t)sel.anchor].buf_begin;
  book_nav::SetPage(book, ts, (uint16_t)(page + 1));
  if (!RecapturePage(book, ts)) {
    // Nothing selectable there (e.g. a picture): stay where we were.
    book_nav::SetPage(book, ts, (uint16_t)page);
    RecapturePage(book, ts);
    RedrawSelection(app, book, ts);
    return true;
  }
  sel.carried_page = page;
  sel.carried_buf_begin = start;
  sel.anchor = 0;
  sel.cursor = 0;
  RedrawSelection(app, book, ts);
  return true;
}

// Back over the page break: the selection again starts and ends on the
// previous page.
static void ReturnSelectionToPreviousPage(App &app, Book *book, Text *ts) {
  TextSelectionState &sel = app.MutableTextSelection();
  const int page = sel.carried_page;
  const int start = sel.carried_buf_begin;
  sel.carried_page = -1;
  sel.carried_buf_begin = -1;
  book_nav::SetPage(book, ts, (uint16_t)page);
  RecapturePage(book, ts);
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words || words->empty()) {
    ExitSelectionMode(app, book, ts);
    return;
  }
  sel.anchor = 0;
  for (size_t i = 0; i < words->size(); i++) {
    if ((*words)[i].buf_begin >= start) {
      sel.anchor = (int)i;
      break;
    }
  }
  sel.cursor = (int)words->size() - 1;
  RedrawSelection(app, book, ts);
}

static uint64_t HighlightUnderCursor(Book *book,
                                     const TextSelectionState &sel) {
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words || sel.cursor < 0 || sel.cursor >= (int)words->size())
    return 0;
  return book->FindAnnotationAt(book->GetPosition(),
                                (*words)[(size_t)sel.cursor].buf_begin);
}

// The selected text (normalized like a highlight's quote).
static std::string SelectedText(Book *book, const TextSelectionState &sel) {
  Page *page = book->GetPage(sel.popup_page);
  if (!page || !page->GetBuffer())
    return std::string();
  std::string quote;
  std::string prefix;
  if (sel.popup_page != book->GetPosition()) {
    Page *next = book->GetPage(sel.popup_page + 1);
    if (!next || !next->GetBuffer() ||
        !annotation_text_utils::BuildAnchorAcrossPages(
            page->GetBuffer(), page->GetLength(), sel.popup_buf_begin,
            next->GetBuffer(), next->GetLength(), sel.popup_buf_end,
            kMaxCharacterNameChars + 1, 0, &quote, &prefix))
      return std::string();
  } else if (!annotation_text_utils::BuildAnchorFromBufferRange(
                 page->GetBuffer(), page->GetLength(), sel.popup_buf_begin,
                 sel.popup_buf_end, kMaxCharacterNameChars + 1, 0, &quote,
                 &prefix)) {
    return std::string();
  }
  return word_lookup_utils::CleanSelectedWord(quote);
}

static void OpenPopupForSelection(App &app, Book *book, Text *ts) {
  TextSelectionState &sel = app.MutableTextSelection();
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words)
    return;
  const bool carried = sel.carried_page >= 0;
  const bool single_word =
      !carried && (sel.anchor < 0 || sel.anchor == sel.cursor);
  const uint64_t existing = single_word ? HighlightUnderCursor(book, sel) : 0;
  sel.popup_index = 0;
  if (existing) {
    sel.popup = SelectionPopup::ExistingHighlight;
    sel.popup_annotation_id = existing;
    const Annotation *a = book->FindAnnotation(existing);
    sel.popup_color = a ? a->color : 0;
  } else if (carried && sel.cursor >= 0 && sel.cursor < (int)words->size()) {
    sel.popup = SelectionPopup::NewSelection;
    sel.popup_page = sel.carried_page;
    sel.popup_buf_begin = sel.carried_buf_begin;
    sel.popup_buf_end = (*words)[(size_t)sel.cursor].buf_end;
    sel.popup_color = app.prefs ? app.prefs->highlight_color : 0;
  } else if (text_selection_utils::SelectionBufRange(
                 *words, sel.anchor, sel.cursor, &sel.popup_buf_begin,
                 &sel.popup_buf_end)) {
    sel.popup = SelectionPopup::NewSelection;
    sel.popup_page = book->GetPosition();
    sel.popup_color = app.prefs ? app.prefs->highlight_color : 0;
  } else {
    return;
  }
  if (sel.popup == SelectionPopup::NewSelection) {
    sel.popup_word = SelectedText(book, sel);
    sel.popup_character_id = CharacterNameFits(sel.popup_word)
                                 ? book->FindCharacter(sel.popup_word)
                                 : 0;
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

// Changes the popup's color; an existing highlight changes at once.
static void StepPopupColor(App &app, Book *book, Text *ts, int step) {
  TextSelectionState &sel = app.MutableTextSelection();
  sel.popup_color = highlight_color_utils::Step(sel.popup_color, step);
  if (app.prefs)
    app.prefs->highlight_color = sel.popup_color;
  if (sel.popup == SelectionPopup::ExistingHighlight &&
      book->SetAnnotationColor(sel.popup_annotation_id, sel.popup_color)) {
    // The snapshot still shows the old color.
    RecapturePage(book, ts);
  }
  RedrawSelection(app, book, ts);
}

static uint64_t AddSelectedHighlight(Book *book,
                                     const TextSelectionState &sel,
                                     const std::string &note) {
  if (sel.popup_page != book->GetPosition())
    return book->AddAnnotationAcrossPages(sel.popup_page, sel.popup_buf_begin,
                                          sel.popup_buf_end, note,
                                          sel.popup_color);
  return book->AddAnnotationFromPageRange(sel.popup_page, sel.popup_buf_begin,
                                          sel.popup_buf_end, note,
                                          sel.popup_color);
}

// --- Word lookup -----------------------------------------------------------

static const size_t kMaxDictionaryResults = 6;
// The landscape status bar is drawn at the bottom of the touch screen.
static const int kLandscapeStatusReservePx = 20;

static dictionary::DictionarySet &Dictionaries() {
  static dictionary::DictionarySet *set = NULL;
  if (!set) {
    set = new dictionary::DictionarySet();
    // An SD card copy of a dictionary wins over the bundled one.
    set->AddDirectory(paths::GetDictionaryDir());
    set->AddDirectory(paths::kRomfsDictDir);
  }
  return *set;
}

static std::string WordBoxText(const annotation_text_utils::VisibleText &text,
                               const WordBox &w) {
  std::vector<uint32_t> cps;
  for (size_t i = 0; i < text.chars.size(); i++)
    if (text.buf_index[i] >= w.buf_begin && text.buf_index[i] < w.buf_end)
      cps.push_back(text.chars[i]);
  return annotation_text_utils::CodepointsToUtf8(cps, 0, cps.size());
}

static bool OnLaterLine(const WordBox &a, const WordBox &b) {
  return b.screen_index > a.screen_index ||
         (b.screen_index == a.screen_index && b.bounds.y0 > a.bounds.y0);
}

// The word at index as text, rejoined when hyphenation split it over two
// lines, without the punctuation around it.
static std::string PickedWord(Book *book, const std::vector<WordBox> &words,
                              int index) {
  Page *page = book->GetPage();
  if (!page || !page->GetBuffer() || index < 0 || index >= (int)words.size())
    return std::string();
  annotation_text_utils::VisibleText text;
  annotation_text_utils::ExtractVisibleText(page->GetBuffer(),
                                            page->GetLength(), &text);
  const WordBox &w = words[(size_t)index];
  const std::string raw = WordBoxText(text, w);
  std::string joined;
  if (index + 1 < (int)words.size() && OnLaterLine(w, words[(size_t)index + 1]))
    joined = word_lookup_utils::JoinHyphenated(
        raw, WordBoxText(text, words[(size_t)index + 1]));
  if (joined.empty() && index > 0 && OnLaterLine(words[(size_t)index - 1], w))
    joined = word_lookup_utils::JoinHyphenated(
        WordBoxText(text, words[(size_t)index - 1]), raw);
  return word_lookup_utils::CleanSelectedWord(joined.empty() ? raw : joined);
}

// The inline link (index in the page's rendered links) the word is part
// of, -1 if none.
static int LinkAtWord(Book *book, const WordBox &w) {
  const std::vector<Page::InlineLinkRenderEntry> *links =
      CurrentPageInlineLinks(book);
  if (!links)
    return -1;
  for (size_t i = 0; i < links->size(); i++) {
    const Page::InlineLinkRenderEntry &e = (*links)[i];
    if (e.screen_index == w.screen_index && e.bounds.x0 < w.bounds.x1 &&
        w.bounds.x0 < e.bounds.x1 && e.bounds.y0 < w.bounds.y1 &&
        w.bounds.y0 < e.bounds.y1)
      return (int)i;
  }
  return -1;
}

static void OpenLookupPopup(App &app, Book *book, Text *ts) {
  TextSelectionState &sel = app.MutableTextSelection();
  const std::vector<WordBox> *words = CurrentPageWords(book);
  if (!words || sel.cursor < 0 || sel.cursor >= (int)words->size())
    return;
  sel.popup_word = PickedWord(book, *words, sel.cursor);
  sel.popup_link = LinkAtWord(book, (*words)[(size_t)sel.cursor]);
  if (sel.popup_word.empty() && sel.popup_link < 0) {
    app.PrintStatus("No word here to look up");
    return;
  }
  sel.popup = SelectionPopup::WordLookup;
  // On a link (a footnote mark, say) following it is the likely choice.
  sel.popup_index = sel.popup_link >= 0 ? 2 : 0;
  RedrawSelection(app, book, ts);
}

static void ShowLookupPanel(App &app, Book *book, Text *ts,
                            const std::vector<word_lookup_panel::Section> &sections,
                            PanelSource source) {
  const TextSelectionState &sel = app.MutableTextSelection();
  s_panel_source = source;
  s_panel_drag_active = false;
  const char *footer = source == PanelSource::Dictionary
                           ? "A: look up online   B: back"
                           : "A: dictionary   B: back";
  const int reserve =
      orientation_utils::IsLandscape((unsigned char)book->GetOrientation())
          ? kLandscapeStatusReservePx
          : 0;
  word_lookup_panel::Show(ts, sel.popup_word, sections, footer, reserve);
  RedrawSelection(app, book, ts);
}

static void ShowLookupMessage(App &app, Book *book, Text *ts,
                              const std::string &message, PanelSource source) {
  std::vector<word_lookup_panel::Section> sections(1);
  sections[0].text = message;
  ShowLookupPanel(app, book, ts, sections, source);
}

// Shows "Looking up..." now; the lookup runs on the next frame.
static void StartLookup(App &app, Book *book, Text *ts, PendingLookup kind) {
  if (app.MutableTextSelection().popup_word.empty())
    return;
  const bool online = kind == PendingLookup::Online;
  ShowLookupMessage(app, book, ts,
                    online ? "Looking up online...\nThis takes a few seconds."
                           : "Looking up...",
                    online ? PanelSource::Online : PanelSource::Dictionary);
  s_pending_lookup = kind;
}

static void RunDictionaryLookup(App &app, Book *book, Text *ts) {
  const std::string word = app.MutableTextSelection().popup_word;
  dictionary::DictionarySet &set = Dictionaries();
  const std::vector<dictionary::Result> results =
      set.Lookup(word, kMaxDictionaryResults);
  std::vector<word_lookup_panel::Section> sections;
  for (size_t i = 0; i < results.size(); i++) {
    word_lookup_panel::Section section;
    section.heading = results[i].dictionary;
    section.text = results[i].text;
    sections.push_back(section);
  }
  if (!sections.empty()) {
    ShowLookupPanel(app, book, ts, sections, PanelSource::Dictionary);
    return;
  }
  std::string message;
  if (set.size() == 0) {
    message = "No dictionary found. Put StarDict dictionaries (.ifo, .idx and "
              ".dict.dz) in " +
              paths::GetDictionaryDir() + "/";
    for (size_t i = 0; i < set.errors().size(); i++)
      message += "\n" + set.errors()[i];
  } else {
    message = "\"" + word + "\" isn't in the dictionary.";
  }
  ShowLookupMessage(app, book, ts, message, PanelSource::Dictionary);
}

static void RunOnlineLookup(App &app, Book *book, Text *ts) {
  web_lookup::Result result;
  web_lookup::Lookup(app.MutableTextSelection().popup_word, &result);
  std::vector<word_lookup_panel::Section> sections;
  for (size_t i = 0; i < result.sections.size(); i++) {
    word_lookup_panel::Section section;
    section.heading = result.sections[i].title;
    section.text = result.sections[i].text;
    if (!result.sections[i].url.empty())
      section.text += "\n" + result.sections[i].url;
    sections.push_back(section);
  }
  if (sections.empty()) {
    ShowLookupMessage(app, book, ts, result.error, PanelSource::Online);
    return;
  }
  ShowLookupPanel(app, book, ts, sections, PanelSource::Online);
}

static void FollowLinkFromLookup(App &app, Book *book, Text *ts) {
  const int link = app.MutableTextSelection().popup_link;
  app.MutableTextSelection().ResetSelection();
  word_lookup_panel::Hide();
  s_pending_lookup = PendingLookup::None;
  ReleasePageSnapshot();
  book->SetWordCaptureEnabled(false);
  book->SetFocusedInlineLinkIndex(link);
  if (!FollowFocusedInlineLink(book, ts)) {
    book->ClearFocusedInlineLink();
    book_nav::DrawPage(book, ts);
    app.PrintStatus("Link target not found");
  }
  app.RequestStatusRedraw();
}

static void RunLookupOption(App &app, Book *book, Text *ts, int option) {
  switch (LookupActionAt(app.MutableTextSelection(), option)) {
  case LookupAction::Dictionary:
    StartLookup(app, book, ts, PendingLookup::Dictionary);
    break;
  case LookupAction::Online:
    StartLookup(app, book, ts, PendingLookup::Online);
    break;
  case LookupAction::FollowLink:
    FollowLinkFromLookup(app, book, ts);
    break;
  default:
    ClosePopup(app, book, ts);
    break;
  }
}

// In lookup mode, moving past the last word (or before the first) turns
// the page.
static bool TurnLookupPage(App &app, Book *book, Text *ts, int direction) {
  TextSelectionState &sel = app.MutableTextSelection();
  const int page = book->GetPosition();
  const int target = page + direction;
  if (target < 0 || target >= (int)book->GetPageCount())
    return false;
  book_nav::SetPage(book, ts, (uint16_t)target);
  if (!RecapturePage(book, ts)) {
    // Nothing to pick there (e.g. a picture): stay where we were.
    book_nav::SetPage(book, ts, (uint16_t)page);
    RecapturePage(book, ts);
    RedrawSelection(app, book, ts);
    return true;
  }
  const std::vector<WordBox> *words = CurrentPageWords(book);
  sel.cursor = direction > 0 ? 0 : (int)words->size() - 1;
  RedrawSelection(app, book, ts);
  return true;
}

static void RunPopupOption(App &app, Book *book, Text *ts, int option) {
  TextSelectionState &sel = app.MutableTextSelection();
  if (sel.popup == SelectionPopup::WordLookup) {
    RunLookupOption(app, book, ts, option);
    return;
  }
  if (option == ColorOption(sel.popup)) {
    StepPopupColor(app, book, ts, 1);
    return;
  }
  if (sel.popup == SelectionPopup::NewSelection) {
    if (option == 0) {
      if (!AddSelectedHighlight(book, sel, ""))
        app.PrintStatus("Nothing to highlight");
    } else if (option == 1) {
      std::string note;
      if (note_editor::Edit("", &note) && !AddSelectedHighlight(book, sel, note))
        app.PrintStatus("Nothing to highlight");
      // The keyboard applet replaced both screens.
      ts->MarkAllScreensDirty();
    } else if (option == kCharacterOption) {
      if (!CharacterNameFits(sel.popup_word))
        return;
      uint64_t id = sel.popup_character_id;
      if (!id)
        id = book->AddCharacter(sel.popup_word);
      if (!id)
        return;
      // Leave selection mode without redrawing the page: the character
      // list replaces it, and the page is drawn again when it closes.
      sel.ResetSelection();
      word_lookup_panel::Hide();
      s_pending_lookup = PendingLookup::None;
      ReleasePageSnapshot();
      book->SetWordCaptureEnabled(false);
      app.ShowCharactersView(id);
      return;
    } else {
      // Cancel keeps selection mode open so the range can be adjusted.
      sel.anchor = -1;
      if (sel.carried_page >= 0) {
        sel.carried_page = -1;
        sel.carried_buf_begin = -1;
      }
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
    } else if (option == 2) {
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

// One frame while the lookup panel is shown: runs a pending lookup, and
// otherwise scrolls (D-pad, L/R, dragging a finger, or tapping the upper or
// lower half), switches source (A) or goes back to the page (B).
static void HandleLookupPanelInput(App &app, Book *book, Text *ts,
                                   const FrameInput &input) {
  using text_selection_utils::ScreenDirection;
  TextSelectionState &sel = app.MutableTextSelection();
  if (s_pending_lookup != PendingLookup::None) {
    const PendingLookup kind = s_pending_lookup;
    s_pending_lookup = PendingLookup::None;
    if (kind == PendingLookup::Online)
      RunOnlineLookup(app, book, ts);
    else
      RunDictionaryLookup(app, book, ts);
    return;
  }
  const uint32_t keys = input.keys_down;
  const uint32_t held = input.keys_held;
  const ScreenDirection pressed = PressedDirection(app, book, keys);
  const ScreenDirection held_dir = PressedDirection(app, book, held);
  const uint64_t now_ms = input.timestamp_ms;
  if (keys & app.key.b) {
    ExitSelectionMode(app, book, ts);
    return;
  }
  if (keys & app.key.a) {
    StartLookup(app, book, ts,
                s_panel_source == PanelSource::Dictionary
                    ? PendingLookup::Online
                    : PendingLookup::Dictionary);
    return;
  }
  if (keys & (app.key.start | app.key.select | app.key.x)) {
    ExitSelectionMode(app, book, ts);
    return;
  }
  bool moved = false;
  if (keys & KEY_TOUCH) {
    s_panel_drag_active = true;
    s_panel_drag_moved = false;
    s_panel_drag_start_y = app.MapTouch(input).py;
    s_panel_drag_start_top = word_lookup_panel::Top();
    return;
  }
  if (s_panel_drag_active) {
    if (held & KEY_TOUCH) {
      const int dy = (int)app.MapTouch(input).py - s_panel_drag_start_y;
      if (dy >= kPanelDragSlopPx || dy <= -kPanelDragSlopPx)
        s_panel_drag_moved = true;
      if (s_panel_drag_moved) {
        const int row = std::max(1, word_lookup_panel::RowHeightPx(ts));
        // Content follows the finger: dragging up shows later lines.
        moved = word_lookup_panel::ScrollTo(ts, s_panel_drag_start_top -
                                                    dy / row);
      }
    } else {
      s_panel_drag_active = false;
      if (!s_panel_drag_moved)
        moved = word_lookup_panel::Scroll(
            ts,
            s_panel_drag_start_y < ts->LogicalHeightFor(false) / 2 ? -1 : 1,
            true);
    }
    if (moved)
      RedrawSelection(app, book, ts);
    return;
  }
  if (keys & app.key.l) {
    moved = word_lookup_panel::Scroll(ts, -1, true);
  } else if (keys & app.key.r) {
    moved = word_lookup_panel::Scroll(ts, 1, true);
  } else if (pressed == ScreenDirection::Up ||
             pressed == ScreenDirection::Down) {
    sel.repeat_direction = pressed;
    sel.repeat_next_ms = now_ms + kCursorRepeatDelayMs;
    moved = word_lookup_panel::Scroll(
        ts, pressed == ScreenDirection::Up ? -1 : 1, false);
  } else if (pressed == ScreenDirection::Left ||
             pressed == ScreenDirection::Right) {
    moved = word_lookup_panel::Scroll(
        ts, pressed == ScreenDirection::Left ? -1 : 1, true);
  } else if (held_dir != ScreenDirection::None &&
             held_dir == sel.repeat_direction && now_ms >= sel.repeat_next_ms) {
    sel.repeat_next_ms = now_ms + kCursorRepeatIntervalMs;
    moved = word_lookup_panel::Scroll(
        ts, held_dir == ScreenDirection::Up ? -1 : 1, false);
  } else if (held_dir != sel.repeat_direction) {
    sel.repeat_direction = ScreenDirection::None;
  }
  if (moved)
    RedrawSelection(app, book, ts);
}

// Handles one frame of input while selection mode is active. Always
// consumes the input.
static bool HandleSelectionInput(App &app, Book *book, Text *ts,
                                 const FrameInput &input) {
  using text_selection_utils::ScreenDirection;
  TextSelectionState &sel = app.MutableTextSelection();
  const uint32_t keys = input.keys_down;
  const uint32_t held = input.keys_held;
  if (word_lookup_panel::IsVisible()) {
    HandleLookupPanelInput(app, book, ts, input);
    return true;
  }
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
      // Straight back to the page; Cancel returns to the cursor instead.
      ExitSelectionMode(app, book, ts);
    } else if (pressed == ScreenDirection::Up ||
               pressed == ScreenDirection::Down) {
      sel.popup_index = text_selection_utils::StepPopupIndex(
          PopupOptionCount(sel), sel.popup_index,
          pressed == ScreenDirection::Up ? -1 : 1);
      RedrawSelection(app, book, ts);
    } else if (sel.popup != SelectionPopup::WordLookup &&
               (pressed == ScreenDirection::Left ||
                pressed == ScreenDirection::Right)) {
      StepPopupColor(app, book, ts,
                     pressed == ScreenDirection::Left ? -1 : 1);
    } else if (keys & KEY_TOUCH) {
      const touchPosition mapped = app.MapTouch(input);
      const int option = PopupOptionAt(sel, ts, mapped.px, mapped.py);
      if (option >= 0) {
        sel.popup_index = option;
        RunPopupOption(app, book, ts, option);
      }
    }
    return true;
  }

  // Touch drag: select from the touched word to the word under the finger
  // (lookup: the word under the finger when it lifts).
  if (keys & KEY_TOUCH) {
    const int word = TouchedWord(app, book, ts, *words, input);
    if (word >= 0) {
      // A selection carried from the previous page keeps its start.
      if (!sel.lookup && sel.carried_page < 0)
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
      if (sel.lookup)
        OpenLookupPopup(app, book, ts);
      else
        OpenPopupForSelection(app, book, ts);
    }
    return true;
  }

  // Cursor movement, repeating while a direction is held.
  const ScreenDirection held_dir = PressedDirection(app, book, held);
  const uint64_t now_ms = input.timestamp_ms;
  // A new press past the last word carries a running selection to the next
  // page; before the first word it goes back (held repeats never turn).
  const bool forward =
      pressed == ScreenDirection::Right || pressed == ScreenDirection::Down;
  const bool backward =
      pressed == ScreenDirection::Left || pressed == ScreenDirection::Up;
  if (forward && sel.anchor >= 0 && sel.anchor <= sel.cursor &&
      sel.cursor == word_count - 1 &&
      CarrySelectionToNextPage(app, book, ts)) {
    sel.repeat_direction = ScreenDirection::None;
    return true;
  }
  if (backward && sel.carried_page >= 0 && sel.cursor == 0) {
    ReturnSelectionToPreviousPage(app, book, ts);
    sel.repeat_direction = ScreenDirection::None;
    return true;
  }
  if (sel.lookup && ((forward && sel.cursor == word_count - 1) ||
                     (backward && sel.cursor == 0)) &&
      TurnLookupPage(app, book, ts, forward ? 1 : -1)) {
    sel.repeat_direction = ScreenDirection::None;
    return true;
  }
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
    if (sel.lookup) {
      OpenLookupPopup(app, book, ts);
    } else if (sel.anchor < 0 && !HighlightUnderCursor(book, sel)) {
      sel.anchor = sel.cursor;
      RedrawSelection(app, book, ts);
    } else {
      OpenPopupForSelection(app, book, ts);
    }
  } else if (keys & app.key.b) {
    if (sel.anchor >= 0) {
      sel.anchor = -1;
      sel.carried_page = -1;
      sel.carried_buf_begin = -1;
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
  const uint64_t now_ms = input.timestamp_ms;
  TextSelectionState &selection = app.MutableTextSelection();

  if (selection.active) {
    app.ResetPageRepeat();
    HandleSelectionInput(app, book, ts, input);
    return true;
  }

  // X: short press cycles the colour theme (on release), hold enters text
  // selection for highlights and notes.
  if (keys & app.key.x) {
    selection.x_hold_armed = true;
    selection.x_hold_consumed = false;
    selection.x_hold_started_ms = now_ms;
  }
  if (selection.x_hold_armed && (held & app.key.x) &&
      !selection.x_hold_consumed &&
      now_ms >= selection.x_hold_started_ms + kSelectionHoldThresholdMs) {
    selection.x_hold_consumed = true;
    app.ResetPageRepeat();
    if (EnterSelectionMode(app, book, ts, false))
      return true;
  }
  if (selection.x_hold_armed && !(held & app.key.x)) {
    const bool consumed = selection.x_hold_consumed;
    selection.x_hold_armed = false;
    selection.x_hold_consumed = false;
    selection.x_hold_started_ms = 0;
    if (!consumed) {
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

  // Y: short press toggles a bookmark (on release), hold enters word lookup.
  if (keys & app.key.y) {
    selection.y_hold_armed = true;
    selection.y_hold_consumed = false;
    selection.y_hold_started_ms = now_ms;
  }
  if (selection.y_hold_armed && (held & app.key.y) &&
      !selection.y_hold_consumed &&
      now_ms >= selection.y_hold_started_ms + kSelectionHoldThresholdMs) {
    selection.y_hold_consumed = true;
    app.ResetPageRepeat();
    if (EnterSelectionMode(app, book, ts, true))
      return true;
  }

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
    // y: bookmark on release / word lookup on hold, handled above
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

  if (selection.y_hold_armed && !(held & app.key.y)) {
    const bool consumed = selection.y_hold_consumed;
    selection.y_hold_armed = false;
    selection.y_hold_consumed = false;
    selection.y_hold_started_ms = 0;
    if (!consumed)
      app.ToggleBookmark();
  }

  return status_dirty;
}

} // namespace reflow_input
