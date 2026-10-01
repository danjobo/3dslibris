#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
. tests/test_build.sh
build_test test_library_paint_utils \
  "$TEST_ROOT/tests/test_library_paint_utils.cpp" \
  "$TEST_ROOT/source/library/library_paint_utils.cpp" \
  "$TEST_ROOT/source/library/library_theme_utils.cpp"
