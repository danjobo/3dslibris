/*
 * Minimal Text stub for host integration tests.
 * Replaces ui/text.h when compiled with -I tests/stubs.
 * Returns fixed layout metrics so reflowable parsers can produce pages
 * without a real FreeType/framebuffer setup.
 */
#pragma once

#include <cstdint>
#include <string>
#include "3ds/types.h"
#include "shared/text_token_constants.h"

class IStatusReporter;

class Text {
public:
  int pixelsize;
  struct { u8 r, g, b; } bgcolor;
  u16 fgcolor;
  bool usefgcolor;
  bool usebgcolor;
  u16 *screen, *screenleft, *screenright, *offscreen;
  struct { int left, right, top, bottom; } margin;
  struct { int width, height; } display;
  int linespacing;
  bool linebegan, bold, italic;
  bool screenleft_dirty, screenright_dirty;

  Text()
      : pixelsize(14), fgcolor(0), usefgcolor(false), usebgcolor(false),
        screen(nullptr), screenleft(nullptr), screenright(nullptr),
        offscreen(nullptr), linespacing(1), linebegan(false), bold(false),
        italic(false), screenleft_dirty(true), screenright_dirty(true) {
    bgcolor.r = 15; bgcolor.g = 15; bgcolor.b = 15;
    margin.left = 12; margin.right = 12; margin.top = 10; margin.bottom = 36;
    display.width = 400; display.height = 240;
  }
  ~Text() {}

  // Layout metrics
  u8 GetHeight() const { return (u8)pixelsize; }
  u8 GetAdvance(u32) const { return (u8)(pixelsize * 8 / 14); }
  u8 GetAdvance(u32, u8) const { return (u8)(pixelsize * 8 / 14); }
  u8 GetPixelSize() const { return (u8)pixelsize; }
  void SetPixelSize(u8 size) { pixelsize = (int)size; }
  int GetStringAdvance(const char *) { return 0; }

  // Style
  void SetStyle(int) {}
  int GetStyle() const { return 0; }
  std::string GetFontFile(u8) const { return ""; }
  std::string GetFontFile(u8, int) const { return ""; }
  int GetColorMode() { return 0; }
  u16 GetFgColor() { return fgcolor; }
  void SetTextColorOverride(u16) {}
  void ClearTextColorOverride() {}

  // Reporter
  void SetReporter(IStatusReporter *) {}
  IStatusReporter *GetReporter() const { return nullptr; }
  void SetFontDir(const std::string &) {}

  // Pen / position. With track_pen set, the pen follows TextRenderer's rules
  // (InitPen, PrintNewLine, glyph advance and bottom clipping) so Page::Draw
  // can be checked on the host; clipped_glyphs counts glyphs the real
  // renderer would silently skip below the bottom margin.
  bool track_pen = false;
  int pen_x = 0;
  int pen_y = 0;
  int clipped_glyphs = 0;
  // Optional: records each line advance ("<screen>:<baseline>") for tests
  // that compare renderer line positions against the paginator.
  std::string *line_trace = nullptr;

  void InitPen() {
    if (!track_pen)
      return;
    pen_x = margin.left;
    pen_y = margin.top + GetHeight();
  }
  u16 GetPenX() { return track_pen ? (u16)pen_x : 0; }
  u16 GetPenY() { return track_pen ? (u16)pen_y : 0; }
  void SetPen(u16 x, u16 y) {
    if (!track_pen)
      return;
    pen_x = x;
    pen_y = y;
  }
  int CurrentScreenHeight() const {
    return LogicalHeightFor(screen == screenleft);
  }

  // Geometry. The stub keeps display.* configurable per test, so the buffer
  // stride and logical width mirror it instead of the real fixed constants.
  int BufferStride() const { return display.height; }
  int LogicalWidthFor(bool) const { return display.width; }
  int LogicalHeightFor(bool is_left_buffer) const {
    return is_left_buffer ? 400 : 320;
  }
  int LogicalWidth() const { return display.width; }
  int LogicalHeight() const { return display.height; }

  // Screen management
  u16 *GetScreen() { return screen; }
  void SetScreen(u16 *s) { screen = s; }
  void MarkScreenDirty(u16 *) {}
  void MarkScreenDirtyRect(u16 *, int, int, int, int) {}
  void CopyScreen(u16 *, u16 *) {}

  // Drawing
  void FillRect(u16, u16, u16, u16, u16) {}
  bool PrintNewLine() {
    if (!track_pen)
      return false;
    pen_x = margin.left;
    const int height = GetHeight();
    const int y = pen_y + height + linespacing;
    if (y > CurrentScreenHeight() - margin.bottom) {
      if (screen == screenleft) {
        screen = screenright;
        pen_y = margin.top + height;
        return true;
      }
      return false;
    }
    pen_y += height + linespacing;
    if (line_trace)
      *line_trace += std::string(screen == screenleft ? "L" : "R") + ":" +
                     std::to_string(pen_y) + " ";
    return true;
  }
  void ClearScreen() {}
  void PrintChar(u32 c) { PrintChar(c, 0); }
  void PrintChar(u32 c, u8 style) {
    if (!track_pen)
      return;
    if (c != ' ' && pen_y > CurrentScreenHeight() - margin.bottom)
      clipped_glyphs++;
    pen_x += GetAdvance(c, style);
  }
  void PrintString(const char *) {}
  void PrintString(const char *, u8) {}

  // Wrap / clip flags
  bool IsAutoWrapEnabled() const { return false; }
  void SetAutoWrapEnabled(bool) {}
  bool IsClipToContentEnabled() const { return false; }
  void SetClipToContentEnabled(bool) {}

  // Script scale (superscript/subscript)
  void SetScriptScale(float) {}
  float GetScriptScale() const { return 1.0f; }
};
