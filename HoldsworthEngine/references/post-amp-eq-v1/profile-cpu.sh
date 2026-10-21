#!/usr/bin/env bash
set -euo pipefail
EQ_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
EQ_REPO="$(cd -- "$EQ_PACKAGE/../../.." && pwd)"
EQ_BUILD="${EQ_BUILD:-/private/tmp/nam-post-amp-eq-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
EQ_CXX="$(xcrun --find clang++)"
EQ_SDK="$(xcrun --show-sdk-path)"
EQ_COMMON=(-isysroot "$EQ_SDK" -isystem "$EQ_SDK/usr/include/c++/v1")
mkdir -p "$EQ_BUILD/profiling" "$EQ_PACKAGE/logs"
"$EQ_CXX" "${EQ_COMMON[@]}" -std=c++20 -pthread -O3 -DNDEBUG -Wl,-export_dynamic \
  "$EQ_PACKAGE/eq-cpu.cpp" "$EQ_REPO/HoldsworthEngine/dsp/PostAmpEQProcessor.cpp" \
  -o "$EQ_BUILD/profiling/eq-cpu"
"$EQ_CXX" "${EQ_COMMON[@]}" -std=c++20 -O2 -dynamiclib -undefined dynamic_lookup \
  "$EQ_PACKAGE/rt-audit.cpp" -o "$EQ_BUILD/profiling/rt-audit.dylib"
if [[ "${1:-}" == --build-only ]]; then exit 0; fi
DYLD_INSERT_LIBRARIES="$EQ_BUILD/profiling/rt-audit.dylib" \
  "$EQ_BUILD/profiling/eq-cpu" --interposer-self-test > "$EQ_PACKAGE/logs/rt-self-test.log" 2>&1
DYLD_INSERT_LIBRARIES="$EQ_BUILD/profiling/rt-audit.dylib" \
  "$EQ_BUILD/profiling/eq-cpu" > "$EQ_PACKAGE/logs/eq-cpu.csv" 2> "$EQ_PACKAGE/logs/rt-audit.log"
python3 "$EQ_PACKAGE/record-cpu-results.py"
