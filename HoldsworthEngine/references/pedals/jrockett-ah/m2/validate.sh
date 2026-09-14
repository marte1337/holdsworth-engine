#!/usr/bin/env bash
set -euo pipefail
AH_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
AH_REPO="$(cd -- "$AH_PACKAGE/../../../../.." && pwd)"
AH_BUILD="${AH_BUILD:-/tmp/jrockett-ah-m2}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
mkdir -p "$AH_BUILD/build-scripts"
cd "$AH_REPO"
cat > "$AH_BUILD/build-scripts/clear_audiounit_caches.command" <<'SCRIPT'
#!/bin/sh
# Isolated build: leave installed plugins and user AU caches unchanged.
exit 0
SCRIPT
chmod +x "$AH_BUILD/build-scripts/clear_audiounit_caches.command"
for configuration in Debug Release; do
  xcodebuild -project NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj \
    -target HoldsworthEngineTests -configuration "$configuration" CODE_SIGNING_ALLOWED=NO \
    ARCHS=arm64 ONLY_ACTIVE_ARCH=YES CONFIGURATION_BUILD_DIR="$AH_BUILD/$configuration" \
    OBJROOT="$AH_BUILD/obj-$configuration" CLANG_MODULE_CACHE_PATH="$AH_BUILD/modules-$configuration" \
    > "$AH_BUILD/$configuration-build.log" 2>&1
  "$AH_BUILD/$configuration/HoldsworthEngineTests" > "$AH_BUILD/$configuration-tests.log"
done
xcrun clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp -pthread -o "$AH_BUILD/tests-sanitized"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$AH_BUILD/tests-sanitized" > "$AH_BUILD/sanitizer-tests.log"
xcrun clang++ -std=c++20 -O1 -g -fsanitize=thread \
  HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp -pthread -o "$AH_BUILD/tests-tsan"
"$AH_BUILD/tests-tsan" 'AH Boost concurrent' > "$AH_BUILD/tsan-tests.log"
for source in HoldsworthEngine/dsp/JRockettAHBoostProcessor.cpp \
              HoldsworthEngine/tests/JRockettAHBoostProcessorTests.cpp \
              HoldsworthEngine/tests/DevelopmentPreNAMSelectorTests.cpp \
              "$AH_PACKAGE/realtime.cpp"; do
  xcrun clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsyntax-only "$source"
done
xcrun clang++ --analyze -std=c++20 HoldsworthEngine/dsp/JRockettAHBoostProcessor.cpp -o "$AH_BUILD/analyzer.plist"
xcrun clang++ -std=c++20 -O3 -DNDEBUG HoldsworthEngine/dsp/JRockettAHBoostProcessor.cpp \
  "$AH_PACKAGE/../m1/measure.cpp" -o "$AH_BUILD/measure"
# Keep new measurements in the temporary build directory; compare with retained M1 data.
"$AH_BUILD/measure" "$AH_BUILD" > "$AH_BUILD/measurements.json"
xcodebuild -project NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj \
  -target APP -target VST3 -target AU -configuration Release CODE_SIGNING_ALLOWED=NO \
  ARCHS=arm64 ONLY_ACTIVE_ARCH=YES SYMROOT="$AH_BUILD/plugin-build" OBJROOT="$AH_BUILD/plugin-obj" \
  CLANG_MODULE_CACHE_PATH="$AH_BUILD/modules-plugin" APP_PATH="$AH_BUILD/products" \
  VST3_PATH="$AH_BUILD/products" AU_PATH="$AH_BUILD/products" SCRIPTS_PATH="$AH_BUILD/build-scripts" \
  > "$AH_BUILD/plugin-build.log" 2>&1
xcrun clang++ -std=c++20 -O3 -DNDEBUG HoldsworthEngine/dsp/JRockettAHBoostProcessor.cpp \
  "$AH_PACKAGE/realtime.cpp" -o "$AH_BUILD/realtime"
"$AH_BUILD/realtime" > "$AH_BUILD/realtime.csv"
plutil -lint NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj
python3 "$AH_PACKAGE/audit.py"
git diff --check
