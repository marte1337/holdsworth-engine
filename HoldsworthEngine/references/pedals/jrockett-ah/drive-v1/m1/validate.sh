#!/usr/bin/env bash
set -euo pipefail
AH_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
AH_REPO="$(cd -- "$AH_PACKAGE/../../../../../.." && pwd)"
AH_BUILD="${AH_BUILD:-/private/tmp/ah-drive-m1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
mkdir -p "$AH_BUILD/build-support"
cp "$AH_PACKAGE/build-support/clear_audiounit_caches.command" "$AH_BUILD/build-support/"
chmod +x "$AH_BUILD/build-support/clear_audiounit_caches.command"
cd "$AH_REPO"
for configuration in Debug Release; do
  xcodebuild -quiet -project NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj \
    -target HoldsworthEngineTests -configuration "$configuration" CODE_SIGNING_ALLOWED=NO \
    ARCHS=arm64 ONLY_ACTIVE_ARCH=YES CONFIGURATION_BUILD_DIR="$AH_BUILD/$configuration" \
    OBJROOT="$AH_BUILD/obj-$configuration" CLANG_MODULE_CACHE_PATH="$AH_BUILD/modules-$configuration" \
    > "$AH_BUILD/$configuration-build.log" 2>&1
  "$AH_BUILD/$configuration/HoldsworthEngineTests" > "$AH_BUILD/$configuration-tests.log"
  tail -1 "$AH_BUILD/$configuration-tests.log"
done
xcrun clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp -pthread -o "$AH_BUILD/tests-sanitized"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$AH_BUILD/tests-sanitized" > "$AH_BUILD/sanitizer-tests.log"
tail -1 "$AH_BUILD/sanitizer-tests.log"
xcrun clang++ -std=c++20 -O1 -g -fsanitize=thread \
  HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp -pthread -o "$AH_BUILD/tests-tsan"
"$AH_BUILD/tests-tsan" 'AH Drive concurrent' > "$AH_BUILD/tsan-tests.log"
tail -1 "$AH_BUILD/tsan-tests.log"
for source in HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp \
              HoldsworthEngine/tests/JRockettAHDriveProcessorTests.cpp \
              "$AH_PACKAGE/oracle_bridge.cpp" "$AH_PACKAGE/benchmark.cpp"; do
  xcrun clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsyntax-only "$source"
done
xcrun clang++ --analyze -std=c++20 HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp -o "$AH_BUILD/analyzer.plist"
xcrun clang++ -std=c++20 -O3 -DNDEBUG -dynamiclib HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp \
  "$AH_PACKAGE/oracle_bridge.cpp" -o "$AH_BUILD/oracle.dylib"
xcrun clang++ -std=c++20 -O3 -DNDEBUG HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp \
  "$AH_PACKAGE/benchmark.cpp" -o "$AH_BUILD/benchmark"
# No Drive source is enrolled in product targets; still check their existing builds.
xcodebuild -quiet -project NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj \
  -target APP -target VST3 -target AU -configuration Release CODE_SIGNING_ALLOWED=NO \
  ARCHS=arm64 ONLY_ACTIVE_ARCH=YES SYMROOT="$AH_BUILD/plugin-build" OBJROOT="$AH_BUILD/plugin-obj" \
  CLANG_MODULE_CACHE_PATH="$AH_BUILD/modules-plugin" APP_PATH="$AH_BUILD/products" \
  VST3_PATH="$AH_BUILD/products" AU_PATH="$AH_BUILD/products" SCRIPTS_PATH="$AH_BUILD/build-support" \
  > "$AH_BUILD/plugin-build.log" 2>&1
plutil -lint NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj
git diff --check
echo "PASS: native validation; oracle and quiet CPU benchmark can now run separately."
