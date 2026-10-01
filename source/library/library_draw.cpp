#include "library/library_draw.h"

#include <string.h>

#include "book/book.h"
#include "library/browser_presentation_utils.h"
#include "ui/text.h"

namespace library_draw {

using library_paint_utils::Surface;

Surface SurfaceFor(Text *ts, uint16_t *screen) {
  Surface s;
  const bool left = screen == ts->screenleft;
  s.pixels = screen;
  s.stride = ts->BufferStride();
  s.width = ts->LogicalWidthFor(left);
  s.height = ts->LogicalHeightFor(left);
  return s;
}

library_progress_utils::BookProgress ProgressFor(Book *book) {
  return library_progress_utils::Compute(
      book->GetPosition(), (int)book->GetLibraryPageCount(),
      book->GetLastOpenedTime(), book->GetLibraryMsPerPage());
}

void DrawProgressBar(Text *ts, uint16_t *screen, int x, int y, int w, int h,
                     int percent,
                     const library_theme_utils::LibraryPalette &pal) {
  if (w <= 0 || h <= 0 || percent < 0)
    return;
  const Surface s = SurfaceFor(ts, screen);
  const int filled = library_progress_utils::FilledWidth(percent, w);
  library_paint_utils::FillRect(s, x, y, x + filled, y + h, pal.accent);
  library_paint_utils::FillRect(s, x + filled, y, x + w, y + h, pal.track);
  ts->MarkScreenDirtyRect(screen, x, y, x + w, y + h);
}

int DrawBadge(Text *ts, uint16_t *screen, int x, int y, const char *label,
              uint16_t bg, uint16_t fg, bool right_aligned) {
  if (!label || !label[0])
    return 0;
  ts->SetPixelSize(9);
  const int text_w = ts->GetStringWidth(label, TEXT_STYLE_BROWSER);
  const int w = text_w + 8;
  const int h = 13;
  if (right_aligned)
    x -= w;
  const Surface s = SurfaceFor(ts, screen);
  // A pill: a rectangle with its four corner pixels left out.
  library_paint_utils::FillRect(s, x + 1, y, x + w - 1, y + h, bg);
  library_paint_utils::FillRect(s, x, y + 1, x + 1, y + h - 1, bg);
  library_paint_utils::FillRect(s, x + w - 1, y + 1, x + w, y + h - 1, bg);
  ts->MarkScreenDirtyRect(screen, x, y, x + w, y + h);

  ts->SetTextColorOverride(fg);
  ts->SetPen((u16)(x + 4), (u16)(y + 10));
  ts->PrintString(label, TEXT_STYLE_BROWSER);
  ts->ClearTextColorOverride();
  return w;
}

void DrawStatusBadge(Text *ts, uint16_t *screen, int x0, int y0, int x1,
                     const library_progress_utils::BookProgress &progress,
                     const library_theme_utils::LibraryPalette &pal) {
  if (progress.is_new)
    DrawBadge(ts, screen, x1 - 3, y0 + 3, "NEW", pal.new_bg, pal.new_fg, true);
  else if (progress.finished)
    DrawBadge(ts, screen, x1 - 3, y0 + 3, "DONE", pal.done_bg, pal.done_fg,
              true);
  (void)x0;
}

std::vector<std::string> WrapLines(Text *ts, const std::string &text,
                                   uint8_t style, int max_w, int max_lines) {
  std::vector<std::string> lines;
  if (text.empty() || max_w <= 0 || max_lines <= 0)
    return lines;
  if (max_w > 255)
    max_w = 255;

  size_t pos = 0;
  while (pos < text.size() && (int)lines.size() < max_lines) {
    while (pos < text.size() && text[pos] == ' ')
      pos++;
    if (pos >= text.size())
      break;
    const char *rest = text.c_str() + pos;
    const unsigned char fit =
        ts->GetCharCountInsideWidth(rest, style, (u8)max_w);
    if (!fit)
      break;
    size_t take = browser_presentation_utils::Utf8BytesForCharCount(rest, fit);
    const bool more = pos + take < text.size();
    const bool last_line = (int)lines.size() == max_lines - 1;
    if (more && !last_line) {
      size_t back = take;
      while (back > 0 && text[pos + back - 1] != ' ' && text[pos + back] != ' ')
        back--;
      if (back > 0)
        take = back;
    }
    std::string line = text.substr(pos, take);
    while (!line.empty() && line[line.size() - 1] == ' ')
      line.erase(line.size() - 1);
    pos += take;

    if (last_line && pos < text.size()) {
      // Make room for "..." at the end of the last line.
      const std::string dots = "...";
      const int dots_w = ts->GetStringWidth(dots.c_str(), style);
      const unsigned char keep = ts->GetCharCountInsideWidth(
          line.c_str(), style, (u8)(max_w > dots_w ? max_w - dots_w : 0));
      line = line.substr(0, browser_presentation_utils::Utf8BytesForCharCount(
                                line.c_str(), keep));
      while (!line.empty() && line[line.size() - 1] == ' ')
        line.erase(line.size() - 1);
      line += dots;
    }
    if (!line.empty())
      lines.push_back(line);
  }
  return lines;
}

void PrintCentered(Text *ts, const std::string &line, uint8_t style, int x0,
                   int x1, int baseline_y) {
  if (line.empty())
    return;
  const int w = ts->GetStringWidth(line.c_str(), style);
  int x = x0 + ((x1 - x0) - w) / 2;
  if (x < x0)
    x = x0;
  ts->SetPen((u16)x, (u16)baseline_y);
  ts->PrintString(line.c_str(), style);
}

TextStateGuard::TextStateGuard(Text *ts)
    : ts_(ts), screen_(ts->GetScreen()), style_(ts->GetStyle()),
      pixel_size_(ts->GetPixelSize()), margin_left_(ts->margin.left),
      margin_right_(ts->margin.right), clip_(ts->IsClipToContentEnabled()),
      wrap_(ts->IsAutoWrapEnabled()) {
  ts->margin.left = 0;
  ts->margin.right = 0;
  ts->SetClipToContentEnabled(true);
  ts->SetAutoWrapEnabled(false);
}

TextStateGuard::~TextStateGuard() {
  ts_->ClearTextColorOverride();
  ts_->SetScreen(screen_);
  ts_->SetStyle(style_);
  ts_->SetPixelSize((u8)pixel_size_);
  ts_->margin.left = margin_left_;
  ts_->margin.right = margin_right_;
  ts_->SetClipToContentEnabled(clip_);
  ts_->SetAutoWrapEnabled(wrap_);
}

} // namespace library_draw
