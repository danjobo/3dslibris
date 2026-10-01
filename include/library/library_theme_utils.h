/*
    3dslibris - library_theme_utils.h

    Colors for the library screens (selection frame, progress bars, badges,
    secondary text, cover shadows) in each of the six color modes. Built on
    the shared ThemePalette, so they follow the light, dark and sepia themes.
    Pure, host-testable.
*/

#pragma once

#include <stdint.h>

namespace library_theme_utils {

struct LibraryPalette {
  uint16_t background;  // Behind the covers (the gradient's top color).
  uint16_t text;
  uint16_t muted;       // Author, page and time-left lines.
  uint16_t accent;      // Selection frame and progress fill.
  uint16_t track;       // Unfilled part of a progress bar.
  uint16_t shadow;      // Under covers.
  uint16_t placeholder; // Cover box of a book without a cover.
  uint16_t new_bg, new_fg;
  uint16_t done_bg, done_fg;
  uint16_t shelf;       // Shelf view planks,
  uint16_t shelf_edge;  // and their darker front edge.
};

LibraryPalette ForColorMode(int color_mode);

bool IsDarkColorMode(int color_mode);

} // namespace library_theme_utils
