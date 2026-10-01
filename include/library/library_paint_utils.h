/*
    3dslibris - library_paint_utils.h

    Small RGB565 drawing primitives for the library screens: a scaled cover
    blit (bilinear when enlarging, area average when shrinking), clipped
    rectangle fills and frames. They write straight into a software screen
    buffer, so they don't depend on Text and are host-testable.
*/

#pragma once

#include <stdint.h>

namespace library_paint_utils {

// A screen buffer: pixels[y * stride + x], drawable within width x height.
struct Surface {
  uint16_t *pixels;
  int stride;
  int width;
  int height;
};

uint16_t Rgb565(int r, int g, int b);
// Mixes a toward b by t/255.
uint16_t Blend565(uint16_t a, uint16_t b, int t);

// Fills [x0, x1) x [y0, y1), clipped to the surface.
void FillRect(const Surface &s, int x0, int y0, int x1, int y1,
              uint16_t color);
// A frame `thickness` pixels wide just inside [x0, x1) x [y0, y1).
void FrameRect(const Surface &s, int x0, int y0, int x1, int y1,
               int thickness, uint16_t color);

// Draws a src_w x src_h image scaled to dst_w x dst_h at (dst_x, dst_y).
void BlitScaled(const Surface &s, const uint16_t *src, int src_w, int src_h,
                int dst_x, int dst_y, int dst_w, int dst_h);

// Anti-aliased shapes. Coordinates are in pixels, with pixel (x, y) covering
// [x, x+1) x [y, y+1), so a centre of (10, 10) sits on a pixel corner.
// alpha (0-255) scales the coverage, e.g. for soft shadows.
void FillCircle(const Surface &s, float cx, float cy, float radius,
                uint16_t color, int alpha);
// A line segment `width` pixels wide with round ends.
void DrawLine(const Surface &s, float x0, float y0, float x1, float y1,
              float width, uint16_t color);

// Largest size with the source's aspect ratio that fits max_w x max_h. With
// allow_upscale false the image is never enlarged.
void FitSize(int src_w, int src_h, int max_w, int max_h, bool allow_upscale,
             int *out_w, int *out_h);

} // namespace library_paint_utils
