#pragma once

namespace status_layout_utils {

struct BookStatusHudLayout {
  int text_y; // baselines, like every pen y
  int clear_top;
  int clear_bottom;
  int progress_bar_y;
  int progress_bar_height;
  int eta_y; // the time-remaining line, below the progress bar
};

struct FixedLayoutBottomHudLayout {
  int time_y;
  int time_clear_top;
  int time_clear_bottom;
  int page_y;
  int page_clear_top;
  int page_clear_bottom;
  int right_margin;
};

struct LandscapeBookStatusHudLayout {
  int text_y;
  int clear_top;
  int clear_bottom;
  int progress_bar_y;
  int progress_bar_height;
  int left_margin;
  int right_margin;
};

inline BookStatusHudLayout ComputeBookStatusHudLayout(int screen_height,
                                                      int font_height,
                                                      int footer_reserved_height) {
  BookStatusHudLayout layout = {};
  if (screen_height <= 0)
    return layout;

  const int bottom_padding = 13;
  layout.progress_bar_height = 8;
  layout.text_y = screen_height - 25;
  layout.progress_bar_y = layout.text_y + 4;

  const int progress_bottom = layout.progress_bar_y + layout.progress_bar_height;
  const int max_progress_bottom = screen_height - bottom_padding;
  if (progress_bottom > max_progress_bottom) {
    const int shift = progress_bottom - max_progress_bottom;
    layout.text_y -= shift;
    layout.progress_bar_y -= shift;
  }

  // The time-remaining line goes under the bar: its glyphs (about 3/4 of
  // the font height above the baseline) start a pixel below it. A tall
  // font that would run off the screen moves the whole row up.
  const int ascent = (font_height * 3 + 3) / 4;
  layout.eta_y =
      layout.progress_bar_y + layout.progress_bar_height + 1 + ascent;
  const int max_eta_y = screen_height - 1;
  if (layout.eta_y > max_eta_y) {
    const int shift = layout.eta_y - max_eta_y;
    layout.text_y -= shift;
    layout.progress_bar_y -= shift;
    layout.eta_y -= shift;
  }

  const int reserved_top = screen_height - footer_reserved_height;
  layout.clear_top = layout.text_y - font_height - 8;
  if (layout.clear_top < reserved_top)
    layout.clear_top = reserved_top;
  if (layout.clear_top < 0)
    layout.clear_top = 0;
  layout.clear_bottom = screen_height;
  return layout;
}

inline FixedLayoutBottomHudLayout
ComputeFixedLayoutBottomHudLayout(int screen_height, int font_height) {
  FixedLayoutBottomHudLayout layout = {};
  if (screen_height <= 0)
    return layout;

  layout.right_margin = 8;
  layout.time_y = 10;
  layout.time_clear_top = 0;
  layout.time_clear_bottom = layout.time_y + 8;

  layout.page_y = screen_height - font_height - 10;
  if (layout.page_y < 0)
    layout.page_y = 0;
  layout.page_clear_top = layout.page_y - 9;
  if (layout.page_clear_top < 0)
    layout.page_clear_top = 0;
  layout.page_clear_bottom = layout.page_y + 8;
  if (layout.page_clear_bottom > screen_height)
    layout.page_clear_bottom = screen_height;
  return layout;
}

inline LandscapeBookStatusHudLayout
ComputeLandscapeBookStatusHudLayout(int screen_width, int screen_height,
                                    int font_height) {
  LandscapeBookStatusHudLayout layout = {};
  if (screen_width <= 0 || screen_height <= 0)
    return layout;

  const int strip_height = 22;
  layout.clear_top = screen_height > strip_height
                         ? screen_height - strip_height
                         : 0;
  layout.clear_bottom = screen_height;
  layout.text_y = screen_height - 5;
  if (layout.text_y < font_height)
    layout.text_y = font_height;
  layout.progress_bar_height = 4;
  layout.progress_bar_y = layout.clear_top + 9;
  layout.left_margin = 6;
  layout.right_margin = 6;
  return layout;
}

} // namespace status_layout_utils
