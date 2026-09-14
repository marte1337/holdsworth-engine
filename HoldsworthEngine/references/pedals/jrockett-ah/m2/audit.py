#!/usr/bin/env python3
"""Read-only preservation and actual integration-wiring audit; no UI automation."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

package = Path(__file__).resolve().parent
repo = package.parents[4]
expected = json.loads((package / 'preserved-sha256.json').read_text())
for name, digest in expected.items():
    assert hashlib.sha256((repo / name).read_bytes()).hexdigest() == digest, name
cpp = (repo / 'NeuralAmpModeler/NeuralAmpModeler.cpp').read_text()
hdr = (repo / 'NeuralAmpModeler/NeuralAmpModeler.h').read_text()
ui = (repo / 'HoldsworthEngine/ui/DevelopmentPanel.h').read_text()
for suffix in ('Boost', 'Type', 'Emphasis'):
    assert f'case kMsgTagAH{suffix}:' in cpp
    assert f'kCtrlTagAH{suffix}' in cpp
    assert f'developmentMessage(kMsgTagAH{suffix})' in ui
    assert f'kCtrlTagAH{suffix}' in ui
    assert f'SendControlValueFromDelegate(kCtrlTagAH{suffix}' in cpp
assert 'ctrlTag, expectedTag, dataSize, pData, value' in cpp
assert 'mAHBoostProcessor.setControls(controls)' in cpp
assert 'mAHRealtimeSupported = holdsworth::integration::ahRealtimeRateSupported(sampleRate)' in cpp
assert 'mAHBoostProcessor, mAHRealtimeSupported' in cpp
assert ' / 3.0);' in cpp
assert '{"OFF", "TC BLD", "MC402", "J. ROCKETT AH"}' in ui
assert '"Behavioral Boost"' in ui
assert ': PositionSlider(bounds, action, "Boost", 0.0)' in ui
assert 'void OnMouseDblClick(float, float, const IMouseMod&) override { ResetToDefault(); }' in ui
# Preserve old named control/message positions by requiring append-only enums.
old = subprocess.check_output(['git', 'show', 'HEAD:NeuralAmpModeler/NeuralAmpModeler.h'], cwd=repo, text=True)
for enum in ('ECtrlTags', 'EMsgTags'):
    pattern = rf'enum {enum}\s*\{{.*?\n\}};'
    before = re.search(pattern, old, re.S).group()
    after = re.search(pattern, hdr, re.S).group()
    after = re.sub(r'  k(?:Ctrl|Msg)TagAH(?:Boost|Type|Emphasis),\n', '', after)
    assert before == after, enum
old_cpp = subprocess.check_output(['git', 'show', 'HEAD:NeuralAmpModeler/NeuralAmpModeler.cpp'], cwd=repo, text=True)
# All functions after the old MC402 message case, including calibration, unchanged.
anchor = '    case kMsgTagMC402Boost:'
assert cpp[cpp.index(anchor):] == old_cpp[old_cpp.index(anchor):]
anchor = '  _ApplyDSPStaging();'
end = 'void NeuralAmpModeler::OnReset()'
assert cpp[cpp.index(anchor):cpp.index(end)] == old_cpp[old_cpp.index(anchor):old_cpp.index(end)]
project = repo / 'NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj'
objects = json.loads(subprocess.check_output(['plutil', '-convert', 'json', '-o', '-', str(project)]))['objects']
def phases_for(filename):
    refs = {key for key, value in objects.items() if value.get('path') == filename}
    builds = {key for key, value in objects.items() if value.get('fileRef') in refs}
    return {key for key, value in objects.items() if set(value.get('files', [])) & builds}
assert phases_for('JRockettAHBoostProcessor.cpp') == phases_for('MC402CleanBoostProcessor.cpp')
print('PASS: preserved DSP/profile, old message IDs, control wiring, calibrated placement, downstream source and target enrollment')
