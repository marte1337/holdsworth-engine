# M1 isolated AH Boost

Implemented profile: **`JROCKETT-AH-BOOST-BEHAVIORAL-V1`**. M0 approved by the
user with one correction: crossfade complete EQ responses instead of fading to
silence. **All curves and the 0–20 dB range remain unmeasured software audition
choices.** These measurements describe this implementation, not AH hardware.
Drive, selector/UI changes and NAM integration remain deferred. No commit.

## Files and scope

New DSP: [profile constants](../../../../dsp/JRockettAHBoostProfile.h),
[processor API](../../../../dsp/JRockettAHBoostProcessor.h),
[implementation](../../../../dsp/JRockettAHBoostProcessor.cpp).
New [focused tests](../../../../tests/JRockettAHBoostProcessorTests.cpp).
TestHarness/TestMain register the suite; the macOS Xcode project enrolls AH DSP
and its tests **only in HoldsworthEngineTests**, not APP/VST3/AU or other product
targets. Existing processor, selector, UI, NAM, delay and preset files are
unchanged. The parent README/design/milestones now record approval and M1.

This directory adds this report, [response data](responses.csv),
[software measurement summary](measurements.json), [measurement tool](measure.cpp),
[validation summary](validation.json) and [reproduction script](validate.sh).
Raw build logs and binaries remain in `/tmp/jrockett-ah-m1`.

## Exact profile and filter implementation

Constants are centralized in `JRockettAHBoostProfile`: low shelf **250 Hz**,
 high shelf **2500 Hz**; F/L **+6/0**, F/H **+3/+3**, C/L **+3/0**, C/H **0/+3**,
T/L **-3/+3**, T/H **-6/+6 dB**. Default **C/L, 0 dB**; this is not a flat-wire
setting or an Allan preset. Scalar level is **0–20 dB**, default 0 dB, with no
mode loudness normalization. Level and EQ crossfade durations are **10 ms**.
State-priming history is **20 ms**, capped at **15,360 samples**.

Each response contains two first-order shelves in series, low then high, using
transposed direct form II (one double state per shelf). The frozen analog
formulas are in [v1-design.md](../v1-design.md). For each shelf independently,
`k=tan(pi*f_shelf/fs)` implements the bilinear transform with frequency prewarp.
A 0 dB shelf is an exact wire. Six coefficient pairs are calculated in prepare;
only the active response runs when settled. No waveshaper, clipping transfer,
oversampling, FIR, lookahead, noise or Drive model exists. Added latency is **0
samples**; ordinary output can exceed normalized ±1.

Preparation accepts finite **8–768 kHz inclusive**, positive maximum block size.
The six project validation rates are 44.1/48/88.2/96/176.4/192 kHz; additional
lifecycle/switching coverage checks 8/22.05/123.456/384/768 kHz. Frequencies near
Nyquist follow the defined warped response; there is no promise of an unwarped
analog match there. Invalid prepare arguments throw before changing a valid
prepared instance. Processing is `noexcept`, supports exact in-place or distinct
equal-length spans, and rejects invalid/unprepared/oversized calls with a Debug
assert or a copy of the available input in Release. Arbitrary partial overlap
is not supported. Empty calls do not adopt controls or advance time.

## Complete-response mode crossfade

Two complete EQ paths retain separate filter states. At a mode change, the
incoming path replays the latest sanitized input history into initially silent
states **before it becomes audible**. The outgoing path is never reset. This
replay does not delay the audio stream. With less than 20 ms since reset, all
available history is replayed; subsequently, only the old, exponentially small
IIR tail is omitted. It is an approximation to an always-running warm path,
not a claim of bit identity to one.

Over `N=ceil(0.010*fs)` samples, the mix is `(1-t)*old + t*new`, with
`t=i/(N-1)`, `i=0..N-1`: exact old/new endpoints and complementary weights.
There is no fade-to-silence stage, state discontinuity in an audible path, or
identity mix. Linear weighting avoids the correlated-signal gain bump of an
equal-power crossfade; frequency-dependent phase differences can still affect
the intermediate response. At completion the incoming state becomes current
and the other path stops processing immediately.

A request during a fade replaces one pending target. The current fade finishes,
then the processor starts a fresh complete-response fade toward that latest
mode if necessary. Intermediate pending requests are discarded. Requesting the
current destination cancels any older pending destination; requesting the old
source may therefore complete the current fade and then fade back. A final
stationary request settles within at most two fades after block adoption
(approximately 20 ms). There is no growing blend, queue or starvation under
rapid changes. This is deliberately software transition behavior.

One control producer publishes the entire **gain/mode tuple** through a fixed
three-slot SPSC mailbox, using one lock-free 32-bit atomic exchange. The audio
thread consumes at most once per nonempty block. Each thread owns its slot;
release/acquire exchange of the middle slot prevents torn tuples and reuse while
being read. Every request can publish, even when processing is stopped. There
are no retry/spin loops in the processor. `prepare/reset` require lifecycle
synchronization with both threads; multiple control producers are not supported.

## Level and numerical behavior

`setControls` calculates `10^(boostDb/20)` outside processing. A changed gain
starts a linear-amplitude ramp from the current gain over `ceil(0.010*fs)` samples;
the first sample advances one step and the last snaps to the exact target.
Repeated identical gain requests do not restart the ramp. A new level during
an EQ transition scales the complete mixture, so it never changes the chosen
EQ coefficients. Settled gains at **0/10/20 dB** are **1/sqrt(10)/10**.

