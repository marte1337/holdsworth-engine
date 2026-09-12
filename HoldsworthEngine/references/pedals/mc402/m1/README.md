> **Historical Overdrive research — rejected for product integration.** The accepted
> M1c review ended development of these provisional nonlinear profiles: insufficient
> evidence plus failure of the approved fidelity envelope even with ideal resampling.
> Overdrive is deferred pending stronger circuit evidence or hardware measurement.
> Current product scope is [MC402-CLEAN-BOOST-V1](../clean-boost-v1/README.md);
> rejected source and reproduction guidance are in the [archive](../rejected-overdrive/README.md).
> References below to production, integration blockers or M2 status describe the
> historical milestone only. Its measurements do not describe the new clean Boost.

# MC402 isolated M1 implementation and measurements

2026-09-10. Base repository: `fa6c0f8293fc832cac98338f3b05dd8ac2065e53`.
Frozen profile: **`MC402-BOUNDED-V1-PROVISIONAL`**.

**Implementation and functional checks are complete; the M1 alias acceptance gate
fails. No oversampling factor is qualified for M2.** The reviewed starting
factors remain available for isolated evaluation only. No live integration or
commit was made. The Gain-minimum mute endpoint is a regression of this named
provisional profile, **not a hardware-verified MC402 fact**.

M1a follow-up: [focused alias diagnosis and ADAA experiments](m1a/README.md).
The measurement audit confirms the failures; no production solution is selected.

M1b review follow-up: [PRODUCT fidelity versus TORTURE robustness](m1b/README.md).
The original full stress-grid alias result above remains historical evidence;
it is not automatically the fidelity requirement for extreme overload in the
new envelope proposal. M1b retains -70 dBc for its proposed PRODUCT domain and
reports absolute/aggregate alias levels separately. Production remains frozen
and M2 remains deferred; M1b is an offline review, not integration approval.

M1b review accepted the return to design review. The authorized
[M1c minimal saturation-knee experiment](m1c/README.md) defines a separate
offline profile, **`MC402-BOUNDED-V1-PROVISIONAL-R2`**. Its minimal C² quartic
preserves the 0.9/1.1 anchors and every other model constant, but ADAA2 4x/8x
still fail the unchanged product gate with both existing FIR and ideal
resampling. **M1c decision D: reconsider the broader provisional Overdrive
model.** R2 is not adopted; production V1 and all M1 historical measurements
remain unchanged. M2 has not started.

## Files

Paths below are relative to the repository root.

| Files | Change |
| --- | --- |
| `HoldsworthEngine/dsp/MC402BoostOverdriveProcessor.h`, `.cpp` | New concrete mono, volts-domain processor; private state, resampling and transitions. |
| `HoldsworthEngine/dsp/MC402ProvisionalProfile.h` | New single named home for provisional DSP values, time constants, supported rates and starting factors. |
| `HoldsworthEngine/dsp/MC402HalfBandCoefficients.h` | New generated, full-precision private FIR arrays; generator and filter specifications identified in the header. |
| `HoldsworthEngine/tests/MC402BoostOverdriveProcessorTests.cpp`, `MC402TestAccess.h` | New 13-test suite and offline-only friend access. |
| `HoldsworthEngine/tests/TestHarness.h`, `TestMain.cpp` | Register the suite; optional test-name substring permits focused execution. |
| `NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj` | File references and enrollment in **HoldsworthEngineTests only**. No MC402 source enrollment in APP/VST3/AU. |
| `HoldsworthEngine/references/pedals/mc402/README.md`, `dsp-design.md`, `milestones.md` | Record M0 approval, profile freeze and current M1 result. Approved transfer and gate retained. `sources.md` remains the M0 evidence register. |
| This `m1/` directory | Report, measured JSON data, figure and reproducible offline tools listed below. |

Artifacts: [numerical-report.json](numerical-report.json),
[behavior-report.json](behavior-report.json),
[benchmark-report.json](benchmark-report.json),
[validation-summary.json](validation-summary.json),
[measurements.png](measurements.png).

Tools: [FIR generator](tools/generate_fir.py),
[compiled DSP bridge](tools/MC402OfflineBridge.cpp),
[numerical/alias validation](tools/validate_numerics.py),
[endpoint/level measurements](tools/measure_behavior.py),
[CPU benchmark](tools/MC402Benchmark.cpp),
[figure generator](tools/plot_results.py), [Python versions](tools/requirements.txt).

