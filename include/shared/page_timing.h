/*
    3dslibris - page_timing.h

    Optional page-turn timing readout for performance work. Build with
    `make PAGE_TIMING=1` to show, for the last page turn, in milliseconds:
      d  drawing the page
      h  the rest of the reader's input handling that frame
      s  the last status bar redraw (on the frame after a turn)
      p  presenting the screens
      f  the whole frame, from vblank to presented (much more than the
         others added up means another thread had the CPU)
    Compiles to nothing otherwise.
*/

#pragma once

#ifndef PAGE_TIMING
#define PAGE_TIMING 0
#endif

#if PAGE_TIMING
#include <3ds.h>
#include <stdint.h>

namespace page_timing {

struct Stats {
  uint32_t draw_us;
  uint32_t handle_us;
  uint32_t status_us;
  uint32_t present_us;
  uint32_t frame_us;
};

// Shown in the status bar: the last frame that drew a page.
inline Stats &Last() {
  static Stats stats = {0, 0, 0, 0, 0};
  return stats;
}

// The frame in progress; copied to Last() when it drew a page.
struct Frame {
  uint64_t start;
  bool drew;
  Stats stats;
};

inline Frame &Current() {
  static Frame frame = {0, false, {0, 0, 0, 0, 0}};
  return frame;
}

inline uint64_t Now() { return svcGetSystemTick(); }

inline uint32_t ElapsedUs(uint64_t start_ticks) {
  const uint64_t ticks = svcGetSystemTick() - start_ticks;
  return (uint32_t)(ticks * 1000ULL / (uint64_t)CPU_TICKS_PER_MSEC);
}

} // namespace page_timing
#endif
