/*
    3dslibris - page_timing.h

    Optional page-turn timing readout for performance work. Build with
    `make PAGE_TIMING=1` to show the last page draw and screen present times
    (milliseconds) in the reader status bar. Compiles to nothing otherwise.
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
  uint32_t present_us;
};

inline Stats &Last() {
  static Stats stats = {0, 0};
  return stats;
}

inline uint64_t Now() { return svcGetSystemTick(); }

inline uint32_t ElapsedUs(uint64_t start_ticks) {
  const uint64_t ticks = svcGetSystemTick() - start_ticks;
  return (uint32_t)(ticks * 1000ULL / (uint64_t)CPU_TICKS_PER_MSEC);
}

} // namespace page_timing
#endif
