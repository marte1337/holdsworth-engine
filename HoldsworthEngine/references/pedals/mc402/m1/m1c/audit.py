"""Scope, copied-topology, dataset and artifact audit for the R2 review package."""

import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess

import numpy as np

from evaluate import DEST, OLD, METHODS, P, read, write


def main():
    root = DEST.parents[5]
    protected = read(DEST / "protected-source-sha256.json")
    for path, digest in protected.items():
        assert hashlib.sha256((root / path).read_bytes()).hexdigest() == digest, path
    original = (
        (DEST.parent / "m1a/OfflineExperiment.cpp")
        .read_text()
        .split("struct Filter\n", 1)[1]
        .strip()
    )
    candidate = (
        (DEST / "R2Experiment.cpp")
        .read_text()
        .split("struct Filter\n", 1)[1]
        .split('extern "C" void m1c_stage', 1)[0]
        .strip()
    )
    assert original.replace("mc402_m1a", "mc402_m1c") == candidate
    summary = read(DEST / "summary.json")
    primary = 0
    torture = 0
    for group in summary:
        if group["kind"] == "product":
            assert len(group["worst_relative"]) == 3
            assert not group["worst_relative"][0]["relative_pass"]
        filename = f'data/{group["kind"]}-{group["sample_rate"]}-{group["method"]}-{group["resampler"]}.json.gz'
        rows = read(DEST / filename)
        for r in rows:
            for key in [
                "fundamental_peak",
                "alias_peak",
                "total_alias_rms_volts",
                "alias_dbc",
            ]:
                assert np.isfinite(r[key]), (filename, key)
            assert r["relative_pass"] == (r["alias_dbc"] <= -70.0)
            assert r["absolute_pass"] == (
                r["total_alias_dbfs_sine_equivalent"] <= -80.0
            )
            assert r["alias_dbfs"] <= r["total_alias_dbfs_sine_equivalent"] + 1e-9
            assert r["output"] == 1.0 and r["boost_db"] == 0.0
        if group["kind"] == "product":
            primary += len(rows)
        else:
            torture += len(rows)
    assert primary == 803880 and torture == 12600, (primary, torture)
    assert sum(r["count"] for r in summary if r["kind"] == "product") == 805860
    assert len(read(DEST / "kernel-checks.json")) > 0
    assert len(read(DEST / "periodic-checks.json")) == 315
    assert len(read(DEST / "robustness.json")) == 60
    assert len(read(DEST / "performance.json")["measurements"]) == 30
    assert len(read(DEST / "continuous-reference-checks.json")) == 76
    assert len(read(DEST / "window-phase-checks.json")) == 12
    assert len(read(DEST / "highrate-reference-checks.json")) == 6
    assert len(read(DEST / "scalar-checks.json")["rows"]) == 360
    for path in DEST.glob("*.py"):
        ast.parse(path.read_text(), str(path))
    for path in DEST.rglob("*.json"):
        read(path)
    for path in DEST.glob("*.md"):
        for link in re.findall(r"\]\(([^)]+)\)", path.read_text()):
            if not link.startswith(("https:", "http:", "#")):
                assert (path.parent / link.split("#")[0]).exists(), (str(path), link)
    assert (
        subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True
        ).strip()
        == "fa6c0f8293fc832cac98338f3b05dd8ac2065e53"
    )
    subprocess.run(["git", "diff", "--check"], cwd=root, check=True)
    for path in DEST.rglob("*"):
        if path.is_file() and path.suffix in [".py", ".cpp", ".h", ".md", ".sh"]:
            assert all(
                line == line.rstrip() for line in path.read_text().splitlines()
            ), str(path)
    print(
        "Scope/data/link audit passed:",
        len(protected),
        "protected pre-existing files,",
        primary,
        "primary product renders,",
        torture,
        "historical renders",
    )


if __name__ == "__main__":
    main()
