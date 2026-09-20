#!/usr/bin/env bash
set -euo pipefail
PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
REPO="$(cd -- "$PACKAGE/../../../../../.." && pwd)"
BUILD="${AH_BUILD:-/private/tmp/ah-full-m2}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
CXX="$(xcrun --find clang++)"
SDK="$(xcrun --show-sdk-path)"
COMMON=(-std=c++20 -pthread -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1"
  -isystem iPlug2/IPlug -isystem iPlug2/IPlug/Extras -isystem iPlug2/WDL)
SOURCES=("$PACKAGE/pedal-host-tests.cpp" iPlug2/IPlug/IPlugProcessor.cpp
  HoldsworthEngine/dsp/JRockettAHBoostProcessor.cpp HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp)
mkdir -p "$BUILD"
cd "$REPO"
for mode in debug release asan tsan; do
  FLAGS=(-O0 -g)
  case "$mode" in
    release) FLAGS=(-O3 -DNDEBUG);;
    asan) FLAGS=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer);;
    tsan) FLAGS=(-O1 -g -fsanitize=thread);;
  esac
  "$CXX" "${COMMON[@]}" "${FLAGS[@]}" HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp -o "$BUILD/engine-$mode"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$BUILD/engine-$mode" > "$BUILD/engine-$mode.log" 2>&1
  tail -1 "$BUILD/engine-$mode.log"
  "$CXX" "${COMMON[@]}" "${FLAGS[@]}" "${SOURCES[@]}" -o "$BUILD/pedal-host-$mode"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$BUILD/pedal-host-$mode" > "$BUILD/pedal-host-$mode.log" 2>&1
  tail -1 "$BUILD/pedal-host-$mode.log"
done
"$CXX" "${COMMON[@]}" -O3 -DNDEBUG -DAH_TRACK_REALTIME -Wl,-export_dynamic "${SOURCES[@]}" -o "$BUILD/pedal-host-rt"
"$CXX" "${COMMON[@]}" -O2 -dynamiclib -undefined dynamic_lookup "$PACKAGE/rt-audit.cpp" -o "$BUILD/rt-audit.dylib"
DYLD_INSERT_LIBRARIES="$BUILD/rt-audit.dylib" "$BUILD/pedal-host-rt" > "$BUILD/pedal-host-rt.log" 2>&1
tail -1 "$BUILD/pedal-host-rt.log"
"$BUILD/pedal-host-release" --benchmark > "$BUILD/cpu.csv"
"$CXX" "${COMMON[@]}" -Wall -Wextra -Wpedantic -Werror -fsyntax-only \
  HoldsworthEngine/tests/JRockettAHPedalTests.cpp "$PACKAGE/pedal-host-tests.cpp"
"$CXX" "${COMMON[@]}" --analyze HoldsworthEngine/tests/JRockettAHPedalTests.cpp -o "$BUILD/pedal-analyzer.plist"
"$CXX" "${COMMON[@]}" --analyze "$PACKAGE/pedal-host-tests.cpp" -o "$BUILD/host-analyzer.plist"
python3 "$PACKAGE/audit.py"
plutil -lint NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj
git diff --check
git -C iPlug2 diff --check
echo 'M2 native checks passed; format builds and manual audition are separate.'
