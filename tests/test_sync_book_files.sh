set -eu
# Uses mkdtemp and POSIX directories; Windows (MinGW) hosts lack them.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "SKIP test_sync_book_files: needs POSIX file APIs"
    exit 0
    ;;
esac
source "$(dirname "$0")/test_build.sh"
build_test test_sync_book_files \
  "$TEST_ROOT/tests/test_sync_book_files.cpp" \
  "$TEST_ROOT/source/sync/sync_book_files.cpp" \
  "$TEST_ROOT/source/sync/sync_session.cpp" \
  "$TEST_ROOT/source/sync/sync_protocol.cpp" \
  "$TEST_ROOT/source/sync/sync_manifest.cpp" \
  "$TEST_ROOT/source/sync/sync_merge.cpp" \
  "$TEST_ROOT/source/book/annotation_store_utils.cpp"
