/*
    3dslibris - reading_pace_utils.h

    Reading-pace estimate behind the "time remaining" readout.

    Only time spent reading new pages counts: a sample is taken when the
    reader turns forward off the furthest page reached so far (the frontier),
    timed from when they arrived on it. Turning back, re-reading earlier
    pages, flipping forward through them again and jumps (index, bookmarks,
    go to page) never produce samples, so they can't shrink the estimate.
*/

#pragma once

#include <stdint.h>

namespace reading_pace_utils {

static const uint32_t kSampleMinMs = 600;
static const uint32_t kSampleMaxMs = 240000;
static const float kEmaAlpha = 0.20f;
static const int kMinSamples = 3;

struct PaceState {
  float ms_per_page;
  uint32_t arrived_ms;      // when the current page was reached; 0 = unknown
  uint16_t samples;
  int frontier;             // furthest page reached; -1 = not yet known
  int frontier_page_count;  // page count the frontier refers to

  PaceState()
      : ms_per_page(0.0f), arrived_ms(0), samples(0), frontier(-1),
        frontier_page_count(0) {}
};

// Records a move from page prev to page next at time now_ms. page_count is
// the book's current page count (a change means it was re-paginated, which
// invalidates the frontier's page number).
void OnPositionChange(PaceState *state, int prev, int next, int page_count,
                      uint32_t now_ms);

bool HasEstimate(const PaceState &state);

// Starts from a pace remembered from an earlier session (ms_per_page 0 does
// nothing), so the estimate shows right away when the book is reopened. It
// counts as an established estimate; new pages keep refining it.
void Seed(PaceState *state, uint32_t ms_per_page);

// Minutes to read remaining_pages at the estimated pace (at least 1), 0 when
// nothing remains, -1 without an estimate.
int RemainingMinutes(const PaceState &state, int remaining_pages);

} // namespace reading_pace_utils
