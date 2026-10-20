#!/usr/bin/env bash
set -euo pipefail
LEGATO_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
LEGATO_REPO="$(cd -- "$LEGATO_PACKAGE/../../.." && pwd)"
LEGATO_BUILD="${LEGATO_BUILD:-/private/tmp/nam-legato-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
LEGATO_CXX="$(xcrun --find clang++)"
LEGATO_SDK="$(xcrun --show-sdk-path)"
mkdir -p "$LEGATO_BUILD/measurements"
cd "$LEGATO_REPO"
common=(-std=c++20 -pthread -isysroot "$LEGATO_SDK" -isystem "$LEGATO_SDK/usr/include/c++/v1")
sources=(HoldsworthEngine/dsp/LegatoCompressorProcessor.cpp
  HoldsworthEngine/tests/LegatoCompressorProcessorTests.cpp
  HoldsworthEngine/tests/LegatoCompressorIntegrationTests.cpp "$LEGATO_PACKAGE/measure.cpp")
"$LEGATO_CXX" "${common[@]}" -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror \
  -fsyntax-only "${sources[@]}" > "$LEGATO_BUILD/strict.log" 2>&1
for source in "${sources[@]}"; do
  name="$(basename -- "$source" .cpp)"
  "$LEGATO_CXX" "${common[@]}" --analyze -Xanalyzer -analyzer-werror "$source" -o "$LEGATO_BUILD/$name-analyzer.plist"
done
# TC bridge is an unchanged dependency; its existing local-shadow diagnostic
# under -Wshadow is outside this feature's strict-warning scope.
"$LEGATO_CXX" "${common[@]}" -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Werror -Wl,-export_dynamic \
  "$LEGATO_PACKAGE/measure.cpp" HoldsworthEngine/dsp/LegatoCompressorProcessor.cpp \
  HoldsworthEngine/dsp/TCBLDCleanBoostProcessor.cpp -o "$LEGATO_BUILD/measure"
"$LEGATO_CXX" "${common[@]}" -O2 -dynamiclib -undefined dynamic_lookup \
  "$LEGATO_PACKAGE/rt-audit.cpp" -o "$LEGATO_BUILD/rt-audit.dylib"
DYLD_INSERT_LIBRARIES="$LEGATO_BUILD/rt-audit.dylib" \
  "$LEGATO_BUILD/measure" "$LEGATO_BUILD/measurements" > "$LEGATO_BUILD/measurements/rt-audit.log"
python3 "$LEGATO_PACKAGE/analyze.py" "$LEGATO_BUILD/measurements" > "$LEGATO_BUILD/measurements/analysis.log"
python3 "$LEGATO_PACKAGE/source-audit.py" "$LEGATO_BUILD/source-audit.json"
plutil -lint NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj
git diff --check
cat "$LEGATO_BUILD/measurements/rt-audit.log"
