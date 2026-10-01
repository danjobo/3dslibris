#include "library/library_details_view.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <string>
#include <vector>

#include "book/book.h"
#include "library/browser_presentation_utils.h"
#include "library/library_draw.h"
#include "library/library_paint_utils.h"
#include "ui/text.h"

namespace library_details_view {

namespace {

static const int kCoverTop = 20;
static const int kCoverMaxW = 168;
static const int kCoverMaxH = 232;
static const int kPlaceholderW = 150;
static const int kPlaceholderH = 210;
static const int kSidePad = 16;
static const int kBarW = 180;
static const int kBarH = 5;

// "EPUB", "PDF"...: shown in the cover box of a book without a cover.
std::string FormatLabel(Book *book) {
  const char *name = book->GetFileName();
  const char *dot = name ? strrchr(name, '.') : NULL;
  std::string label;
  if (!dot || !dot[1])
    return label;
  for (const char *c = dot + 1; *c && label.size() < 5; c++)
    label.push_back((char)((*c >= 'a' && *c <= 'z') ? *c - 32 : *c));
  return label;
}

// Draws the cover (or a placeholder box) centered at the top; returns its
// bottom edge.
int DrawCover(Text *ts, Book *book,
              const library_theme_utils::LibraryPalette &pal) {
  const library_paint_utils::Surface s =
      library_draw::SurfaceFor(ts, ts->screenleft);
  // The large cover when it's loaded, else the grid thumbnail enlarged.
  const u16 *pixels = book->coverPixels;
  int src_w = book->coverWidth;
  int src_h = book->coverHeight;
  if (book->largeCoverPixels && book->largeCoverWidth > 0 &&
      book->largeCoverHeight > 0) {
    pixels = book->largeCoverPixels;
    src_w = book->largeCoverWidth;
    src_h = book->largeCoverHeight;
  }
  if (pixels && src_w > 0 && src_h > 0) {
    int w = 0;
    int h = 0;
    library_paint_utils::FitSize(src_w, src_h, kCoverMaxW, kCoverMaxH, true,
                                 &w, &h);
    const int x = (s.width - w) / 2;
    const int y = kCoverTop;
    library_paint_utils::FillRect(s, x + 3, y + 3, x + w + 3, y + h + 3,
                                  pal.shadow);
    library_paint_utils::BlitScaled(s, pixels, src_w, src_h, x, y, w, h);
    return y + h + 3;
  }

  const int x = (s.width - kPlaceholderW) / 2;
  const int y = kCoverTop;
  library_paint_utils::FillRect(s, x + 3, y + 3, x + kPlaceholderW + 3,
                                y + kPlaceholderH + 3, pal.shadow);
  library_paint_utils::FillRect(s, x, y, x + kPlaceholderW, y + kPlaceholderH,
                                pal.placeholder);
  // A spine line down the left edge, so it reads as a book.
  library_paint_utils::FillRect(s, x + 8, y, x + 10, y + kPlaceholderH,
                                pal.track);
  const std::string label =
      book->IsBrowserFolder() ? std::string("FOLDER") : FormatLabel(book);
  if (!label.empty()) {
    ts->SetPixelSize(18);
    ts->SetTextColorOverride(pal.muted);
    library_draw::PrintCentered(ts, label, TEXT_STYLE_BROWSER, x + 10,
                                x + kPlaceholderW, y + kPlaceholderH / 2 + 6);
    ts->ClearTextColorOverride();
  }
  return y + kPlaceholderH + 3;
}

} // namespace

void Draw(Text *ts, Book *book) {
  if (!ts || !book)
    return;
  library_draw::TextStateGuard guard(ts);
  ts->SetScreen(ts->screenleft);
  const library_theme_utils::LibraryPalette pal =
      library_theme_utils::ForColorMode(ts->GetColorMode());
  const int width = ts->LogicalWidthFor(true);

  int y = DrawCover(ts, book, pal) + 22;

  // Title: up to two lines.
  ts->SetPixelSize(15);
  const std::string title =
      browser_presentation_utils::BuildBrowserDisplayName(book);
  const std::vector<std::string> title_lines = library_draw::WrapLines(
      ts, title, TEXT_STYLE_BOLD, width - kSidePad * 2, 2);
  const int title_step = ts->GetHeight();
  for (size_t i = 0; i < title_lines.size(); i++) {
    library_draw::PrintCentered(ts, title_lines[i], TEXT_STYLE_BOLD, kSidePad,
                                width - kSidePad, y);
    y += title_step;
  }

  ts->SetPixelSize(11);
  const int small_step = ts->GetHeight() + 2;
  ts->SetTextColorOverride(pal.muted);
  if (!book->IsBrowserFolder() && !book->GetAuthor().empty()) {
    const std::vector<std::string> author = library_draw::WrapLines(
        ts, book->GetAuthor(), TEXT_STYLE_BROWSER, width - kSidePad * 2, 1);
    if (!author.empty())
      library_draw::PrintCentered(ts, author[0], TEXT_STYLE_BROWSER, kSidePad,
                                  width - kSidePad, y);
    y += small_step;
  }
  y += 4;

  if (book->IsBrowserFolder()) {
    library_draw::PrintCentered(ts, "Folder", TEXT_STYLE_BROWSER, 0, width, y);
    ts->MarkScreenDirty(ts->screenleft);
    return;
  }

  const library_progress_utils::BookProgress progress =
      library_draw::ProgressFor(book);
  const int page_count = (int)book->GetLibraryPageCount();
  char line[64];

  if (progress.is_new) {
    library_draw::DrawBadge(ts, ts->screenleft, (width - 30) / 2, y - 2, "NEW",
                            pal.new_bg, pal.new_fg, false);
    y += 13 + small_step;
    ts->SetPixelSize(11);
    ts->SetTextColorOverride(pal.muted);
    if (page_count > 0)
      snprintf(line, sizeof(line), "Not started - %d pages", page_count);
    else
      snprintf(line, sizeof(line), "Not started");
    library_draw::PrintCentered(ts, line, TEXT_STYLE_BROWSER, 0, width, y);
  } else {
    if (progress.finished) {
      // A larger seal in place of the full bar.
      library_draw::DrawDoneSeal(ts, ts->screenleft, width / 2, y + 10, 24,
                                 pal);
      y += 24 + small_step;
    } else if (progress.percent >= 0) {
      library_draw::DrawProgressBar(ts, ts->screenleft, (width - kBarW) / 2, y,
                                    kBarW, kBarH, progress.percent, pal);
      y += kBarH + small_step;
    } else {
      y += small_step - 4;
    }
    ts->SetPixelSize(11);
    ts->SetTextColorOverride(pal.muted);
    const int page = book->GetPosition() + 1;
    if (progress.finished)
      snprintf(line, sizeof(line), "Finished - %d pages", page_count);
    else if (page_count > 0)
      snprintf(line, sizeof(line), "Page %d of %d  (%d%%)", page, page_count,
               progress.percent);
    else
      snprintf(line, sizeof(line), "Page %d", page);
    library_draw::PrintCentered(ts, line, TEXT_STYLE_BROWSER, 0, width, y);
    y += small_step;
    if (!progress.finished && progress.minutes_left > 0) {
      library_progress_utils::FormatTimeLeft(progress.minutes_left, line,
                                             sizeof(line));
      library_draw::PrintCentered(ts, line, TEXT_STYLE_BROWSER, 0, width, y);
    }
  }
  ts->MarkScreenDirty(ts->screenleft);
}

} // namespace library_details_view
