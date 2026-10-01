#include "library/library_paint_utils.h"
#include "library/library_theme_utils.h"

#include <vector>

#include "test_assert.h"

using library_paint_utils::Surface;

namespace {

const int kStride = 16;

Surface MakeSurface(std::vector<uint16_t> *buf, int w, int h) {
  buf->assign((size_t)kStride * (size_t)h, 0x1234);
  Surface s;
  s.pixels = buf->data();
  s.stride = kStride;
  s.width = w;
  s.height = h;
  return s;
}

void TestFillRectClips() {
  std::vector<uint16_t> buf;
  const Surface s = MakeSurface(&buf, 8, 6);
  library_paint_utils::FillRect(s, -3, -3, 2, 2, 0xFFFF);
  test::ExpectEqU("inside filled", buf[1 * kStride + 1], 0xFFFF);
  test::ExpectEqU("outside untouched", buf[2 * kStride + 2], 0x1234);
  library_paint_utils::FillRect(s, 6, 4, 40, 40, 0x0001);
  test::ExpectEqU("clipped fill reaches the edge", buf[5 * kStride + 7],
                  0x0001);
  test::ExpectEqU("nothing written past the logical width",
                  buf[5 * kStride + 8], 0x1234);
}

void TestFrameRect() {
  std::vector<uint16_t> buf;
  const Surface s = MakeSurface(&buf, 10, 10);
  library_paint_utils::FrameRect(s, 1, 1, 9, 9, 2, 0x00FF);
  test::ExpectEqU("frame corner", buf[1 * kStride + 1], 0x00FF);
  test::ExpectEqU("frame inner edge", buf[2 * kStride + 4], 0x00FF);
  test::ExpectEqU("frame right edge", buf[5 * kStride + 8], 0x00FF);
  test::ExpectEqU("inside the frame untouched", buf[4 * kStride + 4], 0x1234);
}

void TestBlitSameSizeCopies() {
  std::vector<uint16_t> buf;
  const Surface s = MakeSurface(&buf, 8, 8);
  const uint16_t src[4] = {1, 2, 3, 4};
  library_paint_utils::BlitScaled(s, src, 2, 2, 3, 3, 2, 2);
  test::ExpectEqU("top-left", buf[3 * kStride + 3], 1);
  test::ExpectEqU("bottom-right", buf[4 * kStride + 4], 4);
}

void TestBlitUpscaleIsSmoothAndKeepsSolidColors() {
  std::vector<uint16_t> buf;
  const Surface s = MakeSurface(&buf, 16, 16);
  const uint16_t red = library_paint_utils::Rgb565(255, 0, 0);
  const uint16_t solid[4] = {red, red, red, red};
  library_paint_utils::BlitScaled(s, solid, 2, 2, 0, 0, 8, 8);
  bool all_red = true;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      all_red = all_red && buf[(size_t)y * kStride + (size_t)x] == red;
  test::ExpectTrue("a solid image stays solid when enlarged", all_red);

  // Black to white: the middle of an enlarged 2-pixel ramp is grey.
  const uint16_t ramp[2] = {0x0000, 0xFFFF};
  library_paint_utils::BlitScaled(s, ramp, 2, 1, 0, 10, 8, 1);
  const uint16_t left = buf[10 * kStride + 0];
  const uint16_t mid = buf[10 * kStride + 4];
  const uint16_t right = buf[10 * kStride + 7];
  test::ExpectEqU("left end stays black", left, 0x0000);
  test::ExpectEqU("right end stays white", right, 0xFFFF);
  test::ExpectTrue("the middle is in between", mid != 0x0000 && mid != 0xFFFF);
}

void TestBlitDownscaleAverages() {
  std::vector<uint16_t> buf;
  const Surface s = MakeSurface(&buf, 8, 8);
  // A 2x2 checker of black and white shrinks to one mid-grey pixel.
  const uint16_t checker[4] = {0x0000, 0xFFFF, 0xFFFF, 0x0000};
  library_paint_utils::BlitScaled(s, checker, 2, 2, 0, 0, 1, 1);
  const uint16_t c = buf[0];
  const int r5 = (c >> 11) & 0x1F;
  test::ExpectTrue("averaged red is mid-range", r5 >= 15 && r5 <= 16);
}

void TestFitSize() {
  int w = 0, h = 0;
  library_paint_utils::FitSize(85, 115, 168, 232, true, &w, &h);
  test::ExpectEq("upscale fits the width", w, 168);
  test::ExpectEq("upscale keeps the aspect", h, 227);
  library_paint_utils::FitSize(60, 115, 85, 115, false, &w, &h);
  test::ExpectEq("no upscale keeps width", w, 60);
  test::ExpectEq("no upscale keeps height", h, 115);
  library_paint_utils::FitSize(200, 100, 85, 115, false, &w, &h);
  test::ExpectEq("wide image fits the width", w, 85);
  test::ExpectEq("wide image height", h, 43);
}

void TestCircleAndLine() {
  std::vector<uint16_t> buf;
  const Surface s = MakeSurface(&buf, 16, 16);
  library_paint_utils::FillCircle(s, 8.0f, 8.0f, 5.0f, 0xFFFF, 255);
  test::ExpectEqU("circle centre filled", buf[8 * kStride + 8], 0xFFFF);
  test::ExpectEqU("circle corner untouched", buf[1 * kStride + 1], 0x1234);
  const uint16_t edge = buf[8 * kStride + 3];
  test::ExpectTrue("circle edge is blended",
                   edge != 0x1234 || buf[8 * kStride + 2] == 0x1234);

  std::vector<uint16_t> buf2;
  const Surface s2 = MakeSurface(&buf2, 16, 16);
  library_paint_utils::FillCircle(s2, 8.0f, 8.0f, 5.0f, 0x0000, 0);
  test::ExpectEqU("zero alpha draws nothing", buf2[8 * kStride + 8], 0x1234);
  library_paint_utils::DrawLine(s2, 2.0f, 2.0f, 12.0f, 12.0f, 2.0f, 0xFFFF);
  test::ExpectEqU("line passes through the middle", buf2[7 * kStride + 7],
                  0xFFFF);
  test::ExpectEqU("line leaves far pixels", buf2[2 * kStride + 12], 0x1234);
  // Clipping: nothing written outside the logical surface.
  std::vector<uint16_t> buf3;
  const Surface s3 = MakeSurface(&buf3, 10, 10);
  library_paint_utils::FillCircle(s3, 9.0f, 5.0f, 4.0f, 0x0F0F, 255);
  test::ExpectEqU("drawn up to the logical width", buf3[5 * kStride + 9],
                  0x0F0F);
  test::ExpectEqU("clipped past the logical width", buf3[5 * kStride + 10],
                  0x1234);
}

void TestPalettesAreDistinct() {
  for (int mode = 0; mode < 6; mode++) {
    const library_theme_utils::LibraryPalette p =
        library_theme_utils::ForColorMode(mode);
    test::ExpectTrue("accent differs from background", p.accent != p.background);
    test::ExpectTrue("track differs from accent", p.track != p.accent);
    test::ExpectTrue("muted differs from text", p.muted != p.text);
    test::ExpectTrue("badge text readable", p.new_fg != p.new_bg &&
                                                p.done_fg != p.done_bg);
  }
}

} // namespace

int main() {
  TestFillRectClips();
  TestFrameRect();
  TestBlitSameSizeCopies();
  TestBlitUpscaleIsSmoothAndKeepsSolidColors();
  TestBlitDownscaleAverages();
  TestFitSize();
  TestCircleAndLine();
  TestPalettesAreDistinct();
  return 0;
}
