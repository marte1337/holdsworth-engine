#!/usr/bin/env bash
set -euo pipefail
PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
REPO="$(cd -- "$PACKAGE/../../../../../.." && pwd)"
BUILD="${AH_BUILD:-/private/tmp/ah-full-m2}"
# Installed Command Line Tools suffice for native tests; format builds use Xcode.
CXX="${AH_NATIVE_CXX:-/Library/Developer/CommandLineTools/usr/bin/clang++}"
SDK="${AH_NATIVE_SDK:-/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk}"
COMMON=(-std=c++20 -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1"
        -I iPlug2/IPlug -I iPlug2/IPlug/Extras -I iPlug2/WDL)
mkdir -p "$BUILD"
cd "$REPO"
for mode in debug release asan tsan; do
  FLAGS=(-O0 -g)
  case "$mode" in
    release) FLAGS=(-O3 -DNDEBUG -DAH_TRACK_REALTIME -Wl,-export_dynamic);;
    asan) FLAGS=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer);;
    tsan) FLAGS=(-O1 -g -fsanitize=thread);;
  esac
  "$CXX" "${COMMON[@]}" "${FLAGS[@]}" "$PACKAGE/latency-tests.cpp" \
    iPlug2/IPlug/IPlugProcessor.cpp -o "$BUILD/latency-$mode"
  if [ "$mode" = release ]; then
    "$CXX" "${COMMON[@]}" -O2 -dynamiclib -undefined dynamic_lookup \
      "$PACKAGE/rt-audit.cpp" -o "$BUILD/rt-audit.dylib"
  fi
  python3 - "$BUILD" "$mode" <<'PY'
import os, subprocess, sys
from pathlib import Path
build, mode = Path(sys.argv[1]), sys.argv[2]
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
if mode == "release":
    env["DYLD_INSERT_LIBRARIES"] = str(build / "rt-audit.dylib")
with (build / f"latency-{mode}.log").open("w") as log:
    try:
        result = subprocess.run([str(build / f"latency-{mode}")], env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=60)
    except subprocess.TimeoutExpired:
        log.write("FAIL: native test timed out (including sanitizer startup).\n")
        sys.exit(1)
sys.exit(result.returncode)
PY
  tail -1 "$BUILD/latency-$mode.log"
done
"$CXX" "${COMMON[@]}" -O2 HoldsworthEngine/dsp/*.cpp HoldsworthEngine/tests/*.cpp \
  -pthread -o "$BUILD/engine-latency-regression"
"$BUILD/engine-latency-regression" > "$BUILD/engine-latency-regression.log"
tail -1 "$BUILD/engine-latency-regression.log"
git diff --check
git -C iPlug2 diff --check
echo "Native latency checks passed; APP/VST3/AU builds are a separate required gate."
