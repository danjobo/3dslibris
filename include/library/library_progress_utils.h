/*
    3dslibris - library_progress_utils.h

    What the library shows about a book's reading progress: the percentage
    for the progress bar, the NEW and DONE badges and the time left. The
    inputs are what prefs remember about a closed book (page, page count,
    last opened time, reading pace). Pure, host-testable.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace library_progress_utils {

struct BookProgress {
  // 0..100, or -1 when the page count isn't known yet (the book hasn't been
  // open since page counts were first remembered).
  int percent;
  // Never opened: no saved page and no last opened time.
  bool is_new;
  // On the last page.
  bool finished;
  // Minutes left at the remembered pace, or -1 without one.
  int minutes_left;
};

// position: 0-based page. page_count: 0 when unknown. ms_per_page: 0 when
// no pace is known.
BookProgress Compute(int position, int page_count, uint32_t last_opened,
                     uint32_t ms_per_page);

// "3h 10m left", "25m left". Writes "" for minutes < 0.
void FormatTimeLeft(int minutes, char *buf, size_t bufsz);

// Width in pixels of the filled part of a bar `width` wide.
int FilledWidth(int percent, int width);

} // namespace library_progress_utils
