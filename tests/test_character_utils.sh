set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_character_utils \
  "$TEST_ROOT/tests/test_character_utils.cpp" \
  "$TEST_ROOT/source/book/character_utils.cpp" \
  "$TEST_ROOT/source/book/annotation_text_utils.cpp"
