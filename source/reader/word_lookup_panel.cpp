/*
    3dslibris - word_lookup_panel.cpp

    See include/reader/word_lookup_panel.h.
*/

#include "reader/word_lookup_panel.h"

#include <algorithm>
#include <stdio.h>

#include "dictionary/word_lookup_utils.h"
#include "ui/text.h"

namespace word_lookup_panel {

namespace {

const int kPixelSize = 12;
const int kMargin = 8;
const int kLineGap = 3;

struct Line {
  std::string text;
  int indent_px;
  bool heading;
};

struct State {
  bool visible = false;
  std::string title;
  std::string footer;
  std::vector<Line> lines;
  int top = 0; // first line shown
  int bottom_reserved_px = 0;
} s_panel;

// Saves the renderer state the reader relies on and sets the panel's.
class TextStateScope {
public:
  explicit TextStateScope(Text *ts)
      : ts_(ts), screen_(ts->GetScreen()), style_(ts->GetStyle()),
        pixel_size_(ts->GetPixelSize()), wrap_(ts->IsAutoWrapEnabled()) {
    ts_->SetScreen(ts_->screenright);
    ts_->SetPixelSize(kPixelSize);
    ts_->SetAutoWrapEnabled(false);
  }
  ~TextStateScope() {
    ts_->SetAutoWrapEnabled(wrap_);
    ts_->SetPixelSize(pixel_size_);
    ts_->SetStyle(style_);
    ts_->SetScreen(screen_);
  }

private:
  Text *ts_;
  u16 *screen_;
  int style_;
  u8 pixel_size_;
  bool wrap_;
};

int Advance(void *ctx, uint32_t codepoint) {
  return (int)static_cast<Text *>(ctx)->GetAdvance(codepoint);
}

int RowHeight(Text *ts) { return (int)ts->GetHeight() + kLineGap; }

int Width(Text *ts) { return ts->LogicalWidthFor(false); }

int Height(Text *ts) { return ts->LogicalHeightFor(false); }

int BodyTop(Text *ts) { return kMargin + RowHeight(ts) + 6; }

int BodyBottom(Text *ts) {
  return Height(ts) - s_panel.bottom_reserved_px - RowHeight(ts) - 6;
}

int VisibleLines(Text *ts) {
  return std::max(1, (BodyBottom(ts) - BodyTop(ts)) / RowHeight(ts));
}

void AddWrapped(Text *ts, const std::string &text, bool heading,
                int max_width) {
  ts->SetStyle(heading ? TEXT_STYLE_BOLD : TEXT_STYLE_REGULAR);
  const std::vector<word_lookup_utils::WrappedLine> wrapped =
      word_lookup_utils::WrapText(text, max_width, &Advance, ts);
  for (size_t i = 0; i < wrapped.size(); i++) {
    Line line = {wrapped[i].text, wrapped[i].indent_px, heading};
    s_panel.lines.push_back(line);
  }
}

} // namespace

void Show(Text *ts, const std::string &title,
          const std::vector<Section> &sections, const std::string &footer,
          int bottom_reserved_px) {
  s_panel.visible = true;
  s_panel.title = title;
  s_panel.footer = footer;
  s_panel.lines.clear();
  s_panel.top = 0;
  s_panel.bottom_reserved_px = bottom_reserved_px;
  TextStateScope scope(ts);
  const int max_width = Width(ts) - 2 * kMargin;
  for (size_t i = 0; i < sections.size(); i++) {
    if (i > 0) {
      Line gap = {std::string(), 0, false};
      s_panel.lines.push_back(gap);
    }
    if (!sections[i].heading.empty())
      AddWrapped(ts, sections[i].heading, true, max_width);
    AddWrapped(ts, sections[i].text, false, max_width);
  }
}

void Hide() {
  s_panel.visible = false;
  std::vector<Line>().swap(s_panel.lines);
}

bool IsVisible() { return s_panel.visible; }

static int VisibleLinesNow(Text *ts) {
  TextStateScope scope(ts);
  return VisibleLines(ts);
}

bool ScrollTo(Text *ts, int top) {
  if (!s_panel.visible)
    return false;
  const int max_top =
      std::max(0, (int)s_panel.lines.size() - VisibleLinesNow(ts));
  top = std::max(0, std::min(max_top, top));
  if (top == s_panel.top)
    return false;
  s_panel.top = top;
  return true;
}

bool Scroll(Text *ts, int amount, bool pages) {
  if (!s_panel.visible)
    return false;
  const int step =
      pages ? amount * std::max(1, VisibleLinesNow(ts) - 1) : amount;
  return ScrollTo(ts, s_panel.top + step);
}

int Top() { return s_panel.top; }

int RowHeightPx(Text *ts) {
  TextStateScope scope(ts);
  return RowHeight(ts);
}

void Draw(Text *ts) {
  if (!s_panel.visible)
    return;
  TextStateScope scope(ts);
  const int w = Width(ts);
  const int row = RowHeight(ts);
  const int ascent = (int)ts->GetHeight();
  const u16 fg = ts->GetFgColor();
  ts->FillRect(0, 0, (u16)w,
               (u16)(Height(ts) - s_panel.bottom_reserved_px),
               ts->GetBgColor());

  // Title and a rule under it.
  ts->SetStyle(TEXT_STYLE_BOLD);
  ts->SetPen(kMargin, (u16)(kMargin + ascent));
  ts->PrintString(s_panel.title.c_str());
  const int rule_y = kMargin + row + 2;
  ts->FillRect(kMargin, (u16)rule_y, (u16)(w - kMargin), (u16)(rule_y + 1), fg);

  const int visible = VisibleLines(ts);
  int y = BodyTop(ts) + ascent;
  for (int i = s_panel.top;
       i < (int)s_panel.lines.size() && i < s_panel.top + visible; i++) {
    const Line &line = s_panel.lines[(size_t)i];
    ts->SetStyle(line.heading ? TEXT_STYLE_BOLD : TEXT_STYLE_REGULAR);
    ts->SetPen((u16)(kMargin + line.indent_px), (u16)y);
    ts->PrintString(line.text.c_str());
    y += row;
  }

  // Footer: key hints, and where we are when it scrolls.
  const int footer_rule = BodyBottom(ts) + 2;
  ts->FillRect(kMargin, (u16)footer_rule, (u16)(w - kMargin),
               (u16)(footer_rule + 1), fg);
  ts->SetStyle(TEXT_STYLE_BROWSER);
  const int footer_y = footer_rule + 3 + ascent;
  ts->SetPen(kMargin, (u16)footer_y);
  ts->PrintString(s_panel.footer.c_str());
  const int total = (int)s_panel.lines.size();
  if (total > visible) {
    char where[24];
    snprintf(where, sizeof(where), "%d%%",
             (int)((long)std::min(total, s_panel.top + visible) * 100 / total));
    const int where_w = ts->GetStringWidth(where, TEXT_STYLE_BROWSER);
    ts->SetPen((u16)(w - kMargin - where_w), (u16)footer_y);
    ts->PrintString(where);
  }
  ts->MarkScreenDirty(ts->screenright);
}

} // namespace word_lookup_panel
