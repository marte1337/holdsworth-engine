# Isolated AH Drive M1

Profile frozen by user approval: `JROCKETT-AH-DRIVE-BEHAVIORAL-V1`.
Implementation and validation complete; report finalized 2026-09-15. No live
integration or host latency work. Drive is enrolled only in HoldsworthEngineTests.

Historical note: this isolated M1 and its retained oracle measurements used a
−6…+6 dB Treble shelf. After full M2 audition, the user accepted only a Treble
range refinement to −9…+9 dB at the same 2.5 kHz frequency and exact 0 dB noon.
The final accepted profile is recorded in the [M2 report](../m2/README.md); the
M1 results below remain an accurate record of the earlier isolated validation.
Full M2, fixed pedal-domain alignment and dynamic NAM latency subsequently
passed real REAPER VST3/AU validation and were accepted on 2026-10-02. That
host result is recorded in M2; it does not revise the isolated measurements.

## Approved Volume correction

The single correction to the [approved design](../README.md) is a normalized
audio-style Volume taper with exact mute:

`amplitude(v) = 10^(12/20) × v³`, for `v ∈ [0,1]`.

For positive v, `dB(v) = 12 + 60 log10(v)`. Default v is
`10^(-12/60) = 0.6309573444801932`, giving 0 dB. Maximum is +12 dB;
minimum is exact silence. At v=.1/.25/.5/.75 the levels are
−48/−24.1236/−6.0618/+4.5037 dB. This taper is smooth through zero,
retains useful attenuation throughout the lower travel, and has no finite
attenuation floor. It is a software behavioral convention, not a measured AH pot.

Volume amplitude is ramped over 10 ms and snapped to the exact target at the
last sample. Thus a mute request settles to exact silence after the ramp; it
does not hard-cut an already sounding note at the control-message boundary.
Processing and state histories continue while muted, so unmuting has current
filter history. Startup/reset at Volume zero is immediately silent.

All other sound constants and the Bass → asinh → Treble → Volume topology remain
as approved. Production constants live only in
[JRockettAHDriveProfile.h](../../../../../dsp/JRockettAHDriveProfile.h).
The earlier offline profile/results are retained as historical design evidence;
their −24…+12 dB normalized-control proposal is superseded here.

## Processor and frozen constants

[JRockettAHDriveProcessor](../../../../../dsp/JRockettAHDriveProcessor.h) is a
concrete mono processor with prepare/reset/setControls/processBlock, exact
in-place operation and a declared maximum-block contract. Only the six approved
rates are accepted: 44.1/48/88.2/96/176.4/192 kHz. Invalid prepare arguments throw
outside realtime; processing never throws. Invalid processing spans assert in
Debug and pass through their common extent in Release without advancing state.

```text
input → Bass shelf → 4× interpolation → gain/asinh/compensation
      → 4× decimation → Treble shelf → Volume
```

| Frozen element | Implementation |
| --- | --- |
| Gain | normalized 0–1, default .5; `D = 10^(24g/20)` |
| Nonlinearity | `Vref × D^(-.35) × asinh(Dx/Vref)`; native double-precision asinh |
| Sensitivity | Vref = 1 software volt, not measured hardware headroom |
| Bass | pre-drive first-order low shelf, 250 Hz, −6…+6 dB |
| Treble | post-drive first-order high shelf, 2.5 kHz, −6…+6 dB |
| Tone noon | .5 = exact wire; software convention |
| Volume | cubic mapping above; pure post-drive scalar |
| Smoothing | 10 ms, independent Gain/Bass/Treble/Volume groups |
| FIR | fixed 4×, 129 taps each direction, Kaiser β=8.6, cutoff=.5×base rate |
| Latency | 32 base samples at every supported rate |

Shelves use the approved analytic first-order definitions with bilinear
prewarping at their centers and DF2-transposed state. No oversampling choice,
frequency, gain law, sensitivity, transfer, tone curve or default Gain/Tone was
retuned. The only sonic-profile deviation is the requested Volume correction.

## Oversampler and measured latency

At prepare, a bounded I0 series builds the Kaiser-windowed sinc coefficients,
then normalizes their DC sum. This reproduces the approved `firwin(129,.25,
window=('kaiser',8.6))` construction. Four interpolation phases use ×4 tap scaling;
the decimator uses the unscaled coefficients and observes high-rate phase zero.
Duplicated fixed circular buffers make dot products contiguous. No block buffering,
FFT, ideal resampler, hidden drive limiter or variable oversampling is present.

