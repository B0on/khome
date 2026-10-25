#!/usr/bin/env bash
# khome renderer build.
#
#   ./build.sh host   -> build/host/khome-render (macOS, PNG output)
#   ./build.sh arm    -> build/arm/khome-bench and build/arm/khome (Paperwhite 2, static)
#   ./build.sh all    -> both
#   ./build.sh test   -> host build, C unit tests, render/mock/golden tests
#   ./build.sh golden -> rewrite launcher/tests/golden/device/*.png

set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-cc}"
ZIG="${ZIG:-zig}"
COMMON="-std=c11 -O2 -Wall -Wextra -Werror -Ithird_party"
HOST_SRC="src/canvas.c src/gpos.c src/text.c src/layout.c src/kindletimes.c src/fixtures.c src/tabs.c src/urledit.c src/png_out.c src/render_main.c"
ARM_SRC="src/canvas.c src/gpos.c src/text.c src/layout.c src/kindletimes.c src/fixtures.c src/tabs.c src/urledit.c src/bench_main.c"
ARM_KHOME_SRC="src/canvas.c src/gpos.c src/text.c src/layout.c src/kindletimes.c src/gesture.c \
  src/fb.c src/input.c src/lipc.c src/state.c src/tabs.c src/urledit.c src/main.c"
TEST_SRC="src/canvas.c src/gpos.c src/text.c src/layout.c src/kindletimes.c src/fixtures.c \
  src/gesture.c src/input.c src/lipc.c src/state.c src/tabs.c src/urledit.c \
  tests/test_harness.c tests/test_text.c tests/test_text_wrap.c \
  tests/test_layout.c tests/test_canvas.c \
  tests/test_gesture.c tests/test_input.c tests/test_lipc_parse.c \
  tests/test_lipc_watch.c tests/test_state.c \
  tests/test_tabs.c tests/test_urledit.c tests/test_favorites.c \
  tests/test_gaps.c tests/test_news.c tests/test_browser_row.c \
  tests/test_long_press.c tests/test_kindletimes.c tests/test_main.c"

build_host() {
  mkdir -p build/host
  echo "cc -> build/host/khome-render"
  $CC $COMMON $HOST_SRC -o build/host/khome-render -lm
}

build_tests() {
  mkdir -p build/host
  echo "cc -> build/host/khome-tests"
  $CC $COMMON -Isrc -Itests/stub $TEST_SRC -o build/host/khome-tests -lm
}

build_arm() {
  mkdir -p build/arm
  echo "zig cc -> build/arm/khome-bench"
  $ZIG cc -target arm-linux-musleabi -mcpu=cortex_a9 -static $COMMON \
    $ARM_SRC -o build/arm/khome-bench -lm
  echo "zig cc -> build/arm/khome"
  $ZIG cc -target arm-linux-musleabi -mcpu=cortex_a9 -static $COMMON \
    $ARM_KHOME_SRC -o build/arm/khome -lm
}

NEWS_FIXTURE="$PWD/tests/fixtures/news-text"
NEWS_SCREENS="news-front news-list news-article news-article-mid news-article-last"

render_news_screens() {
  local out="$1"
  local screen
  for screen in $NEWS_SCREENS; do
    KHOME_NEWS_TEXT_DIR="$NEWS_FIXTURE" \
      ./build/host/khome-render "$screen" design "$out/design/$screen.png"
    KHOME_NEWS_TEXT_DIR="$NEWS_FIXTURE" \
      ./build/host/khome-render "$screen" device "$out/device/$screen.png"
  done
}

write_golden() {
  local out="build/golden-out"
  build_host
  rm -rf "$out"
  ./build/host/khome-render all "$out"
  render_news_screens "$out"
  mkdir -p tests/golden/device
  cp "$out"/device/*.png tests/golden/device/
  echo "golden: wrote tests/golden/device/*.png from $out/device"
}

run_tests() {
  local status=0
  local out="build/test-out"
  build_host
  build_tests
  rm -rf "$out"
  ./build/host/khome-render all "$out"
  render_news_screens "$out"
  echo
  echo "--- C unit tests ---"
  ./build/host/khome-tests || status=1
  echo
  echo "--- render tests ---"
  python3 tests/test_render.py --render "$out" || status=1
  echo
  if [ "$status" -eq 0 ]; then
    echo "build.sh test: all tests passed"
  else
    echo "build.sh test: FAILURES (see FAIL lines above)"
  fi
  return "$status"
}

case "${1:-all}" in
  host) build_host ;;
  arm) build_arm ;;
  all) build_host; build_arm ;;
  test) run_tests ;;
  golden) write_golden ;;
  *)
    echo "usage: $0 [host|arm|all|test|golden]" >&2
    exit 2
    ;;
esac
