# Rejected MC402 Overdrive source archive

The user accepted M1c and **rejected both provisional Overdrive profiles for
product integration**. Evidence was insufficient to justify further model
speculation, and the tested models failed the approved product fidelity
envelope even with ideal resampling. Overdrive is deferred pending stronger
circuit evidence or hardware measurement. No further experiments are authorized.

The original four DSP files and two MC402 test files moved here byte-for-byte
from `HoldsworthEngine/dsp` and `HoldsworthEngine/tests`. The original test
harness and runner are preserved as additional snapshots. Their original
identifiers are retained for reproducibility, not product status. All eight
files are covered by [source-sha256.json](source-sha256.json). None is enrolled
in product builds or the current HoldsworthEngine test suite.

M0 documentation and all [M1](../m1/README.md), [M1a](../m1/m1a/README.md),
[M1b](../m1/m1b/README.md) and [M1c](../m1/m1c/README.md) reports and curated
measurement evidence remain in place. Bulk datasets and raw renders are
reproducible local outputs; see the [retention inventory](../repository-hygiene.md).
Their original profiles are historical experimental profiles:

- `MC402-BOUNDED-V1-PROVISIONAL` — quadratic knee.
- `MC402-BOUNDED-V1-PROVISIONAL-R2` — offline C² quartic knee.

The accepted production profile is separately documented as
[MC402-CLEAN-BOOST-V1](../clean-boost-v1/README.md).

## Offline reproduction after the move

Historical numerical tools use the old relative DSP/test paths. Restore those
paths **outside production** into a new directory with:

```sh
python3 HoldsworthEngine/references/pedals/mc402/rejected-overdrive/export_research.py /tmp/mc402-research-export
cd /tmp/mc402-research-export
```

This only copies files and verifies archived source hashes; it does not run
measurements. The M1–M1c numerical build commands then resolve their original
relative paths. Python dependencies remain recorded in those packages.
The exported TestMain enrolls only the archived MC402 suite. Old whole-repository
freeze audits are omitted from the exported M1b/M1c shell wrappers because
they describe a historical checkout, not the later accepted scope change.
Original numerical tools and audit scripts remain preserved. Historical
manifests retain their original digests for durable inputs; build-cache,
machine-metadata and excluded bulk-data entries were pruned during hygiene.
Historical validation counts describe the original runs, not today's inventory.
No product alias qualification is implied by the exporter or a compilation check.

The export does not include removed bulk datasets. Run M1a's reproduction
wrapper to regenerate its grids when needed. For M1c, first run the exported
M1b wrapper: M1c reads M1b product/dense/torture coordinates and worst cases.
Then run the exported M1c wrapper, which regenerates its datasets, level
sweeps and paired NPZ/WAV renders before dependent analysis. No measurement
was rerun as part of repository cleanup.

Historical full-plugin Xcode commands in M1 are not applicable to this small
export. For historical isolated source compilation, use the exported
`HoldsworthEngine/dsp/MC402BoostOverdriveProcessor.cpp` plus the required
M1 offline bridge, benchmark or archived test sources. The current task only
checks that the archived numerical bridges still compile; it does not rerun
or extend Overdrive alias experiments.
