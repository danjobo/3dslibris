#include "reader/text_selection_utils.h"

#include <algorithm>
#include <limits>

#include "shared/orientation_utils.h"

namespace text_selection_utils {

namespace {

// Stack the second screen below the first so vertical movement continues
// across screens in reading order (same trick as inline link navigation).
static const int kSecondScreenYOffset = 420;

inline_link_utils::LinkRect StackedRect(const WordBox &word) {
  inline_link_utils::LinkRect r = word.bounds;
  if (word.screen_index != 0) {
    r.y0 += kSecondScreenYOffset;
    r.y1 += kSecondScreenYOffset;
  }
  return r;
}

} // namespace

int StepWord(int word_count, int current, int direction) {
  if (word_count <= 0)
    return -1;
  int next = current + (direction > 0 ? 1 : (direction < 0 ? -1 : 0));
  if (next < 0)
    next = 0;
  if (next >= word_count)
    next = word_count - 1;
  return next;
}

int VerticalNeighbor(const std::vector<WordBox> &words, int current,
                     bool down) {
  if (current < 0 || current >= (int)words.size())
    return -1;
  std::vector<inline_link_utils::LinkRect> rects;
  rects.reserve(words.size());
  for (size_t i = 0; i < words.size(); i++)
    rects.push_back(StackedRect(words[i]));
  return inline_link_utils::FindNeighborIndex(
      rects, current,
      down ? inline_link_utils::INLINE_LINK_NAV_DOWN
           : inline_link_utils::INLINE_LINK_NAV_UP);
}

int WordAtPoint(const std::vector<WordBox> &words, uint8_t screen_index, int x,
                int y, int pad_px) {
  int best = -1;
  long best_dist = std::numeric_limits<long>::max();
  for (size_t i = 0; i < words.size(); i++) {
    const WordBox &w = words[i];
    if (w.screen_index != screen_index)
      continue;
    const inline_link_utils::LinkRect &r = w.bounds;
    if (!inline_link_utils::IsValidRect(r))
      continue;
    if (y < r.y0 - pad_px || y >= r.y1 + pad_px)
      continue;
    if (x >= r.x0 - pad_px && x < r.x1 + pad_px)
      return (int)i;
    // Same line but between words: pick the horizontally closest one.
    const long dx = x < r.x0 ? (long)(r.x0 - x) : (long)(x - r.x1);
    if (dx < best_dist) {
      best_dist = dx;
      best = (int)i;
    }
  }
  return best;
}

bool SelectionBufRange(const std::vector<WordBox> &words, int a, int b,
                       int *buf_begin, int *buf_end) {
  if (!buf_begin || !buf_end || words.empty())
    return false;
  if (a < 0)
    a = b;
  if (a < 0 || b < 0 || a >= (int)words.size() || b >= (int)words.size())
    return false;
  const int first = std::min(a, b);
  const int last = std::max(a, b);
  *buf_begin = words[(size_t)first].buf_begin;
  *buf_end = words[(size_t)last].buf_end;
  return *buf_end > *buf_begin;
}

ScreenDirection PhysicalToScreenDirection(unsigned char orientation, bool up,
                                          bool down, bool left, bool right) {
  ScreenDirection physical = ScreenDirection::None;
  if (up)
    physical = ScreenDirection::Up;
  else if (down)
    physical = ScreenDirection::Down;
  else if (left)
    physical = ScreenDirection::Left;
  else if (right)
    physical = ScreenDirection::Right;
  if (physical == ScreenDirection::None ||
      orientation_utils::IsLandscape(orientation))
    return physical;

  // Turned left: the console's top points to the reader's left, so its
  // left button points down the page. Turned right is the mirror image.
  const bool turned_right = orientation_utils::IsTurnedRight(orientation);
  switch (physical) {
  case ScreenDirection::Up:
    return turned_right ? ScreenDirection::Right : ScreenDirection::Left;
  case ScreenDirection::Down:
    return turned_right ? ScreenDirection::Left : ScreenDirection::Right;
  case ScreenDirection::Left:
    return turned_right ? ScreenDirection::Up : ScreenDirection::Down;
  case ScreenDirection::Right:
    return turned_right ? ScreenDirection::Down : ScreenDirection::Up;
  default:
    return ScreenDirection::None;
  }
}

uint8_t TouchScreenIndex(unsigned char orientation) {
  // The touch screen is the right buffer; it is reading screen 0 only when
  // the console is turned right.
  return orientation_utils::FirstScreenIsLeft(orientation) ? 1 : 0;
}

uint16_t TintPixel565(uint16_t pixel, uint16_t tint, bool dark_theme) {
  const int pr = (pixel >> 11) & 0x1F, pg = (pixel >> 5) & 0x3F,
            pb = pixel & 0x1F;
  const int tr = (tint >> 11) & 0x1F, tg = (tint >> 5) & 0x3F,
            tb = tint & 0x1F;
  int r, g, b;
  if (dark_theme) {
    r = (pr + tr) / 2;
    g = (pg + tg) / 2;
    b = (pb + tb) / 2;
  } else {
    r = (pr * tr + 15) / 31;
    g = (pg * tg + 31) / 63;
    b = (pb * tb + 15) / 31;
  }
  return (uint16_t)((r << 11) | (g << 5) | b);
}

MirrorMap BuildMirrorMap(int src_w, int src_h, int dst_w, int dst_h) {
  MirrorMap map;
  map.src_w = src_w;
  map.src_h = src_h;
  map.dst_w = dst_w;
  map.dst_h = dst_h;
  if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    map.draw_w = map.draw_h = map.off_x = map.off_y = 0;
    return map;
  }
  // Keep the aspect ratio: fit whichever axis is the tighter constraint.
  if ((long)dst_w * src_h <= (long)dst_h * src_w) {
    map.draw_w = dst_w;
    map.draw_h = (int)((long)src_h * dst_w / src_w);
  } else {
    map.draw_h = dst_h;
    map.draw_w = (int)((long)src_w * dst_h / src_h);
  }
  map.off_x = (dst_w - map.draw_w) / 2;
  map.off_y = (dst_h - map.draw_h) / 2;
  return map;
}

bool MirrorDstToSrc(const MirrorMap &map, int dx, int dy, int *sx, int *sy) {
  if (!sx || !sy || map.draw_w <= 0 || map.draw_h <= 0)
    return false;
  const int x = dx - map.off_x;
  const int y = dy - map.off_y;
  if (x < 0 || y < 0 || x >= map.draw_w || y >= map.draw_h)
    return false;
  *sx = (int)((long)x * map.src_w / map.draw_w);
  *sy = (int)((long)y * map.src_h / map.draw_h);
  return true;
}

int StepPopupIndex(int option_count, int current, int direction) {
  if (option_count <= 0)
    return 0;
  int next = current + (direction > 0 ? 1 : -1);
  if (next < 0)
    next = option_count - 1;
  if (next >= option_count)
    next = 0;
  return next;
}

} // namespace text_selection_utils
