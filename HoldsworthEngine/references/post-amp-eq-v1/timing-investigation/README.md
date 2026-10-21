# Retained timing investigation evidence

The investigation ran on 2026-10-20 and was archived with the accepted EQ v1
milestone on 2026-10-21. The original implementation measurements remain in
`../cpu-profile.json` and `../logs/eq-cpu.csv` unchanged.

Read `timing-diagnosis.md` for the diagnosis and scope limitations, and
`diagnosis.json` for all aggregates, outlier details and original raw hashes.
Neither artifact was rewritten during archiving. Paths in these historical
records identify the temporary locations used during measurement.

`evidence.tar.gz` retains 174 regular text files (42,028,431 uncompressed bytes):
all isolated and APP callback CSVs, calibration/summary CSVs, realtime-audit
stderr, aggregate/per-run results, harness sources, compiler commands, baseline
source/object provenance and initial-failure/successful-build logs. It contains
no executable, plugin/app bundle, object, cache or exported baseline source tree.
The baseline source can be reconstructed from accepted pre-EQ commit
`52e432585918adac8b1b41d53311aa8635c28e44`, using the vendor revisions in its
provenance record. Harness sources retain their actual measurement-time paths;
adapt those paths when reproducing on another checkout.

`archive-manifest.json` records every member's uncompressed SHA-256, byte count
and the compressed archive SHA-256. All 75 raw CSV hashes referenced by the
original diagnosis were verified against the archive. All 174 archive members
were decompressed and hash-checked against the original investigation files.
`SHA256SUMS` additionally verifies this README, the archive, manifest and two
unaltered diagnosis files:

```sh
cd HoldsworthEngine/references/post-amp-eq-v1/timing-investigation
shasum -a 256 -c SHA256SUMS
# Extract into a separate temporary directory to inspect the raw data.
tar -tzf evidence.tar.gz
```

The archive retains 442,368 measured callback rows: 294,912 isolated rows
(two clock protocols, each six conditions x twelve runs x 2,048 calls) and
147,456 APP rows (six conditions x twelve runs x 2,048 calls). Calibration
and summary rows are additional. No observation or outlier was discarded.
