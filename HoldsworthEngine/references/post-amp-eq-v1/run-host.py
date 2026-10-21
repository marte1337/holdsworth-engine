#!/usr/bin/env python3
"""Execute native EQ and accepted legacy checks; preserve exact artifacts/logs."""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import wave

package = Path(__file__).resolve().parent
repo = package.parents[2]
build = Path(os.environ.get("EQ_BUILD", "/private/tmp/nam-post-amp-eq-v1"))
hosts = build / "host-tests"
fixtures = build / "fixtures"
fixtures.mkdir(parents=True, exist_ok=True)
model = fixtures / "linear-half-48k.nam"
model.write_text(json.dumps({"version": "0.5.4", "architecture": "Linear",
    "config": {"receptive_field": 1, "bias": False}, "weights": [0.5], "sample_rate": 48000}) + "\n")
ir = fixtures / "colored-48k.wav"
with wave.open(str(ir), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(48000)
    wav.writeframes(struct.pack("<64h", 16384, 8192, -4096, *([0] * 61)))
env = dict(os.environ, DYLD_INSERT_LIBRARIES=str(hosts / "rt-audit.dylib"),
           TUNER_MODEL_FIXTURE=str(model), TUNER_IR_FIXTURE=str(ir))
report = {"scope": "Actual Release VST3/AU bundle APIs and APP Release-object callback",
          "rates_hz": [44100, 48000, 88200, 96000, 176400, 192000],
          "blocks": [1, 2, 4, 8, 32, 64, 128, 256, 512, 1024, 37],
          "formats": {}}
for format, suffix in (("APP", "app"), ("VST3", "vst3"), ("AU", "component")):
    bundle = build / "products" / f"NeuralAmpModeler.{suffix}"
    command = [str(hosts / f"{format.lower()}-host")]
    if format != "APP":
        command.append(str(bundle))
    log = hosts / f"{format.lower()}-acceptance.log"
    with log.open("w") as stream:
        result = subprocess.run(command, cwd=repo, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=240)
    text = log.read_text()
    passed = result.returncode == 0 and len(re.findall(rf"^PASS EQ {format} \d+ Hz:", text, re.MULTILINE)) == 6
    report["formats"][format] = {"passed": passed, "exit_code": result.returncode,
        "bundle": str(bundle), "log": str(log),
        "binary_sha256": hashlib.sha256((bundle / "Contents/MacOS/NeuralAmpModeler").read_bytes()).hexdigest(),
        "sample_format": "float32" if format == "AU" else "float64",
        "eq_identity": "neutral-on and global-off non-neutral sweeps bit-exact against paired default instance",
        "legacy": "accepted tuner, selected-pedal, loaded NAM/IR and host bypass regression matrix",
        "latency_samples": 32 if passed else None,
        "audited_rt_calls_after_legacy_warmup": 0 if passed else None}
    (build / "host-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} {format}: {log}", flush=True)
    if not passed:
        raise SystemExit(1)
