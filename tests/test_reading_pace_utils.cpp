#include "book/reading_pace_utils.h"

#include "test_assert.h"

using reading_pace_utils::OnPositionChange;
using reading_pace_utils::PaceState;

namespace {

const int kPages = 500;

// Reads forward from `from` for `count` pages at `ms_per_page`, starting at
// time *now. Returns the final page.
int ReadForward(PaceState *s, int from, int count, uint32_t ms_per_page,
                uint32_t *now) {
  int page = from;
  for (int i = 0; i < count; i++) {
    *now += ms_per_page;
    OnPositionChange(s, page, page + 1, kPages, *now);
    page++;
  }
  return page;
}

void TestForwardReadingBuildsEstimate() {
  PaceState s;
  uint32_t now = 100000;
  // The first turn only establishes the frontier and the arrival time.
  int page = ReadForward(&s, 10, 4, 30000, &now);
  test::ExpectEq("three samples after four turns", s.samples, 3);
  test::ExpectTrue("estimate available", reading_pace_utils::HasEstimate(s));
  test::ExpectEq("pace is 30s/page", (int)s.ms_per_page, 30000);
  test::ExpectEq("frontier follows reading", s.frontier, page);
  test::ExpectEq("100 pages at 30s = 50 min",
                 reading_pace_utils::RemainingMinutes(s, 100), 50);
}

void TestBacktrackingAndRereadingDoNotSample() {
  PaceState s;
  uint32_t now = 100000;
  int page = ReadForward(&s, 10, 5, 30000, &now);
  const uint16_t samples = s.samples;
  const float pace = s.ms_per_page;

  // Flip back three pages, then forward again over pages already read.
  for (int i = 0; i < 3; i++) {
    now += 1000;
    OnPositionChange(&s, page, page - 1, kPages, now);
    page--;
  }
  page = ReadForward(&s, page, 3, 1000, &now);
  test::ExpectEq("re-read pages add no samples", s.samples, samples);
  test::ExpectTrue("pace unchanged by re-reading", s.ms_per_page == pace);

  // Back at the frontier: the time there counts from arrival.
  page = ReadForward(&s, page, 1, 30000, &now);
  test::ExpectEq("new page after re-reading samples again", s.samples,
                 samples + 1);
  test::ExpectEq("pace still 30s/page", (int)s.ms_per_page, 30000);
}

void TestJumpsDoNotSample() {
  PaceState s;
  uint32_t now = 100000;
  int page = ReadForward(&s, 10, 4, 30000, &now);
  const uint16_t samples = s.samples;
  now += 30000;
  OnPositionChange(&s, page, 200, kPages, now); // chapter jump forward
  test::ExpectEq("jump adds no sample", s.samples, samples);
  test::ExpectEq("jump moves frontier", s.frontier, 200);
  now += 20000;
  OnPositionChange(&s, 200, 201, kPages, now);
  test::ExpectEq("reading after jump samples", s.samples, samples + 1);
  (void)page;
}

void TestRepaginationResetsFrontier() {
  PaceState s;
  uint32_t now = 100000;
  ReadForward(&s, 300, 4, 30000, &now);
  const uint16_t samples = s.samples;
  // Bigger font: fewer pages, position remapped from 304 to 150.
  now += 1000;
  OnPositionChange(&s, 304, 150, 250, now);
  test::ExpectEq("frontier follows the new numbering", s.frontier, 150);
  now += 30000;
  OnPositionChange(&s, 150, 151, 250, now);
  now += 30000;
  OnPositionChange(&s, 151, 152, 250, now);
  test::ExpectTrue("reading continues to sample after re-pagination",
                   s.samples > samples);
}

void TestSampleBounds() {
  PaceState s;
  uint32_t now = 100000;
  OnPositionChange(&s, 0, 1, kPages, now);
  now += 100; // too quick to be reading
  OnPositionChange(&s, 1, 2, kPages, now);
  now += 600000; // left the console sitting
  OnPositionChange(&s, 2, 3, kPages, now);
  test::ExpectEq("out-of-range samples ignored", s.samples, 0);
  test::ExpectFalse("no estimate", reading_pace_utils::HasEstimate(s));
  test::ExpectEq("no estimate minutes", reading_pace_utils::RemainingMinutes(s, 5),
                 -1);
  test::ExpectEq("nothing remaining", reading_pace_utils::RemainingMinutes(s, 0),
                 0);
}

void TestSeedGivesImmediateEstimate() {
  PaceState s;
  reading_pace_utils::Seed(&s, 0);
  test::ExpectFalse("seeding with no pace does nothing",
                    reading_pace_utils::HasEstimate(s));
  reading_pace_utils::Seed(&s, 60000);
  test::ExpectTrue("seeded pace is an estimate",
                   reading_pace_utils::HasEstimate(s));
  test::ExpectEq("ten pages at a minute each",
                 reading_pace_utils::RemainingMinutes(s, 10), 10);

  // New pages keep refining a seeded estimate.
  uint32_t now = 100000;
  ReadForward(&s, 10, 6, 30000, &now);
  test::ExpectLt("faster reading lowers the seeded pace",
                 (int)s.ms_per_page, 60000);
}

} // namespace

int main() {
  TestForwardReadingBuildsEstimate();
  TestBacktrackingAndRereadingDoNotSample();
  TestJumpsDoNotSample();
  TestRepaginationResetsFrontier();
  TestSampleBounds();
  TestSeedGivesImmediateEstimate();
  return 0;
}
