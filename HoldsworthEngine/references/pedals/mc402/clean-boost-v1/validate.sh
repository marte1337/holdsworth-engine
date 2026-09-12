#!/usr/bin/env bash
set -euo pipefail
MC402_PACKAGE_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
MC402_REPO_ROOT="$(cd -- "$MC402_PACKAGE_DIR/../../../../.." && pwd)"
MC402_BUILD="${MC402_BUILD:-/tmp/mc402-boost-v1-revalidate}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
mkdir -p "$MC402_BUILD/build-scripts"
cd "$MC402_REPO_ROOT"
cat > "$MC402_BUILD/build-scripts/clear_audiounit_caches.command" <<'SCRIPT'
#!/bin/sh
# Isolated build: leave user AU caches unchanged.
exit 0
SCRIPT
chmod +x "$MC402_BUILD/build-scripts/clear_audiounit_caches.command"

for configuration in Debug Release; do
  xcodebuild -project NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj \
    -target HoldsworthEngineTests -configuration "$configuration" \
    CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
    CONFIGURATION_BUILD_DIR="$MC402_BUILD/$configuration" OBJROOT="$MC402_BUILD/obj-$configuration" \
    CLANG_MODULE_CACHE_PATH="$MC402_BUILD/modules" > "$MC402_BUILD/$configuration-build.log" 2>&1
  "$MC402_BUILD/$configuration/HoldsworthEngineTests" > "$MC402_BUILD/$configuration-tests.log"
done
xcrun clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp -pthread -o "$MC402_BUILD/tests-sanitized"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$MC402_BUILD/tests-sanitized" > "$MC402_BUILD/sanitizer-tests.log"
xcrun clang++ -std=c++20 -O1 -g -fsanitize=thread \
  HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp -pthread -o "$MC402_BUILD/tests-tsan"
"$MC402_BUILD/tests-tsan" 'MC402 Boost concurrent' > "$MC402_BUILD/tsan-tests.log"
for source in HoldsworthEngine/dsp/MC402CleanBoostProcessor.cpp \
              HoldsworthEngine/tests/MC402CleanBoostProcessorTests.cpp \
              HoldsworthEngine/tests/DevelopmentPreNAMSelectorTests.cpp; do
  xcrun clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsyntax-only "$source"
done
xcrun clang++ --analyze -std=c++20 HoldsworthEngine/dsp/MC402CleanBoostProcessor.cpp -o "$MC402_BUILD/analyzer.plist"
xcodebuild -project NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj \
  -target APP -target VST3 -target AU -configuration Debug CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
  SYMROOT="$MC402_BUILD/plugin-build" OBJROOT="$MC402_BUILD/plugin-obj" CLANG_MODULE_CACHE_PATH="$MC402_BUILD/modules" \
  APP_PATH="$MC402_BUILD/products" VST3_PATH="$MC402_BUILD/products" AU_PATH="$MC402_BUILD/products" \
  SCRIPTS_PATH="$MC402_BUILD/build-scripts" > "$MC402_BUILD/plugin-build.log" 2>&1
plutil -lint NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj
python3 "$MC402_PACKAGE_DIR/audit.py"
git diff --check
# Launch is a separate, explicit manual-inspection step:
# open "$MC402_BUILD/products/NeuralAmpModeler.app"
