#!/usr/bin/env python3
"""Summarize deterministic objective traces using only the Python standard library.

Trace columns: input, output, power, detector dB, requested GR, smoothed GR,
applied gain, local enable mix. Little-endian float64, 48 kHz, no trace header.
"""
import array
import csv
import json
import math
from pathlib import Path
import sys

directory = Path(sys.argv[1])

def trace(name):
    data = array.array('d')
    data.frombytes((directory / (name + '.f64')).read_bytes())
    if sys.byteorder != 'little':
        data.byteswap()
    return [data[i::8] for i in range(8)]

def db(gain):
    return 20 * math.log10(max(gain, 1e-30))

def read(name):
    with (directory / (name + '.csv')).open() as file:
        return list(csv.DictReader(file))

def solve(matrix, values):
    a = [row[:] + [value] for row, value in zip(matrix, values)]
    for col in range(len(a)):
        pivot = max(range(col, len(a)), key=lambda i: abs(a[i][col]))
        a[col], a[pivot] = a[pivot], a[col]
        value = a[col][col]
        a[col] = [x/value for x in a[col]]
        for row in range(len(a)):
            if row != col:
                value = a[row][col]
                a[row] = [x-value*y for x, y in zip(a[row], a[col])]
    return [row[-1] for row in a]

def tone_fit(data, hz):
    # Joint least-squares fit removes carrier/DC leakage at noninteger pitches.
    # This is a gain-modulation measurement, not an audibility threshold.
    size = 5
    matrix = [[0.]*size for _ in range(size)]
    values = [0.]*size
    for i, value in enumerate(data):
        phase = 2*math.pi*hz*i/48000
        row = [1., math.sin(phase), math.cos(phase), math.sin(3*phase), math.cos(3*phase)]
        for j in range(size):
            values[j] += row[j]*value
            for k in range(size):
                matrix[j][k] += row[j]*row[k]
    coeff = solve(matrix, values)
    return db(math.hypot(*coeff[3:5])/math.hypot(*coeff[1:3]))

notes = read('notes')
timing = read('timing')
calibration = read('calibration')
cpu = read('cpu')
control_rate = read('control-rate')
detector_timing = read('detector-timing')
result = {'scope': 'Objective synthetic measurements; final musical acceptance deferred',
          'rates_hz': [44100,48000,88200,96000,176400,192000]}
result['timing'] = {
    'maximum_error_percent': max(float(r['error_percent']) for r in timing),
    'nominal_by_rate': [r for r in timing if r['setting_ms'] in ('15', '60')],
    'definition': 'Fractionally interpolated 63.2% dB stage crossing, RMS acquisition excluded; 4 ms power stage tested separately'}
result['control_rate_comparison'] = control_rate
result['detector_timing'] = detector_timing
result['first_weak_note'] = [r for r in notes if r['fixture'] in ('first-weak-body', 'pick-weak-150ms') and r['picked']=='0']
result['rapid_runs_48k'] = [r for r in notes if r['rate_hz']=='48000' and r['fixture'].startswith('rapid-') and r['picked']=='0']
result['weak_note_attenuation_flags'] = [r for r in notes if r['fixture'].startswith('rapid-') and r['picked']=='0' and float(r['gain_db']) < -.5]
result['weak_onset_attenuation_flags'] = [r for r in notes if r['fixture'].startswith('rapid-') and r['picked']=='0' and float(r['first5ms_gain_db']) < -.5]
result['level_stress_48k'] = [r for r in notes if r['rate_hz']=='48000' and r['fixture'].startswith('stress-')]
result['picked_transient'] = [r for r in notes if r['fixture']=='pick-weak-150ms' and r['picked']=='1']
result['calibration'] = {'cases':len(calibration),
    'maximum_detector_error_db':max(float(r['max_level_error_db']) for r in calibration),
    'maximum_applied_gain_error':max(float(r['max_gain_error']) for r in calibration),
    'maximum_power_error':max(float(r['max_power_error']) for r in calibration)}

noise = trace('noise-hum')
gain = [db(x) for x in noise[6][24000:]]
result['noise_hum'] = {'settled_gain_db':sum(gain)/len(gain), 'gain_ripple_peak_to_peak_db':max(gain)-min(gain),
    'maximum_settled_reduction_db':max(noise[5][24000:]),
    'finding':'Below threshold: manual makeup raises broadband noise/hum by 3 dB; no compressor modulation'}
decay = trace('sustained-decay')
result['sustained_decay'] = [{'time_ms':t, 'gain_db':db(decay[6][min(len(decay[6])-1,round(t*48))]),
    'reduction_db':decay[5][min(len(decay[5])-1,round(t*48))]} for t in (3,20,50,100,200,400,800,1200,1999)]
result['steady_tones'] = []
for hz in (82.4069,110.,220.,440.,1000.):
    data = trace(f'sine-{hz:.6f}')
    gain = [db(x) for x in data[6][48000:]]
    low = data[6][48000:] # Full-rate applied gain avoids analysis downsampling aliases.
    mean = sum(low)/len(low)
    window = [.5-.5*math.cos(2*math.pi*i/(len(low)-1)) for i in range(len(low))]
    spectrum = []
    for freq in range(1,81):
        re = im = 0.
        for i, (value, w) in enumerate(zip(low,window)):
            phase = 2*math.pi*freq*i/48000
            re += w*(value-mean)*math.cos(phase)
            im += w*(value-mean)*math.sin(phase)
        spectrum.append((freq,2*math.hypot(re,im)/sum(window)/mean))
    worst = max(spectrum, key=lambda pair:pair[1])
    result['steady_tones'].append({'hz':hz, 'mean_gain_db':sum(gain)/len(gain),
        'gain_ripple_peak_to_peak_db':max(gain)-min(gain),
        'output_3f_dbc':tone_fit(data[1][48000:],hz),
        'input_3f_dbc_control':tone_fit(data[0][48000:],hz),
        'largest_gain_modulation_1_to_80hz':{'hz':worst[0],'relative_amplitude_db':db(worst[1])}})

numeric = [{k:(v if k=='mode' else float(v)) for k,v in row.items()} for row in cpu]
for row in numeric:
    row['p99_budget_percent']=100*row['p99_us']/row['deadline_us']
reference_cpu = [r for r in numeric if r['mode']=='full-rate-reference']
numeric = [r for r in numeric if r['mode']!='full-rate-reference']
result['cpu'] = {'production_cases':len(numeric), 'reference_cases':len(reference_cpu), 'observations_per_case':2048,
    'worst_p99':max(numeric,key=lambda r:r['p99_budget_percent']),
    'worst_normal_block_p99':max((r for r in numeric if r['block']>=64),key=lambda r:r['p99_budget_percent']),
    'raw_max_deadline_outliers':[r for r in numeric if r['max_us']>r['deadline_us']],
    'reference_48k_128':[r for r in reference_cpu if r['rate_hz']==48000 and r['block']==128],
    'on_48k_128':[r for r in numeric if r['rate_hz']==48000 and r['block']==128 and r['mode']=='on'],
    'limitation':'Microbenchmarks include timer granularity and scheduler noise; no hard realtime guarantee'}
(directory/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({key:result[key] for key in ('noise_hum','steady_tones','calibration','cpu')},indent=2))
