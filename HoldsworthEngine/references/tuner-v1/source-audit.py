#!/usr/bin/env python3
"""Freeze audible methods against the accepted tuner branch base, allowing only taps."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

package = Path(__file__).resolve().parent
repo = package.parents[2]
baseline = "3c86168"
cpp_path = "NeuralAmpModeler/NeuralAmpModeler.cpp"
header_path = "NeuralAmpModeler/NeuralAmpModeler.h"

def original(path):
    return subprocess.check_output(["git", "show", f"{baseline}:{path}"], cwd=repo)

def method(text, name):
    matches = list(re.finditer(r"^[^\n]*NeuralAmpModeler::" + re.escape(name)
                              + r"\([^)]*\)[^{]*\{\n.*?^}\n", text, re.MULTILINE | re.DOTALL))
    assert len(matches) == 1, name
    return matches[0].group()

current = (repo / cpp_path).read_text()
accepted = original(cpp_path).decode()
audited = {}
for name in ("ProcessBlock", "OnLatencyBypassBlock", "_ProcessInput", "_ProcessOutput",
             "_UpdateLatency", "OnLatencyPrepareBlock"):
    actual = method(current, name)
    expected = method(accepted, name)
    if name == "ProcessBlock":
        tap = "#ifdef NAM_HOLDSWORTH_DELAY_DEV\n  _CaptureTunerInput(inputs, nFrames);\n#endif\n"
        assert actual.count(tap) == 1
        actual = actual.replace(tap, "", 1)
    elif name == "OnLatencyBypassBlock":
        tap = "  _CaptureTunerInput(inputs, nFrames);\n"
        assert actual.count(tap) == 1
        actual = actual.replace(tap, "", 1)
    assert actual == expected, f"Audible method changed beyond authorized tuner tap: {name}"
    audited[name] = hashlib.sha256(actual.encode()).hexdigest()

def enum_names(text, name):
    match = re.search(r"enum " + name + r"\s*\{(.*?)\n};", text, re.DOTALL)
    assert match, name
    return re.findall(r"\b(k\w+)\s*(?:=[^,\n]+)?\s*(?:,|$)", match.group(1), re.MULTILINE)

header = (repo / header_path).read_text()
old_header = original(header_path).decode()
for enum in ("ECtrlTags", "EMsgTags"):
    old, new = enum_names(old_header, enum), enum_names(header, enum)
    # Count sentinels grow with appended controls/messages; existing IDs do not.
    assert old[-1].startswith("kNum") and new[-1] == old[-1]
    assert new[:len(old) - 1] == old[:-1], f"Non-append-only IDs: {enum}"
assert enum_names(header, "EParams") == enum_names(old_header, "EParams")
for name in ("SerializeState", "UnserializeState"):
    assert method(current, name) == method(accepted, name), name
assert (repo / "NeuralAmpModeler/config.h").read_bytes() == original("NeuralAmpModeler/config.h")

frozen_paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", baseline,
                                       "HoldsworthEngine/dsp"], cwd=repo, text=True).splitlines()
for path in frozen_paths:
    assert (repo / path).read_bytes() == original(path), f"Existing accepted DSP changed: {path}"
assert not subprocess.check_output(["git", "diff", "--name-only"], cwd=repo / "iPlug2"), "iPlug2 modified"
result = {"baseline": baseline, "audible_method_sha256_after_only_tap_removal": audited,
          "existing_dsp_files_frozen": len(frozen_paths), "existing_tags": "append-only",
          "serialized_params_and_latency_config": "unchanged", "iPlug2": "unchanged"}
if len(sys.argv) > 1:
    Path(sys.argv[1]).write_text(json.dumps(result, indent=2) + "\n")
print("PASS: accepted audible arithmetic, existing DSP, latency configuration, serialization and IDs frozen")
