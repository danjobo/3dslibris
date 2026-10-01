#include "library/library_progress_utils.h"

#include "test_assert.h"

using library_progress_utils::BookProgress;
using library_progress_utils::Compute;

namespace {

void TestNewBook() {
  const BookProgress p = Compute(0, 300, 0, 0);
  test::ExpectTrue("never opened is new", p.is_new);
  test::ExpectFalse("new is not finished", p.finished);
  test::ExpectEq("new book at 0%", p.percent, 0);
  test::ExpectEq("no pace, no time left", p.minutes_left, -1);

  const BookProgress opened = Compute(0, 300, 1700000000u, 0);
  test::ExpectFalse("opened on page 1 is not new", opened.is_new);
}

void TestUnknownPageCount() {
  const BookProgress p = Compute(40, 0, 1700000000u, 30000);
  test::ExpectEq("unknown page count has no percent", p.percent, -1);
  test::ExpectFalse("unknown page count is not finished", p.finished);
  test::ExpectEq("unknown page count has no time left", p.minutes_left, -1);
}

void TestInProgress() {
  // Page 100 of 400 (0-based 99), 30 s per page: 300 pages left = 150 min.
  const BookProgress p = Compute(99, 400, 1700000000u, 30000);
  test::ExpectEq("percent", p.percent, 25);
  test::ExpectFalse("not finished", p.finished);
  test::ExpectEq("minutes left", p.minutes_left, 150);

  const BookProgress near_end = Compute(398, 400, 1700000000u, 1000);
  test::ExpectEq("a few seconds left rounds up to a minute",
                 near_end.minutes_left, 1);
  test::ExpectEq("one page before the end is 99%", near_end.percent, 99);
}

void TestFinished() {
  const BookProgress p = Compute(399, 400, 1700000000u, 30000);
  test::ExpectTrue("last page is finished", p.finished);
  test::ExpectEq("finished is 100%", p.percent, 100);
  test::ExpectEq("nothing left", p.minutes_left, 0);

  const BookProgress past = Compute(500, 400, 1700000000u, 0);
  test::ExpectTrue("a position past the end is finished", past.finished);
}

void TestFormatTimeLeft() {
  char buf[32];
  library_progress_utils::FormatTimeLeft(25, buf, sizeof(buf));
  test::ExpectStrEq("minutes only", buf, "25m left");
  library_progress_utils::FormatTimeLeft(190, buf, sizeof(buf));
  test::ExpectStrEq("hours and minutes", buf, "3h 10m left");
  library_progress_utils::FormatTimeLeft(-1, buf, sizeof(buf));
  test::ExpectStrEq("unknown is empty", buf, "");
}

void TestFilledWidth() {
  test::ExpectEq("0%", library_progress_utils::FilledWidth(0, 80), 0);
  test::ExpectEq("unknown", library_progress_utils::FilledWidth(-1, 80), 0);
  test::ExpectEq("1% still shows", library_progress_utils::FilledWidth(1, 30), 1);
  test::ExpectEq("half", library_progress_utils::FilledWidth(50, 80), 40);
  test::ExpectEq("full", library_progress_utils::FilledWidth(100, 80), 80);
}

} // namespace

int main() {
  TestNewBook();
  TestUnknownPageCount();
  TestInProgress();
  TestFinished();
  TestFormatTimeLeft();
  TestFilledWidth();
  return 0;
}
