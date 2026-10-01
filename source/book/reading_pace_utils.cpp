#include "book/reading_pace_utils.h"

namespace reading_pace_utils {

void OnPositionChange(PaceState *state, int prev, int next, int page_count,
                      uint32_t now_ms) {
  if (!state || next == prev)
    return;

  if (state->frontier < 0) {
    // First move since the book opened: the current page is the frontier.
    state->frontier = prev;
    state->frontier_page_count = page_count;
  } else if (state->frontier_page_count != page_count) {
    // Re-pagination renumbered every page, and this move is the remap to the
    // new numbering. Restart the frontier from where the reader now is.
    state->frontier = next;
    state->frontier_page_count = page_count;
    state->arrived_ms = now_ms;
    return;
  }

  const bool read_new_page = (next == prev + 1) && prev >= state->frontier;
  if (read_new_page && state->arrived_ms != 0 && now_ms > state->arrived_ms) {
    const uint32_t elapsed_ms = now_ms - state->arrived_ms;
    if (elapsed_ms >= kSampleMinMs && elapsed_ms <= kSampleMaxMs) {
      const float sample = (float)elapsed_ms;
      if (state->samples == 0)
        state->ms_per_page = sample;
      else
        state->ms_per_page =
            state->ms_per_page * (1.0f - kEmaAlpha) + sample * kEmaAlpha;
      if (state->samples < 0xFFFF)
        state->samples++;
    }
  }

  if (next > state->frontier)
    state->frontier = next;
  // Time on a page counts only from arrival; reaching the frontier again
  // after re-reading starts its clock fresh.
  state->arrived_ms = now_ms;
}

bool HasEstimate(const PaceState &state) {
  return state.samples >= kMinSamples && state.ms_per_page > 0.0f;
}

void Seed(PaceState *state, uint32_t ms_per_page) {
  if (!state || ms_per_page == 0)
    return;
  state->ms_per_page = (float)ms_per_page;
  if (state->samples < kMinSamples)
    state->samples = kMinSamples;
}

int RemainingMinutes(const PaceState &state, int remaining_pages) {
  if (remaining_pages <= 0)
    return 0;
  if (!HasEstimate(state))
    return -1;
  const float total_ms = (float)remaining_pages * state.ms_per_page;
  int minutes = (int)((total_ms + 59999.0f) / 60000.0f);
  if (minutes < 1)
    minutes = 1;
  return minutes;
}

} // namespace reading_pace_utils
