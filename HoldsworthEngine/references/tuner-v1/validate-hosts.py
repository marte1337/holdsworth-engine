#!/usr/bin/env python3
"""Run actual-format acceptance and retain the complete CPU/RT evidence."""
import csv
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import wave

package = Path(__file__).resolve().parent
repo = package.parents[2]
build = Path(os.environ.get("TUNER_BUILD", "/private/tmp/nam-tuner-v1"))
host_tests = build / "host-tests"
parser = argparse.ArgumentParser()
mode = parser.add_mutually_exclusive_group()
mode.add_argument("--acceptance-only", action="store_true")
mode.add_argument("--benchmark-only", action="store_true")
parser.add_argument("--reuse-acceptance", action="store_true",
    help="Record already-passed logs from this source-frozen product build without repeating host rendering")
options = parser.parse_args()
if options.reuse_acceptance and not options.acceptance_only:
    parser.error("--reuse-acceptance requires --acceptance-only")
env = dict(os.environ, DYLD_INSERT_LIBRARIES=str(host_tests / "rt-audit.dylib"))
fixtures = build / "fixtures"
fixtures.mkdir(parents=True, exist_ok=True)
model = fixtures / "linear-half-48k.nam"
model.write_text(json.dumps({"version": "0.5.4", "architecture": "Linear",
    "config": {"receptive_field": 1, "bias": False}, "weights": [.5], "sample_rate": 48000}, indent=2) + "\n")
ir = fixtures / "colored-48k.wav"
with wave.open(str(ir), "wb") as wav:
    wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(48000)
    wav.writeframes(struct.pack("<64h", 16384, 8192, -4096, *([0] * 61)))
env.update(TUNER_MODEL_FIXTURE=str(model), TUNER_IR_FIXTURE=str(ir))

def run(command, path, timeout=180):
    with path.open("w") as log:
        subprocess.run(command, cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=timeout, check=True)

results = {"kind": "Actual Release AU/VST3 bundle rendering and production APP-object rendering",
           "scope": "Headless native acceptance hosts; no DAW/manual guitar audition claim",
           "rates_hz": [44100, 48000, 88200, 96000, 176400, 192000],
           "callback_samples": [1, 2, 4, 8, 32, 64, 128, 256, 512, 1024, 37],
           "cold_legacy_first_use": "Disabled whole-plugin rendering warms existing scratch/filter allocation before audit; tuner first enable remains audited",
           "formats": {}, "cpu": {}}
if options.benchmark_only:
    previous = json.loads((build / "host-validation.json").read_text())
for format, extension in (("APP", "app"), ("VST3", "vst3"), ("AU", "component")):
    runner = host_tests / f"{format.lower()}-host"
    bundle = build / "products" / f"NeuralAmpModeler.{extension}"
    executable = bundle / "Contents/MacOS/NeuralAmpModeler"
    assert runner.is_file() and executable.is_file()
    run_args = [str(runner)]
    if format != "APP": run_args.append(str(bundle))
    acceptance = host_tests / f"{format.lower()}-acceptance.log"
    if not options.benchmark_only and not options.reuse_acceptance:
        run(run_args, acceptance)
    elif options.benchmark_only:
        assert previous["formats"][format]["binary_sha256"] == hashlib.sha256(executable.read_bytes()).hexdigest(), \
            f"{format} changed after acceptance; rerun acceptance before benchmarking"
    cpu = host_tests / f"{format.lower()}-cpu.csv"
    if not options.acceptance_only:
        run(run_args + ["--benchmark"], cpu)
    text = acceptance.read_text()
    assert len(re.findall(rf"^PASS {format} \d+ Hz: 11 blocks", text, re.MULTILINE)) == 6, text
    assert f"PASS {format} selected-pedal on/off arithmetic" in text
    assert f"PASS {format} real NAM/IR fixtures" in text
    results["formats"][format] = {"status": "passed", "bundle": str(bundle),
        "host_sample_format": "float32" if format == "AU" else "float64 (VST3 kSample64)" if format == "VST3" else "double (APP)",
        "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
        "acceptance_log_sha256": hashlib.sha256(acceptance.read_bytes()).hexdigest(),
        "acceptance_log": str(acceptance), "cpu_csv": str(cpu),
        "audio": "bit-exact tuner on/off at all rates/blocks, normal and shared framework bypass",
        "raw_pitch": "A2 within 2 cents, independent of trim and AH Drive",
        "representative_pedal_routes": "Off/TC/MC/AH at 48k and blocks64/128/1024; normal/bypass bit-exact on/off",
        "real_model_and_ir": {"model": str(model), "model_linear_weight": .5, "ir": str(ir),
            "ir_samples": [0.5, 0.25, -0.125], "rate_hz": 48000,
            "loading": "existing serialized-state path; both fixtures audibly verified by output RMS",
            "checks": "loaded-pipeline on/off bit-exact; raw A2 unchanged; latency32"},
        "lifecycle": "off/closed stop analysis, reopen reacquires; stopped-audio backlog after >150ms idle gap cannot publish; fresh full window required",
        "latency_samples": 32, "rt_allocation_free_lock_wait_calls_after_warmup": 0,
        "interposition_self_test": "malloc, free and pthread_mutex_lock interception proven"}
    print(f"PASS {format} actual host acceptance" + (" and callback measurements" if not options.acceptance_only else ""), flush=True)

capture_csv = host_tests / "capture-cpu.csv"
if not options.acceptance_only:
    run([str(host_tests / "capture-cpu")], capture_csv)

for format in (() if options.acceptance_only else ("app", "vst3", "au", "capture")):
    rows = list(csv.DictReader((host_tests / f"{format}-cpu.csv").open()))
    assert rows
    numeric = []
    for row in rows:
        parsed = {key: (value if key in ("format", "mode") else float(value)) for key, value in row.items()}
        parsed["p99_budget_percent"] = 100. * parsed["p99_us"] / parsed["deadline_us"]
        numeric.append(parsed)
    normal = [row for row in numeric if row["block"] >= 64]
    results["cpu"][format] = {"cases": len(numeric), "observations_per_case": 2048,
        "worst_p99_budget": max(numeric, key=lambda row: row["p99_budget_percent"]),
        "worst_normal_block_p99_budget": max(normal, key=lambda row: row["p99_budget_percent"]),
        "raw_maximum_deadline_outliers": [row for row in numeric if row["max_us"] > row["deadline_us"]]}
    if format == "capture":
        drained = [row for row in normal if row["mode"] == "enabled-drained"]
        assert max(row["p99_budget_percent"] for row in drained) < 5., "Capture exceeded 5% normal-block p99 budget target"
results["cpu"]["limits"] = ("Full-plugin measurements use unloaded NAM/IR with pedal/delay disabled; "
    "include real framework and downstream processing. Capture-only cases drain outside timing, "
    "include disabled/editor-closed and full-queue cases. Raw scheduler outliers retained; "
    "no hard realtime OS or every-model headroom guarantee.")
subprocess.run(["python3", str(package / "source-audit.py"), str(build / "source-audit.json")], cwd=repo, check=True)
(build / "host-validation.json").write_text(json.dumps(results, indent=2) + "\n")
print(f"PASS host artifact report: {build / 'host-validation.json'}", flush=True)
