#!/usr/bin/env python3
"""Summarize the actual engine logs without changing any acceptance threshold."""
import hashlib
import json
import pathlib
import re

package = pathlib.Path(__file__).resolve().parent
repo = package.parents[2]
results = {}
for mode in ['debug', 'release', 'asan', 'tsan']:
    path = package / 'logs' / f'tests-{mode}.log'
    text = path.read_text() if path.exists() else ''
    match = re.search(r'All (\d+) HoldsworthEngine tests passed\.', text)
    failures = [line for line in text.splitlines() if line.startswith('FAIL:') or 'runtime error:' in line or 'WARNING: ThreadSanitizer' in line or 'ERROR: AddressSanitizer' in line]
    results[mode] = {'passed': bool(match) and not failures,
                     'test_count': int(match[1]) if match else None,
                     'failures': failures,
                     'metrics': [line for line in text.splitlines() if line.startswith('METRIC eq ')]}
source_files = ['HoldsworthEngine/dsp/PostAmpEQProcessor.cpp', 'HoldsworthEngine/dsp/PostAmpEQProcessor.h', 'HoldsworthEngine/dsp/PostAmpEQControls.h']
result = {'status': 'pass' if all(item['passed'] for item in results.values()) else 'fail',
          'matrix': results,
          'response_acceptance': {'maximum_abs_db': .01, 'minimum_response_db': -80., 'tolerance_relaxed': False},
          'method': {'oracle': 'Independent RBJ numerator/denominator complex response; no production SVF coefficient function used.',
                     'impulse': '54 configurations across six sample rates, >=1.5 s zero-state responses, radix-2 FFT, 361 DC/logarithmic/Nyquist grid candidates per configuration; compare points above -80 dB.',
                     'sine': 'One-second settling followed by 40 ms sine/cosine least-squares amplitude fit; 324 center and analytically bilinear-warped half-gain points plus 72 HP/LP points.',
                     'rates_hz': [44100, 48000, 88200, 96000, 176400, 192000],
                     'ramps': 'Per-sample friend-state assertions for exact durations, transformed primitive ramps, exact endpoints, mid-ramp retarget and unchanged-target behavior; independent scalar output checks for 10 ms trim and bypass fades.',
                     'tsan': 'All named concurrent handoff tests in the existing and new engine suites; prepare/reset are lifecycle synchronized by contract.'},
          'source_sha256': {name: hashlib.sha256((repo / name).read_bytes()).hexdigest() for name in source_files},
          'development_failures_resolved': [
              {'failure': 'First focused Release binary failed the canonical normalized/physical trim-default control test.', 'cause': 'Compiler FMA produced a tiny nonzero dB value at normalized trim default coordinate.', 'resolution': 'Core conversion explicitly recognizes every continuous parameter default coordinate; original exact-neutral test kept unchanged.'},
              {'failure': 'First validation-script compile could not find Xcode C++ standard-library headers.', 'cause': 'Xcode compiler needed explicit SDK standard-library include path in this environment.', 'resolution': 'Added -isysroot SDK and -isystem SDK/usr/include/c++/v1 consistently; no source or test acceptance change.'},
              {'failure': 'Intermediate Release link failed for postAmpEQTransitionTests().', 'cause': 'Suite registration landed before the separately owned test source during an already running matrix.', 'resolution': 'Waited for all test sources, then rebuilt and reran the final complete matrix; original linker output retained in logs/initial-registration-link-failure.log.'}],
          'limitations': ['No human audition or visual UI inspection performed; audible transition acceptance awaits manual audition.', 'Magnitude accuracy gate applies above -80 dB; endpoint rejection measured separately.']}
(package / 'engine-validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
raise SystemExit(result['status'] != 'pass')
