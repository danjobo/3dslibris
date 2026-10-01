set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_console_id \
  "$TEST_ROOT/tests/test_console_id.cpp" \
  "$TEST_ROOT/source/shared/console_id.cpp"
