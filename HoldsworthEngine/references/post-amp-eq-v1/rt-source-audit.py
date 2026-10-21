#!/usr/bin/env python3
"""Bounded source audit supplementing the native RT interposition run.

This is a deliberately narrow guard, not proof about arbitrary C++ programs.
Known realtime entry points and their production call graph are checked; the
only throwing lifecycle function (prepare) is excluded explicitly.
"""
import json
import pathlib
import re

package = pathlib.Path(__file__).resolve().parent
repo = package.parents[2]
source = (repo / 'HoldsworthEngine/dsp/PostAmpEQProcessor.cpp').read_text()
clean = re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.S)
forbidden = re.compile(r'\b(?:new|delete|malloc|calloc|realloc|free|alloca|mutex|lock_guard|unique_lock|scoped_lock|condition_variable|semaphore|sleep|usleep|yield|printf|fprintf|cout|cerr|fopen|fstream|throw|try|catch)\b')
signatures = list(re.finditer(r'\b(?:void|bool|double)\s+PostAmpEQProcessor::([A-Za-z_:]+)\([^)]*\)(?:\s+noexcept)?\s*\{', clean))
functions = {}
for match in signatures:
    start, depth, end = match.end(), 1, match.end()
    while depth:
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    functions[match.group(1)] = clean[start:end - 1]
violations = []
for name, body in functions.items():
    if name == 'prepare':
        continue
    for token in forbidden.finditer(body):
        violations.append({'function': name, 'token': token.group()})
for name in ['processBlock', 'blend', 'adoptControls', 'Section::tick', 'Section::updateCoefficients', 'Ramp::next']:
    if name not in functions:
        violations.append({'function': name, 'token': 'missing-audit-target'})
    elif re.search(r'\b(?:tan|sin|cos|pow|exp|log)\s*\(', functions[name]):
        violations.append({'function': name, 'token': 'per-sample-transcendental'})
result = {'status': 'pass' if not violations else 'fail', 'violations': violations,
          'checked_functions': sorted(set(functions) - {'prepare'}),
          'lifecycle_exclusion': 'prepare validates contracts and may throw outside realtime processing',
          'bounded_target_work': 'configure is called at most once per nonempty valid block after a coherent mailbox adoption; 5 tan and up to 4 pow calls, none inside the sample loop',
          'mailbox': 'three fixed slots; one lock-free uint32 exchange on publish; consumer load plus at most one exchange; compile-time is_always_lock_free assertion; no retry loops',
          'limits': 'Text-level regression guard. Dynamic allocation/lock/wait interposition and TSan are separate evidence; no claim of complete static proof.'}
(package / 'rt-source-audit.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
raise SystemExit(bool(violations))
