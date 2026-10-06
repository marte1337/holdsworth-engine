# Tuner v1 implementation and validation

Tuner v1 completed manual acceptance in Standalone, REAPER VST3 and REAPER AU,
as reported by the user on 2026-10-06. The current UI and behavior are accepted.
The [manual acceptance record](manual-acceptance.json) preserves the results.

Tuner v1 analyzes the untrimmed clean mono host input, before calibration, the
selected pedal, NAM and IR processing. The audible path is unchanged. A4 is
fixed at 440 Hz; the display is chromatic, with note, octave, signed cents and
flat/in-tune/sharp indications. No muting or saved tuner preference is added.

## Runtime design

- `dsp/ChromaticTuner.{h,cpp}` owns fixed-storage YIN analysis. The 2,048-sample
  window runs at 22.05 or 24 kHz after integer decimation by 2/4/8. A Kaiser FIR
  limits aliasing and upper-partial interpolation bias. CMNDF selects the first
  credible period; interpolation uses its neighboring raw-difference trough.
  The supported fundamental range is 35–1,400 Hz, including E1–E6.
- `integration/TunerCaptureBuffer.h` is the preallocated SPSC capture transport:
  256-sample chunks, 256 usable queue slots. Audio only folds the connected raw
  inputs according to the existing APP/plugin mono convention, converts to
  floats, copies chunks and publishes lock-free metadata. It never waits for
  the consumer. Off or editor-closed returns before reading input samples.
- `integration/TunerAnalysisService.{h,cpp}` drains a bounded queue snapshot and
  performs filtering/detection on the main/control thread from `OnIdle`, at a
  nominal 20 Hz with at most one analysis per idle call. Generation, reset
  epoch, sequence and age checks prevent
  joining stale or discontinuous windows. Samples more than 150 ms behind the
  producer are rejected. The first idle and idle gaps over 150 ms discard the
  bounded backlog and require fresh input, including when audio has stopped.
- `integration/TunerDisplayState.{h,cpp}` owns the whole coherent display
  snapshot. Two fresh high-confidence estimates confirm acquisition or a note
  change. Same-note pitch uses a three-estimate median and a 100 ms log-frequency
  EMA. Note changes hold the previous whole snapshot while confirming rather
  than smoothing across semitones. Held values have no in-tune highlight.
- `ui/TunerControl.h` adds the compact TUNER section to `DevelopmentPanel.h`.
  Off, listening, no-signal, unstable and unsupported-rate states are explicit.
  Both normal processing and the existing latency-bypass callback capture raw
  input; analysis is enabled only while the tuner and editor are both open.

Acquisition requires RMS at least −65 dBFS and normalized YIN difference at most
0.10. Sustain allows −70 dBFS and 0.15; quieter input becomes no signal and
nonperiodic input becomes unstable. Periodicity is a measurement, not a
probability. In-tune enters at ±2 cents and exits at ±3 cents. A reading becomes
held after 150 ms without usable evidence and clears after 250 ms; lifecycle or
history invalidation clears it immediately. FIR/window startup takes about
88–96 ms; subsequent confidence confirmation and idle scheduling determine
visible acquisition time.

Only 44.1/48/88.2/96/176.4/192 kHz are supported. Other rates report unsupported.
The detector and service do not allocate or lock. No worker thread, worker join,
audio-thread analysis, audio-thread wall clock or UI call is introduced.

## Objective validation

The [M1 detector report](m1/README.md) records the signal matrix and measured
accuracy: all 61 chromatic notes E1–E6, ±5/±10/±25 cents, phases, amplitude/DC,
white/pink noise, synthetic decaying eight-harmonic signals, 25 dB SNR,
upper-harmonic boundary/phase probes, note transitions and irregular partitions
at all six rates. Gates are ≤1 cent for clean sines, ≤3 cents for clean harmonic
fixtures and ≤5 cents at 25 dB SNR. Broadband-noise fixtures must not acquire a
high-confidence note. Synthetic accuracy does not establish guitar accuracy.

Capture/service tests cover control generations, resets, rates, chunk assembly,
zero/stereo channels, queue overrun, nonfinite/float-overflow input, sequence
wrap, concurrent producer/consumer and stalled-idle/audio-stop recovery. Display
tests cover confirmations, weaker sustain, note boundaries, coherent signed
cents, EMA/median behavior, direction hysteresis, duplicate evidence, decay,
silence and lifecycle invalidation.

