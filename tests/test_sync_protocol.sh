set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_sync_protocol \
  "$TEST_ROOT/tests/test_sync_protocol.cpp" \
  "$TEST_ROOT/source/sync/sync_protocol.cpp"
