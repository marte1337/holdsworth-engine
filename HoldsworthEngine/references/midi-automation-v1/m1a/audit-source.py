#!/usr/bin/env python3
"""Check M1a's production scope, preserved M0 work and unchanged DSP/identity."""
import hashlib
import json
from pathlib import Path
import subprocess

package = Path(__file__).resolve().parent
repo = package.parents[3]
base = 'eaa21571a442bec3bde73b7b917ea272e607376d'
def git(*args):
    return subprocess.check_output(['git', *args], cwd=repo, text=True)
assert git('branch', '--show-current').strip() == 'feature/midi-automation-v1'
assert git('rev-parse', 'HEAD').strip() == base
assert git('rev-parse', 'feature/legato-compressor-v1').strip() == '48b10743194d665062dfd883777cc448c1fea54f'
assert not git('diff', '--cached', '--name-only')
preserved = json.loads((package/'m0-preservation.json').read_text())
assert len(preserved) == 45
for name, sha in preserved.items():
    assert hashlib.sha256((repo/name).read_bytes()).hexdigest() == sha, name
allowed = {'NeuralAmpModeler/NeuralAmpModeler.cpp', 'NeuralAmpModeler/NeuralAmpModeler.h',
           'NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj'}
assert set(git('diff', '--name-only', base).splitlines()) == allowed
for vendor in ['iPlug2', 'NeuralAmpModelerCore', 'AudioDSPTools', 'eigen']:
    assert not subprocess.check_output(['git', 'status', '--porcelain=v1', '--untracked-files=no'], cwd=repo/vendor), vendor
name = 'NeuralAmpModeler/NeuralAmpModeler.cpp'
old = git('show', base+':'+name)
new = (repo/name).read_text()
for first, last in [('void NeuralAmpModeler::ProcessBlock', 'bool NeuralAmpModeler::SerializeState'),
                    ('void NeuralAmpModeler::OnUIOpen', None)]:
    def section(text):
        start = text.index(first)
        return text[start:text.index(last, start)] if last else text[start:]
    assert section(old) == section(new), first
header = 'NeuralAmpModeler/NeuralAmpModeler.h'
def parameter_enum(text):
    return text[text.index('enum EParams'):text.index('const int numKnobs')]
assert parameter_enum(git('show', base+':'+header)) == parameter_enum((repo/header).read_text())
assert 'PLUG_DOES_MIDI_IN 0' in (repo/'NeuralAmpModeler/config.h').read_text()
assert 'PLUG_DOES_MIDI_OUT 0' in (repo/'NeuralAmpModeler/config.h').read_text()
subprocess.run(['git', 'diff', '--check'], cwd=repo, check=True)
print(json.dumps({'status':'pass', 'base_commit':base, 'M0_files_preserved':45,
    'tracked_production_changes':sorted(allowed), 'audio_reset_idle_and_control_methods':'byte-identical',
    'DSP_defaults_presets_and_identity':'unchanged', 'legacy_host_enum':'unchanged',
    'vendored_worktrees':'unchanged', 'new_host_parameters':0, 'MIDI_enabled':False,
    'index_changes':0, 'compressor_branch':'untouched'}, indent=2))
