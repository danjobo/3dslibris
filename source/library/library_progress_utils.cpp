#include "library/library_progress_utils.h"

#include <stdio.h>

namespace library_progress_utils {

BookProgress Compute(int position, int page_count, uint32_t last_opened,
                     uint32_t ms_per_page) {
  BookProgress p;
  p.percent = -1;
  p.is_new = position <= 0 && last_opened == 0;
  p.finished = false;
  p.minutes_left = -1;
  if (position < 0)
    position = 0;
  if (page_count <= 0)
    return p;
  if (position > page_count - 1)
    position = page_count - 1;

  p.finished = !p.is_new && position >= page_count - 1;
  if (p.is_new)
    p.percent = 0;
  else if (p.finished)
    p.percent = 100;
  else
    p.percent = (int)((long long)(position + 1) * 100 / page_count);
  if (p.percent > 100)
    p.percent = 100;

  if (p.finished) {
    p.minutes_left = 0;
  } else if (ms_per_page > 0) {
    const long long remaining = page_count - 1 - position;
    const long long total_ms = remaining * (long long)ms_per_page;
    int minutes = (int)((total_ms + 59999) / 60000);
    if (minutes < 1)
      minutes = 1;
    p.minutes_left = minutes;
  }
  return p;
}

void FormatTimeLeft(int minutes, char *buf, size_t bufsz) {
  if (!buf || bufsz == 0)
    return;
  if (minutes < 0) {
    buf[0] = '\0';
    return;
  }
  const int hours = minutes / 60;
  const int mins = minutes % 60;
  if (hours > 0)
    snprintf(buf, bufsz, "%dh %02dm left", hours, mins);
  else
    snprintf(buf, bufsz, "%dm left", mins);
}

int FilledWidth(int percent, int width) {
  if (width <= 0 || percent <= 0)
    return 0;
  if (percent >= 100)
    return width;
  const int filled = (percent * width + 50) / 100;
  // Any progress at all shows at least one pixel.
  return filled < 1 ? 1 : filled;
}

} // namespace library_progress_utils
