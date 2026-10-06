# Tuner v1 — M1 detector

M1 adds an isolated, consumer-owned chromatic pitch detector. It does not capture
host audio, modify the audible path, run a worker, update the UI, smooth displayed
pitch, or change any NAM/pedal/IR behavior. A4 is fixed at 440 Hz.

## Implementation

`dsp/ChromaticTuner.h/.cpp` uses fixed storage throughout. `prepare()` accepts
44.1, 48, 88.2, 96, 176.4 and 192 kHz; unsupported rates invalidate its state.
Integer decimation factors 2/4/8 produce 22.05 or 24 kHz analysis samples. A
unity-normalized Kaiser FIR (beta 8.6, `64 * factor + 1` taps, cutoff `0.20 *
analysisRate`) runs only on decimation outputs. FIR startup is discarded before
collecting the 2,048-sample analysis window.

YIN uses a fixed integration width of 1,417 samples at 22.05 kHz or 1,361 at
24 kHz. The remaining support supplies lags through `ceil(analysisRate / 35)`
and one interpolation neighbor. All lags are calculated; the first normalized
trough at or below 0.15 selects a candidate. Shorter periods are also examined
so an above-range first candidate is rejected rather than replaced by a later
in-range octave. Parabolic refinement uses the **raw** difference trough to
avoid the cents bias of interpolating the normalized difference. Refined
frequencies outside 35–1,400 Hz are rejected.

RMS is measured on the DC-rejected, filtered clean input before normalization.
Below −70 dBFS gives `noSignal`. A valid estimate has `highConfidence` only when
the interpolated normalized trough is at most 0.10 and RMS is at least −65 dBFS.
A weaker valid estimate can support an existing display lock; acquisition must
require high confidence in the later display tracker. `periodicity` is one minus
the normalized trough, not a probability. Noise without a credible trough gives
`unstable`; no unconditional global-minimum fallback is used.

`pushSamples()` never analyzes automatically. `analyze()` returns note, octave,
frequency and signed cents against the nearest equal-tempered note. Reset or a
nonfinite input sample discards all preceding history. Startup requires a fresh
complete FIR history and analysis window, approximately 88–96 ms of native input.

## Validation recorded for M1

The checked-in [Release log](logs/tests-release.log) passes all nine focused
detector tests, alongside later tuner-layer tests. The fixtures cover all six
rates, all 61 notes E1–E6, cents offsets −25/−10/−5/0/+5/+10/+25, three phases,
DC and quiet input, silence,
white/pink noise, weak-fundamental eight-harmonic decaying signals, 25 dB noise,
note transitions, range and alias rejection, nonfinite recovery, conversion
boundaries and varied input partitions.

| Measurement | Recorded maximum | Acceptance gate |
|---|---:|---:|
| Clean sines, 7,686 cases | 0.263483 cents | 1 cent |
| Amplitude/DC fixtures | 0.215237 cents | 1 cent |
| Clean harmonic fixtures | 1.28923 cents | 3 cents |
| Harmonic fixtures at 25 dB SNR | 1.31206 cents | 5 cents |
| Upper harmonic boundary/phase fixtures, 2,496 cases | 1.34443 cents | 3 cents |
| Detector acquisition, 10 ms analysis polls | 100 ms | 150 ms |
| Tracked allocations during reset/feed/analyze | 0 | 0 |

The original harmonic/noisy matrix contains 1,464 cases. The additional 2,496
clean upper-range cases cover notes E5–E6, both harmonic-weight patterns, offsets
±25/±49 cents and four harmonic-phase patterns. At ±49 cents, an allowed small
measurement error can cross the nearest-note boundary; the regression verifies
frequency error and coherent conversion of the measured frequency, rather than
requiring the source's nominal note label. Deterministic broadband fixtures
produced no high-confidence pitch. Whole and irregularly partitioned feeds give
exactly matching frequency, periodicity and cents. Standalone strict warnings
and Clang static analysis were clean; commands are in
[strict analysis log](logs/strict-analysis.log).

[Benchmark results](logs/benchmark.jsonl) were recorded with Apple Clang 21.0.0,
`-O3 -DNDEBUG`, on arm64 macOS. These are local measurements, not host scheduling
or end-to-end UI timing guarantees. Feed and analysis remain outside the audio
callback in the proposed integration. The filter correction changes coefficients
while preserving the tap count and amount of computation.

Run `bash validate.sh debug release` to rebuild the focused tests and Release
benchmark. M1 does not validate a capture service, display lifecycle or live
APP/VST3/AU tuner because those are subsequent milestones.

## Measured filter correction

The original `0.42 * analysisRate` cutoff retained upper harmonics that increase
the bias of a three-point raw-trough parabola. The expanded upper-range probe
found a 3.140686-cent maximum and 12 cases exceeding the 3-cent clean gate,
all standard-weight E6 +49-cent fixtures at 44.1/88.2/176.4 kHz. Their confidence
was high; increasing rejection thresholds would not address interpolation bias.

Lowering the cutoff to `0.20 * analysisRate` (4.41/4.8 kHz) preserves every
supported fundamental and useful guitar harmonics while reducing upper partials.
The same independent probe now measures 1.344433 cents maximum, zero cases above
3 cents and zero high-confidence misses. The original clean/noisy harmonic
maxima improve from 2.98263/2.99673 to 1.28923/1.31206 cents. Sine accuracy and
100 ms acquisition are unchanged. Confidence/RMS thresholds were preserved.
The new boundary/phase regression permanently covers this measured failure.
The [original Release log](logs/tests-release-before-filter-correction.log) and
[original benchmark](logs/benchmark-before-filter-correction.jsonl) are retained.

## Limits retained for later milestones

Do not loosen the gates or claim hardware guitar accuracy from synthetic
fixtures. The detector is monophonic; periodic hum and fundamentally ambiguous missing-fundamental signals
can register as notes. Captured sample gaps, enable generations, rate epochs,
idle stalls, smoothing and coherent UI publication belong to the capture/service
and display layers rather than this detector.
