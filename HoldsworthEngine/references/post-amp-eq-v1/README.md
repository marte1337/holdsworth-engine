# Post-amp / pre-cab EQ v1

Implemented from the approved design, without musical tuning or EQ presets.
Objective validation and the user's Standalone audition are accepted. No MIDI,
DAW automation, permanent parameter serialization or effects framework is added.

## Acceptance — 2026-10-21

The user accepted the objective results and passed the Standalone audition:
neutral EQ was transparent, with no audible clicks or instability reported.
The fixed 12 dB/octave HP/LP slopes were noted as conservative; stronger slopes
remain possible future polish. No response, control, preset or acceptance
threshold was changed. Manual VST3/AU checks are deferred to release QA,
distinct from the passed automated Release-build and native-host checks. No
detailed visual checklist completion is inferred from the audition report.

The timing investigation preserved the original isolated-EQ **27.097583 ms**
wall maximum against its **23.219954649 ms** comparison budget. In 49,152
continuous-sweep calls across two timing protocols, that overrun did not recur;
the largest wall interval was **0.239208 ms**. A neutral-EQ wall spike of
**34.043 ms** and four full-APP wall overruns were also retained. The latter
had only **0.726–1.115 ms** of paired thread CPU and large wall-minus-CPU gaps.
No measured thread-CPU interval exceeded the budget. Evidence supports
benchmark/system timing interruptions, but the original event has no paired
CPU trace and its precise cause remains unproven. No DSP fix is justified.

The full-APP comparator was independently rebuilt from accepted pre-EQ commit
`52e432585918adac8b1b41d53311aa8635c28e44`; the original accepted binaries were
unavailable. It compared matched fallback paths without a loaded NAM/IR, on
ordinary CLI threads. The baseline had no wall overruns in this finite sample;
that does not establish equal tail-event probabilities or a realtime-host
guarantee. See [the archived diagnosis](timing-investigation/timing-diagnosis.md),
[raw evidence manifest](timing-investigation/archive-manifest.json) and
[acceptance record](acceptance-2026-10-21.json).

## Final integration checks — 2026-10-21

Fresh Debug, Release and ASan/UBSan runs each passed 329 tests; TSan passed
seven concurrency tests. Strict warnings, all four Clang analyzer checks,
source/realtime audits, project lint and diff checks passed. Fresh APP/VST3/AU
Release builds and native-host matrices passed, including bit-exact neutral
and bypass, unchanged latency and zero warmed realtime-audit violations.
All 40 frozen source/evidence hashes still match the accepted snapshot.

`integration-validation-2026-10-21.json` records the commands, results, fresh
response metrics, product hashes and evidence checksums.
`git-integration-validation.tar.gz` preserves the final logs and runner,
including the first sandboxed Xcode cache failure and successful retry.
These checks supplement the historical records below. They do not replace
the original CPU measurements. The source audit now explicitly compares with
accepted pre-EQ commit `52e432585918adac8b1b41d53311aa8635c28e44`, so it remains
reproducible after committing and merging the EQ.

## Recorded results — 2026-10-20

These implementation-time measurements and their original machine-readable
records are preserved. The later acceptance and investigation above supplement
them rather than replacing the outlier or rerunning it away.

Final full Debug, Release and ASan/UBSan runs each passed all **329** tests;
TSan passed all **7** concurrent-handoff tests. Strict warnings and Clang
analysis passed for the DSP and three new test translation units. APP, VST3 and
AU Release builds, signatures, plist/project lint and native-host tests passed.
The standalone also launched successfully; its UI was not automatically inspected.

| Measurement | Maximum observed error |
|---|---:|
| Independent impulse response, 19,418 comparisons | 3.6826031596275191e-7 dB |
| Bell center/half-gain sine fits, 324 cases | 3.9026559761623503e-12 dB |
| HP/LP sine response | 5.3771796376199461e-9 dB |
| Reversed colored/HP/LP fades vs independent envelopes | 5.467848396278896e-15 sample units |
| Normal-control first output vs 20 ms analytic step | 1.3877787807814457e-17 sample units |
| Actual native wrapper active response, including loaded NAM/IR | 1.91615300515e-7 dB |

Settled bypass and stale-state neutral operation were bit-exact. Extreme
parameter-change renders stayed finite; their two-second silence tail was at
most 3.9060987101715592e-35. Isolated and warmed native-host audits observed
zero allocation/free/lock/wait calls attributable to processing; positive
interposer tests caught all three deliberately injected forbidden calls.

