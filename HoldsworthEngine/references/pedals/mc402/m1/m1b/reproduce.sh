#!/bin/sh
# From repository root; uses the pinned M1 Python requirements.
set -eu
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
MC402_M1B_BUILD=${MC402_M1B_BUILD:-/tmp/mc402-m1b}
MC402_M1B_PYTHON=${MC402_M1B_PYTHON:-python3}
MC402_M1B_SOURCE=HoldsworthEngine/references/pedals/mc402/m1/m1b
mkdir -p "$MC402_M1B_BUILD"
export MC402_M1A_LIBRARY="$MC402_M1B_BUILD/experiment.dylib"
export MC402_M1B_BENCHMARK="$MC402_M1B_BUILD/benchmark.dylib"
export OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 PYTHONDONTWRITEBYTECODE=1
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/audit.py" frozen
xcrun clang++ -std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -dynamiclib \
  "$MC402_M1B_SOURCE/../m1a/OfflineExperiment.cpp" -o "$MC402_M1A_LIBRARY"
xcrun clang++ -std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -dynamiclib \
  "$MC402_M1B_SOURCE/Benchmark.cpp" -o "$MC402_M1B_BENCHMARK"
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/check_harness.py"
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/evaluate.py" product
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/evaluate.py" dense
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/evaluate.py" torture
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/evaluate.py" tails
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/evaluate.py" reference
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/witness_checks.py"
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/fir_diagnosis.py"
xcrun clang++ -std=c++20 -O3 -fno-fast-math -ffp-contract=off \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  "$MC402_M1B_SOURCE/Robustness.cpp" -o "$MC402_M1B_BUILD/robustness"
"$MC402_M1B_BUILD/robustness" > "$MC402_M1B_SOURCE/robustness.json"
# Keep the CPU estimate isolated from other heavy diagnostic processes.
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/evaluate.py" performance
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/summarize.py"
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/report_tables.py"
MPLCONFIGDIR="$MC402_M1B_BUILD/matplotlib" \
  "$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/plot_results.py"
xcrun clang++ -std=c++20 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -fno-fast-math -ffp-contract=off \
  "$MC402_M1B_SOURCE/../m1a/OfflineChecks.cpp" -o "$MC402_M1B_BUILD/checks-sanitize"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$MC402_M1B_BUILD/checks-sanitize"
"$MC402_M1B_PYTHON" "$MC402_M1B_SOURCE/audit.py" results
printf '%s\n' 'M1b diagnostics complete. Read summary.json for PRODUCT nonqualification; no production implementation authorized.'