## Exact implemented profile

All sound-defining values are in
[MC402ProvisionalProfile.h](../rejected-overdrive/dsp/MC402ProvisionalProfile.h).
This is an unmeasured behavioral reduction, not an OP275 device model.

| Quantity | Exact v1 value / expression |
| --- | --- |
| Input resistance / capacitance | `22000.0` ohm / `47.0e-9` F |
| Feedback resistance | `470000.0` ohm |
| A1 and provisional A2 | `-470000.0 / 22000.0` = approximately -21.36363636363636 each |
| Input high-pass | `1 / (2*pi*22000*47e-9)` = 153.92160840608835 Hz |
| Interstage / output high-pass | `154.0` / `10.0` Hz |
| Stage saturation swings | `H1 = H2 = 2.5` provisional V |
| Normalized knee start / end / width / quadratic coefficient | `0.9`, `1.1`, `0.2`, `0.1` |
| Gain and Output taper exponent | `3.321928094887362`; attenuation is `position^exponent` |
| Tone | `500.0 * 16.0^position` Hz: 500 / 2000 / 8000 Hz |
| Boost | `10^(dB/20)`, clamped control range 0–20 dB |
| Defaults | Both sections off, Boost 0 dB, Gain/Tone/Output `0.5` |
| Continuous control smoothing | Linear ramps of gain, attenuation and TPT coefficient over `ceil(0.010 * rate)` samples; OD controls use internal rate, Boost uses host rate |
| OD section crossfade | Linear aligned dry/wet, `ceil(0.005 * hostRate)` samples, after `2*L` host-sample FIR warmup when enabling dormant OD |
| Boost switch transition | Same 10 ms gain ramp to/from unity; no second nonlinear path |
| Supported host rates | 44100, 48000, 88200, 96000, 176400, 192000 Hz; other rates and zero maximum block size throw during lifecycle `prepare()` |
| Starting factors, **not alias-qualified** | `{4, 4, 2, 2, 1, 1}` in the above rate order |
| FIR lengths by successive 2x stage | `{65, 33, 33, 33, 33, 65}`; stages beyond the starting factor exist only for offline diagnostics through 64x |
| Fixed capacities | 6 stages, 65 maximum FIR taps, 64 host samples of dry-delay storage |
| Numerical guards, not analog behavior | Nonfinite input becomes zero; OD state-driving input limited to ±`1e100`; IIR state below `1e-300` becomes zero; Boost multiplication saturates only at representable `double` overflow |

For `q = abs(z)/H`, the stage returns `z` for `q <= 0.9`,
`sign(z)*H` for `q >= 1.1`, otherwise
`sign(z)*H*(0.9 + 0.2*u - 0.1*u*u)`, `u=(q-0.9)/0.2`.
Both joins have continuous first derivative. No clipping or gain normalization
was added to Boost. Individual stage limits do not imply a hard output cap after
filter transients and Boost.

Signal order is: upsample, input HP, A1/saturation, Gain attenuation,
interstage HP, A2/saturation, Tone LP, output HP, Output attenuation,
downsample, Boost. All four section states work independently. No sound
parameters were tuned against a NAM, IR, Holdsworth preset or current guitar rig.

## Actual oversampler and latency

Private cascaded 2x, symmetric linear-phase equiripple half-band FIRs, with
zero insertion and 2x interpolation scaling, even-phase decimation, double
accumulation, exact zero taps and symmetric pair evaluation. Every stage has
separate up/down histories. The first pair uses 65 taps; subsequent pairs use
33 taps, with a final 65-tap pair for the offline 64x diagnostic so total host
latency stays integral. There is no IIR resampler or borrowed vendor oversampler.

The generator uses SciPy `remez`, `fs=1`, `maxiter=200`, `grid_density=64`,
equal pass/stop weights, exact half-band zeros, symmetric coefficients,
center 0.5 and odd-tap DC normalization. Frequency edges below are cycles per
sample at that stage's higher rate. Full coefficients are checked into the FIR
header and were checked against the generator.

