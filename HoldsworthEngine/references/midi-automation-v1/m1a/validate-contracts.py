#!/usr/bin/env python3
"""M1a isolated contracts, frozen M0 tests and complete existing DSP regressions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

package = Path(__file__).resolve().parent
repo = package.parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=Path('/private/tmp/nam-midi-automation-m1a'))
parser.add_argument('--engine', action='store_true', help='Run fresh existing DSP suite instead of isolated contracts.')
args = parser.parse_args()
build = args.build
(build/'logs').mkdir(parents=True, exist_ok=True)
env = dict(os.environ, DEVELOPER_DIR=os.environ.get('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer'))
cxx = subprocess.check_output(['xcrun', '--find', 'clang++'], env=env, text=True).strip()
sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], env=env, text=True).strip()
common = [cxx, '-isysroot', sdk, '-isystem', sdk+'/usr/include/c++/v1', '-std=c++20', '-pthread']
strict = ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wsign-conversion', '-Werror']
report_path = build/('engine-validation.json' if args.engine else 'contract-validation.json')
report = {'scope': 'M1a fresh existing DSP regression' if args.engine else 'M1a production codec and unchanged M0 contracts', 'checks': []}

def run(name, command):
    start = time.monotonic()
    path = build/'logs'/(name+'.log')
    with path.open('w') as log:
        result = subprocess.run(command, cwd=repo, env=dict(env, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'), stdout=log, stderr=subprocess.STDOUT)
    data = path.read_bytes()
    lines = data.decode(errors='replace').splitlines()
    record = {'name': name, 'command': command, 'exit_code': result.returncode, 'elapsed_seconds': round(time.monotonic()-start, 3),
              'log_sha256': hashlib.sha256(data).hexdigest(), 'summary': lines[-1] if lines else 'clean'}
    report['checks'].append(record)
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    print(name, result.returncode, record['summary'], flush=True)
    if result.returncode:
        raise SystemExit(f'{name} failed: {path}')

fixtures = str(package.parent/'m0/fixtures')
tests = [
    ('codec', ['HoldsworthEngine/control/ProductionStateCodec.cpp', 'HoldsworthEngine/control/LegacyStateCodec.cpp', 'HoldsworthEngine/tests/compatibility/ProductionStateCodecTests.cpp'], [fixtures]),
    ('m0-registry', ['HoldsworthEngine/tests/compatibility/ParameterRegistryTests.cpp'], []),
    ('m0-state', ['HoldsworthEngine/control/LegacyStateCodec.cpp', 'HoldsworthEngine/tests/compatibility/LegacyStateCodecTests.cpp'], [fixtures]),
    ('m0-migration', ['HoldsworthEngine/control/LegacyStateCodec.cpp', 'HoldsworthEngine/tests/compatibility/LegacyRegistryCompatibilityTests.cpp'], [fixtures])]
if args.engine:
    sources = sorted(str(p.relative_to(repo)) for directory in ['dsp', 'integration', 'tests'] for p in (repo/'HoldsworthEngine'/directory).glob('*.cpp'))
    tests = [('engine', sources, [])]
for mode, flags in [('debug', ['-O0', '-g']), ('release', ['-O3', '-DNDEBUG']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
    for name, sources, arguments in tests:
        binary = str(build/(name+'-'+mode))
        run('compile-'+name+'-'+mode, common + ([] if args.engine else strict) + flags + sources + ['-o', binary])
        run('tests-'+name+'-'+mode, [binary, *arguments])
if not args.engine:
    for source in ['HoldsworthEngine/control/ProductionStateCodec.cpp', 'HoldsworthEngine/tests/compatibility/ProductionStateCodecTests.cpp']:
        run('analyzer-'+Path(source).stem, common+['--analyze', '-Xanalyzer', '-analyzer-output=text', source, '-o', '/dev/null'])
    run('frozen-fixtures', ['python3', str(package.parent/'m0/fixtures/generate-fixtures.py'), '--check'])
    run('project-lint', ['plutil', '-lint', 'NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj'])
    run('diff-check', ['git', 'diff', '--check'])
