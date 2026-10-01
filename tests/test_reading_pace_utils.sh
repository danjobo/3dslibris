set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_reading_pace_utils \
  "$TEST_ROOT/tests/test_reading_pace_utils.cpp" \
  "$TEST_ROOT/source/book/reading_pace_utils.cpp"
