#!/usr/bin/env python3
"""Verify frozen evidence and prove M0 has no production connection."""
import hashlib
import json
from pathlib import Path
import plistlib
import re
import subprocess

package = Path(__file__).resolve().parent
repo = package.parents[3]
base = 'eaa21571a442bec3bde73b7b917ea272e607376d'
production = ['NeuralAmpModeler', 'HoldsworthEngine/dsp', 'HoldsworthEngine/integration',
              'HoldsworthEngine/ui', 'iPlug2', 'AudioDSPTools', 'NeuralAmpModelerCore', 'eigen']
subprocess.run(['git', 'diff', '--exit-code', base, '--', *production], cwd=repo, check=True)
assert not subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '--', *production], cwd=repo)
for vendor in ['iPlug2', 'AudioDSPTools', 'NeuralAmpModelerCore', 'eigen']:
    assert not subprocess.check_output(['git', 'status', '--porcelain=v1', '--untracked-files=no'], cwd=repo/vendor), vendor
subprocess.run(['python3', str(package/'fixtures/generate-fixtures.py'), '--check'], check=True)
payloads = 0
for manifest_path in [package/'fixtures/manifest.json', package/'fixtures/native-current/manifest.json']:
    manifest = json.loads(manifest_path.read_text())
    for record in manifest['fixtures']:
        path = manifest_path.parent / record['file']
        data = path.read_bytes()
        assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256'], path
        if path.suffix in ('.plist', '.aupreset'):
            state = plistlib.loads(data)
            assert state['type'] == int.from_bytes(b'aufx', 'big')
            assert state['subtype'] == int.from_bytes(b'1YEo', 'big')
            assert state['manufacturer'] == int.from_bytes(b'SDAa', 'big')
        payloads += 1

config = (repo/'NeuralAmpModeler/config.h').read_text()
for name, value in [('PLUG_DOES_MIDI_IN', '0'), ('PLUG_DOES_MIDI_OUT', '0'),
                    ('PLUG_UNIQUE_ID', "'1YEo'"), ('PLUG_MFR_ID', "'SDAa'")]:
    assert re.search(r'^#define\s+' + name + r'\s+' + re.escape(value) + r'\s*$', config, re.M), name

frozen = {}
files = subprocess.check_output(['git', 'ls-files', '-z', '--', *production], cwd=repo).split(b'\0')
for raw in files:
    if not raw: continue
    path = repo/raw.decode()
    if path.is_file(): frozen[str(path.relative_to(repo))] = hashlib.sha256(path.read_bytes()).hexdigest()
assert not any(b'ParameterRegistry.h' in (repo/name).read_bytes() or b'LegacyStateCodec.h' in (repo/name).read_bytes()
               for name in frozen), 'M0 code was connected to production'
print(json.dumps({'status':'pass', 'base_commit':base, 'unchanged_production_files':len(frozen),
                  'production_source_fingerprint_sha256':hashlib.sha256(json.dumps(frozen,sort_keys=True).encode()).hexdigest(),
                  'frozen_fixture_payloads_verified':payloads, 'new_host_parameters_exposed':0,
                  'production_writer_changed':False, 'MIDI_enabled':False,
                  'AU_identity':['aufx','1YEo','SDAa'], 'vendored_worktrees_unchanged':True}, indent=2))
