#!/usr/bin/env python3
"""Build isolated M0 contracts and exercise accepted native state APIs.

No production source, plugin installation, identity, MIDI flag or writer change.
--capture-native writes candidate goldens only under the temporary build root.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shlex
import subprocess
import time

package = Path(__file__).resolve().parent
repo = package.parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--native-only', action='store_true')
parser.add_argument('--capture-native', action='store_true')
args = parser.parse_args()
build = Path(os.environ.get('M0_BUILD', '/private/tmp/nam-midi-automation-m0'))
products = Path(os.environ.get('M0_PRODUCT_BUILD', '/private/tmp/nam-eq-git-integration'))
(build / 'logs').mkdir(parents=True, exist_ok=True)
env = dict(os.environ, DEVELOPER_DIR=os.environ.get('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer'))
cxx = subprocess.check_output(['xcrun', '--find', 'clang++'], env=env, text=True).strip()
sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], env=env, text=True).strip()
common = [cxx, '-isysroot', sdk, '-isystem', sdk + '/usr/include/c++/v1', '-std=c++20', '-pthread']
strict = ['-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wsign-conversion', '-Werror']
report = {'scope': 'M0 isolated compatibility foundation; not production integration',
          'base_commit': 'eaa21571a442bec3bde73b7b917ea272e607376d',
          'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
          'capture_native': args.capture_native, 'checks': []}

def run(name, command, extra_env=None, cwd=repo):
    start = time.monotonic()
    log = build / 'logs' / (name + '.log')
    with log.open('w') as output:
        result = subprocess.run(command, cwd=cwd, env=dict(env, **(extra_env or {})), stdout=output, stderr=subprocess.STDOUT)
    data = log.read_bytes()
    lines = data.decode(errors='replace').splitlines()
    check = {'name': name, 'command': command, 'exit_code': result.returncode,
             'cwd': str(cwd), 'elapsed_seconds': round(time.monotonic() - start, 3),
             'log_sha256': hashlib.sha256(data).hexdigest(),
             'summary': lines[-1] if lines else 'clean'}
    report['checks'].append(check)
    (build / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(name, result.returncode, check['summary'], flush=True)
    if result.returncode:
        raise SystemExit(f'{name} failed: {log}')

# Prove M0 remains disconnected from the accepted plugin and all audio sources.
production = ['NeuralAmpModeler', 'HoldsworthEngine/dsp', 'HoldsworthEngine/integration',
              'HoldsworthEngine/ui', 'iPlug2', 'AudioDSPTools', 'NeuralAmpModelerCore', 'eigen']
run('production-source-guard', ['git', 'diff', '--exit-code', report['base_commit'], '--', *production])
untracked_production = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '--', *production], cwd=repo, text=True)
assert not untracked_production, untracked_production
run('foundation-audit', ['python3', str(package / 'audit-foundation.py')])

if not args.native_only:
    tests = [('registry', ['HoldsworthEngine/tests/compatibility/ParameterRegistryTests.cpp'], []),
             ('state', ['HoldsworthEngine/control/LegacyStateCodec.cpp', 'HoldsworthEngine/tests/compatibility/LegacyStateCodecTests.cpp'], [str(package / 'fixtures')]),
             ('ownership', ['HoldsworthEngine/tests/compatibility/ControlOwnershipContractTests.cpp'], []),
             ('legacy-registry', ['HoldsworthEngine/control/LegacyStateCodec.cpp', 'HoldsworthEngine/tests/compatibility/LegacyRegistryCompatibilityTests.cpp'], [str(package / 'fixtures')])]
    for mode, flags in [('debug', ['-O0', '-g']), ('release', ['-O3', '-DNDEBUG']),
                        ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
        for name, sources, arguments in tests:
            binary = str(build / (name + '-' + mode))
            run('compile-' + name + '-' + mode, common + strict + flags + sources + ['-o', binary])
            run('tests-' + name + '-' + mode, [binary, *arguments],
                {'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    run('compile-ownership-tsan', common + strict + ['-O1', '-g', '-fsanitize=thread', tests[2][1][0], '-o', str(build / 'ownership-tsan')])
    run('tests-ownership-tsan', [str(build / 'ownership-tsan')])
    for source in ['HoldsworthEngine/control/LegacyStateCodec.cpp'] + [item for _, sources, _ in tests for item in sources if 'tests/' in item]:
        run('analyzer-' + Path(source).stem, common + ['--analyze', '-Xanalyzer', '-analyzer-output=text', source, '-o', '/dev/null'])

# The native harness reuses the accepted format hosts. APP links the exact
# Release objects; VST3/AU load the actual isolated bundles. These objects must
# be built from the source fingerprint above. Rebuild with the existing EQ
# package-products.sh and M0_PRODUCT_BUILD when the temporary products are absent.
frameworks = []
for name in ['Cocoa', 'Carbon', 'CoreFoundation', 'CoreData', 'Foundation', 'CoreServices',
             'QuartzCore', 'AppKit', 'CoreMIDI', 'CoreAudio', 'AudioToolbox', 'AudioUnit',
             'Accelerate', 'Metal', 'MetalKit']:
    frameworks.extend(['-framework', name])
sdk_vst = repo / 'iPlug2/Dependencies/IPlug/VST3_SDK'
for format, extension in [('APP', 'app'), ('VST3', 'vst3'), ('AU', 'component')]:
    objects = products / f'build-{format}/obj/NeuralAmpModeler-macOS.build/Release/{format}.build/Objects-normal/arm64'
    responses = [p for p in objects.glob('*common-args.resp') if '-std=c++20' in p.read_text()]
    if not responses:
        raise SystemExit(f'Missing accepted Release objects: {objects}; see README reproduction instructions.')
    options = [item for item in shlex.split(responses[0].read_text()) if item not in ('-fvisibility=hidden', '-fvisibility-inlines-hidden')]
    sources = [str(repo / 'HoldsworthEngine/tests/compatibility/NativeLegacyContractTests.mm'),
               str(repo / 'HoldsworthEngine/control/LegacyStateCodec.cpp')]
    if format == 'APP':
        sources += [path for path in (objects / 'NeuralAmpModeler.LinkFileList').read_text().splitlines() if Path(path).name != 'IPlugAPP_main.o']
    else:
        # Metadata accessors are nonvirtual and hidden inside actual bundles.
        # Link the identical, unmodified framework accessor implementation into
        # the test host; all inspected IParam objects still belong to the bundle.
        sources.append(str(repo / 'iPlug2/IPlug/IPlugParameter.cpp'))
        if format == 'VST3':
            sources += [str(sdk_vst / p) for p in ['pluginterfaces/base/funknown.cpp', 'pluginterfaces/base/coreiids.cpp', 'public.sdk/source/vst/vstinitiids.cpp']]
    binary = build / ('native-' + format.lower())
    run('compile-native-' + format.lower(), [cxx, '-arch', 'arm64', '-isysroot', sdk, '-mmacosx-version-min=12.0', '-pthread',
        *options, '-UNDEBUG', '-O3', '-DM0_HOST_' + format, '-Wl,-export_dynamic', '-Wl,-dead_strip',
        *sources, *frameworks, '-o', str(binary)], cwd=repo / 'NeuralAmpModeler/projects')
    captures = build / ('capture-' + format.lower())
    command = [str(binary), str(package / 'fixtures'), str(captures)]
    bundle = products / 'products' / ('NeuralAmpModeler.' + extension)
    if format != 'APP': command.append(str(bundle))
    run('tests-native-' + format.lower(), command, {'M0_CAPTURE': '1'} if args.capture_native else {})
    for path in captures.glob('*.plist'):
        plist = plistlib.loads(path.read_bytes())
        path.write_bytes(plistlib.dumps(plist, fmt=plistlib.FMT_XML, sort_keys=True))
        if not args.capture_native:
            assert path.read_bytes() == (package / 'fixtures/native-current' / path.name).read_bytes()
    report.setdefault('native_products', {})[format] = {
        'binary': str(bundle / 'Contents/MacOS/NeuralAmpModeler'),
        'sha256': hashlib.sha256((bundle / 'Contents/MacOS/NeuralAmpModeler').read_bytes()).hexdigest()}

run('diff-check', ['git', 'diff', '--check'])
(build / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
