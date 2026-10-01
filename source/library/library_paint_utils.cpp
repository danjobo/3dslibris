#include "library/library_paint_utils.h"

#include <stddef.h>

#include <math.h>

#include <vector>

namespace library_paint_utils {

namespace {

inline int R5(uint16_t c) { return (c >> 11) & 0x1F; }
inline int G6(uint16_t c) { return (c >> 5) & 0x3F; }
inline int B5(uint16_t c) { return c & 0x1F; }

inline uint16_t Pack(int r5, int g6, int b5) {
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

inline bool Inside(const Surface &s, int x, int y) {
  return x >= 0 && y >= 0 && x < s.width && y < s.height;
}

// Enlarging: bilinear, sampling at pixel centres (16.16 fixed point).
void BlitBilinear(const Surface &s, const uint16_t *src, int src_w, int src_h,
                  int dst_x, int dst_y, int dst_w, int dst_h) {
  std::vector<int> col0((size_t)dst_w), col1((size_t)dst_w), colf((size_t)dst_w);
  for (int x = 0; x < dst_w; x++) {
    int pos = (int)((((long long)(2 * x + 1) * src_w << 16) / (2 * dst_w)) -
                    (1 << 15));
    if (pos < 0)
      pos = 0;
    int i = pos >> 16;
    if (i >= src_w - 1) {
      col0[(size_t)x] = col1[(size_t)x] = src_w - 1;
      colf[(size_t)x] = 0;
    } else {
      col0[(size_t)x] = i;
      col1[(size_t)x] = i + 1;
      colf[(size_t)x] = (pos >> 8) & 0xFF;
    }
  }

  for (int y = 0; y < dst_h; y++) {
    const int py = dst_y + y;
    if (py < 0 || py >= s.height)
      continue;
    int pos = (int)((((long long)(2 * y + 1) * src_h << 16) / (2 * dst_h)) -
                    (1 << 15));
    if (pos < 0)
      pos = 0;
    int r0 = pos >> 16;
    int r1 = r0 + 1;
    int fy = (pos >> 8) & 0xFF;
    if (r0 >= src_h - 1) {
      r0 = r1 = src_h - 1;
      fy = 0;
    }
    const uint16_t *row0 = src + (size_t)r0 * (size_t)src_w;
    const uint16_t *row1 = src + (size_t)r1 * (size_t)src_w;
    uint16_t *out = s.pixels + (size_t)py * (size_t)s.stride;
    for (int x = 0; x < dst_w; x++) {
      const int px = dst_x + x;
      if (px < 0 || px >= s.width)
        continue;
      const uint16_t a = row0[col0[(size_t)x]];
      const uint16_t b = row0[col1[(size_t)x]];
      const uint16_t c = row1[col0[(size_t)x]];
      const uint16_t d = row1[col1[(size_t)x]];
      const int fx = colf[(size_t)x];
      const int w00 = (256 - fx) * (256 - fy);
      const int w01 = fx * (256 - fy);
      const int w10 = (256 - fx) * fy;
      const int w11 = fx * fy;
      const int r = (R5(a) * w00 + R5(b) * w01 + R5(c) * w10 + R5(d) * w11 +
                     32768) >> 16;
      const int g = (G6(a) * w00 + G6(b) * w01 + G6(c) * w10 + G6(d) * w11 +
                     32768) >> 16;
      const int bl = (B5(a) * w00 + B5(b) * w01 + B5(c) * w10 + B5(d) * w11 +
                      32768) >> 16;
      out[px] = Pack(r, g, bl);
    }
  }
}

// Shrinking (in at least one direction): average the source pixels each
// destination pixel covers.
void BlitBoxAverage(const Surface &s, const uint16_t *src, int src_w,
                    int src_h, int dst_x, int dst_y, int dst_w, int dst_h) {
  std::vector<int> x_start((size_t)dst_w), x_end((size_t)dst_w);
  for (int x = 0; x < dst_w; x++) {
    int a = (int)((long long)x * src_w / dst_w);
    int b = (int)((long long)(x + 1) * src_w / dst_w);
    if (b <= a)
      b = a + 1;
    if (b > src_w)
      b = src_w;
    x_start[(size_t)x] = a;
    x_end[(size_t)x] = b;
  }
  for (int y = 0; y < dst_h; y++) {
    const int py = dst_y + y;
    if (py < 0 || py >= s.height)
      continue;
    int y0 = (int)((long long)y * src_h / dst_h);
    int y1 = (int)((long long)(y + 1) * src_h / dst_h);
    if (y1 <= y0)
      y1 = y0 + 1;
    if (y1 > src_h)
      y1 = src_h;
    uint16_t *out = s.pixels + (size_t)py * (size_t)s.stride;
    for (int x = 0; x < dst_w; x++) {
      const int px = dst_x + x;
      if (px < 0 || px >= s.width)
        continue;
      int r = 0, g = 0, b = 0, n = 0;
      for (int sy = y0; sy < y1; sy++) {
        const uint16_t *row = src + (size_t)sy * (size_t)src_w;
        for (int sx = x_start[(size_t)x]; sx < x_end[(size_t)x]; sx++) {
          const uint16_t c = row[sx];
          r += R5(c);
          g += G6(c);
          b += B5(c);
          n++;
        }
      }
      out[px] = Pack((r + n / 2) / n, (g + n / 2) / n, (b + n / 2) / n);
    }
  }
}

} // namespace

uint16_t Rgb565(int r, int g, int b) {
  if (r < 0) r = 0; else if (r > 255) r = 255;
  if (g < 0) g = 0; else if (g > 255) g = 255;
  if (b < 0) b = 0; else if (b > 255) b = 255;
  return Pack(r >> 3, g >> 2, b >> 3);
}

uint16_t Blend565(uint16_t a, uint16_t b, int t) {
  if (t <= 0)
    return a;
  if (t >= 255)
    return b;
  const int r = R5(a) + ((R5(b) - R5(a)) * t + 127) / 255;
  const int g = G6(a) + ((G6(b) - G6(a)) * t + 127) / 255;
  const int bl = B5(a) + ((B5(b) - B5(a)) * t + 127) / 255;
  return Pack(r, g, bl);
}

void FillRect(const Surface &s, int x0, int y0, int x1, int y1,
              uint16_t color) {
  if (!s.pixels)
    return;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > s.width) x1 = s.width;
  if (y1 > s.height) y1 = s.height;
  for (int y = y0; y < y1; y++) {
    uint16_t *row = s.pixels + (size_t)y * (size_t)s.stride;
    for (int x = x0; x < x1; x++)
      row[x] = color;
  }
}

void FrameRect(const Surface &s, int x0, int y0, int x1, int y1,
               int thickness, uint16_t color) {
  if (thickness <= 0 || x1 <= x0 || y1 <= y0)
    return;
  FillRect(s, x0, y0, x1, y0 + thickness, color);
  FillRect(s, x0, y1 - thickness, x1, y1, color);
  FillRect(s, x0, y0 + thickness, x0 + thickness, y1 - thickness, color);
  FillRect(s, x1 - thickness, y0 + thickness, x1, y1 - thickness, color);
}

void BlitScaled(const Surface &s, const uint16_t *src, int src_w, int src_h,
                int dst_x, int dst_y, int dst_w, int dst_h) {
  if (!s.pixels || !src || src_w <= 0 || src_h <= 0 || dst_w <= 0 ||
      dst_h <= 0)
    return;
  if (dst_w == src_w && dst_h == src_h) {
    for (int y = 0; y < dst_h; y++) {
      const int py = dst_y + y;
      if (py < 0 || py >= s.height)
        continue;
      for (int x = 0; x < dst_w; x++) {
        if (Inside(s, dst_x + x, py))
          s.pixels[(size_t)py * (size_t)s.stride + (size_t)(dst_x + x)] =
              src[(size_t)y * (size_t)src_w + (size_t)x];
      }
    }
    return;
  }
  if (dst_w >= src_w && dst_h >= src_h)
    BlitBilinear(s, src, src_w, src_h, dst_x, dst_y, dst_w, dst_h);
  else
    BlitBoxAverage(s, src, src_w, src_h, dst_x, dst_y, dst_w, dst_h);
}

namespace {

// Blends color over pixel (x, y) by coverage 0..1.
inline void Plot(const Surface &s, int x, int y, uint16_t color,
                 float coverage) {
  if (coverage <= 0.0f || !Inside(s, x, y))
    return;
  uint16_t *px = s.pixels + (size_t)y * (size_t)s.stride + (size_t)x;
  *px = coverage >= 1.0f ? color : Blend565(*px, color, (int)(coverage * 255.0f));
}

inline float Clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

void FillCircle(const Surface &s, float cx, float cy, float radius,
                uint16_t color, int alpha) {
  if (!s.pixels || radius <= 0.0f || alpha <= 0)
    return;
  const float a = (alpha > 255 ? 255 : alpha) / 255.0f;
  const int x0 = (int)floorf(cx - radius - 1.0f);
  const int x1 = (int)ceilf(cx + radius + 1.0f);
  const int y0 = (int)floorf(cy - radius - 1.0f);
  const int y1 = (int)ceilf(cy + radius + 1.0f);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float dx = (float)x + 0.5f - cx;
      const float dy = (float)y + 0.5f - cy;
      const float dist = sqrtf(dx * dx + dy * dy);
      Plot(s, x, y, color, Clamp01(radius + 0.5f - dist) * a);
    }
  }
}

