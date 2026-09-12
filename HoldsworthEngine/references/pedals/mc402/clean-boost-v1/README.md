# MC402 CLEAN BOOST V1 — production processor and development M2

Accepted profile: **`MC402-CLEAN-BOOST-V1`**. Implemented 2026-09-11.

This profile models only a flat clean Boost, **0 to +20 dB**, before NAM.
There is no modeled clipping at normalized ±1, invented EQ, sag/noise,
Overdrive state, resampling, FIR or added latency. The product decision does
not establish which section Allan used: the historical record supports the
MC402 pedal's inclusion, not a Boost-only historical claim.

The original provisional Overdrive was **rejected for product integration**:
insufficient evidence for further speculation, and failure of the approved
fidelity envelope even with ideal resampling. V1 quadratic/R2 C² profiles and
all their numerical findings remain historical experiments. Overdrive is
deferred pending stronger circuit evidence or hardware measurement. See the
[source archive](../rejected-overdrive/README.md) and [research index](../README.md).

## Production API and exact behavior

[MC402CleanBoostProcessor](../../../../dsp/MC402CleanBoostProcessor.h)
is a concrete mono `double` processor with:

- `prepare(sampleRate, maximumBlockSize)` — accepts finite 8–768 kHz and a
  positive maximum block size; lifecycle validation may throw.
- `setBoostDb(boostDb)` — thread-safe atomic target publication. Finite values
  clamp to `[0,20]`; nonfinite controls select the 0 dB default.
- `processBlock(span<const double>, span<double>) noexcept` — exact in-place
  or separate equal-length buffers, allocation-free and lock-free.
- `reset() noexcept` — snaps to the latest requested gain, with no signal tail.
- `latencySamples()` — always **0**; readiness/block-limit accessors support
  the existing wrapper's lifecycle checks.

Settled mapping is exactly `y = x * 10^(boostDb/20)`. Default and double-click
reset are **0 dB**, gain **1**; +10 dB is **sqrt(10)** and +20 dB is **10**.
Finite settled 0 dB samples are copied exactly, including signed zero and
subnormals when the host does not flush them. Outputs may exceed ±1.

The setter computes the target gain outside the sample loop and publishes one
lock-free atomic double. The audio thread loads it once per block; a changed
target starts a linear-amplitude ramp from the current gain over
`ceil(0.010 * sampleRate)` samples. The final sample snaps exactly to target.
Repeated targets do not restart a ramp. Startup/reselection/reset use the
requested settled gain. Parameter changes are block-boundary handoffs, not
sample-accurate host automation.

Nonfinite audio becomes zero. Multiplication that would overflow `double`
saturates at signed `double::max`; this is numerical protection, not a modeled
pedal clip or a ±1 limiter. Invalid/unprepared/oversized buffer calls assert
in Debug and copy the available input in Release where possible. Exact
in-place buffers are supported; arbitrary partial overlap is not supported.
`prepare/reset` require ordinary lifecycle synchronization with processing.

## Single pre-NAM selector and calibration

[DevelopmentPreNAMSelector](../../../../integration/DevelopmentPreNAMSelector.h)
selects **Off / TC BLD / MC402 Boost** once per callback. It checks readiness
and maximum block size before choosing a processor, falling back to Off if
the requested processor is unavailable. It never calls both processors.
TC retains its reset-on-engage/disengage behavior and its original controls,
coefficient handoff and DSP source. MC402 is reset to its requested gain on
selection changes. No parallel crossfade, additional buffer, dynamic latency
or host-latency policy was introduced.

The insertion point and sequence are unchanged:

`input mono preparation / trim / volts bridge -> selected pedal -> NAM normalization -> gate trigger -> NAM -> existing downstream processing`.

When active input calibration and model metadata apply, the existing bridge
uses `F(C)=sqrt(2)*0.775*10^(C/20)` peak volts. Host-to-pedal gain is input trim
times `F(hostCalibration)`; pedal output is divided by `F(modelCalibration)`.
Without applicable calibration (including no model or absent input-level
metadata), it retains the existing convention: one post-trim sample unit is
one provisional volt and the post-pedal scalar is 1. The wrapper's existing
calibration and model-staging timing are retained. No calibration or NAM/cab
behavior was redesigned.

Off preserves the old stock `mInputGain` arithmetic and invokes neither pedal.
The isolated Boost's 0 dB transfer is bit-exact; the active calibrated bridge
still performs its existing two floating-point scalar operations, so whole-
plugin Off/active-wire equivalence is numerical rather than guaranteed bit-exact.
Tests compare the selected TC path with the previous scalar/TC/scalar sequence
bit-for-bit at all six supported validation rates.

