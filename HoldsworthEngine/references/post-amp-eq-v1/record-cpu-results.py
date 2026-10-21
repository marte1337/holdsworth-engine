#!/usr/bin/env python3
"""Summarize an existing benchmark; never rerun it to repair metadata."""
import csv
import json
import pathlib
import platform
import subprocess

package = pathlib.Path(__file__).resolve().parent
rows = list(csv.DictReader((package / 'logs/eq-cpu.csv').open()))
cpu_query = subprocess.run(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True, capture_output=True)
critical = [row for row in rows if row['rate'] == '192000' and row['block'] == '32']
result = {'machine': platform.platform(), 'architecture': platform.machine(),
          'cpu_model': cpu_query.stdout.strip() if cpu_query.returncode == 0 else 'unavailable in filesystem sandbox',
          'cpu_model_metadata_error': cpu_query.stderr.strip() if cpu_query.returncode else None,
          'clang': subprocess.check_output(['xcrun', 'clang++', '--version'], text=True).splitlines()[0],
          'method': 'O3 double mono DSP; clock interval includes production audio controls adoption, coefficient derivation and processBlock; producer setControls and synthetic controls generation outside interval; native macOS dyld allocation/lock/wait interposer active; no UI or NAM.',
          'rows': len(rows), 'critical_192k_32frame': critical,
          'largest_p99_percent': max(rows, key=lambda row: float(row['p99_percent'])),
          'largest_observed_percent': max(rows, key=lambda row: float(row['max_percent'])),
          'wall_clock_deadline_exceedance_rows': [row for row in rows if float(row['max_percent']) > 100.],
          'rt_audit': (package / 'logs/rt-audit.log').read_text().strip(),
          'interposer_self_test': (package / 'logs/rt-self-test.log').read_text().strip(),
          'limits': 'Wall-clock maxima include scheduler preemption; no portable CPU guarantee or inferred audibility claim.',
          'development_failures_resolved': ['The first benchmark completed all 210 timing cases and its RT audit, then metadata generation failed because sysctl CPU-model access was denied by the sandbox. Metadata handling now records that denial; this summary uses the original CSV and does not rerun or replace its measurements.']}
(package / 'cpu-profile.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
