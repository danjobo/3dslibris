set -eu
source "$(dirname "$0")/test_build.sh"
# A native Windows test binary needs C:/... rather than /c/... paths.
ROOT_PATH="$TEST_ROOT"
if command -v cygpath >/dev/null 2>&1; then
  ROOT_PATH="$(cygpath -m "$TEST_ROOT")"
fi
build_test test_word_lookup \
  "$TEST_ROOT/tests/test_word_lookup.cpp" \
  "$TEST_ROOT/source/dictionary/word_lookup_utils.cpp" \
  "$TEST_ROOT/source/dictionary/stardict.cpp" \
  "$TEST_ROOT/source/dictionary/dictionary_set.cpp" \
  -DTEST_ROOT_DIR=\""$ROOT_PATH"\" \
  -lz