Quiet timing used an Apple M1 Macmini9,1, macOS 26.6.2 arm64 and Apple Clang
21.0.0. The complete 210-case matrix is retained. For **192 kHz / 32 frames**,
with 4,096 timed callbacks per case:

| Mode | p50 (us) | p99 (us) | Maximum (us) |
|---|---:|---:|---:|
| Bypass, states tracking | 0.542 | 0.666 | 80.833 |
| Neutral, states tracking | 0.542 | 0.625 | 16.875 |
| Settled, all five sections | 0.583 | 0.667 | 6.250 |
| Continuous parameter sweep | 0.667 | 0.750 | 8.334 |
| Continuous sweep plus enable changes | 0.709 | 0.834 | 16.250 |

The last case's p99 is 0.5004% of its 166.666667 us callback budget. Across
every matrix row the largest p99 budget share was 2.4%, at 192 kHz / one frame.
One of 210 rows had a wall-clock maximum beyond its deadline: 176.4 kHz / 4,096
frames, continuous sweep, **27,097.583 us vs 23,219.954649 us** (116.699552%).
Its p99 was 292.292 us (1.258797%). This outlier is retained unchanged; its
cause cannot be established from wall-clock timing alone. It is not hidden by
rerunning or replacing the measurements. Manual audible acceptance and the
observed timing/deadline limitation were review items at this recording date;
the later acceptance and investigation are recorded above.

## DSP and integration contract

`PostAmpEQProcessor` processes one host-rate mono stream:

    optional HP -> Bell 1 -> Bell 2 -> Bell 3 -> optional LP -> EQ trim

The live insertion is after the existing NAM tone stack and before cabinet IR.
Gate detection/gain order, NAM, tone stack, cabinet implementation, raw-input
tuner, pedals, Yamaha presets/routing, DC blocker and final output are unchanged.
EQ affects both direct audio and subsequent delay input. Existing delay tails
are not rewritten by a new EQ setting. Unsupported EQ rates preserve the
wrapper's legacy signal path.

The physical parameter contract and shared normalized mappings are in
`dsp/PostAmpEQControls.h`; UI code only formats the values. Three bells share
80–8,000 Hz, -12…+12 dB and Q 0.35–4.0. Their defaults are 250/1,000/3,500 Hz,
0 dB and Q 0.7. HP is off with remembered 80 Hz (20–200 Hz), LP is off with
remembered 8 kHz (3–16 kHz), and trim is 0 dB (-24…+6 dB). Global enable is
off. Frequency/Q coordinates are logarithmic; dB coordinates are linear.
Canonical default coordinates return the exact physical default, including
0 dB trim despite compiler FMA optimization.

