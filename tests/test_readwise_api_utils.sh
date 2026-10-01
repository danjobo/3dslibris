set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_readwise_api_utils \
  "$TEST_ROOT/tests/test_readwise_api_utils.cpp" \
  "$TEST_ROOT/source/book/readwise_api_utils.cpp"
