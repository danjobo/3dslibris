set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_text_selection_utils \
  "$TEST_ROOT/tests/test_text_selection_utils.cpp" \
  "$TEST_ROOT/source/reader/text_selection_utils.cpp" \
  "$TEST_ROOT/source/reader/inline_link_utils.cpp"
