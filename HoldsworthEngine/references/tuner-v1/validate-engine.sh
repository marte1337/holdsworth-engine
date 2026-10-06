#!/usr/bin/env bash
set -euo pipefail
TUNER_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
TUNER_REPO="$(cd -- "$TUNER_PACKAGE/../../.." && pwd)"
TUNER_BUILD="${TUNER_BUILD:-/private/tmp/nam-tuner-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
TUNER_CXX="$(xcrun --find clang++)"
TUNER_SDK="$(xcrun --show-sdk-path)"
mkdir -p "$TUNER_BUILD/engine"
cd "$TUNER_REPO"
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
  "$TUNER_CXX" -std=c++20 -pthread -isysroot "$TUNER_SDK" -isystem "$TUNER_SDK/usr/include/c++/v1" \
    "${flags[@]}" HoldsworthEngine/dsp/*.cpp HoldsworthEngine/integration/*.cpp HoldsworthEngine/tests/*.cpp \
    -o "$TUNER_BUILD/engine/tests-$mode"
  if [[ "$mode" == tsan ]]; then
    "$TUNER_BUILD/engine/tests-$mode" 'Tuner capture' > "$TUNER_BUILD/engine/tests-$mode.log" 2>&1
    "$TUNER_BUILD/engine/tests-$mode" 'Tuner service' >> "$TUNER_BUILD/engine/tests-$mode.log" 2>&1
  else
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
      "$TUNER_BUILD/engine/tests-$mode" > "$TUNER_BUILD/engine/tests-$mode.log" 2>&1
  fi
  tail -1 "$TUNER_BUILD/engine/tests-$mode.log"
done
