# Full J. Rockett AH M2 — implementation and audition

2026-09-16. Authorized full-pedal integration. These are unmeasured behavioral
models, not a verified hardware recreation. Manual full-pedal audition passed.
The one audition-driven refinement, accepted on 2026-09-24, expands only the
Drive Treble range to −9…+9 dB at the unchanged 2.5 kHz shelf frequency and
exact 0 dB noon. No other profile, routing, latency or layout value changed.
On 2026-10-02 the user accepted the full milestone after real REAPER VST3/AU
validation, including the subsequent fixed pedal-domain and latency repair.

## Routing and transitions

| Boost | Drive | Settled path | Pedal latency |
| --- | --- | --- | --- |
| Off | Off | Input delayed 32 samples | 32 |
| On | Off | Frozen Boost, then 32-sample delay | 32 |
| Off | On | Frozen Drive | 32 |
| On | On | Frozen Boost -> frozen Drive | 32 |

Drive is Bass 250 Hz ±6 dB -> fixed 4× / 129-tap Kaiser FIR asinh
Drive -> Treble 2.5 kHz ±9 dB -> Volume. Gain, sensitivity, compensation,
10 ms control smoothing and cubic Volume taper are the isolated M1 constants.
Volume zero is mute; default 0.6309573444801932 is 0 dB; maximum is +12 dB.
Boost retains all six shelves, 0–20 dB range, level smoothing and complete-EQ
response crossfades. Both profiles' claim limits remain unchanged.

Section enables use complementary 10 ms fades. An inactive Drive resets while
inaudible and receives 32 samples before its fade starts; the dry route remains
aligned to its natural FIR group delay. A reversal during a fade starts from
the current weight. Dormant sections stop processing after their fade ends.
The 32-sample dry history keeps advancing; it never adds another delay after
Drive. Boost naturally feeds Drive during both-on operation.

The outer selector retains identities Off=0, TC=1, MC402=2, AH=3. Every selection
has 32 samples of pedal-domain latency. Off/TC/MC retain their settled arithmetic
followed by an exact 32-sample ring; AH uses its existing 32-sample path without
another delay. A separate 32-sample dry ring stays warm for transitions.
Outer switches fade through that aligned dry signal: 10 ms when Off is an
endpoint, otherwise 5 ms out + 5 ms in. Each incoming engaged route warms for
32 samples while inaudible (at most 0.73 ms at supported rates).
At most one pedal executes per input sample, including outer transitions.
No permanent parallel pedal path exists. An underway transition finishes;
the latest requested selection is adopted at the following block boundary.
Selection uses the existing atomic UI handoff directly and does not depend on
host permission, model-latency state, or the control-thread idle service.

## Latency guarantees and limits

The [framework repair](latency-repair.md) documents the old buffer-mutation
race, ownership changes and exact eight-file dependency inventory. Rendering
publishes requests and settled totals. One control service in `OnIdle()` handles host interaction;
the framework timer services it with the editor closed as well.

**Plugin guarantees:** lock-free configuration publication, audio-owned delay
history/taps, no render-time resize/allocation/lock in the new paths, bounded
complementary alignment fades and coherent latest-target adoption. Framework
bypass has a separate 10 ms enable fade to avoid exposing two differently
timed paths with an abrupt switch. Its buffers allocate only in the existing
inactive block-size lifecycle. While fully bypassed, only the selected pre-NAM
stage is maintained; NAM/cab/downstream processing stays bypassed. The host's
bypass output remains its delay-compensated input, not the pedal output.

**Host-dependent model transition:** audio finishes its bypass-tap fade and
publishes the new total before the control thread notifies VST3 or AU.
The value stays pinned until notification returns. Query-first VST3 hosts see
the new total immediately; hosts performing deactivate/reactivate see the same
published total. Reactivation neither permits adoption nor republishes a stale
request. Notification return is not a sample-timed PDC acknowledgement.
Host compensation may briefly differ during a model change; hosts may interrupt
playback for reactivation. AU's same timing limitation remains. APP needs no
host PDC notification. Outer/internal pedal switches never notify the host.

