"""Protect the frozen source files and verify complete, consistent artifacts."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
from evaluate import DEST, P, all_rows, read, write

ROOT = DEST.parents[5]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["frozen", "results"])
    args = parser.parse_args()
    protected = read("protected-source-sha256.json")
    for name, expected in protected.items():
        assert digest(ROOT / name) == expected, name
    print("Frozen source audit passes:", len(protected), "files")
    if args.command == "frozen":
        return
    product = all_rows("product") + all_rows("dense")
    keys = set()
    for r in product:
        key = tuple(
            r[k] for k in ["sample_rate", "method", "k", "amplitude", "gain", "tone"]
        )
        assert key not in keys, key
        keys.add(key)
        assert 40 <= r["input_hz"] <= 10000 and r["amplitude"] <= 4.5
        assert r["alias_dbfs"] <= r["total_alias_dbfs_sine_equivalent"] + 1e-12
        assert abs(r["alias_dbc"] - (r["alias_dbfs"] - r["fundamental_dbfs"])) < 1e-10
    summary = read("summary.json")
    assert summary["product_cases"] == len(product)
    assert summary["decision"] == "C" and not summary["product_qualified"]
    robustness = read("robustness.json")
    assert len(robustness) == 36
    assert max(r["recovery_peak_after_one_second"] for r in robustness) < 1e-12
    references = read("continuous-reference-checks.json")
    assert max(r["coefficient_convergence_db"] for r in references) < -100
    write(
        "validation.json",
        dict(
            environment=platform.platform(),
            frozen_source_file_count=len(protected),
            product_case_count=len(product),
            torture_case_count=len(all_rows("torture")),
            high_frequency_tail_case_count=len(all_rows("tails")),
            continuous_reference_case_count=len(references),
            continuous_reference_worst_convergence_db=max(
                r["coefficient_convergence_db"] for r in references
            ),
            diagnostic_assertions_pass=True,
            product_qualified=False,
            production_changes=False,
            artifacts_sha256={
                str(p.relative_to(DEST)): digest(p)
                for p in sorted(DEST.iterdir())
                if p.is_file() and p.name != "validation.json"
            },
        ),
    )
    print("Artifact consistency passes; PRODUCT remains unqualified")


if __name__ == "__main__":
    main()
