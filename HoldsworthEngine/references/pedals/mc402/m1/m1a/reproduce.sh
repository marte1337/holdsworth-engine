#!/bin/sh
# Run from repository root. Requires the Python packages in ../tools/requirements.txt.
set -eu
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
MC402_M1A_BUILD=${MC402_M1A_BUILD:-/tmp/mc402-m1a}
MC402_M1A_PYTHON=${MC402_M1A_PYTHON:-python3}
MC402_M1A_SOURCE=HoldsworthEngine/references/pedals/mc402/m1/m1a
mkdir -p "$MC402_M1A_BUILD"
export MC402_M1A_LIBRARY="$MC402_M1A_BUILD/experiment.dylib"
export MC402_M1_LIBRARY="$MC402_M1A_BUILD/production.dylib"
xcrun clang++ -std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -dynamiclib \
  "$MC402_M1A_SOURCE/OfflineExperiment.cpp" -o "$MC402_M1A_LIBRARY"
xcrun clang++ -std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off \
  -dynamiclib HoldsworthEngine/dsp/MC402BoostOverdriveProcessor.cpp \
  HoldsworthEngine/references/pedals/mc402/m1/tools/MC402OfflineBridge.cpp -o "$MC402_M1_LIBRARY"
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/experiment.py" sanity
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/check_antiderivatives.py"
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/experiment.py" scan
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/run_diagnosis.py" localize
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/additional_checks.py" regions
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/additional_checks.py" reference
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/run_diagnosis.py" references
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/run_diagnosis.py" adaa
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/strategy_probes.py" probes
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/strategy_probes.py" response
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/strategy_probes.py" cpu
"$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/summarize.py"
MPLCONFIGDIR="$MC402_M1A_BUILD/matplotlib" "$MC402_M1A_PYTHON" "$MC402_M1A_SOURCE/plot_results.py"
xcrun clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -fno-fast-math -ffp-contract=off "$MC402_M1A_SOURCE/OfflineChecks.cpp" -o "$MC402_M1A_BUILD/checks-sanitize"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$MC402_M1A_BUILD/checks-sanitize"
# Successful diagnostics do not mean the -70 dBc product gate passes. Read summary.json.
printf '%s\n' 'Diagnostics complete. Product alias gate: FAIL. No production solution selected.'
