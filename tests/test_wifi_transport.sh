set -eu
# Uses POSIX sockets over loopback; Windows (MinGW) hosts lack them.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "SKIP test_wifi_transport: needs POSIX sockets"
    exit 0
    ;;
esac
source "$(dirname "$0")/test_build.sh"
build_test test_wifi_transport \
  "$TEST_ROOT/tests/test_wifi_transport.cpp" \
  "$TEST_ROOT/source/sync/wifi_transport.cpp" \
  "$TEST_ROOT/source/sync/sync_session.cpp" \
  "$TEST_ROOT/source/sync/sync_protocol.cpp" \
  "$TEST_ROOT/source/sync/sync_manifest.cpp" \
  "$TEST_ROOT/source/sync/sync_merge.cpp" \
  "$TEST_ROOT/source/book/annotation_store_utils.cpp"