void DrawLine(const Surface &s, float x0, float y0, float x1, float y1,
              float width, uint16_t color) {
  if (!s.pixels || width <= 0.0f)
    return;
  const float half = width * 0.5f;
  const float vx = x1 - x0;
  const float vy = y1 - y0;
  const float len2 = vx * vx + vy * vy;
  const int bx0 = (int)floorf((x0 < x1 ? x0 : x1) - half - 1.0f);
  const int bx1 = (int)ceilf((x0 > x1 ? x0 : x1) + half + 1.0f);
  const int by0 = (int)floorf((y0 < y1 ? y0 : y1) - half - 1.0f);
  const int by1 = (int)ceilf((y0 > y1 ? y0 : y1) + half + 1.0f);
  for (int y = by0; y <= by1; y++) {
    for (int x = bx0; x <= bx1; x++) {
      const float px = (float)x + 0.5f - x0;
      const float py = (float)y + 0.5f - y0;
      float t = len2 > 0.0f ? (px * vx + py * vy) / len2 : 0.0f;
      t = Clamp01(t);
      const float dx = px - t * vx;
      const float dy = py - t * vy;
      const float dist = sqrtf(dx * dx + dy * dy);
      Plot(s, x, y, color, Clamp01(half + 0.5f - dist));
    }
  }
}

void FitSize(int src_w, int src_h, int max_w, int max_h, bool allow_upscale,
             int *out_w, int *out_h) {
  if (!out_w || !out_h)
    return;
  *out_w = 0;
  *out_h = 0;
  if (src_w <= 0 || src_h <= 0 || max_w <= 0 || max_h <= 0)
    return;
  if (!allow_upscale && src_w <= max_w && src_h <= max_h) {
    *out_w = src_w;
    *out_h = src_h;
    return;
  }
  // Compare max_w / src_w with max_h / src_h without floating point.
  if ((long long)max_w * src_h <= (long long)max_h * src_w) {
    *out_w = max_w;
    *out_h = (int)(((long long)src_h * max_w + src_w / 2) / src_w);
  } else {
    *out_h = max_h;
    *out_w = (int)(((long long)src_w * max_h + src_h / 2) / src_h);
  }
  if (*out_w < 1)
    *out_w = 1;
  if (*out_h < 1)
    *out_h = 1;
}

} // namespace library_paint_utils
