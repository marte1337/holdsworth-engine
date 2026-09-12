> **Historical Overdrive research — rejected for product integration.** The accepted
> M1c review ended development of these provisional nonlinear profiles: insufficient
> evidence plus failure of the approved fidelity envelope even with ideal resampling.
> Overdrive is deferred pending stronger circuit evidence or hardware measurement.
> Current product scope is [MC402-CLEAN-BOOST-V1](clean-boost-v1/README.md);
> rejected source and reproduction guidance are in the [archive](rejected-overdrive/README.md).
> References below to production, integration blockers or M2 status describe the
> historical milestone only. Its measurements do not describe the new clean Boost.

# MC402 milestones and validation

M0 approved; `MC402-BOUNDED-V1-PROVISIONAL` frozen. M1 implementation and
measurements are recorded in the [M1 report](m1/README.md). The alias gate fails,
so the M1 exit condition is not met and M2 has not started. Calibration, selector,
NAM-test-double routing and live-placement rows below belong to M2; they are
not part of the authorized isolated M1 work.

**M1c review result:** after the accepted M1b return to design review, the
offline `MC402-BOUNDED-V1-PROVISIONAL-R2` replaces only the quadratic knee
with the minimal C² quartic. [M1c measurements](m1/m1c/README.md) fail the
unchanged PRODUCT rules (strongest alias <= -70 dBc AND total identified
alias <= -80 dBFS sine-equivalent), even with ideal resampling and ADAA2 8x.
Decision D returns the broader provisional Overdrive to review. Original V1,
production DSP and the M2 boundary remain unchanged. The accepted M1b domain
is used for this experiment; historical torture remains a separate robustness
record, with no fidelity waiver for its rows that lie inside PRODUCT.

## Short sequence

| Milestone | Deliverable / exit condition |
| --- | --- |
| M0: evidence/design review | This package. Review the partial-trace reduction, section order, control endpoints, fallback voltage scale and MC402 latency/bypass policy. Approved; `MC402-BOUNDED-V1-PROVISIONAL` frozen. |
| M1: isolated DSP + focused tests | Concrete mono processor, private oversampling, deterministic controls; inspect frequency/level curves, endpoint renders and alias convergence. Record actual coefficients, FIR lengths, latency and numerical errors. No live integration until these pass. |
| M2: pre-NAM development audition | One selector at the existing insertion point, existing TC path preserved, correct voltage bridge/gate placement and latency reporting; audition Boost, Overdrive and both with multiple input levels and whatever NAM/IR baseline is independently being tried. |
| M3: optional evidence-driven refinement | Change only what listening or later healthy-unit measurements justify: Tone, Gain endpoint/taper, stage headroom/recovery, order verification or seamless processor selection. No automatic expansion into a forensic project. |

M1 may tune the **labeled provisional** numerical choices against isolated
renders/listening, recording the resulting profile. A change to the topology,
order or fidelity scope returns to design review. Do not silently replace this
proposal with a Tube Screamer or Black Cat clone.

## Focused acceptance plan

Use `HoldsworthEngine/tests/TestHarness.h` / `TestMain.cpp` allocation tracking
and the style of `TCBLDCleanBoostProcessorTests.cpp`. Existing
`HoldsworthDelayLiveIntegrationTests.cpp` tests integration helpers rather than
the full plugin callback; it is not already a selector/NAM-order regression.
Add focused routing coverage when M2 actually implements the selection.

