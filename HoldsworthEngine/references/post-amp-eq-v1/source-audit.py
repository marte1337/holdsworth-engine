#!/usr/bin/env python3
"""Check the EQ insertion is the only modification to existing audio arithmetic."""
import hashlib
import json
from pathlib import Path
import subprocess

repo = Path(__file__).resolve().parents[3]
name = "NeuralAmpModeler/NeuralAmpModeler.cpp"
# Keep the accepted pre-EQ comparator reproducible after committing/merging EQ.
base_commit = "52e432585918adac8b1b41d53311aa8635c28e44"
base = subprocess.check_output(["git", "show", f"{base_commit}:{name}"], cwd=repo, text=True)
current = (repo / name).read_text()
start = "void NeuralAmpModeler::ProcessBlock("
end = "void NeuralAmpModeler::OnReset()"
before = base[base.index(start):base.index(end)]
after = current[current.index(start):current.index(end)]
eq_start = after.index("  // Mono host-rate EQ, after the stock tone stack and before the cabinet.")
eq_end = after.index("  sample** irPointers = toneStackOutPointers;", eq_start)
insertion = after[eq_start:eq_end]
assert "#ifdef NAM_HOLDSWORTH_DELAY_DEV" in insertion and "mPostAmpEQProcessor.processBlock" in insertion
assert after[:eq_start] + after[eq_end:] == before, "Unrelated ProcessBlock arithmetic changed"
for filename in ("NeuralAmpModeler/ToneStack.cpp", "NeuralAmpModeler/ToneStack.h",
                 "NeuralAmpModeler/Unserialization.cpp", "HoldsworthEngine/dsp/HoldsworthDelayPresets.cpp",
                 "HoldsworthEngine/dsp/DelayModulator.cpp", "HoldsworthEngine/dsp/DelayBand.cpp",
                 "HoldsworthEngine/dsp/TCBLDCleanBoostProcessor.cpp", "HoldsworthEngine/dsp/MC402CleanBoostProcessor.cpp",
                 "HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp", "HoldsworthEngine/dsp/ChromaticTuner.cpp"):
    original = subprocess.check_output(["git", "show", f"{base_commit}:{filename}"], cwd=repo)
    assert (repo / filename).read_bytes() == original, f"Changed accepted source: {filename}"
assert not subprocess.check_output(["git", "diff", "--", "iPlug2", "AudioDSPTools", "NeuralAmpModelerCore", "eigen"], cwd=repo)
marker, following = "bool NeuralAmpModeler::SerializeState(", "int NeuralAmpModeler::UnserializeState("
assert current[current.index(marker):current.index(following)] == base[base.index(marker):base.index(following)]
print(json.dumps({"passed": True,
    "base_commit": base_commit,
    "legacy_ProcessBlock_sha256": hashlib.sha256(before.encode()).hexdigest(),
    "checks": ["Only EQ insertion changes ProcessBlock", "accepted DSP and preset sources unchanged",
               "stock serialization unchanged", "vendored submodules unchanged"]}, indent=2))