Finite dB values clamp to [0,20]; nonfinite values select 0 dB. Invalid type and
emphasis independently select C and L. Nonfinite audio is zero excitation of
otherwise valid filter states; ringing is retained. Extremely large finite
samples clamp at `double::max()/1024` to keep filter/intermediate arithmetic
finite. This numerical overflow guard is not a modeled pedal threshold. Normal
signals have scale-invariant behavior, including during mode fades. Reset adopts
the latest tuple, clears filter/transition state and invalidates history without
clearing its large array; reprepare preserves requested settings.

## Measured responses

Impulse-response DFT, 100 ms render, 0 dB level. The retained CSV has **396 rows**:
six modes, six project rates, eleven frequencies from 20 Hz to 10 kHz. Targets
are calculated independently from the frozen analog formulas evaluated at each
shelf's prewarped frequency; the test uses 41 log-spaced frequencies per response.
The table shows **48 kHz measured gain in dB**, not individual shelf asymptotes:

| Mode | 20 Hz | 250 Hz | 1 kHz | 2.5 kHz | 10 kHz |
| --- | ---: | ---: | ---: | ---: | ---: |
| F/L | 5.9588 | 3.0000 | 0.3755 | 0.0630 | 0.0029 |
| F/H | 2.9807 | 1.5298 | 0.5925 | 1.5298 | 2.8663 |
| C/L | 2.9806 | 1.5000 | 0.1789 | 0.0298 | 0.0014 |
| C/H | 0.0002 | 0.0298 | 0.4135 | 1.5000 | 2.8650 |
| T/L | -2.9804 | -1.4702 | 0.2346 | 1.4702 | 2.8636 |
| T/H | -5.9584 | -2.9370 | 0.4818 | 2.9370 | 5.7130 |

Maximum CSV magnitude error: **2.994e-13 dB** (gate 0.05 dB); maximum complex
response error: **6.820e-14**. These tiny values establish implementation
agreement, not audible accuracy or measurement precision for a physical pedal.

A coherent 1 kHz sine, 0.7 peak, 100 ms warmup plus 100 ms analysis measured
harmonics 2–10 across all modes/rates at a maximum **-301.48 dBc**, effectively
floating-point/DFT residue. Focused tests also cover +20 dB level with a 1e-10
relative harmonic gate. No nonlinear alias-convergence experiment was run.

All ordered mode pairs at all six rates, with DC, 83 Hz sine and a two-tone
signal, were compared against independently running fully warm paths. Maximum
crossfade sample error was **7.198e-12** at 0 dB level (gate 2e-9). Tests also
cover sustained chord-like input, exact fade endpoints, rapid pending requests,
return-to-source requests, and bit-exact event-aligned block partitions. DC
transitions follow the convex blend instead of dipping to silence. These are
bounded test observations, not an absolute guarantee for every possible input.

## CPU and realtime limits

Release `-O3 -DNDEBUG`, local arm64 Mac, 64-sample callbacks, 20,000 callbacks
per case, allocations/initial preparation excluded. Timed processing includes
history priming. Single runs include scheduling/timer noise; they are not hard
worst-case guarantees. Struct size: **123,496 bytes**, predominantly the fixed
history buffer sized for 768 kHz. Both filter paths, six coefficient pairs,
mailbox and pending target use fixed storage.

| Rate | Steady mean ns/sample | Changing each block ns/sample | Changing mean audio budget | Changing max callback |
| --- | ---: | ---: | ---: | ---: |
| 48 kHz | 8.45 | 15.97 | 0.077% | 24.25 us |
| 192 kHz | 8.29 | 15.76 | 0.303% | 45.13 us |
| 768 kHz | 8.24 | 15.94 | 1.224% | 86.29 us |

Replay incurs an **O(min(samples since reset, ceil(0.020*fs)))** bounded burst
at transition start, rather than continuous work on six responses. Steady and
fading sample processing have constant work. The measured maximum at 768 kHz
slightly exceeded that rate's 83.33 us/64-sample budget; this includes scheduling
noise but is not a realtime guarantee at that extreme. Before M2, verify the
actual host's rate/block envelope and onset cost, especially tiny callbacks.
No integration performance claim is made from the standalone average.

Allocation tracking passes for processing, mode/level requests and reset under
rapid changes. Source inspection confirms no locks, I/O, logging, exceptions,
allocation or per-sample transcendental functions. The atomic type is checked
with `is_always_lock_free`; the coherent handoff also passes ThreadSanitizer.

## Validation and M2 boundary

See [validation.json](validation.json) for the completed check inventory.
Reproduce with `bash HoldsworthEngine/references/pedals/jrockett-ah/m1/validate.sh`
from the repository. The script keeps all new build products and regenerated
measurements in `/tmp`; it builds but does not launch/install plugins or clear
user AU caches. Its APP/VST3/AU checks are preservation builds, with AH absent
from their source phases. No hardware or UI automation is required for M1.

The only change to the frozen sonic design is the **explicitly approved mode
transition correction**. History priming, coherent mailbox, request-coalescing
and numerical/lifecycle contracts are implementation details, not new pedal
features. No curve, corner frequency, level range, default, Drive scope or
signal-chain integration changed.

Before M2: review the two-fade latest-request policy and fixed-history onset
cost against the intended callback sizes; then authorize the separate selector,
UI and pre-NAM integration milestone. Actual sound, switching comfort and six-mode
usefulness still need live audition. M1 does not claim hardware calibration or
approve additional Drive work. **M2 has not started.**
