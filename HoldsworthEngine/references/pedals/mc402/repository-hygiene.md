# MC402 repository hygiene and regression accounting

2026-09-12. User-authorized cleanup after the clean ARM64 Release build.
No commit or staging operation. No DSP, UI, target enrollment, calibration,
NAM/cab, TC BLD or Yamaha behavior changed during this cleanup. No rejected
Overdrive experiment was rerun.

## Complete working-tree audit

Counts use `git status --porcelain=v1 --untracked-files=all`, one record per
file (not collapsed untracked directories), against the unchanged HEAD
`fa6c0f8293fc832cac98338f3b05dd8ac2065e53`.

| Classification | Before | After |
| --- | ---: | ---: |
| 1. Permanent source, tests, tools, documentation and scoped ignore rules | 86 | 91 |
| 2. Curated measurement/validation evidence and profile inputs | 60 | 60 |
| 3. Reproducible bulk outputs / generated inventory / full build logs | 211 | 0 |
| **Total changed/untracked files** | **357** | **151** |

The five additions are this report and four scoped `.gitignore` files.
The seven tracked modifications and five new production/test files outside
this reference package were left exactly as they were before hygiene.
All 211 deletions were untracked generated files. Nothing was staged before
or after cleanup; no earlier M1a/M1b/M1c artifact was in the index.
“Retained for Git” means intentional future review material, not staged or
committed material. The original `clean-boost-v1/changes.json` is a historical
implementation/move record, not a current proposed-commit inventory.

Deleted files occupied **93,760,809 bytes (89.42 MiB)**. Numerical summaries,
worst-case rows, qualification decisions and retained graphs were not recomputed
or edited. In particular, PRODUCT nonqualification and TORTURE robustness
remain distinct. All reproduction scripts, native experiment sources, profiles,
requirements and the eight byte-identical rejected-source snapshots remain.

## Excluded families and exact ignore rules

- M1a complete ordinary/ADAA case grids: **2 files**.
- M1b product/dense/torture/tails grids: **72 files**.
- M1c product/torture FIR/ideal grids: **120 files**.
- M1c paired isolated audio/array renders: **12 files**.
- M1c detailed level sweep: **1 files**.
- M1c generated inventory: **1 files**.
- Full Xcode build logs: **3 files**.

The following are the complete rule additions. Every rule is anchored inside
its named MC402 directory; there is no global JSON/gzip/reference-data ignore.

[m1/m1a/.gitignore](m1/m1a/.gitignore):

```gitignore
# Reproducible bulk outputs; keep reports, summaries and reproduction tools.
/ordinary-cases.json
/adaa-cases.json.gz
```

[m1/m1b/.gitignore](m1/m1b/.gitignore):

```gitignore
# Reproducible bulk outputs; keep reports, summaries and reproduction tools.
/product-[0-9]*-*.json.gz
/dense-[0-9]*-*.json.gz
/torture-[0-9]*-*.json.gz
/tails-[0-9]*-*.json.gz
```

[m1/m1c/.gitignore](m1/m1c/.gitignore):

```gitignore
# Reproducible bulk outputs; keep reports, summaries and reproduction tools.
/data/product-[0-9]*-*.json.gz
/data/torture-[0-9]*-*.json.gz
/renders/isolated-[1-6].npz
/renders/isolated-[1-6].wav
/level-sweeps.json.gz
/files.json
```

[clean-boost-v1/logs/.gitignore](clean-boost-v1/logs/.gitignore):

```gitignore
# Reproducible bulk outputs; keep reports, summaries and reproduction tools.
/debug-build.log.gz
/release-build.log.gz
/plugin-build-final.log.gz
```

These patterns match every deleted path. Negative controls include
`envelope.json`, `profile.json`, `summary.json`, the compact compressed
`previous-witnesses.json.gz`, arbitrary unrelated JSON/gzip files and tools;
none is excluded by these additions.

## Retained evidence and hash scope

The inventory below retains all M0/M1/M1a/M1b/M1c reports, protocols, worst-case
tables, derivation and decisions. The 60 evidence/input files include original
M1 behavior/numerical/benchmark summaries; M1a stage isolation, detector sanity,
reference convergence and strategy comparisons; M1b envelope, worst-case
aggregates, FIR witnesses, convergence, performance and robustness; M1c profile,
static extrema/spectra, previous witnesses, convergence, checks, performance,
robustness and method/resampler summaries. Seven small report figures and seven
short test transcripts remain useful for review. Full Xcode transcripts were
removed; scripts already write fresh build logs to temporary directories.

M1c's roughly 0.85 MiB `summary.json` is intentionally retained: it stores
counts and worst relative/absolute/musical examples for all 120 comparison
groups, avoiding dependence on 120 bulk files just to review the decision.
The detailed convergence/witness checks are retained to substantiate the
continuous-time reference and borderline cases. No raw grid is needed to read
the reports or these aggregate/witness results.

