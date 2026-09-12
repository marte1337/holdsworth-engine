"""Boost-only scope, retained protected files, and actual pre-NAM enrollment.

preserved-sha256.json retains original digests for 323 durable files after
authorized bulk-output/metadata cleanup. Historical validation.json records
the original 538-file audit. See ../repository-hygiene.md for the exclusions.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    package = Path(__file__).resolve().parent
    root = package.parents[4]
    hashes = json.loads((package / "preserved-sha256.json").read_text())
    for relative, expected in hashes.items():
        assert hashlib.sha256((root / relative).read_bytes()).hexdigest() == expected, relative
    archive = package.parent / "rejected-overdrive"
    moved = json.loads((archive / "source-sha256.json").read_text())
    for relative, expected in moved.items():
        assert hashlib.sha256((archive / relative).read_bytes()).hexdigest() == expected, relative
    project = (root / "NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj").read_text()
    assert "MC402BoostOverdrive" not in project and "MC402HalfBand" not in project and "MC402ProvisionalProfile" not in project
    assert project.count("MC402CleanBoostProcessor.cpp in Sources */ =") == 9
    assert project.count("MC402CleanBoostProcessorTests.cpp in Sources */ =") == 1
    assert project.count("DevelopmentPreNAMSelectorTests.cpp in Sources */ =") == 1
    for path in (root / "HoldsworthEngine/dsp").glob("MC402*"):
        assert path.name in ["MC402CleanBoostProcessor.h", "MC402CleanBoostProcessor.cpp"], path
    main_cpp = (root / "NeuralAmpModeler/NeuralAmpModeler.cpp").read_text()
    start = main_cpp.index("void NeuralAmpModeler::ProcessBlock")
    mono = main_cpp.index("_ProcessInput(inputs", start)
    selector = main_cpp.index("mPreNAMSelector.processSelected", mono)
    gate = main_cpp.index("// Noise gate trigger", selector)
    assert mono < selector < gate
    assert main_cpp[start:gate].count("processSelected") == 1
    # Prove the rest of ProcessBlock (gate/NAM/cab/output/delay) is unedited.
    old = subprocess.check_output(["git", "show", "HEAD:NeuralAmpModeler/NeuralAmpModeler.cpp"], cwd=root, text=True)
    def tail_of_process(text):
        start = text.index("void NeuralAmpModeler::ProcessBlock")
        marker = text.index("  _ApplyDSPStaging();", start)
        end = text.index("\nvoid NeuralAmpModeler::", marker)
        return text[marker:end]
    assert tail_of_process(main_cpp) == tail_of_process(old)
    # No host-latency changes, no audio UI inspection, no production OD tests.
    assert "mc402BoostOverdriveProcessorTests" not in (root / "HoldsworthEngine/tests/TestMain.cpp").read_text()
    ui = (root / "HoldsworthEngine/ui/DevelopmentPanel.h").read_text()
    old_ui = subprocess.check_output(["git", "show", "HEAD:HoldsworthEngine/ui/DevelopmentPanel.h"], cwd=root, text=True)
    marker = "  g.AttachControl(new Card(IRECT(24, 346, 464, 636)));"
    assert ui[ui.index(marker):] == old_ui[old_ui.index(marker):]
    for path in [package / "README.md", archive / "README.md", package.parent / "README.md"]:
        for target in re.findall(r"\]\(([^)]+)\)", path.read_text()):
            if not target.startswith(("http:", "https:", "#")):
                assert (path.parent / target.split("#")[0]).exists(), (path, target)
    subprocess.run(["git", "diff", "--check"], cwd=root, check=True)
    print(f"PASS: {len(hashes)} preserved files; 8 archived snapshots; source enrollment; unchanged downstream ProcessBlock and Delay/Amp/Cab UI")


if __name__ == "__main__":
    main()
