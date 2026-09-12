#!/usr/bin/env bash
set -euo pipefail

MC402_PACKAGE_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
export MC402_M1C_LIBRARY_DIR="${MC402_M1C_LIBRARY_DIR:-/tmp/mc402-m1c}"
MC402_PYTHON="${MC402_PYTHON:-/tmp/mc402-m1-venv/bin/python}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
export MPLCONFIGDIR="${MPLCONFIGDIR:-/tmp/mc402-m1c/matplotlib}"
mkdir -p "$MC402_M1C_LIBRARY_DIR"
flags=(-std=c++20 -fno-fast-math -ffp-contract=off -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

xcrun clang++ "${flags[@]}" -O3 -DNDEBUG -dynamiclib "$MC402_PACKAGE_DIR/R2Experiment.cpp" -o "$MC402_M1C_LIBRARY_DIR/r2.dylib"
xcrun clang++ "${flags[@]}" -O3 -DNDEBUG -dynamiclib "$MC402_PACKAGE_DIR/LegacyPeriodic.cpp" -o "$MC402_M1C_LIBRARY_DIR/v1.dylib"
xcrun clang++ "${flags[@]}" -O3 -DNDEBUG -dynamiclib "$MC402_PACKAGE_DIR/Benchmark.cpp" -o "$MC402_M1C_LIBRARY_DIR/benchmark.dylib"
xcrun clang++ "${flags[@]}" -O3 "$MC402_PACKAGE_DIR/OfflineChecks.cpp" -o "$MC402_M1C_LIBRARY_DIR/checks-release"
"$MC402_M1C_LIBRARY_DIR/checks-release"
xcrun clang++ "${flags[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$MC402_PACKAGE_DIR/OfflineChecks.cpp" -o "$MC402_M1C_LIBRARY_DIR/checks-sanitized"
"$MC402_M1C_LIBRARY_DIR/checks-sanitized"
xcrun clang++ --analyze "${flags[@]}" "$MC402_PACKAGE_DIR/R2Experiment.cpp" -o "$MC402_M1C_LIBRARY_DIR/analyzer.plist"
xcrun clang++ "${flags[@]}" -O3 "$MC402_PACKAGE_DIR/Robustness.cpp" -o "$MC402_M1C_LIBRARY_DIR/robustness"

"$MC402_PYTHON" "$MC402_PACKAGE_DIR/checks.py" numerics
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/checks.py" periodic
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/evaluate.py" product
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/evaluate.py" torture
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/evaluate.py" summarize
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/analysis.py" static
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/render_spectra.py"
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/analysis.py" witnesses
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/analysis.py" references
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/followup_checks.py"
"$MC402_M1C_LIBRARY_DIR/robustness" > "$MC402_PACKAGE_DIR/robustness.json"
# Keep CPU measurement serial after the numerical/stress work.
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/performance.py"
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/report.py"
"$MC402_PYTHON" "$MC402_PACKAGE_DIR/audit.py"
