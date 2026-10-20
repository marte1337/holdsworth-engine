#!/usr/bin/env python3
"""Objective native hosts only; no installation, DAW, or musical audition."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import wave

package = Path(__file__).resolve().parent
repo = package.parents[2]
build = Path(os.environ.get('LEGATO_BUILD', '/private/tmp/nam-legato-v1'))
fixtures = build/'fixtures'
fixtures.mkdir(exist_ok=True)
for name, rate, gain, metadata in (
    ('linear-half-48k',48000,.5,{}),
    ('calibrated-48k',48000,1.,{'input_level_dbu':6}),
    ('calibrated-44100',44100,1.,{'input_level_dbu':6})):
    (fixtures/(name+'.nam')).write_text(json.dumps({'version':'0.5.4','architecture':'Linear',
        'config':{'receptive_field':1,'bias':False},'weights':[gain],
        'sample_rate':rate,'metadata':metadata})+'\n')
with wave.open(str(fixtures/'colored-48k.wav'),'wb') as wav:
    wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(48000)
    wav.writeframes(struct.pack('<64h',16384,8192,-4096,*([0]*61)))
env = dict(os.environ, DYLD_INSERT_LIBRARIES=str(build/'host-tests/rt-audit.dylib'),
    TUNER_MODEL_FIXTURE=str(fixtures/'linear-half-48k.nam'),
    TUNER_IR_FIXTURE=str(fixtures/'colored-48k.wav'),
    LEGATO_CALIBRATED_MODEL=str(fixtures/'calibrated-48k.nam'),
    LEGATO_RESAMPLED_MODEL=str(fixtures/'calibrated-44100.nam'))
results = {'scope':'Headless actual Release APP objects / VST3 and AU bundles; no musical acceptance',
           'formats':{}}
for fmt, ext in (('APP','app'),('VST3','vst3'),('AU','component')):
    runner = build/'host-tests'/f'{fmt.lower()}-host'
    bundle = build/'products'/f'NeuralAmpModeler.{ext}'
    args = [str(runner)] + ([] if fmt=='APP' else [str(bundle)])
    log = build/'host-tests'/f'{fmt.lower()}-acceptance.log'
    with log.open('w') as file:
        result = subprocess.run(args,cwd=repo,env=env,stdout=file,stderr=subprocess.STDOUT,timeout=180)
    if result.returncode: raise SystemExit(f'FAIL {fmt} host; see {log}')
    text = log.read_text()
    assert text.count(f'PASS {fmt} Legato ') == 7, text
    assert f'PASS {fmt} real NAM latency32->61->32' in text
    assert f'PASS {fmt} real NAM/IR fixtures' in text
    cpu = build/'host-tests'/f'{fmt.lower()}-cpu.csv'
    with cpu.open('w') as file:
        subprocess.run(args+['--benchmark'],cwd=repo,env=env,stdout=file,stderr=subprocess.STDOUT,timeout=180,check=True)
    results['formats'][fmt] = {'bundle':str(bundle),
        'sha256':hashlib.sha256((bundle/'Contents/MacOS/NeuralAmpModeler').read_bytes()).hexdigest(),
        'acceptance_log':str(log),'cpu_csv':str(cpu),'status':'passed',
        'audited_calls_after_legacy_warmup':0,
        'sample_format':'float32' if fmt=='AU' else 'float64',
        'latency_samples':32,'NAM_resampled_latency_samples':61}
    print(f'PASS {fmt} native acceptance and CPU',flush=True)
(build/'host-validation.json').write_text(json.dumps(results,indent=2)+'\n')