The native acceptance hosts exercise the actual Release AU and VST3 bundles
through AudioUnitRender and IAudioProcessor::process. The APP runner relinks the
exact Release target objects, replacing only the graphical entry point, and
uses production IPlugAPP::AppProcess. The tested host sample formats are APP
double, VST3 `kSample64` and AU float32; this run does not claim VST3 float32
coverage. APP shared-framework bypass is tested
directly because the standalone wrapper has no external host-bypass interface.
No product test implementation replaces the production DSP or capture service.

For each format the host matrix covers all six rates and callback sizes
1/2/4/8/32/64/128/256/512/1024/37; normal and bypass output must be bit-exact
between otherwise identical tuner-off/on instances. Off/TC/MC/AH pedal routes
are repeated at 48 kHz with 64/128/1024-sample blocks. A2 must be within 2 cents
and the stable display within 3 whole cents, including +18 dB trim/AH Drive,
bypass and editor close/reopen. A stopped-audio backlog after a long idle gap
cannot publish a note; a new full window must reacquire it. Latency remains
32 samples throughout.

Real stock state loading supplies a 48 kHz half-gain Linear NAM model and a
colored three-tap IR. Output RMS verifies both fixtures actually took effect;
loaded-pipeline on/off arithmetic remains bit-exact and the raw-input A2 pitch
remains unchanged. Fixtures are generated deterministically by the runner.

The separate dyld RT interposer audits malloc/calloc/realloc/free,
pthread_mutex_lock/trylock, pthread_cond_wait, pthread_join and sem_wait only
while executing audio callbacks. An intentional allocator/free/mutex self-test
proves interception in every runner. Existing whole-plugin first-use scratch
and filter allocations are warmed with the tuner disabled, including a newly
loaded NAM/IR pipeline. First tuner enable and all subsequent audited callbacks
must have zero calls. This does not claim the legacy cold first callback is
allocation-free. Source comparison with accepted base `3c86168` independently
requires byte-identical audible arithmetic after removing only the added raw
capture calls; parameter/state serialization, existing DSP files and vendored iPlug2
remain unchanged.

## Reproduce

Run from the repository root on macOS with Xcode installed. Outputs remain in
`/private/tmp/nam-tuner-v1`; products are not installed and user AU caches are
not cleared. The macOS build may require permission for Xcode's own caches;
native AU acceptance also needs access to the system AudioComponent registrar.

```sh
TUNER_BUILD=/private/tmp/nam-tuner-v1 bash HoldsworthEngine/references/tuner-v1/validate-engine.sh
TUNER_BUILD=/private/tmp/nam-tuner-v1 bash HoldsworthEngine/references/tuner-v1/validate-source.sh
TUNER_BUILD=/private/tmp/nam-tuner-v1 bash HoldsworthEngine/references/tuner-v1/package-products.sh
TUNER_BUILD=/private/tmp/nam-tuner-v1 python3 HoldsworthEngine/references/tuner-v1/compile-host-tests.py
TUNER_BUILD=/private/tmp/nam-tuner-v1 python3 HoldsworthEngine/references/tuner-v1/validate-hosts.py --acceptance-only
# Run with build/test jobs finished to avoid contaminating CPU measurements:
TUNER_BUILD=/private/tmp/nam-tuner-v1 python3 HoldsworthEngine/references/tuner-v1/validate-hosts.py --benchmark-only
```

The second host pass verifies the accepted binary hashes before measuring CPU.
Callback and capture CSVs retain p50/p99/max and deadline percentages, including
scheduler outliers. Direct capture covers disabled, editor-closed,
enabled/drained and full-queue states. Its enabled/drained p99 gate for blocks
at least 64 samples is less than 5% of the callback deadline. Full callback
measurements use the actual unloaded NAM/IR fallback pipeline with pedal/delay
disabled, so they report absolute time and deadline percentage rather than
claiming headroom for every model. Analysis/filter time is measured separately
outside audio. No operating-system hard realtime guarantee is inferred.

## Recorded run

All gates below passed on the final frozen production source. The
[engine report](engine-validation.json), [source audit](source-audit.json),
[product verification](product-validation.json) and
[native host report](host-validation.json) retain commands, hashes, scope and
logs. Hardware: Macmini9,1, Apple M1, 8 cores (4 performance and
4 efficiency), 16 GB RAM; macOS 26.6.2 arm64; Apple Clang 21. Products are arm64
Release with macOS deployment target 12.0, ad-hoc signed and verified.

| Gate | Result |
|---|---|
| Full engine Debug / Release / ASan+UBSan | 309 tests passed in each configuration |
| TSan | All 8 capture and 6 service tests passed; no diagnostics |
| New source strict warnings / Clang analyzer | 7 translation units clean; zero analyzer diagnostics |
| Project lint / source audit / diff check | Passed |
| Native APP / VST3 / AU functional hosts | All six rates and 11 block sizes passed; normal/bypass bit-exact; latency 32; zero audited RT calls |
| Real model/IR and all selected pedal fixtures | Passed in all three formats |
| Capture-to-analysis / capture-to-stable-display synthetic acquisition | At most 100 / 160 ms in deterministic tests |