The two 64-high-rate-sample FIR group delays total **32 base samples**. An actual
small-signal impulse peaks at sample 32 at all six rates, including one-sample
callbacks. Linear-phase impulse symmetry and the causal Python oracle agree.
Delay is 0.726/0.667/0.363/0.333/0.181/0.167 ms at ascending supported rates.
`latencySamples()` reports 32; no host latency notification is implemented.

## Controls, handoff and recovery

One control producer computes a complete sanitized tuple and its target gains/
shelf coefficients outside processing. A three-slot SPSC mailbox transfers
ownership with lock-free uint32 atomics; audio adopts the latest complete target
at a nonempty block boundary. No spin waits or growing queues. Prepare and reset
require external lifecycle exclusion from the producer and consumer.

Each changed group ramps from its current values to target over `ceil(.010 fs)`
base samples. Gain's D and compensation advance at all four internal samples;
Bass/Treble coefficients and Volume amplitude advance at base rate. Duplicate
targets do not restart ramps; changing Volume does not restart an ongoing Gain
ramp. Rapid requests retarget the affected group from its current state. Endpoints
snap exactly to target. The settled Gain compensation law is exact; intermediate
Gain and compensation are independently interpolated as approved in the design.

Filter states are preserved during control movement, including passing through
tone noon. First-order real poles interpolate between stable endpoints. The
10 ms duration describes parameter ramps; existing IIR/FIR memory still rings
out normally. Controls do not create a mute transition except for a deliberate
Volume-zero request. Full-state reset clears both FIR histories and tone state
and immediately applies the latest complete controls. Reprepare rebuilds rate-
dependent coefficients while retaining those controls.

Nonfinite controls use their defaults; finite out-of-range controls clamp to
0–1. NaN/Inf audio becomes zero excitation without poisoning histories. Inputs
beyond DBL_MAX/4096 are guarded solely to prevent floating-point overflow, far
outside the declared product and torture envelopes. Tests confirm finite recovery
from alternating maximum doubles and subsequent silence. This guard is not a
pedal clipping threshold or a change to ordinary-signal asinh behavior.

## Oracle, fidelity and cleanup results

[measure.py](measure.py) calls the actual compiled C++ processor through the
offline-only [oracle bridge](oracle_bridge.cpp), removes its measured delay for
comparison, and compares against the unchanged approved Python Drive oracle.
The Volume comparison applies the new cubic post-scalar explicitly. The oracle's
historical dB-valued helper remains useful independently of a normalized UI taper.

- Maximum C++ versus approved 4× oracle absolute error: **1.2074 × 10⁻¹⁴**;
  maximum relative peak error: **1.3626 × 10⁻¹⁴**.
- All **810** product-grid cases pass the −75 dBr criterion against 32×.
  Worst: **−90.988 dBr**, 44.1 kHz, ~4 kHz/.5 V input, maximum Gain/Bass/Treble.
- Volume scalar comparison, including mute/default/max: maximum error
  **2.8866 × 10⁻¹⁵**; mute output is exact positive zero.
- Actual DI comparisons and 24 Boost-mode/level cascades also pass. The Boost
  input is a settled offline transcription; the live Boost implementation and
  its mode transitions are untouched.
- Small-signal tone tests cover all nine min/noon/max combinations at every
  supported rate. 1,212 tone coefficient settings were checked during design;
  M1 additionally verifies complete filtered output magnitude and phase.

Actual C++ THD at ~440 Hz/.2 V, neutral tones: **0.1638%, 2.1013%, 10.6406%**
at Gain 0/.5/1. Reducing input to .05 V gives **0.0104%, 0.1623%, 2.0853%**.
Thus Gain changes nonlinear behavior and reduced input cleans up; Volume only
changes output level. No NAM or IR participates in the tests or comparisons.

The declared envelope remains the approved 0.02–0.5 software V peak guitar input,
strong components through 4 kHz with quieter overtones through 8 kHz, evaluated
over 20 Hz–10 kHz. These are numerical fidelity results for our model, not
hardware error measurements. Earlier near-Nyquist/10 V failures remain stress
observations; the product envelope and transfer were not altered to fit them.

## CPU and realtime evidence

[benchmark.cpp](benchmark.cpp) measures 1,000 callbacks in each of 84 cases:
six rates × seven callback sizes × settled/continuously retargeted controls.
Control-side coefficient calculation and warmup are outside the timer; audio
mailbox adoption, all ramps and complete DSP are inside. The retained run was
made after compilation/oracle work finished. Native arm64 Release, -O3 without
fast-math; object storage **5,424 bytes**, no heap scratch.

