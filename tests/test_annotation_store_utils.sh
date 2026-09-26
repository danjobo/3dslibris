set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_annotation_store_utils \
  "$TEST_ROOT/tests/test_annotation_store_utils.cpp" \
  "$TEST_ROOT/source/book/annotation_store_utils.cpp"