## Development UI v2

The existing Boost / Drive card now has a compact **OFF | TC BLD | MC402**
selector. Startup is Off. TC BLD reveals the existing Gain/Bass/Treble sliders
and Focus/Full behavior. MC402 reveals only one **Boost** slider, 0–20 dB,
with the existing track/handle/drag language and double-click-to-default behavior.
Off hides both sets. Control values survive switching processors and editor
reopening. Development controls remain nonserialized.

Panel hierarchy, 1100 × 660 editor dimensions, card rectangles, Amp/Cab module,
Delay controls/preset indices, and NAM settings remain unchanged. Existing NAM
message IDs are preserved; the new Boost message is appended. There are no
fake/disabled Gain/Tone/Output controls for the rejected Overdrive.

## Validation and artifacts

- Eight MC402 production tests: identity/latency, +10/+20 mapping, in-place,
  10 ms smoothing, reset/reprepare, block partitions, finite/extreme input,
  control sanitization, allocation tracking and concurrent handoff/lifecycle.
- Four selector tests: exclusive routing, before-gate/NAM observation, invalid/
  unprepared/oversized fallback, calibrated/fallback volts, bit-exact TC path
  preservation, and no processing/reset allocations.
- Full Xcode Debug: **244/244 pass**.
- Full Xcode Release: **244/244 pass**.
- Full ASan + UBSan: **244/244 pass**, no diagnostics (platform LeakSanitizer
  unsupported; `detect_leaks=0`).
- ThreadSanitizer: concurrent MC402 control-handoff test passes.
- Strict warnings on new DSP/tests and Clang static analysis pass.
- APP, VST3 and AU: Debug arm64 builds pass with isolated products in `/tmp`.
- Xcode project lint, source enrollment, preserved-source/data hashes and
  `git diff --check` pass. No rejected Overdrive test is a product regression.
- Archived V1 bridge and R2 experiment still compile in an exported historical
  layout. **No Overdrive numerical/alias experiments were rerun or extended.**

New code uses the project's ordinary floating-point build settings; TC BLD and
Yamaha regression expectations were not altered. The initial attempt to impose
strict warnings on every legacy source encountered existing missing-field and
shadow warnings; strict checks were scoped to the new files. Xcode initially
failed before compilation on sandbox module-cache access; authorized developer-
service access resolved that. Build products stayed isolated, and a temporary
no-op replaced the build-invocation AU cache cleanup script without project or
user-cache changes.

See [validation.json](validation.json), [changes.json](changes.json), and the
retained [test/build log summaries](logs/README.md). Reproduction commands are
in [validate.sh](validate.sh); historical research reproduction is separate.

These validation and change-inventory records describe the implementation
checkpoint. The later [repository hygiene audit](../repository-hygiene.md)
removes reproducible bulk outputs and prunes machine/build metadata from hash
manifests. The current preservation audit checks 323 durable files against
their original hashes, plus all eight archived source snapshots. Historical
538-file validation claims have not been rewritten as new measurements.
All 232 pre-existing tests remain: 245 = 232 + 13 provisional MC402 tests;
244 = 232 + 8 clean-Boost tests + 4 selector tests.

The later clean ARM64 Release APP build uses `-O3`, `RELEASE=1`, `NDEBUG=1`
and was launched from `/tmp/mc402-boost-v1-release/products/NeuralAmpModeler.app`.
The original temporary APP was Debug (`-O0`). No source behavior changed;
the installed application was untouched. Release audio audition remains
necessary to determine whether optimization resolves the reported IR crackle.

## Manual inspection and audition

The newly built standalone at
`/tmp/mc402-boost-v1/products/NeuralAmpModeler.app` was launched and its running
executable path verified. Installed plugins were not replaced. Visual
inspection remains manual; no screenshots or UI automation were performed.

Check selector highlighting and control visibility, TC control/range retention,
MC402 dB readout and double-click reset, editor reopening, and the unchanged
card/Amp/Cab/Delay layout. Audition Boost movement and level changes into your
chosen NAM, with and without calibration. Processor switching is immediate at
block boundaries and may produce a level discontinuity; this work does not
claim seamless selector switching. No sound tuning against that rig was done.

No commit was created. Only accepted Boost-only M2 development integration was
performed; the Overdrive rejection remains final for this product v1 scope.
