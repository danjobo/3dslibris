set -eu
source "$(dirname "$0")/test_build.sh"
build_test test_sync_merge \
  "$TEST_ROOT/tests/test_sync_merge.cpp" \
  "$TEST_ROOT/source/sync/sync_merge.cpp" \
  "$TEST_ROOT/source/sync/sync_manifest.cpp" \
  "$TEST_ROOT/source/book/annotation_store_utils.cpp"
