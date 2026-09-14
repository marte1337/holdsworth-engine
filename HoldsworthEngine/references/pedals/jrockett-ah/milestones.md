# Milestones and stopping rules

M0 and M1 are approved. M2 live integration is implemented with the same complete-EQ
crossfade behavior. See the [M2 report](m2/README.md) for tests, official rate
support and the Release standalone. Actual audio/UI audition remains manual.

| Milestone | Deliverable and exit condition |
| --- | --- |
| M0 research/design freeze | Review these four documents and choose whether the explicitly provisional Boost scope is useful. Freeze one profile ID, curves/range, Drive deferral and claim limits. Approved and frozen as `JROCKETT-AH-BOOST-BEHAVIORAL-V1`, with the user's 10 ms complete-response crossfade correction |
| M1 isolated DSP | Isolated Boost and focused tests implemented; see [M1 report](m1/README.md) for measured responses and validation. No live integration |
| M2 live integration/audition | Add J. Rockett AH to the exclusive selector; show Boost controls and scope. Preserve TC/MC402/Off, calibration, gate/NAM order and downstream regressions. Build/launch standalone; user manually inspects UI and auditions the musical value |
| M3 measurement/refinement, optional | Only if audition exposes a specific deficiency or a healthy unit/finished verified trace becomes available. Measure the smallest set that answers it; revise claims/profile only to match evidence |

## Acceptance appropriate to the model

Historical M0 validation: local links, source/assumption separation, exact profile table,
file scope and whitespace including new files. No DSP builds/tests are needed
for this documentation-only change. Downloads/renders stay in `/tmp` and are
not deliverables or dependencies of the package.

M1: check all six settled curves against the stated analytic targets, gain
endpoints, finite/stable output, silence recovery, DC/impulse behavior and
in-place processing. At 44.1/48/88.2/96/176.4/192 kHz, require agreement with
the defined discrete reference to 0.05 dB over 20 Hz–10 kHz. That is numerical
implementation accuracy, not hardware accuracy. Verify scale invariance at
ordinary finite amplitudes, no ±1 clipping, and no generated steady-state
harmonics above numerical error. Level changes preserve response shape.

Exercise mode changes during sustained notes and repeated changes during fades,
reset/reprepare, invalid controls/audio, oversized buffers, coherent control
handoff, bounded CPU and no processing allocations/locks. Check deterministic
block partitions for identical sample-timed events; UI adoption is still at
block boundaries. No nonlinear alias-convergence project is warranted for this
linear design; inspect switching transients and modulation artifacts directly.

M2: selector call-count tests prove at most one pedal runs; Off/TC/MC402 preserve
existing behavior and unavailable AH falls back to Off. Verify four-choice UI
mapping, bridge with/without metadata/calibration/model, mono preparation and
pre-gate/pre-NAM placement using a nonlinear NAM test double. Audition all six
combinations at matched output loudness for comparison, then at unchanged Boost
level to hear their differing NAM excitation. Use a clean and a driven NAM,
low/high DI levels, single notes and chords. Do not bake comparison loudness
compensation into the processor. Retain only changes that improve useful tone
choice; no claim that audition measures the original pedal.

M1/M2 DSP milestones follow [AGENTS.md](../../../../AGENTS.md): applicable full
Debug/Release tests, ASan/UBSan, strict warnings, static analysis, APP/VST3/AU
builds, project lint and diff checks. Preserve TC reference data and existing
Yamaha bit-exact regressions. Standalone visual inspection is manual per repo
guidance; no automated screen capture is part of this task.

## Optional measurements, bounded by a question

For Boost, one healthy unit at documented 9 V is enough to improve this
proposal: record revision, calibrated terminal levels, source/load impedances;
take six small-signal sweeps at one level and a short Boost knob sweep, then
repeat selected states at higher amplitude. This answers mode/emphasis curves,
range/taper, level-dependent EQ and the useful linear operating envelope.
Use low enough initial excitation to avoid clipping and compare harmonics.
Do not assume a digital recording's amplitude is pedal-input volts.

Only if Drive becomes desirable: add Bass/Treble endpoint/midpoint sweeps at low
Gain, then Gain and input-level sweeps with Volume adjusted to avoid interface
clipping. Compare two Gain/Volume pairs with similar output level for distortion
and spectral differences. Check both-on versus Boost-only/Drive-only and retain
withheld settings for validation. This can support a bounded measured behavioral
model without reconstructing every component. A complete trustworthy AH trace
is an alternative starting point, but a related pedal's trace is not.

**Stop rule:** no automatic M3, exhaustive PCB reconstruction, revision survey,
inferred Allan settings or nonlinear antialias optimization. If the proposed
Boost profiles lack useful musical distinction, revise the small EQ profile
once on audition evidence or defer the processor. If Drive still needs an
unanchored transfer, leave it deferred. Hardware acquisition or contacting
others is not part of this task.
