/*
    3dslibris - highlight_color_utils.h

    Highlight colors: the palette stored with each highlight (as an index,
    so it syncs and survives theme changes), its display names, and the
    RGB565 tint drawn behind highlighted text in light and dark themes.
    Header-only: page drawing, storage and the selection UI all use it.
*/

#pragma once

#include <stdint.h>

namespace highlight_color_utils {

enum Color : uint8_t {
  kYellow = 0, // default; also what older highlights load as
  kGreen,
  kBlue,
  kPink,
  kPurple,
  kCount,
};

// Out-of-range values (e.g. from a newer version) fall back to yellow.
inline uint8_t Clamp(uint8_t color) { return color < kCount ? color : 0; }

inline const char *Name(uint8_t color) {
  static const char *const kNames[kCount] = {"yellow", "green", "blue", "pink",
                                             "purple"};
  return kNames[Clamp(color)];
}

// Next color forward (step > 0) or back, wrapping around.
inline uint8_t Step(uint8_t color, int step) {
  const int count = (int)kCount;
  int next = ((int)Clamp(color) + step) % count;
  if (next < 0)
    next += count;
  return (uint8_t)next;
}

// Tint behind glyphs (RGB565): soft on light/sepia backgrounds, muted on
// dark ones so light text stays readable.
inline uint16_t Tint(uint8_t color, bool dark_theme) {
  static const uint16_t kLight[kCount] = {
      0xFF71, // yellow  (255, 236, 136)
      0xB734, // green   (180, 230, 160)
      0xA67F, // blue    (165, 205, 250)
      0xFDB9, // pink    (250, 180, 200)
      0xCDBE, // purple  (205, 180, 240)
  };
  static const uint16_t kDark[kCount] = {
      0x5A82, // yellow  (dark olive)
      0x2AA5, // green   (40, 85, 40)
      0x21ED, // blue    (35, 60, 110)
      0x6948, // pink    (110, 40, 70)
      0x496D, // purple  (75, 45, 110)
  };
  return dark_theme ? kDark[Clamp(color)] : kLight[Clamp(color)];
}

} // namespace highlight_color_utils
