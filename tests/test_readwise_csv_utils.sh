set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_readwise_csv_utils \
  "$TEST_ROOT/tests/test_readwise_csv_utils.cpp" \
  "$TEST_ROOT/source/book/readwise_csv_utils.cpp"