| Bank | Pass / stop edges | Maximum pass error | Worst stop level |
| --- | --- | ---: | ---: |
| 65 taps | 0–0.20 / 0.30–0.50 | 0.000102366 dB | -98.573120 dB |
| 33 taps | 0–0.15 / 0.35–0.50 | 0.000072511 dB | -101.568225 dB |

Measured round-trip 20 Hz–10 kHz ripple across all six rates and all seven
factors is at most **0.000471337 dB**, below 0.1 dB. Both FIR banks exceed the
80 dB image/stopband rejection requirement. These linear-filter checks do not
establish rejection of aliases already created inside the nonlinear core.

`L = sum((Nstage - 1)/2^stage)` host samples includes the up and down filters.
The actual compiled wire-core impulse peak, symmetry and delayed-copy path
confirm the following values; these are not guessed from the factor.

| Host rate | Evaluated default factor | Measured L | Milliseconds | Max small-signal error |
| ---: | ---: | ---: | ---: | ---: |
| 44100 | 4x | 40 | 0.907029 | 0.058527 dB |
| 48000 | 4x | 40 | 0.833333 | 0.049242 dB |
| 88200 | 2x | 32 | 0.362812 | 0.058341 dB |
| 96000 | 2x | 32 | 0.333333 | 0.049188 dB |
| 176400 | 1x | 0 | 0 | 0.058546 dB |
| 192000 | 1x | 0 | 0 | 0.049363 dB |

All section combinations retain this L. Both-off and settled 0 dB Boost preserve
finite sample bits after alignment, including signed zero. OD's intentional IIR
phase is separate from the host latency. Forced 1/2/4/8/16/32/64x measurements
are respectively **0/32/40/44/46/47/48 samples** at every host rate. At 44.1 kHz,
8x is 0.997732 ms; 16x is already 1.043084 ms and 64x is 1.088435 ms.

## Aliasing gate: FAIL

The compiled processor was measured independently of any NAM/cabinet. For each
rate/factor: 150 single-tone cases, nearest odd coherent FFT bins to 100, 1000,
5000, 9500 and 19000 Hz; input peaks 0.005, 0.02, 0.12, 0.5 and 10 provisional V;
Gain 0.1/0.5/1; Tone 0/1; Output 1; Boost off. **6300 single-tone cases** total.

Host FFT length is 8192, rectangular window, with at least 0.5 seconds of
whole-period warmup before one analysis period. The 20 Hz–20 kHz mask excludes
DC and intended, unfolded harmonics. The strongest remaining bin is reported
relative to the measured output fundamental. Positive dBc means the alias is
stronger than that fundamental. Both Tone extremes are included; no output
low-pass was added to conceal folding. Exact winning stimuli and alias
frequencies for every row are in the JSON.

Worst identified single-tone alias in dBc; required **<= -70 dBc**:

| Hz | 1x | 2x | 4x | 8x | 16x | 32x | 64x |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 44100 | +20.59 | -3.32 | -5.19 | -14.32 | -14.65 | -26.46 | -26.46 |
| 48000 | +18.18 | +11.93 | -13.74 | -13.96 | -21.72 | -22.42 | -22.42 |
| 88200 | -3.78 | -6.46 | -13.05 | -13.85 | -24.12 | -24.12 | -24.12 |
| 96000 | +11.72 | -11.55 | -17.63 | -17.68 | -21.31 | -37.60 | -38.86 |
| 176400 | -7.67 | -12.34 | -12.40 | -31.35 | -31.36 | -31.36 | -42.25 |
| 192000 | -8.92 | -9.14 | -18.62 | -23.87 | -37.28 | -37.72 | -39.05 |

Examples at Gain 1, Tone 0, Output 1, 10 V input peak:

- 44.1 kHz / 4x: 18997.668457 Hz input, 48.449707 Hz alias,
  **-5.189137 dBc**; output fundamental peak 0.079833614 V.
- 44.1 kHz / 8x: 9501.525879 Hz input, 1243.542480 Hz alias,
  **-14.320719 dBc**; fundamental peak 0.166879156 V.
- 48 kHz / 8x: 19001.953125 Hz input, 802.734375 Hz alias,
  **-13.957664 dBc**; fundamental peak 0.083054473 V.

