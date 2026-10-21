#!/usr/bin/env bash
set -euo pipefail
EQ_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
EQ_REPO="$(cd -- "$EQ_PACKAGE/../../.." && pwd)"
EQ_BUILD="${EQ_BUILD:-/private/tmp/nam-post-amp-eq-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
EQ_CXX="$(xcrun --find clang++)"
EQ_SDK="$(xcrun --show-sdk-path)"
EQ_COMMON=(-isysroot "$EQ_SDK" -isystem "$EQ_SDK/usr/include/c++/v1")
mkdir -p "$EQ_BUILD/engine" "$EQ_PACKAGE/logs"
cd "$EQ_REPO"
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
  "$EQ_CXX" "${EQ_COMMON[@]}" -std=c++20 -pthread "${flags[@]}" HoldsworthEngine/dsp/*.cpp \
    HoldsworthEngine/integration/*.cpp HoldsworthEngine/tests/*.cpp -o "$EQ_BUILD/engine/tests-$mode" \
    > "$EQ_PACKAGE/logs/compile-$mode.log" 2>&1
  if [[ "$mode" == tsan ]]; then
    "$EQ_BUILD/engine/tests-$mode" 'concurr' > "$EQ_PACKAGE/logs/tests-$mode.log" 2>&1
  else
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
      "$EQ_BUILD/engine/tests-$mode" > "$EQ_PACKAGE/logs/tests-$mode.log" 2>&1
  fi
  tail -1 "$EQ_PACKAGE/logs/tests-$mode.log"
done
