#include "reader/text_selection_utils.h"

#include <algorithm>
#include <limits>

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
