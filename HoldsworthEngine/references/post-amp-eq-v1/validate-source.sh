#!/usr/bin/env bash
set -euo pipefail
EQ_PACKAGE="$(cd -- "$(dirname -- "$0")" && pwd)"
EQ_REPO="$(cd -- "$EQ_PACKAGE/../../.." && pwd)"
EQ_BUILD="${EQ_BUILD:-/private/tmp/nam-post-amp-eq-v1}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
EQ_CXX="$(xcrun --find clang++)"
EQ_SDK="$(xcrun --show-sdk-path)"
EQ_COMMON=(-isysroot "$EQ_SDK" -isystem "$EQ_SDK/usr/include/c++/v1")
mkdir -p "$EQ_BUILD/analyzer" "$EQ_PACKAGE/logs"
cd "$EQ_REPO"
python3 "$EQ_PACKAGE/rt-source-audit.py" > "$EQ_PACKAGE/logs/rt-source-audit.log"
"$EQ_CXX" "${EQ_COMMON[@]}" -std=c++20 -pthread -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror \
  -fsyntax-only HoldsworthEngine/dsp/PostAmpEQProcessor.cpp HoldsworthEngine/tests/PostAmpEQTests.cpp \
  HoldsworthEngine/tests/PostAmpEQControlTests.cpp HoldsworthEngine/tests/PostAmpEQTransitionTests.cpp \
  > "$EQ_PACKAGE/logs/strict-warnings.log" 2>&1
: > "$EQ_PACKAGE/logs/static-analyzer.log"
for source in HoldsworthEngine/dsp/PostAmpEQProcessor.cpp HoldsworthEngine/tests/PostAmpEQ{,Control,Transition}Tests.cpp; do
  "$EQ_CXX" "${EQ_COMMON[@]}" -std=c++20 --analyze -Xanalyzer -analyzer-output=text \
    "$source" -o "$EQ_BUILD/analyzer/$(basename "$source" .cpp).plist" \
    >> "$EQ_PACKAGE/logs/static-analyzer.log" 2>&1
done
git diff --check > "$EQ_PACKAGE/logs/diff-check.log" 2>&1
printf '%s\n' 'EQ source audit, strict warnings, static analyzer and diff whitespace passed.'
