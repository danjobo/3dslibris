/*
    3dslibris - library_draw.h

    Drawing helpers shared by the library views (grid, list and the top
    screen details panel): progress bars, NEW/DONE badges and wrapped,
    centered text.
*/

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "library/library_paint_utils.h"
#include "library/library_progress_utils.h"
#include "library/library_theme_utils.h"

class Book;
class Text;

namespace library_draw {

// The logical drawing area of one of the Text screen buffers.
library_paint_utils::Surface SurfaceFor(Text *ts, uint16_t *screen);

// Reading progress as the library shows it (from Book's remembered stats).
library_progress_utils::BookProgress ProgressFor(Book *book);

void DrawProgressBar(Text *ts, uint16_t *screen, int x, int y, int w, int h,
                     int percent,
                     const library_theme_utils::LibraryPalette &pal);

// The DONE seal: a green disc with a light ring, a soft shadow and a check
// mark, `diameter` pixels across, centered on (cx, cy).
void DrawDoneSeal(Text *ts, uint16_t *screen, int cx, int cy, int diameter,
                  const library_theme_utils::LibraryPalette &pal);

// The NEW dot: an accent-colored disc with a light ring and a soft shadow,
// in the same style as the DONE seal.
void DrawNewDot(Text *ts, uint16_t *screen, int cx, int cy, int diameter,
                const library_theme_utils::LibraryPalette &pal);

// NEW or DONE badge for the book's progress, in the top-right corner of the
// rectangle (x0, y0)-(x1, y1). Nothing for books in progress.
void DrawStatusBadge(Text *ts, uint16_t *screen, int x0, int y0, int x1,
                     const library_progress_utils::BookProgress &progress,
                     const library_theme_utils::LibraryPalette &pal);

// Splits text into at most max_lines lines no wider than max_w, breaking at
// spaces where possible. A last line that doesn't fit ends in "...".
std::vector<std::string> WrapLines(Text *ts, const std::string &text,
                                   uint8_t style, int max_w, int max_lines);

// Prints one line centered between x0 and x1 at baseline y.
void PrintCentered(Text *ts, const std::string &line, uint8_t style, int x0,
                   int x1, int baseline_y);

// Saves the Text pen/margin/clip/wrap/color state and restores it on scope
// exit, so a view can draw freely.
class TextStateGuard {
public:
  explicit TextStateGuard(Text *ts);
  ~TextStateGuard();

private:
  Text *ts_;
  uint16_t *screen_;
  int style_;
  int pixel_size_;
  int margin_left_;
  int margin_right_;
  bool clip_;
  bool wrap_;
};

} // namespace library_draw
