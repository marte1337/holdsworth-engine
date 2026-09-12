"""Restore the historical relative layout in a NEW offline directory.

Does not run experiments or alter archived measurements/production files.
Old repository-wide freeze audits do not apply after the accepted scope change;
their commands are omitted only in the exported reproduction wrappers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    archive = Path(__file__).resolve().parent
    destination = args.destination.resolve()
    if destination.exists():
        parser.error("Destination must not exist; refusing to overwrite any files")
    hashes = json.loads((archive / "source-sha256.json").read_text())
    for name, expected in hashes.items():
        assert hashlib.sha256((archive / name).read_bytes()).hexdigest() == expected, name
    engine = destination / "HoldsworthEngine"
    package = engine / "references/pedals/mc402"
    package.mkdir(parents=True)
    for folder in ["dsp", "tests"]:
        shutil.copytree(archive / folder, engine / folder)
    shutil.copytree(archive.parent / "m1", package / "m1",
                    ignore=shutil.ignore_patterns("__pycache__", ".DS_Store"))
    for name in ["dsp-design.md", "sources.md", "milestones.md"]:
        shutil.copy2(archive.parent / name, package / name)
    # The old TestMain snapshot stays immutable in the archive. This standalone
    # export only enrolls the historical MC402 suite, never product regressions.
    main_cpp = engine / "tests/TestMain.cpp"
    main_cpp.write_text(re.sub(r"const std::array suites\{.*?\};",
        "const std::array suites{mc402BoostOverdriveProcessorTests()};",
        main_cpp.read_text(), count=1, flags=re.S))
    for relative in ["m1/m1b/reproduce.sh", "m1/m1c/reproduce.sh"]:
        path = package / relative
        path.write_text("\n".join(
            "# Historical repository-wide audit omitted in isolated export; archived core hashes verified by exporter."
            if 'audit.py"' in line else line for line in path.read_text().splitlines()) + "\n")
    (destination / "ARCHIVE-NOTES.txt").write_text(
        "Rejected MC402 Overdrive research only. Never a production target.\n"
        "DSP and archived test source hashes were verified before export.\n"
        "Numerical sources/data retain their original relative layout.\n"
        "TestMain is narrowed to historical MC402 tests; M1b/M1c reproduction wrappers omit\n"
        "old whole-repository frozen-scope audits, which are inapplicable to this export.\n"
        "All other exported numerical sources/data are copied unchanged.\n")
    print(destination)


if __name__ == "__main__":
    main()
