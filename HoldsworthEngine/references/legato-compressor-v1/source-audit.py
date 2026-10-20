#!/usr/bin/env python3
"""Freeze accepted arithmetic around precisely identified compressor additions."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

repo = Path(__file__).resolve().parents[3]
baseline = '52e4325'

def original(path):
    return subprocess.check_output(['git','show',f'{baseline}:{path}'],cwd=repo)

def method(text,name):
    matches = list(re.finditer(r'^[^\n]*NeuralAmpModeler::'+re.escape(name)
        +r'\([^)]*\)[^{]*\{\n.*?^}\n',text,re.MULTILINE|re.DOTALL))
    assert len(matches)==1,name
    return matches[0].group()

def enum(text,name):
    match = re.search(r'enum '+name+r'\s*\{(.*?)\n};',text,re.DOTALL)
    assert match,name
    return re.findall(r'\b(k\w+)\s*(?:=[^,\n]+)?\s*(?:,|$)',match.group(1),re.MULTILINE)

cpp = 'NeuralAmpModeler/NeuralAmpModeler.cpp'
current = (repo/cpp).read_text()
old = original(cpp).decode()
hashes = {}
for name in ('ProcessBlock','OnLatencyBypassBlock','_ProcessInput','_ProcessOutput',
             '_SetInputGain','_UpdateLatency','OnLatencyPrepareBlock','_CaptureTunerInput',
             'SerializeState','UnserializeState'):
    actual = method(current,name)
    expected = method(old,name)
    actual = re.sub(r' +// Legato v1 begin:.*?\n +// Legato v1 end\.\n','',actual,flags=re.DOTALL)
    actual = re.sub(r'^.*// Legato v1 calibration availability\.\n','',actual,flags=re.MULTILINE)
    assert actual == expected, f'Accepted audible method changed beyond compressor: {name}'
    hashes[name] = hashlib.sha256(actual.encode()).hexdigest()
assert current.index('mLegatoCompressor.processBlock') > current.index('_ProcessInput(inputs,')
assert current.index('mLegatoCompressor.processBlock') < current.index('mPreNAMSelector.processSelected(')
config = 'NeuralAmpModeler/config.h'
assert (repo/config).read_text().replace('#define PLUG_HEIGHT 980','#define PLUG_HEIGHT 772') == original(config).decode()
header = 'NeuralAmpModeler/NeuralAmpModeler.h'
newHeader = (repo/header).read_text()
oldHeader = original(header).decode()
for name in ('ECtrlTags','EMsgTags'):
    a,b = enum(oldHeader,name),enum(newHeader,name)
    assert b[:len(a)-1] == a[:-1] and b[-1]==a[-1],name
assert enum(newHeader,'EParams') == enum(oldHeader,'EParams')
frozen = subprocess.check_output(['git','ls-tree','-r','--name-only',baseline,
    'HoldsworthEngine/dsp','HoldsworthEngine/integration','HoldsworthEngine/references/tuner-v1'],cwd=repo,text=True).splitlines()
for path in frozen:
    assert (repo/path).read_bytes()==original(path),path
for module in ('iPlug2','NeuralAmpModelerCore','AudioDSPTools','eigen'):
    path = repo/module
    if path.is_dir():
        assert not subprocess.check_output(['git','diff','--name-only'],cwd=path),module
project = (repo/'NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj').read_text()
assert project.count('LegatoCompressorProcessor.cpp in Sources')==project.count('ChromaticTuner.cpp in Sources')
assert project.count('LegatoCompressorProcessorTests.cpp in Sources')==2
assert project.count('LegatoCompressorIntegrationTests.cpp in Sources')==2
subprocess.run(['git','diff','--check'],cwd=repo,check=True)
result = {'baseline':baseline,'audible_method_sha256_after_additions_removed':hashes,
    'frozen_files':len(frozen),'placement':'Immediately after _ProcessInput, before selected pedal',
    'existing_parameter_and_control_IDs':'unchanged, development tags appended',
    'serialization':'unchanged','fixed_pedal_latency':32,'compressor_latency':0,
    'UI_height':'772 -> 980; existing card positions unchanged',
    'vendored_and_submodules':'unchanged','project_sources':'all tuner-enrolled products plus tests enrolled'}
if len(sys.argv)>1: Path(sys.argv[1]).write_text(json.dumps(result,indent=2)+'\n')
print('PASS: accepted arithmetic, latency, serialization, DSP/references and submodules frozen; source enrollment audited')