**Exact settled behavior:** `reported = model latency + 32`.
The product configuration starts at 32 samples before the initial host query
(the initial model contribution is zero until a model is installed).
The pedal DSP and framework bypass tap represent that
same total after transitions finish. Internal section switches do not request
a host latency change. NAM/resampler math is untouched; their pre-existing
model-installation behavior is not redefined by this work.

## Accepted real-host validation

User-reported REAPER validation, accepted 2026-10-02, using the final Release
VST3 and AU products. These observations are distinct from automated host doubles.

| Check | VST3 | AU |
| --- | --- | --- |
| Off / TC BLD / MC402 -> J. Rockett | Engages immediately; clean switching | Same behavior passed |
| Reported latency across outer pedal choices | Unchanged, fixed +32 pedal samples | Same behavior passed |
| Rockett internal Boost/Drive switching | Clean | Clean |
| NAM unity-48k -> unity-44k1 -> unity-48k at a 48 kHz host | 32 -> 61 -> 32 samples | 32 -> 61 -> 32 samples |
| Persistent timing offset, doubling, comb filtering or echo | None observed | None observed |

Both formats can produce a very small momentary crackle exactly when replacing
a NAM model. It disappears immediately and is accepted as a non-blocking polish
item. This acceptance does not imply sample-timed host PDC acknowledgement or
seamless model replacement on every host.

The temporary fixtures are `/private/tmp/nam-latency-fixtures/unity-48k.nam` and
`unity-44k1.nam`: NAM version 0.5.4, architecture Linear, config
`receptive_field: 1, bias: false`, weights `[1.0]`. They differ only in top-level
`sample_rate` (48000 / 44100). Both were loaded with production `nam::get_dsp`
and the verbatim production `ResamplingNAM` wrapper; raw Linear behavior is unity.
The latter model exercises the existing Lanczos conversion and contributes
29 host samples, while the native-rate model contributes zero. Fixture files
and validation executables remain outside the repository.

## Published framework dependency

The parent records iPlug2 commit
`8b7def3150e0b7d2d51c38d5a66833a8753a77d7` on branch
`holdsworth-latency-safety` in `https://github.com/marte1337/iPlug2.git`.
It is a direct child of the original `de5a4fb14fd964247f0c70f2c90b32876a3bfc7c`
baseline. An empty temporary repository independently fetched the branch over
public HTTPS and verified that exact SHA and the new `LatencyState.h` object.
`.gitmodules` points to the user-owned fork. The existing upstream remote is
preserved locally. No existing history was rewritten; the parent feature
branch is not merged or pushed by this cleanup.

## UI and placement

Development UI v2 identifies “Behavioral OD/Boost”, with BOOST ON/OFF, Level,
F/C/T and L/H above DRIVE ON/OFF, Gain, Bass, Treble and Volume. Default remains
Boost enabled / Drive disabled. Double-click defaults are Boost 0 dB, Gain .5,
Bass/Treble noon, Volume 0 dB. The card/window grow 112 pixels vertically;
Delay is translated intact, Amp/Cab and the original TC/MC controls retain
their layouts. No oversampling, sensitivity or latency control is exposed.
All development controls remain nonserialized.

Placement is unchanged: mono input/trim and calibrated-volts bridge -> selected
pedal -> gate trigger -> NAM. The existing metadata/calibration enabled/disabled
branches, NAM/tone/cab/DC/delay/output calculations are preserved exactly.

## Validation and reproduction

- `JRockettAHPedalTests.cpp`: four routes and six Boost modes against isolated
  processors, 32-sample peak alignment, post-Drive Volume, section fades,
  rapid requests, bit-identical partitions, in-place/reset/reprepare,
  finite/nonfinite recovery, concurrent controls, allocations, legacy paths,
  calibration combinations and nonlinear NAM/gate test doubles.
- `latency-tests.cpp`: real iPlugProcessor/bypass implementation with model+32
  contribution changes, generic tap changes, in-place buffers, exact settled
  taps, query-first/reactivating VST3 and AU host doubles, pinned synchronous
  queries with newer requests, and concurrent render/control.