| Test | Proposed acceptance |
| --- | --- |
| Bypass | Off is bit-exact legacy output at unchanged timing. Selected MC402 both-off is a pure integer-L delay, bit-exact after alignment, in place and separate buffers; 0 dB Boost does not add arithmetic error. Test transitions separately from settled bypass. |
| Boost | Settled gains 1, sqrt(10), 10 at 0/10/20 dB to <=1e-12 relative error for ordinary finite test values; flat response. No unexpected normalized clipping. Output/Gain/Tone have no effect in Boost-only mode. |
| Overdrive endpoints | All 8 Gain/Tone/Output corners plus midpoint. Gain zero approaches the documented provisional mute endpoint; Output zero becomes exactly silent after smoothing; clockwise Tone increases high/low spectral ratio. Increasing Gain moves second-stage saturation earlier; do not require total THD or loudness to be monotonic for every complex signal. |
| Stage behavior | Small-signal transfer follows the approved linear reduction; isolated-stage saturation caps at H, unity below the knee, continuous slope at joins. Verify first-stage clipping can remain when Gain is reduced. Test cleanup using input-amplitude sweeps. |
| Order / simultaneous sections | At settled controls, both-on output equals Boost applied after the Overdrive-only reference. Boost must not alter its internal clipping. Test all four section states and selection exclusivity using call counts and a nonlinear NAM test double. |
| Finite/stable | Silence, impulse, step/DC, tones, multitone and seeded bursts at all control corners. Test 30 s stress renders, amplitudes 0–10 provisional V, isolated huge finite/NaN/Inf samples, denormals and repeated reset. No runaway state; engaged paths recover to silence and finite values. |
| Block partitions | Same stream bit-exact for 1, 7, 64, maximum and irregular partitions, including odd sizes and in-place operation. Repeat with identical sample-timed target/switch events. A block-boundary UI change is not promised sample-accurate automation. |
| Realtime | No allocations in processing/reset/control adoption/fades after prepare. Inspect locks, atomics, logging, exceptions and bounded work; allocation tracking alone is insufficient. Check small-block CPU at the worst factor. |
| Sample rates | 44.1, 48, 88.2, 96, 176.4, 192 kHz; explicit unsupported-rate policy. Stable endpoints, consistent time constants, correct L and prepare/reset behavior. Target small-signal magnitude agreement within 0.5 dB over 80 Hz–8 kHz after accounting for the intended analog response. |
| Calibration | Active calibration, disabled calibration, missing metadata and no model. Verify ideal-wire scaling, both-off stock arithmetic, fallback convention and expected shift in clipping with input trim; correct mono sum/average semantics. Test model replacement across a block boundary so pre-pedal/post-pedal scales describe one model snapshot. |
| Live placement | Exactly one selected effect before gate trigger and NAM. Gate enabled/disabled, model loaded/unloaded/replaced, prepare failure/oversized block and lifecycle selector changes. Existing tone/cab/DC/delay/output behavior unchanged. |

## Aliasing and oversampling gate

**M1b review distinction:** [the product-envelope proposal](m1/m1b/README.md)
separates intended guitar/pedal fidelity from numerical torture. The full-grid
requirements below describe the historical M0/M1 gate. M1b proposes retaining
-70 dBc inside an explicit voltage/frequency domain, adding absolute alias
information, and judging out-of-scope extremes for stability and gross artifacts.
Those proposed bounds are review material; they do not silently amend the
frozen DSP profile, qualify a factor, or authorize M2. The historical results
and their measured failures remain available unchanged.

Use coherent 100 Hz, 1 kHz, 5 kHz and near-top-band sines plus two-tone and swept
inputs, at low/medium/full Gain, Tone extremes and amplitudes across both knees.
Do not let a dark Tone setting conceal poor alias rejection. Analyze the pedal
alone before NAM/cab; use Boost off for comparison, then confirm scalar scaling.

Compare 1x/2x/4x/8x with an offline 32x reference using the same approved transfer
and a high-quality reference low-pass. First check 16x/32x convergence (64x only
if needed). Align known delay, discard startup/fade transients, compensate known
linear resampler response when interpreting residuals, and distinguish aliased
components from intended in-band harmonics. Reference mismatch alone is not an
alias measurement.

**V numerical target:** input/output-referred resampler passband ripple <=0.1 dB
over 20 Hz–10 kHz; image/stopband rejection >=80 dB; strongest identified in-band
alias <=-70 dBc relative to the output fundamental for single-tone stress cases
(use total output RMS as reference for multi-tone cases). Report FFT lengths,
windows, stimulus amplitude, rate, factor, worst frequency and alias level.
These are engineering thresholds, not the pedal's measured noise floor.

If 4x fails, first improve the private anti-alias filters or raise the factor;
do not weaken the criterion or retune the sound to mask aliasing. If the <=1 ms
latency target and alias criterion cannot both be met, bring that measured
tradeoff to review before M2. No outcome is claimed by this documentation task.

## Later measurements, only if justified

None are performed or required now. A short future healthy-unit check could
settle internal order, Gain-zero behavior, Tone endpoints and input/output level
sweeps at the documented supply. Then investigate rails, revision differences,
source/load interaction or overload recovery only if the model mismatch warrants
it. Knob settings and usage from Holdsworth are historical questions, not tuning
targets to invent.

## Historical M0 validation scope

The original M0 change was documentation only: check local links, evidence/assumption consistency, file scope
and `git diff --check` (including new files). M0 did not require DSP builds or the expensive
engine matrix. M1/M2 DSP milestones follow AGENTS.md's applicable Debug,
Release, sanitizer, strict-warning, analyzer and plugin-build requirements;
focused tests here do not waive the required checks before major DSP milestones.
