#include "library/library_theme_utils.h"

#include "library/library_paint_utils.h"
#include "ui/theme_colors.h"

namespace library_theme_utils {

using library_paint_utils::Blend565;
using library_paint_utils::Rgb565;

bool IsDarkColorMode(int color_mode) {
  return color_mode == 1 || color_mode == 4 || color_mode == 5;
}

LibraryPalette ForColorMode(int color_mode) {
  const ThemePalette &theme = GetThemePalette(color_mode);
  LibraryPalette p;
  p.background = Rgb565((int)theme.bgTopR, (int)theme.bgTopG, (int)theme.bgTopB);
  p.text = theme.textFgColor;
  p.muted = Blend565(p.text, p.background, 100);
  p.placeholder =
      Rgb565((int)theme.btnFillBotR, (int)theme.btnFillBotG, (int)theme.btnFillBotB);

  const bool sepia = color_mode == 2 || color_mode == 5;
  if (IsDarkColorMode(color_mode)) {
    p.shelf = sepia ? Rgb565(100, 72, 46) : Rgb565(88, 66, 48);
    p.shelf_edge = Blend565(p.shelf, Rgb565(0, 0, 0), 110);
    p.accent = sepia ? Rgb565(222, 162, 100) : Rgb565(110, 165, 255);
    p.track = Blend565(p.background, p.text, 60);
    p.shadow = Rgb565(0, 0, 0);
    p.new_bg = p.accent;
    p.new_fg = sepia ? Rgb565(40, 28, 18) : Rgb565(10, 20, 40);
    p.done_bg = sepia ? Rgb565(160, 180, 105) : Rgb565(95, 195, 125);
    p.done_fg = p.new_fg;
  } else {
    p.shelf = sepia ? Rgb565(176, 130, 84) : Rgb565(186, 146, 104);
    p.shelf_edge = Blend565(p.shelf, Rgb565(0, 0, 0), 80);
    p.accent = sepia ? Rgb565(165, 90, 40) : Rgb565(35, 105, 215);
    p.track = Blend565(p.background, p.text, 40);
    p.shadow = Blend565(p.background, p.text, 70);
    p.new_bg = p.accent;
    p.new_fg = sepia ? Rgb565(252, 244, 228) : Rgb565(255, 255, 255);
    p.done_bg = sepia ? Rgb565(95, 120, 55) : Rgb565(40, 140, 75);
    p.done_fg = p.new_fg;
  }
  return p;
}

} // namespace library_theme_utils