Two-tone validation adds **756 cases**: coherent tones at 5 and 23 times a
common odd FFT-bin base near 200 Hz, each tone peak 0.005/0.12/5 V, all three
Gain settings and both Tone extremes. Intended intermodulation lies on multiples
of the common base; other in-band bins identify folding. Alias sinusoid RMS is
compared with total output RMS, using the same warmup and FFT length.

| Hz | 1x | 2x | 4x | 8x | 16x | 32x | 64x |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 44100 | -17.50 | -20.64 | -29.95 | -42.19 | -42.19 | -49.45 | -61.90 |
| 48000 | -24.14 | -24.42 | -36.09 | -36.05 | -48.54 | -48.55 | -63.31 |
| 88200 | -24.83 | -25.11 | -41.85 | -46.58 | -49.42 | -66.82 | -72.26 |
| 96000 | -26.51 | -34.01 | -34.18 | -45.07 | -50.73 | -58.90 | -70.18 |
| 176400 | -36.69 | -43.26 | -49.93 | -50.42 | -58.23 | -66.87 | -82.81 |
| 192000 | -35.61 | -37.99 | -48.40 | -50.72 | -58.63 | -72.62 | -82.95 |

The separate reference directly evaluates the same core at 16/32/64 times the
host rate, feeding an ideal physical sine and projecting the FFT onto the audio
band (ideal brick-wall reference low-pass). It therefore removes production-FIR
uncertainty. The per-rate worst 8x stimuli still give substantial nonharmonic
bins at 32x and 64x. Their 32-to-64 complex audio-band residuals range from
-22.57 to -37.56 dB; none meets the diagnostic -90 dBc reference-alias ceiling
(20 dB margin below the product gate). **Neither 32x nor 64x is a converged,
alias-free reference for this stress envelope.** These residuals include BLT
phase/magnitude differences and are not called alias measurements.

The JSON also compares production 1x/2x/4x/8x with both direct 32x and 64x
references after dividing by each factor's measured **complex wire response**
over 20 Hz–20 kHz. This removes known linear resampler magnitude and latency
from those residuals. It cannot invert nonlinear interaction with the input
filter, and it does not turn the unqualified references into alias-free truth.

Additional two-tone and logarithmic-sweep comparisons at 44.1/48 kHz align the
known FIR delay and discard the first 8192 samples. They are retained as
**convergence residuals only**, including linear resampler/BLT differences, not
substituted for the harmonic-mask alias test. The independent single-tone bins
already establish failure without relying on an unconverged reference.

No factor is selected as passing. The reviewed starting map is retained rather
than presenting a higher failing factor as a solution. Increasing FIR rejection
alone cannot remove folding already present in the directly sampled nonlinear
core. The transfer, knee and test criterion were not weakened or retuned.

## Small signal and control endpoints

The linear reference is independently evaluated from the approved analog
HP/HP/LP/HP cascade and stage gains. A 1e-5 V impulse (32768 samples) remains
below both knees. At all supported rates and Tone 0/0.5/1, maximum magnitude
error over 80 Hz–8 kHz is **0.058546071 dB**, against a 0.5 dB gate.
The actual resampler response is included in these errors, not divided away.
The figure and JSON include response points and a 41-amplitude level sweep at
48 kHz, 1 kHz, Gain 0.1/0.5/1, Tone/Output 1, 1e-5–10 V peaks.

Gain 0 gives exact silence when initialized there; changing to it permits
coupling/filter state to decay to near silence. That behavior is explicitly
provisional. Reduced Gain still allows first-stage clipping; increasing Gain
moves the second-stage clipping onset earlier. The small-input doubling check
is linear, while higher inputs compress. Tone clockwise increases the measured
high-frequency output. Output 0 becomes exact silence after its ramp and FIR
tail; Output 0.5 is 0.1 linear attenuation and Output 1 is unity.

All 8 corners and midpoint are covered, supplemented by 216 endpoint renders
across all six rates (Gain 0/0.1/0.5/1, Tone 0/0.5/1, Output 0/0.5/1).
Every initialized Gain-zero and Output-zero render is exactly silent. Example:
48 kHz, 1 kHz sine, 0.12 V peak, Tone 0.5, Output 1 yields output RMS
0 / 0.015961497 / 1.958221453 / 2.064488912 V at those four Gain settings.
These are profile measurements, not MC402 hardware measurements.

