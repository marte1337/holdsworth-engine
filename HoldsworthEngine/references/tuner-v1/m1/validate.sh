#!/usr/bin/env bash
set -euo pipefail
TUNER_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
TUNER_REPO="$(cd -- "$TUNER_PACKAGE/../../../.." && pwd)"
TUNER_BUILD="${TUNER_BUILD:-/private/tmp/tuner-v1/m1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
TUNER_CXX="$(xcrun --find clang++)"
TUNER_SDK="$(xcrun --show-sdk-path)"
mkdir -p "$TUNER_BUILD"
cd "$TUNER_REPO"
if [[ $# == 0 ]]; then set -- debug release; fi
for mode in "$@"; do
  flags=(-O0 -g)
  if [[ "$mode" == release ]]; then flags=(-O3 -DNDEBUG); fi
  "$TUNER_CXX" -std=c++20 -pthread -isysroot "$TUNER_SDK" -isystem "$TUNER_SDK/usr/include/c++/v1" \
    "${flags[@]}" HoldsworthEngine/dsp/*.cpp HoldsworthEngine/integration/*.cpp HoldsworthEngine/tests/*.cpp -o "$TUNER_BUILD/tests-$mode"
  "$TUNER_BUILD/tests-$mode" Tuner > "$TUNER_BUILD/tests-$mode.log" 2>&1
  cat "$TUNER_BUILD/tests-$mode.log"
done
"$TUNER_CXX" -std=c++20 -O3 -DNDEBUG -isysroot "$TUNER_SDK" -isystem "$TUNER_SDK/usr/include/c++/v1" \
  HoldsworthEngine/dsp/ChromaticTuner.cpp "$TUNER_PACKAGE/benchmark.cpp" -o "$TUNER_BUILD/benchmark"
"$TUNER_BUILD/benchmark" > "$TUNER_BUILD/benchmark.jsonl"
cat "$TUNER_BUILD/benchmark.jsonl"
