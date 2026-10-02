#!/usr/bin/env python3
"""Preserve frozen DSP and inspect actual product wiring; no GUI automation."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

package = Path(__file__).resolve().parent
repo = package.parents[5]
baseline = json.loads((package.parent / 'm1/validation.json').read_text())
for group in ('source_sha256', 'source_preservation'):
    for name, digest in baseline[group].items():
        if not name.startswith('HoldsworthEngine/dsp/'):
            continue
        assert hashlib.sha256((repo / name).read_bytes()).hexdigest() == digest, name

def source(path):
    return (repo / path).read_text()

def original(path):
    return subprocess.check_output(['git', 'show', f'HEAD:{path}'], cwd=repo, text=True)

cpp = source('NeuralAmpModeler/NeuralAmpModeler.cpp')
old = original('NeuralAmpModeler/NeuralAmpModeler.cpp')
hdr = source('NeuralAmpModeler/NeuralAmpModeler.h')
ui = source('HoldsworthEngine/ui/DevelopmentPanel.h')
for suffix in ('Boost', 'Type', 'Emphasis', 'BoostEnabled', 'DriveEnabled', 'DriveGain', 'DriveBass', 'DriveTreble', 'DriveVolume'):
    assert f'case kMsgTagAH{suffix}:' in cpp
    assert f'developmentMessage(kMsgTagAH{suffix})' in ui
    assert f'kCtrlTagAH{suffix}' in ui
    assert f'SendControlValueFromDelegate(kCtrlTagAH{suffix}' in cpp
for enum in ('ECtrlTags', 'EMsgTags'):
    pattern = rf'enum {enum}\s*\{{.*?\n\}};'
    before = re.search(pattern, original('NeuralAmpModeler/NeuralAmpModeler.h'), re.S).group()
    after = re.search(pattern, hdr, re.S).group()
    assert before == after, enum
assert '{"OFF", "TC BLD", "MC402", "J. ROCKETT AH"}' in ui
assert 'Behavioral OD/Boost' in ui
assert 'defaultVolume : .5' in ui
assert 'OnMouseDblClick' in ui and 'ResetToDefault();' in ui
assert 'RequestLatency(mModelLatencyContribution + PLUG_LATENCY)' in cpp
assert 'GetLatencyConfigurationTag' not in cpp
assert cpp.count('DevelopmentPreNAMProcessor>(mPreNAMRequestedProcessor.load') == 1
assert 'mPreNAMRequestedProcessor.load(std::memory_order_relaxed))' in cpp
assert '#define PLUG_LATENCY 32' in source('NeuralAmpModeler/config.h')
vst = source('iPlug2/IPlug/VST3/IPlugVST3.cpp')
assert 'void IPlugVST3::NotifyLatencyChange()' in vst and 'restartComponent(kLatencyChanged)' in vst
assert 'PublishLatencyWhileInactive' not in vst and 'OnLatencyRequest' not in vst
assert 'mAHPedal.setDriveControls(controls)' in cpp
assert cpp.index('mPreNAMSelector.processSelected(') < cpp.index('// Noise gate trigger') < cpp.index('mModel->process(')
# Actual calibration, input sum, NAM and all downstream render calculations.
for start, end in [
    ('  _ApplyDSPStaging();', 'void NeuralAmpModeler::OnReset()'),
    ('void NeuralAmpModeler::_SetInputGain()', 'void NeuralAmpModeler::_SetOutputGain()'),
    ('void NeuralAmpModeler::_ProcessInput(', 'void NeuralAmpModeler::_UpdateControlsFromModel()')]:
    assert cpp[cpp.index(start):cpp.index(end)] == old[old.index(start):old.index(end)], start
assert 'SetLatency(' not in cpp
assert 'ServiceLatencyUpdates();' in cpp[cpp.index('void NeuralAmpModeler::OnIdle()'):cpp.index('void NeuralAmpModeler::OnUIOpen()')]
project = repo / 'NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj'
objects = json.loads(subprocess.check_output(['plutil', '-convert', 'json', '-o', '-', str(project)]))['objects']
def phases_for(filename):
    refs = {key for key, value in objects.items() if value.get('path') == filename}
    builds = {key for key, value in objects.items() if value.get('fileRef') in refs}
    return {key for key, value in objects.items() if set(value.get('files', [])) & builds}
assert phases_for('JRockettAHDriveProcessor.cpp') == phases_for('JRockettAHBoostProcessor.cpp') == phases_for('MC402CleanBoostProcessor.cpp')
assert len(phases_for('JRockettAHPedalTests.cpp')) == 1
print('PASS: frozen Boost/Drive/TC/MC/Yamaha bytes, append-only IDs, UI wiring/defaults, calibrated pre-gate/NAM placement, downstream source, product enrollment')
