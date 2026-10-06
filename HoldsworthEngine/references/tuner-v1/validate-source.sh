#!/usr/bin/env bash
set -euo pipefail
TUNER_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
TUNER_REPO="$(cd -- "$TUNER_PACKAGE/../../.." && pwd)"
TUNER_BUILD="${TUNER_BUILD:-/private/tmp/nam-tuner-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
TUNER_CXX="$(xcrun --find clang++)"
TUNER_SDK="$(xcrun --show-sdk-path)"
mkdir -p "$TUNER_BUILD/source"
cd "$TUNER_REPO"
files=(HoldsworthEngine/dsp/ChromaticTuner.cpp
  HoldsworthEngine/integration/TunerAnalysisService.cpp
  HoldsworthEngine/integration/TunerDisplayState.cpp
  HoldsworthEngine/tests/ChromaticTunerTests.cpp
  HoldsworthEngine/tests/TunerCaptureBufferTests.cpp
  HoldsworthEngine/tests/TunerAnalysisServiceTests.cpp
  HoldsworthEngine/tests/TunerDisplayStateTests.cpp)
common=(-std=c++20 -pthread -isysroot "$TUNER_SDK" -isystem "$TUNER_SDK/usr/include/c++/v1")
"$TUNER_CXX" "${common[@]}" -Wall -Wextra -Wpedantic -Werror -fsyntax-only "${files[@]}" \
  > "$TUNER_BUILD/source/strict-warnings.log" 2>&1
for source in "${files[@]}"; do
  name="$(basename -- "$source" .cpp)"
  "$TUNER_CXX" "${common[@]}" --analyze -Xanalyzer -analyzer-werror "$source" \
    -o "$TUNER_BUILD/source/$name.plist" > "$TUNER_BUILD/source/$name.log" 2>&1
done
plutil -lint NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj
git diff --check
python3 "$TUNER_PACKAGE/source-audit.py"
echo 'PASS strict warnings, static analyzer, project lint, diff and audible-source audit'