The [quiet CPU report](quiet-host-benchmark.json) and raw
[APP](logs/app-cpu.csv), [VST3](logs/vst3-cpu.csv), [AU](logs/au-cpu.csv) and
[capture](logs/capture-cpu.csv) CSVs retain every measured case. There are 132
cases per wrapper and 264 capture cases, with 2,048 observations per case.
Wrapper timing includes harness preparation, sample-format conversions and
bypass setup as well as the actual render call; capture-only isolates the tuner
audio-thread work. The table shows worst p99 deadline percentage among blocks
of at least 64 samples, including disabled and enabled wrapper cases.

| Measured path | Worst normal-block p99 | Share of deadline | Case |
|---|---:|---:|---|
| APP wrapper | 534.208 µs | 4.6013% | 88.2 kHz / 1,024, enabled |
| VST3 wrapper | 87.583 µs | 13.1374% | 192 kHz / 128, disabled |
| AU wrapper | 88.459 µs | 3.3172% | 48 kHz / 128, disabled |
| Enabled/drained capture | 1.750 µs | 0.060293% | 176.4 kHz / 512 |

Across all block sizes, capture p99 is at most 4.042 µs when enabled/drained and
0.084 µs when disabled or editor-closed. Consumer analysis p99 is at most
1.54008 ms; feeding 50 ms of native input through its FIR has p99 at most
0.582208 ms. These operations run outside audio. Raw scheduler outliers remain
in the files, including three APP, eight VST3 and three AU wrapper cases whose
maximum exceeds the callback deadline. The detector's maximum single analysis
outlier is 29.2646 ms. No deadline guarantee is inferred from percentile data.

Final products remain at:

```text
/private/tmp/nam-tuner-v1/products/NeuralAmpModeler.app
/private/tmp/nam-tuner-v1/products/NeuralAmpModeler.vst3
/private/tmp/nam-tuner-v1/products/NeuralAmpModeler.component
```

The actual APP also launched successfully through scoped `open -n`; no automated
UI inspection or screenshot was performed. Apple's AU registrar and LaunchServices
needed scoped system-service access beyond the execution sandbox. Products
were neither installed nor used to clear user AU caches.

## Accepted manual results — 2026-10-06

These results were reported and accepted by the user after guitar audition;
they are separate from the automated native host evidence above.

Standalone detected all six open strings E2/A2/D3/G3/B3/E4 correctly. Acquisition
felt fast, cents were stable and FLAT / IN TUNE / SHARP behavior was natural.
Hard attacks settled correctly; soft notes and sustained decays tracked well.
There were no octave jumps or obviously incorrect notes. Note changes were
stable without feeling sluggish, and hammer-ons, pull-offs and legato tracked
well. Pedal/NAM/IR/delay changes did not affect detection. Tuner on/off was
completely inaudible. The current UI is accepted.

REAPER VST3 detected both DI and live guitar correctly. Tuner on/off was
inaudible, reported latency remained unchanged and pedal/NAM/IR/delay changes
did not affect detection. Host FX bypass/re-enable worked normally. No clicks,
crackle, dropout or level change was reported; the UI was responsive and stable.

REAPER AU passed the same checks and results as VST3, with no stale readings or
AU-specific artifacts. Standalone and both REAPER formats are manually accepted.

The existing objective evidence remains accepted: 309 Debug/Release/ASan-UBSan
tests, 14 TSan tests, maximum sine error 0.264 cents, maximum synthetic-guitar
error 1.35 cents, stable-display acquisition <=160 ms, analysis p99 1.54 ms,
capture p99 4.05 us, bit-exact audible output, unchanged latency and zero audited
tuner-attributable realtime allocation/lock/wait calls. The tuner milestone is
approved for normal Git integration without squash, rebase or history rewriting.

## Final Git integration checks — 2026-10-06

The accepted implementation source fingerprints still match. Fresh full Debug
and Release engine runs each passed all 309 tests. Isolated arm64 Release APP,
VST3 and AU sanity builds passed, including strict signature verification and
plist lint; `git diff --check` passed. See the
[integration validation record](integration-validation.json),
[Debug log](logs/integration-tests-debug.log),
[Release log](logs/integration-tests-release.log) and
[product build log](logs/integration-products.log).

The previously accepted sanitizer, realtime, accuracy and CPU evidence is
retained. Git integration changes only acceptance documentation; no detector,
transport, presentation, audible DSP or UI behavior is revised.