Three hash manifests were curated, keeping original digest values rather than
rehashing changed inputs as if they were unchanged:

| Manifest | Original entries | Retained entries |
| --- | ---: | ---: |
| `m1/m1b/protected-source-sha256.json` | 1791 | 216 |
| `m1/m1c/protected-source-sha256.json` | 1957 | 303 |
| `clean-boost-v1/preserved-sha256.json` | 538 | 323 |

Pruned entries cover excluded generated files, `NeuralAmpModeler/build-mac/`
build products/caches, `.DS_Store`, and `xcuserdata`. The current Boost audit
also excludes the two historical manifests edited by this curation. It still
requires all **323** remaining protected files and all **eight** rejected-source
snapshots to match their original hashes. Unrelated protected source was not
removed from that check. Original historical validation JSON files retain their
original file counts and artifact hashes as provenance; those historical
inventories must not be mistaken for today's checkout or rerun results.

## Reproduction after deleting raw outputs

Use [the archive exporter](rejected-overdrive/export_research.py) as documented
in [the archive README](rejected-overdrive/README.md). Work in a fresh isolated
directory: the numerical tools require the archived DSP's historical relative
paths. Export verifies all eight source hashes and performs no measurements.

- M1a: run its exported `reproduce.sh` to rebuild the ordinary and ADAA grids
  before dependent diagnosis/summary commands.
- M1b: its exported `reproduce.sh` regenerates product, dense, torture and tails
  grids from the retained `envelope.json` and native/Python tools.
- M1c: **run M1b first**. M1c reads the regenerated M1b coordinates and worst
  cases, then its wrapper regenerates its own FIR/ideal datasets, detailed level
  sweep and six NPZ/WAV render pairs before spectra and dependent reports.
- Use the retained requirements and compiler flags. Whole-repository historical
  freeze audits were already omitted by the exporter because they refer to the
  pre-Boost checkout; this cleanup does not claim those old audits pass today.

The exporter, experiment tools, profiles and numerical algorithms remain
byte-for-byte unchanged by cleanup. Regeneration is documented, not executed.
Generated datasets/renders may be absent in the curated package without losing
the scripts or inputs needed to recreate them.

Final cleanup checks passed: all 211 removed paths match the scoped ignore
rules; all retained files and unrelated JSON/gzip negative controls remain
visible to Git; every Markdown file link resolves; retained Python, shell and
JSON files pass syntax/parsing checks; the archive exports successfully without
bulk data; the 323-file/eight-snapshot preservation audit and `git diff --check`
pass. All 12 changed/new production files outside this research package match
their pre-cleanup hashes. The staging area remains empty.

## Why 245 tests became 244

The reduction is entirely the accepted replacement of the rejected provisional
MC402 Boost/Overdrive suite. **No unrelated HoldsworthEngine regression was
lost.** All 21 pre-existing suites remain in their original order. All 21
existing test source files are byte-for-byte equal to HEAD, and all **232**
existing test names remain registered and appear in the Release pass log.
The TC file contains 15 tests: 12 named TC BLD regressions plus three existing
Development-control tests; all 15 remain. The old suite's 13 tests include
combined Boost/Overdrive, provisional knee/FIR/latency and lifecycle coverage;
they were archived together, not misrepresented as 13 purely nonlinear tests.

| Registered coverage | Provisional M1 | Current production |
| --- | ---: | ---: |
| Existing HoldsworthEngine / Yamaha / TC / development regressions | 232 | 232 |
| Rejected provisional MC402 suite | 13 | 0 (archived) |
| Accepted MC402 clean Boost | 0 | 8 |
| Development pre-NAM selector | 0 | 4 |
| **Total** | **245** | **244** |

Exact suite-registration change, comparing the archived M1 runner with the
current runner (the preceding 21 entries are identical):

```diff
   tcBldCleanBoostProcessorTests(),
-  mc402BoostOverdriveProcessorTests()};
+  mc402CleanBoostProcessorTests(),
+  developmentPreNAMSelectorTests()};
```

Corresponding declarations in `TestHarness.h` replace the same MC402 suite
and add the selector suite. The archived processor/test sources remain outside
production targets. The original 13 test names remain in
[the archived suite](rejected-overdrive/tests/MC402BoostOverdriveProcessorTests.cpp).

Unchanged registrations by existing test source:

| Source under `HoldsworthEngine/tests` | Tests before and after |
| --- | ---: |
| `AudioRoutingTests.cpp` | 20 |
| `Connect913PresetTests.cpp` | 4 |
| `DelayBandLoopFilterTests.cpp` | 7 |
| `DelayBandModulationTests.cpp` | 10 |
| `DelayBandSignalPolarityTests.cpp` | 5 |
| `DelayBandTapTests.cpp` | 9 |
| `DelayBandTests.cpp` | 15 |
| `DelayGroupingTests.cpp` | 25 |
| `DelayLoopFilterTests.cpp` | 13 |
| `DelayModulatorTests.cpp` | 15 |
| `FractionalDelayLineTests.cpp` | 14 |
| `Group12RhythmDiagnosticTests.cpp` | 4 |
| `Holdsworth111PresetTests.cpp` | 6 |
| `Holdsworth122PresetTests.cpp` | 6 |
| `Holdsworth223PresetTests.cpp` | 6 |
| `Holdsworth231PresetTests.cpp` | 6 |
| `HoldsworthDelayEngineTests.cpp` | 23 |
| `HoldsworthDelayLiveIntegrationTests.cpp` | 9 |
| `ModulationSyncTests.cpp` | 10 |
| `Sync922PresetTests.cpp` | 10 |
| `TCBLDCleanBoostProcessorTests.cpp` | 15 |

A fresh execution of the existing optimized Release test binary during hygiene
passed **244/244**; its test names match the retained Release transcript. No
production source changed, so no redundant plugin rebuild or new alias sweep
was needed for cleanup.

## Release investigation result carried forward

The original temporary standalone at
`/tmp/mc402-boost-v1/products/NeuralAmpModeler.app` was **Debug ARM64**, with
`-O0`, `DEVELOPMENT=1`, `DEBUG=1`, `_DEBUG`. The clean standalone from the same
working tree at `/tmp/mc402-boost-v1-release/products/NeuralAmpModeler.app`
is **Release, ARM64 only**, with `-O3`, `RELEASE=1`, `NDEBUG=1`; it was launched
for manual audition. Development UI remains enabled. A dSYM does not make
this a Debug build.

APP enrolls only `HoldsworthEngine/dsp/MC402CleanBoostProcessor.cpp` for MC402,
with `MC402CleanBoostProcessor.h`; the link list contains that processor object.
Its reported latency is **0 host samples**. No rejected MC402 Overdrive,
provisional-profile, FIR or oversampling source/object is enrolled. The
unoptimized Debug convolution path is a plausible cause of missed audio
deadlines when loading an IR; the build evidence alone does not prove the
Release audition fixes crackle. The installed application at
`/Users/marte/Applications/NeuralAmpModeler.app` was verified unchanged.

## Complete retained changed-file inventory

All paths below are relative to the named directory. “1” means permanent
source/tests/tools/docs/configuration; “2” means curated evidence/input.
This inventory accounts for every one of the 151 final changed/untracked files.

`HoldsworthEngine/dsp/` (2 files):

- 1: `MC402CleanBoostProcessor.cpp`
- 1: `MC402CleanBoostProcessor.h`

`HoldsworthEngine/integration/` (1 files):

- 1: `DevelopmentPreNAMSelector.h`

`HoldsworthEngine/references/pedals/mc402/` (5 files):

- 1: `README.md`
- 1: `dsp-design.md`
- 1: `milestones.md`
- 1: `repository-hygiene.md`
- 1: `sources.md`

`HoldsworthEngine/references/pedals/mc402/clean-boost-v1/` (6 files):

- 1: `README.md`
- 1: `audit.py`
- 2: `changes.json`
- 2: `preserved-sha256.json`
- 1: `validate.sh`
- 2: `validation.json`

`HoldsworthEngine/references/pedals/mc402/clean-boost-v1/logs/` (9 files):

- 1: `.gitignore`
- 1: `README.md`
- 2: `debug-tests.txt`
- 2: `focused.txt`
- 2: `release-tests.txt`
- 2: `routing.txt`
- 2: `sanitizer-tests.txt`
- 2: `tc-regressions.txt`
- 2: `tsan-tests.txt`

`HoldsworthEngine/references/pedals/mc402/m1/` (6 files):

- 1: `README.md`
- 2: `behavior-report.json`
- 2: `benchmark-report.json`
- 2: `measurements.png`
- 2: `numerical-report.json`
- 2: `validation-summary.json`

`HoldsworthEngine/references/pedals/mc402/m1/m1a/` (28 files):

- 1: `.gitignore`
- 1: `OfflineChecks.cpp`
- 1: `OfflineExperiment.cpp`
- 1: `README.md`
- 2: `adaa-frequency-response.json`
- 2: `adaa-worst-cases.json`
- 1: `additional_checks.py`
- 2: `antiderivative-checks.json`
- 1: `check_antiderivatives.py`
- 2: `continuous-reference-sanity.json`
- 1: `continuous_reference.py`
- 2: `cpu-estimates.json`
- 2: `diagnosis.png`
- 1: `experiment.py`
- 2: `harness-sanity.json`
- 2: `high-precision-checks.json`
- 1: `plot_results.py`
- 2: `reference-convergence.json`
- 1: `reproduce.sh`
- 1: `run_diagnosis.py`
- 2: `stage-control-regions.json`
- 2: `stage-localization.json`
- 2: `strategy-probes.json`
- 1: `strategy_probes.py`
- 1: `summarize.py`
- 2: `summary.json`
- 2: `validation.json`
- 1: `worst-cases.md`