- `pedal-host-tests.cpp`: real framework + full pedal, outer and bypass toggles
  during fades, exact settled agreement, concurrent control handoff; six rates
  × seven callback sizes. Includes AH execution while a model request is
  deliberately left unserviced. Format lifecycle is modeled, not a DAW test.
- Full native Debug, Release, ASan/UBSan and TSan suites; realtime interposition
  of allocation/free and mutex lock/trylock; strict warnings/static analysis;
  APP/VST3/AU Release builds; project lint and both diff checks. See
  [validation.json](validation.json) for results and [cpu.csv](cpu.csv) for raw
  compact timing data. Existing WDL/raw-pointer analyzer warnings match the
  unmodified framework baseline; new pedal/test sources have no diagnostics.
- `audit.py` checks frozen source hashes, append-only message IDs, actual UI
  wiring, calibration/signal placement, unchanged downstream source and
  production/test target enrollment. No automated visual inspection.

Run `bash validate-latency.sh` with the Xcode `AH_NATIVE_CXX`/`AH_NATIVE_SDK`
overrides, then `bash validate-m2.sh` and `python3 audit.py`. The final accepted
Release products live under `/private/tmp/ah-fixed32-host-validation`. Native full-pedal CPU
measurements include framework buffer/latency work, not NAM/IR/delay cost.
All six product rates and 1/2/4/8/32/64/128 callbacks have measured headroom;
raw scheduler outliers are retained and are not a hard realtime OS guarantee.
No 768 kHz product-support claim.

Release audition app:
`/private/tmp/ah-fixed32-host-validation/NeuralAmpModeler.app`

The APP, VST3 and AU builds are arm64 Release using Xcode 27 and a local macOS
12 deployment override. Repository deployment settings are unchanged. Builds
do not install plugins or clear user AU caches. Format binaries are alongside
the app as complete independent bundles (no symlinks), with executable, plist,
resources and verified ad-hoc signatures. A smoke host loads the actual VST3
and verifies its initial 32-sample query before any processing/idle callbacks.
Real REAPER VST3/AU checks passed as recorded above; other hosts remain untested.

## Changed files and manual review

The fixed32 follow-up changes `integration/DevelopmentAHOuterTransition.h`,
`tests/JRockettAHPedalTests.cpp`, `NeuralAmpModeler/NeuralAmpModeler.cpp` and
`NeuralAmpModeler/config.h`. In iPlug2 it changes `Extras/LatencyState.h`,
`IPlugProcessor.h/.cpp` and `VST3/IPlugVST3.h/.cpp` under `IPlug/`.
Focused harnesses are `latency-tests.cpp`, `pedal-host-tests.cpp` and `audit.py`;
records are `validation.json`, `cpu.csv`, this README, `latency-repair.md`, the
package README/milestones and `AGENTS.md`. No DSP profile, processor, UI layout,
AU wrapper, or bypass-delay implementation changed in this follow-up.

Historical full M2 inventory follows:

New implementation: `dsp/JRockettAHPedal.h` and
`integration/DevelopmentAHOuterTransition.h`. Updated:
`integration/DevelopmentJRockettAHControls.h`, `ui/DevelopmentPanel.h`,
`tests/TestHarness.h`, `tests/TestMain.cpp`; new `tests/JRockettAHPedalTests.cpp`.
Plugin changes: `NeuralAmpModeler.h/.cpp`, `config.h` (editor height), and the
macOS project (Drive enrollment and test registration). Documentation updates:
`AGENTS.md`, pedal README/milestones and this `drive-v1/m2` package. The already
uncommitted isolated Drive files and M0/M1 references are preserved.
Vendored files are enumerated in [latency-repair.md](latency-repair.md). The
initial upstream revision was later replaced by the published repair above.
Initial live integration changed no TC/MC/Yamaha/Boost/Drive
profile files. The accepted post-audition promotion later changed only the Drive Treble range,
its display mapping, analytic target test and associated profile records.

Manual full-pedal audition passed, including the direct comparison that selected
the ±9 dB Treble range. Real REAPER VST3/AU latency and routing validation also
passed. The documented API timing limitations remain; no other host outcome is
inferred from that acceptance.
