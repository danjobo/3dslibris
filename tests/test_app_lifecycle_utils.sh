set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_app_lifecycle_utils \
  "$TEST_ROOT/tests/test_app_lifecycle_utils.cpp" \
  "$TEST_ROOT/source/shared/app_lifecycle_utils.cpp"