| Callback frames | Largest p99 duration across cases | Largest p99 budget share |
| --- | ---: | ---: |
| 1 | 0.625 µs | 6.41% |
| 2 | 0.834 µs | 5.60% |
| 4 | 1.709 µs | 5.00% |
| 8 | 3.250 µs | 5.10% |
| 32 | 14.750 µs | 7.05% |
| 64 | 29.417 µs | 7.37% |
| 128 | 49.959 µs | 6.65% |

The two maxima in each row can come from different rates/control conditions.
At 192 kHz/one frame with moving controls: median **.292 µs**, p99 **.334 µs**,
against a **5.208 µs** callback budget. All rates and callback sizes have p99
headroom. **Four raw maximum timings exceeded their individual deadlines**:
88.2 kHz/1 settled (16.375 µs), 96 kHz/1 ramping (22.334 µs), 192 kHz/1 ramping
(12.792 µs), 192 kHz/2 ramping (15.542 µs). Their cause was not isolated; do not
erase them or claim a hard scheduling guarantee. These measurements characterize
incremental Drive cost on this Mac, not arbitrary NAM/cab/delay combinations.

Allocation tracking reports zero for processing/control updates/reset across
all six rates and all seven callback sizes. The processor contains no locks,
file I/O or logging; its mailbox has a compile-time always-lock-free assertion.
ThreadSanitizer verifies the concurrent producer/consumer test. These results
support realtime use of the isolated path; the live host is still out of scope.

## Validation and file inventory

Full **Debug 270/270**, **Release 270/270**, **ASan+UBSan 270/270**; all ten focused
Drive groups pass. ThreadSanitizer concurrent handoff passes. Strict warnings
(-Wall/-Wextra/-Wpedantic/-Wconversion/-Wshadow/-Werror), Clang static analysis,
project lint and `git diff --check` pass. ASan used `detect_leaks=0` because this
platform lacks LeakSanitizer; realtime allocation instrumentation is separate.
Release APP/VST3/AU builds pass with Drive excluded from those targets. Nothing
was installed or launched; the validation build uses a no-op AU-cache helper.

[audit.py](audit.py) verifies 35 tracked legacy DSP/integration/UI/plugin files
against HEAD and verifies Drive enrollment in the test target only. Existing
Boost, TC, MC402, Yamaha, NAM/cab and UI files are unchanged. The first sandboxed
Xcode attempt failed to access Xcode caches; the authorized rerun completed.

Added implementation files: `dsp/JRockettAHDriveProfile.h`,
`dsp/JRockettAHDriveProcessor.h/.cpp`, `tests/JRockettAHDriveProcessorTests.cpp`.
Changed test registration: `tests/TestHarness.h`, `tests/TestMain.cpp` and the
macOS Xcode project (test target only). Updated project guidance and pedal status
documents. This M1 folder adds the report, oracle bridge, measurement/benchmark/
audit/validation tools, no-op build helper, and four compact data records:
[results.json](results.json), [measurements.csv](measurements.csv),
[realtime.csv](realtime.csv), [validation.json](validation.json). No new audio
collection or binaries retained in the repository; logs/products stay in `/private/tmp`.

Reproduce from the repository root (Xcode needs its ordinary cache access):

```sh
bash HoldsworthEngine/references/pedals/jrockett-ah/drive-v1/m1/validate.sh
PYTHONDONTWRITEBYTECODE=1 /private/tmp/ah-drive-design-venv/bin/python HoldsworthEngine/references/pedals/jrockett-ah/drive-v1/m1/measure.py /private/tmp/ah-drive-m1/oracle.dylib --out /private/tmp/ah-drive-m1/recheck
/private/tmp/ah-drive-m1/benchmark
python3 HoldsworthEngine/references/pedals/jrockett-ah/drive-v1/m1/audit.py
```

The design folder documents the pinned NumPy/SciPy/Matplotlib environment. The
audit checks retained CPU/results against the native build logs; benchmark stdout
allows an independent timing run without overwriting the historical table.

## Before live M2

Review the final Volume taper/default location and this M1 evidence. No sonic
deviation beyond the requested endpoint correction. Before live work, decide
how the host will handle Drive's 32 samples relative to zero-latency Boost/Off,
and approve section enable/reselection transitions and parameter/UI mapping.
None is silently resolved here. Then M2 must test calibration-dependent drive
excitation, Boost-only/Drive-only/Boost → Drive operation, host latency and
whole-chain CPU, followed by live audition. No M2 implementation or commit.
