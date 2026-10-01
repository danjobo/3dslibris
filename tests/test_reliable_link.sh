set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_reliable_link \
  "$TEST_ROOT/tests/test_reliable_link.cpp" \
  "$TEST_ROOT/source/sync/reliable_link.cpp" \
  "$TEST_ROOT/source/sync/sync_session.cpp" \
  "$TEST_ROOT/source/sync/sync_protocol.cpp" \
  "$TEST_ROOT/source/sync/sync_manifest.cpp" \
  "$TEST_ROOT/source/sync/sync_merge.cpp" \
  "$TEST_ROOT/source/book/annotation_store_utils.cpp"