Boost 0/10/20 dB meets 1 / sqrt(10) / 10 to the 1e-12 test tolerance, including
inputs beyond normalized ±1. OD controls have no effect in Boost-only mode;
both-on output equals the same Overdrive result multiplied by Boost afterward.

## Realtime, state and block behavior

There are no dynamic buffers in the class. Control targets, FIR histories and
dry delay have fixed storage. A single producer computes `pow`/`tan` outside
processing and publishes a complete snapshot through a three-slot SPSC buffer.
The consumer performs a bounded load/exchange at block entry, with no retries.
`std::atomic<uint32_t>::is_always_lock_free` is asserted. Producer-owned controls
are never read by the audio thread. Lifecycle `prepare()` is synchronized;
`setControls()` may run concurrently with processing/reset, from one producer.

Audio processing/reset/adoption/fades perform no allocation, lock, I/O, logging,
throw or transcendental evaluation. Allocation tracking records zero during
control updates, process/reset and transitions. Clang analysis is clean, and the
concurrent snapshot test passes ThreadSanitizer. The concurrency test covers
20000 producer updates plus unread-update coalescing and eventual newest state.

Identically sample-timed events produce bit-exact output for single-sample,
maximum/event-bounded and irregular partitions containing 1/7/64/odd sizes,
including exact in-place processing. Repeated reset/DC/burst renders are also
bit-exact at 256 versus 7 samples. Block-boundary control adoption does not
claim sample-accurate UI automation. Retargeted ramps continue from their current
value; publishing an unchanged target does not restart a ramp. Re-enabling during
fade-out preserves running history; a dormant enable clears and warms history.

Stress coverage includes 30 seconds of seeded ±10 V input per rate, cycling all
corners, plus silence, DC/step, impulse, sine, multitone bursts, huge finite
values, NaN/Inf, denormals and recovery. Prepared invalid block calls assert in
Debug; Release copies equal-sized spans without advancing state. Partial buffer
overlap is unsupported; exact in-place operation is supported.

The release CPU benchmark measures both sections on, Gain/Tone/Output 1,
Boost 20 dB, a precomputed 0.5 V sine, blocks 1/7/64, all six rates, starting
factors plus forced 8x/64x. On this arm64 macOS 26.3.1 machine, starting-factor
processing used **0.227–0.658%** of one audio-time budget; the worst measured 64x
case used **37.825%** (192 kHz, 64-sample block, 1970 ns/host sample).
These are aggregate wall-clock measurements of an isolated processor, not
scheduler guarantees or a live NAM performance budget. Full per-case data is
recorded; no benchmark instrumentation enters the processor.

## Validation run

- Full Xcode Debug: **245/245 pass**, including all 13 MC402 tests.
- Full Xcode Release: **245/245 pass**.
- ASan + UBSan, full suite: **245/245 pass**, no sanitizer diagnostics.
  LeakSanitizer is unsupported on this platform; `detect_leaks=0` was used.
- ThreadSanitizer: concurrent MC402 snapshot test passes, no race diagnostics.
- Strict Clang warnings on new processor/tests/offline C++: `-Wall -Wextra
  -Wpedantic -Wconversion -Wshadow -Werror`, pass.
- Clang static analyzer on processor and focused tests: no diagnostics.
- APP, VST3, AU Debug arm64 builds: pass. Products isolated in `/tmp`; no
  installation or launch. AU cache cleanup was replaced for this build invocation
  by a temporary no-op script, without changing the project or user caches.
- Xcode project plist lint, source enrollment checks, generated coefficient
  consistency, Python syntax/format, local report links and whitespace: pass.
- Offline FIR / small-signal gates: pass. Single-tone / two-tone / reference
  qualification: failures recorded above. Numerical gate tool returns **1**.

An initial manually compiled full-suite experiment disabled floating-point
contraction globally and changed an existing Yamaha 922 bit-exact fingerprint.
No Yamaha code or expected values were changed. Standard Xcode Debug/Release
and standard-contraction sanitizer builds pass all legacy regressions. The
isolated numerical bridge/benchmark deliberately use `-ffp-contract=off` and
`-fno-fast-math` for reproducible measurements; they do not alter live build flags.

## Reproduction

