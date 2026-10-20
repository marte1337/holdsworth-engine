#!/usr/bin/env bash
set -euo pipefail
LEGATO_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
LEGATO_REPO="$(cd -- "$LEGATO_PACKAGE/../../.." && pwd)"
LEGATO_BUILD="${LEGATO_BUILD:-/private/tmp/nam-legato-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
LEGATO_CXX="$(xcrun --find clang++)"
LEGATO_SDK="$(xcrun --show-sdk-path)"
mkdir -p "$LEGATO_BUILD/engine"
cd "$LEGATO_REPO"
if [[ $# == 0 ]]; then set -- debug release asan tsan; fi
for mode in "$@"; do
  flags=(-O0 -g)
  case "$mode" in
    debug) ;;
    release) flags=(-O3 -DNDEBUG);;
    asan) flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer);;
    tsan) flags=(-O1 -g -fsanitize=thread);;
    *) exit 2;;
  esac
  "$LEGATO_CXX" -std=c++20 -pthread -isysroot "$LEGATO_SDK" -isystem "$LEGATO_SDK/usr/include/c++/v1" \
    "${flags[@]}" HoldsworthEngine/dsp/*.cpp HoldsworthEngine/integration/*.cpp HoldsworthEngine/tests/*.cpp \
    -o "$LEGATO_BUILD/engine/tests-$mode"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 TSAN_OPTIONS=halt_on_error=1 \
    "$LEGATO_BUILD/engine/tests-$mode" > "$LEGATO_BUILD/engine/tests-$mode.log" 2>&1
  tail -1 "$LEGATO_BUILD/engine/tests-$mode.log"
done
