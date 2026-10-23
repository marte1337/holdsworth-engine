#!/usr/bin/env python3
"""Compile M1a native state API probes against freshly built isolated products.

No installation, Git mutation, vendored modification or M0 fixture changes.
Product builds are separate: TUNER_BUILD=<root> tuner-v1/package-products.sh.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

package = Path(__file__).resolve().parent
repo = package.parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=Path('/private/tmp/nam-midi-automation-m1a'))
parser.add_argument('--products', type=Path)
parser.add_argument('--formats', nargs='+', choices=['APP', 'VST3', 'AU'], default=['APP', 'VST3', 'AU'])
parser.add_argument('--sanitize-harness', action='store_true', help='Instrument host/legacy oracle; production objects/bundles retain their selected build instrumentation.')
parser.add_argument('--sanitize-production-app', action='store_true', help='ASan/UBSan: recompile the real APP wrapper and state codec; other accepted framework/DSP objects remain Release.')
args = parser.parse_args()
if args.sanitize_production_app:
    args.sanitize_harness = True
    if args.formats != ['APP']:
        parser.error('--sanitize-production-app requires --formats APP')
build = args.build
products = args.products or build
(build / 'logs').mkdir(parents=True, exist_ok=True)
env = dict(os.environ, DEVELOPER_DIR=os.environ.get('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer'))
cxx = subprocess.check_output(['xcrun', '--find', 'clang++'], env=env, text=True).strip()
sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], env=env, text=True).strip()
report_path = build / ('native-validation' + ('-asan' if args.sanitize_harness else '') + '.json')
report = {'scope': 'M1a actual newly built APP/VST3/AU state APIs; historical fixtures reconstructed, accepted native captures not saved DAW projects',
          'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
          'sanitize_harness_only': args.sanitize_harness and not args.sanitize_production_app,
          'production_state_instrumented': args.sanitize_production_app,
          'rt_audit_enabled': not args.sanitize_harness, 'checks': [], 'native_products': {}}

def run(name, command, cwd=repo, extra_env=None):
    start = time.monotonic()
    log = build / 'logs' / (name + '.log')
    with log.open('w') as output:
        result = subprocess.run(command, cwd=cwd, env=dict(env, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1', **(extra_env or {})), stdout=output, stderr=subprocess.STDOUT)
    data = log.read_bytes()
    lines = data.decode(errors='replace').splitlines()
    check = {'name': name, 'command': command, 'exit_code': result.returncode,
             'cwd': str(cwd), 'elapsed_seconds': round(time.monotonic() - start, 3),
             'log_sha256': hashlib.sha256(data).hexdigest(), 'summary': lines[-1] if lines else 'clean'}
    report['checks'].append(check)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(name, result.returncode, check['summary'], flush=True)
    if result.returncode:
        raise SystemExit(f'{name} failed: {log}')

audit_suffix = '-asan' if args.sanitize_harness else ''
audit = build/('rt-audit'+audit_suffix+'.dylib')
run('compile-native-rt-audit'+audit_suffix, [cxx, '-arch', 'arm64', '-isysroot', sdk,
    '-mmacosx-version-min=12.0', '-pthread', '-std=c++20', '-O2', '-dynamiclib',
    '-undefined', 'dynamic_lookup', str(repo/'HoldsworthEngine/references/tuner-v1/rt-audit.cpp'),
    '-o', str(audit)])

frameworks = []
for name in ['Cocoa', 'Carbon', 'CoreFoundation', 'CoreData', 'Foundation', 'CoreServices',
             'QuartzCore', 'AppKit', 'CoreMIDI', 'CoreAudio', 'AudioToolbox', 'AudioUnit',
             'Accelerate', 'Metal', 'MetalKit']:
    frameworks.extend(['-framework', name])
sdk_vst = repo / 'iPlug2/Dependencies/IPlug/VST3_SDK'
for format in args.formats:
    extension = {'APP': 'app', 'VST3': 'vst3', 'AU': 'component'}[format]
    objects = products / f'build-{format}/obj/NeuralAmpModeler-macOS.build/Release/{format}.build/Objects-normal/arm64'
    responses = [p for p in objects.glob('*common-args.resp') if '-std=c++20' in p.read_text()]
    if not responses:
        raise SystemExit(f'Missing newly built Release objects: {objects}')
    options = [item for item in shlex.split(responses[0].read_text()) if item not in ('-fvisibility=hidden', '-fvisibility-inlines-hidden')]
    sources = [str(repo / 'HoldsworthEngine/tests/compatibility/NativeStateExtensionTests.mm'),
               str(repo / 'HoldsworthEngine/control/LegacyStateCodec.cpp')]
    if format == 'APP':
        sources += [path for path in (objects / 'NeuralAmpModeler.LinkFileList').read_text().splitlines() if Path(path).name != 'IPlugAPP_main.o']
    else:
        # Nonvirtual metadata accessors are hidden in bundles. This unchanged
        # source supplies host accessors; inspected IParams belong to the bundle.
        sources.append(str(repo / 'iPlug2/IPlug/IPlugParameter.cpp'))
        if format == 'VST3':
            sources += [str(sdk_vst / p) for p in ['pluginterfaces/base/funknown.cpp', 'pluginterfaces/base/coreiids.cpp', 'public.sdk/source/vst/vstinitiids.cpp']]
    suffix = '-asan' if args.sanitize_harness else ''
    instrumentation = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-O1', '-g', '-DM1A_SANITIZED_STATE'] if args.sanitize_harness else ['-O3']
    if args.sanitize_production_app:
        # Test the actual production state methods with sanitizers, preserving
        # all other objects and link behavior from the fresh Release APP build.
        replaced = {'NeuralAmpModeler.o', 'ProductionStateCodec.o'}
        sources = [source for source in sources if Path(source).name not in replaced]
        for source in [repo/'NeuralAmpModeler/NeuralAmpModeler.cpp', repo/'HoldsworthEngine/control/ProductionStateCodec.cpp']:
            target = build/(source.stem+'-asan.o')
            run('compile-production-'+source.stem+'-asan', [cxx, '-arch', 'arm64', '-isysroot', sdk,
                '-mmacosx-version-min=12.0', '-pthread', *options, *instrumentation,
                '-c', str(source), '-o', str(target)], cwd=repo/'NeuralAmpModeler/projects')
            sources.append(str(target))
    binary = build / ('native-' + format.lower() + suffix)
    run('compile-native-' + format.lower() + suffix,
        [cxx, '-arch', 'arm64', '-isysroot', sdk, '-mmacosx-version-min=12.0', '-pthread',
         *options, '-UNDEBUG', *instrumentation, '-DM1A_HOST_' + format, '-Wl,-export_dynamic', '-Wl,-dead_strip',
         *sources, *frameworks, '-o', str(binary)], cwd=repo / 'NeuralAmpModeler/projects')
    captures = build / ('native-capture-' + format.lower() + suffix)
    command = [str(binary), str(package.parent / 'm0/fixtures'), str(captures)]
    bundle = products / 'products' / ('NeuralAmpModeler.' + extension)
    if format != 'APP':
        command.append(str(bundle))
    run('tests-native-' + format.lower() + suffix, command,
        extra_env={'DYLD_INSERT_LIBRARIES': '' if args.sanitize_harness else str(audit)})
    product_binary = bundle / 'Contents/MacOS/NeuralAmpModeler'
    report['native_products'][format] = {'binary': str(product_binary), 'sha256': hashlib.sha256(product_binary.read_bytes()).hexdigest()}
    report_path.write_text(json.dumps(report, indent=2) + '\n')
print('PASS M1a native state integration:', ', '.join(args.formats), flush=True)