Run from the repository root with the full Xcode toolchain selected:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
MC402_BUILD=/tmp/mc402-reproduce
MC402_TOOLS=HoldsworthEngine/references/pedals/mc402/m1/tools
mkdir -p "$MC402_BUILD"
python3 -m venv "$MC402_BUILD/venv"
"$MC402_BUILD/venv/bin/pip" install -r "$MC402_TOOLS/requirements.txt"
xcrun clang++ -std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -dynamiclib \
  HoldsworthEngine/dsp/MC402BoostOverdriveProcessor.cpp \
  "$MC402_TOOLS/MC402OfflineBridge.cpp" -o "$MC402_BUILD/libmc402.dylib"
"$MC402_BUILD/venv/bin/python" "$MC402_TOOLS/validate_numerics.py" \
  --library "$MC402_BUILD/libmc402.dylib" --output "$MC402_BUILD/numerical-report.json"
```

The last command currently exits 1 because the gate fails; it still writes its
complete report. Continue the independent behavior and timing measurements:

```sh
"$MC402_BUILD/venv/bin/python" "$MC402_TOOLS/measure_behavior.py" \
  --library "$MC402_BUILD/libmc402.dylib" --output "$MC402_BUILD/behavior-report.json"
xcrun clang++ -std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off \
  HoldsworthEngine/dsp/MC402BoostOverdriveProcessor.cpp \
  "$MC402_TOOLS/MC402Benchmark.cpp" -o "$MC402_BUILD/benchmark"
"$MC402_BUILD/benchmark" > "$MC402_BUILD/benchmark-report.json"
MPLCONFIGDIR="$MC402_BUILD/matplotlib" "$MC402_BUILD/venv/bin/python" \
  "$MC402_TOOLS/plot_results.py" --directory "$MC402_BUILD"
xcodebuild -project NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj \
  -target HoldsworthEngineTests -configuration Debug \
  SYMROOT="$MC402_BUILD/debug" OBJROOT="$MC402_BUILD/debug-obj" \
  CLANG_MODULE_CACHE_PATH="$MC402_BUILD/modules" \
  ARCHS=arm64 ONLY_ACTIVE_ARCH=YES CODE_SIGNING_ALLOWED=NO build
"$MC402_BUILD/debug/HoldsworthEngineTests"
"$MC402_BUILD/debug/HoldsworthEngineTests" MC402
```

Use Release with separate output/object roots for the Release suite. Sanitizer
build command used `xcrun clang++ -std=c++20 -O1 -g
-fsanitize=address,undefined -fno-omit-frame-pointer HoldsworthEngine/dsp/*.cpp
HoldsworthEngine/tests/*Tests.cpp HoldsworthEngine/tests/TestMain.cpp`, then ran
with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.
ThreadSanitizer substituted `-fsanitize=thread` and filtered
`'concurrent coherent snapshots'`. Apple Clang 21.0.0
(`clang-2100.1.1.101`), Python 3.9.6, NumPy 2.0.2 and SciPy 1.13.1 were used.

## Deviations and review before M2

No deviation from the approved topology, controls, provisional values, order or
scope. Actual FIR choices and private higher-factor diagnostics realize the
reviewed oversampling study. The unmet alias exit condition is an explicit
acceptance failure, not an approved design deviation. The documented contingency
requires this measured tradeoff to return to review before M2.

Review the antialias strategy and latency/CPU budget together: no tested factor
through 64x satisfies the current full stress envelope, and the 44.1 kHz 1 ms
latency target is already exceeded above 8x with these FIRs. A new antialias
method or revised acceptance envelope would need an explicit design decision;
neither was silently introduced. Reference convergence must also be established
before claiming fidelity to a higher-rate reference.

After that, review M2 host latency reporting and lifecycle selection using the
measured L, plus the already documented calibration bridge and pre-NAM/gate
placement. Live processor-selection latency is not solved here. Gain-minimum
mute, Tone taper and stage headroom remain named provisional assumptions.
No further archaeology, hardware claim or rig-based tuning is implied.

## Repository retention after the Boost-only decision

See the [repository hygiene audit](../repository-hygiene.md) for the complete retained-file
inventory, excluded generated outputs, historical hash-manifest scope and
reproduction order. Reports and qualification decisions are preserved; bulk
experiment grids are regenerated locally. Historical validation hashes/counts
describe their original run, not the curated checkout.