Five double-precision, two-integrator trapezoidal SVFs use the standard RBJ
peaking-Q convention. Optional cuts are second-order Butterworth, Q = 1/sqrt(2).
Bell primitives are g = tan(pi*f/Fs), A = 10^(gain/40), k = 1/(Q*A), and
m = k*(A*A-1). Bell output is input + m*bandpass. These formulas follow
[Simper's derivation](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)
and the [Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html).
The selected integrator structure has published time-varying stability analysis
under positive frequency/damping and bounded output mixing:
[DAFx 2022, section 6.3](https://dafx.de/paper-archive/2022/papers/DAFx20in22_paper_41.pdf).

g/k/m ramp for ceil(20 ms * Fs) samples, and enable, HP/LP mixes and linear trim
gain ramp for ceil(10 ms * Fs) samples. A new target starts from the current
value; resending an unchanged target does not restart a ramp. Transient curves
interpolate transformed primitives, not an exact logarithmic/dB trajectory.
Derived coefficients remain coupled and are cached when settled. Target work
is bounded to five tan and at most four pow calls per adopted snapshot, never
per sample. Frequency and damping stay strictly positive.

All histories track while bypassed. Endpoint branches preserve the original
finite input exactly during settled bypass and all-neutral operation, including
old non-neutral histories, signed zero and extreme finite input. Zero dB bells
and disabled cuts have exact local identity. No buffering latency is added;
ordinary IIR phase/group delay remains. Reported model-plus-32-sample latency
is unchanged.

One producer publishes a complete physical controls snapshot through a fixed
three-slot SPSC mailbox. The audio consumer adopts at most once per nonempty
valid block. There are no retry loops, allocations, locks, waits, logging or
I/O in processing. prepare/reset require lifecycle synchronization. Finite
controls clamp to canonical bounds; a nonfinite field rejects the entire
snapshot. Nonfinite audio clears histories and substitutes zero; extreme
internal arithmetic is bounded independently of the exact bypass/neutral path.

## Validation and evidence

The response tolerance remains **0.01 dB above -80 dB**, unchanged. The independent
oracle uses RBJ numerator/denominator transfer functions, not production SVF
coefficient generation. Tests cover impulses/FFT, settled sine fits, center and
half-gain bandwidth/Q, reciprocal boost/cut, HP/LP cutoff/endpoints, all six
supported project rates, exact neutral/bypass, reset/reprepare, invalid values,
in-place processing, one/irregular/maximum blocks, sample-timed retargeting,
tail decay, allocation tracking and concurrent coherent snapshots.

Additional output-level transition tests compare colored global bypass and
isolated HP/LP changes against independently calculated 10 ms envelopes with
mid-fade reversals. Tone/noise/impulse/silence inputs keep a continuous sample
clock. Normal frequency/gain/Q changes verify the actual first output follows
the 20 ms primitive step rather than an instantaneous coefficient replacement;
transient and final-target errors are retained without an audibility claim.

`engine-validation.json` and `logs/tests-*.log` record the final full regression
and sanitizer results and numeric response/transition measurements.
`source-audit.json` verifies the EQ insertion is the only change to existing
ProcessBlock arithmetic, with accepted DSP/preset/serialization/vendor source
unchanged. `rt-source-audit.json` records the separately reproducible RT source
guard. Strict warnings and Clang analyzer logs cover the new DSP/test sources.

`host-validation.json` and `logs/*-acceptance.log` record native APP, VST3 and AU
checks: six rates, eleven callback sizes, exact neutral/global-bypass audio,
active independent-oracle response, stock-tone/IR independence, loaded NAM/IR,
editor retention, active parameter updates, and dry-plus-delay-send/tail
linearity. APP uses the actual Release target objects with only its graphical
entry point replaced. VST3 and AU load the actual Release bundles and call their
real host process APIs. Formats tested are APP double, VST3 float64 and AU
float32. This does not claim VST3 float32 validation.

The native RT interposer detects malloc/calloc/realloc/free, mutex lock/trylock,
condition waits, thread joins and semaphore waits. Positive allocator/free/lock
self-tests prove interception. Whole-plugin legacy scratch/filter first use is
warmed before auditing; new EQ first-enable and subsequent parameter updates
remain audited. The isolated EQ additionally audits from its first callback.
The whole legacy plugin's cold first callback is not claimed allocation-free.

`cpu-profile.json` and `logs/eq-cpu.csv` retain the isolated mono EQ performance
matrix: six rates, blocks 1/7/32/64/127/1024/4096, bypass/neutral tracking,
settled five-section processing, continuous sweeps and sweeps with enable
changes. Target adoption/coefficient work is included; control generation and
publication are outside the timing interval. Native RT interposition remains
active. Percentiles and raw maxima include clock/scheduler effects; no hard
deadline guarantee or NAM-model CPU percentage is inferred.

`implementation-validation.json` records final product hashes, verification,
file inventory, measured summary and resolved failures. No acceptance tolerance
was relaxed. The trim default bug was fixed in shared conversion, not hidden
by a comparison tolerance. An intermediate validation link ran before the new
transition-test source existed; the final complete matrix replaces that attempt.
The first Xcode build and AU runner required system cache/registrar access
beyond the execution sandbox. These restrictions were resolved without vendor
changes, plugin installation or AU-cache clearing.

## Reproduce

From the repository root on macOS with Xcode installed:

```sh
bash HoldsworthEngine/references/post-amp-eq-v1/validate-engine.sh
bash HoldsworthEngine/references/post-amp-eq-v1/validate-source.sh
python3 HoldsworthEngine/references/post-amp-eq-v1/source-audit.py
bash HoldsworthEngine/references/post-amp-eq-v1/package-products.sh
python3 HoldsworthEngine/references/post-amp-eq-v1/compile-hosts.py
python3 HoldsworthEngine/references/post-amp-eq-v1/run-host.py
# Finish build/test/host jobs before profiling:
bash HoldsworthEngine/references/post-amp-eq-v1/profile-cpu.sh
python3 HoldsworthEngine/references/post-amp-eq-v1/record-engine-results.py
```

Default product/build root is `/private/tmp/nam-post-amp-eq-v1`, overridable with
`EQ_BUILD`. Release products are arm64, deployment target macOS 12.0, ad-hoc
signed and verified. They are not installed into system/user plugin folders.
The expanded DevelopmentPanel is 1100 x 1032 logical pixels; existing controls
keep their coordinates and the EQ card occupies the bottom. Standalone audible
acceptance has passed; detailed visual inspection remains the user's
responsibility, and manual VST3/AU checks are deferred to release QA.