`HoldsworthEngine/references/pedals/mc402/m1/m1b/` (27 files):

- 1: `.gitignore`
- 1: `Benchmark.cpp`
- 1: `README.md`
- 1: `Robustness.cpp`
- 1: `audit.py`
- 1: `check_harness.py`
- 2: `continuous-reference-checks.json`
- 2: `envelope.json`
- 1: `evaluate.py`
- 2: `fir-diagnosis.json`
- 1: `fir_diagnosis.py`
- 2: `five-khz-neighborhood.json`
- 2: `harness-checks.json`
- 2: `performance.json`
- 1: `plot_results.py`
- 2: `product-aliasing.png`
- 2: `protected-source-sha256.json`
- 1: `protocol.md`
- 1: `report_tables.py`
- 1: `reproduce.sh`
- 2: `robustness.json`
- 1: `summarize.py`
- 2: `summary.json`
- 2: `validation.json`
- 2: `witness-checks.json`
- 1: `witness_checks.py`
- 1: `worst-cases.md`

`HoldsworthEngine/references/pedals/mc402/m1/m1c/` (40 files):

- 1: `.gitignore`
- 1: `Benchmark.cpp`
- 1: `CandidateR2.h`
- 1: `LegacyPeriodic.cpp`
- 1: `OfflineChecks.cpp`
- 1: `PeriodicStage.h`
- 1: `R2Experiment.cpp`
- 1: `README.md`
- 1: `Robustness.cpp`
- 1: `analysis.py`
- 1: `audit.py`
- 1: `checks.py`
- 2: `continuous-reference-checks.json`
- 1: `derivation.md`
- 1: `evaluate.py`
- 1: `followup_checks.py`
- 2: `harmonics.png`
- 2: `highrate-reference-checks.json`
- 2: `kernel-checks.json`
- 2: `performance.json`
- 1: `performance.py`
- 2: `periodic-checks.json`
- 2: `previous-witnesses.json.gz`
- 2: `profile.json`
- 2: `protected-source-sha256.json`
- 1: `protocol.md`
- 2: `render-spectra.json`
- 2: `render-spectra.png`
- 1: `render_spectra.py`
- 1: `report.py`
- 1: `reproduce.sh`
- 1: `requirements.txt`
- 1: `results.md`
- 2: `robustness.json`
- 2: `scalar-checks.json`
- 2: `static-comparison.json`
- 2: `static-comparison.png`
- 2: `summary.json`
- 2: `validation.json`
- 2: `window-phase-checks.json`

`HoldsworthEngine/references/pedals/mc402/m1/tools/` (7 files):

- 1: `MC402Benchmark.cpp`
- 1: `MC402OfflineBridge.cpp`
- 1: `generate_fir.py`
- 1: `measure_behavior.py`
- 1: `plot_results.py`
- 1: `requirements.txt`
- 1: `validate_numerics.py`

`HoldsworthEngine/references/pedals/mc402/rejected-overdrive/` (3 files):

- 1: `README.md`
- 1: `export_research.py`
- 2: `source-sha256.json`

`HoldsworthEngine/references/pedals/mc402/rejected-overdrive/dsp/` (4 files):

- 1: `MC402BoostOverdriveProcessor.cpp`
- 1: `MC402BoostOverdriveProcessor.h`
- 1: `MC402HalfBandCoefficients.h`
- 1: `MC402ProvisionalProfile.h`

`HoldsworthEngine/references/pedals/mc402/rejected-overdrive/tests/` (4 files):

- 1: `MC402BoostOverdriveProcessorTests.cpp`
- 1: `MC402TestAccess.h`
- 1: `TestHarness.h`
- 1: `TestMain.cpp`

`HoldsworthEngine/tests/` (4 files):

- 1: `DevelopmentPreNAMSelectorTests.cpp`
- 1: `MC402CleanBoostProcessorTests.cpp`
- 1: `TestHarness.h`
- 1: `TestMain.cpp`

`HoldsworthEngine/ui/` (2 files):

- 1: `DevelopmentPanel.h`
- 1: `README.md`

`NeuralAmpModeler/` (2 files):

- 1: `NeuralAmpModeler.cpp`
- 1: `NeuralAmpModeler.h`

`NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/` (1 files):

- 1: `project.pbxproj`
