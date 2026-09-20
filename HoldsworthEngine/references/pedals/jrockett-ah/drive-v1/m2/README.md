# Full J. Rockett AH M2 — implementation and audition

2026-09-16. Authorized full-pedal integration. Both accepted profiles remain
byte-identical to their frozen M1/M2 sources. These are unmeasured behavioral
models, not a verified hardware recreation. Manual full-pedal audition is
pending. No commit, new artist settings, or profile retuning.

## Routing and transitions

| Boost | Drive | Settled path | Pedal latency |
| --- | --- | --- | --- |
| Off | Off | Input delayed 32 samples | 32 |
| On | Off | Frozen Boost, then 32-sample delay | 32 |
| Off | On | Frozen Drive | 32 |
| On | On | Frozen Boost -> frozen Drive | 32 |

Drive is unchanged: Bass 250 Hz ±6 dB -> fixed 4× / 129-tap Kaiser FIR asinh
Drive -> Treble 2.5 kHz ±6 dB -> Volume. Gain, sensitivity, compensation,
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

The outer selector retains identities Off=0, TC=1, MC402=2, AH=3. Off/TC/MC
remain zero added pedal latency. AH remains 32 for every internal state.
Zero-latency legacy selection retains its original processing arithmetic.
An outer switch involving AH fades through the calibrated dry wire: 10 ms for
Off <-> AH, or 5 ms out + 5 ms in for another pedal <-> AH. Incoming AH warms
for 32 samples while inaudible. This adds at most 0.73 ms at supported rates.
At most one pedal executes per input sample, including outer transitions.
No permanent parallel pedal path exists. An underway transition finishes;
the latest permitted request is adopted at the following block boundary.
Host/control-service scheduling can add request-to-start time.

## Latency guarantees and limits

The [framework repair](latency-repair.md) documents the old buffer-mutation
race, ownership changes and exact eight-file dependency inventory. Audio only
publishes requests. One control service in `OnIdle()` handles host interaction;
the framework timer services it with the editor closed as well.

**Plugin guarantees:** lock-free configuration publication, audio-owned delay
history/taps, no render-time resize/allocation/lock in the new paths, bounded
complementary alignment fades and coherent latest-target adoption. Framework
bypass has a separate 10 ms enable fade to avoid exposing two differently
timed paths with an abrupt switch. Its buffers allocate only in the existing
inactive block-size lifecycle. While fully bypassed, only the selected pre-NAM
stage is maintained; NAM/cab/downstream processing stays bypassed. The host's
bypass output remains its delay-compensated input, not the pedal output.

**Host-dependent transition:** VST3 requests a latency restart and waits for
the host's actual deactivate/reactivate lifecycle before granting a changed
latency. A host that ignores the restart leaves that request pending. AU
publishes before notifying listeners and pins synchronous query values, but
provides no sample-timed PDC adoption acknowledgement. AU may briefly apply
different compensation during a transition. Seamless/sample-accurate AU PDC
switching is not claimed. APP needs no host PDC notification.

**Exact settled behavior:** `reported = model latency + (AH selected ? 32 : 0)`.
The pedal DSP and framework bypass tap represent that
same total after transitions finish. Internal section switches do not request
a host latency change. NAM/resampler math is untouched; their pre-existing
model-installation behavior is not redefined by this work.

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
- `latency-tests.cpp`: real iPlugProcessor/bypass implementation with model
  contribution changes, 0/32 changes, in-place buffers, exact settled taps,
  synchronous queries, delayed lifecycle and concurrent render/control.
- `pedal-host-tests.cpp`: real framework + full pedal, outer and bypass toggles
  during fades, exact settled agreement, concurrent control handoff; six rates
  × seven callback sizes. Format lifecycle is modeled, not an actual DAW test.
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
overrides, then `bash validate-m2.sh` and `python3 audit.py`. Build logs and
native binaries live under `/private/tmp/ah-full-m2`. Native full-pedal CPU
measurements include framework buffer/latency work, not NAM/IR/delay cost.
All six product rates and 1/2/4/8/32/64/128 callbacks have measured headroom;
raw scheduler outliers are retained and are not a hard realtime OS guarantee.
No 768 kHz product-support claim.

Release audition app:
`/private/tmp/ah-full-m2/products/NeuralAmpModeler.app`

The APP, VST3 and AU builds are arm64 Release using Xcode 27 and a local macOS
12 deployment override. Repository deployment settings are unchanged. Builds
do not install plugins or clear user AU caches. Format binaries are alongside
the app; actual DAW restart/PDC adoption remains a manual host check.

## Changed files and manual review

New implementation: `dsp/JRockettAHPedal.h` and
`integration/DevelopmentAHOuterTransition.h`. Updated:
`integration/DevelopmentJRockettAHControls.h`, `ui/DevelopmentPanel.h`,
`tests/TestHarness.h`, `tests/TestMain.cpp`; new `tests/JRockettAHPedalTests.cpp`.
Plugin changes: `NeuralAmpModeler.h/.cpp`, `config.h` (editor height), and the
macOS project (Drive enrollment and test registration). Documentation updates:
`AGENTS.md`, pedal README/milestones and this `drive-v1/m2` package. The already
uncommitted isolated Drive files and M0/M1 references are preserved.
Vendored files are enumerated in [latency-repair.md](latency-repair.md); iPlug2
revision is unchanged. No TC/MC/Yamaha/Boost/Drive profile files changed in M2.

Manual review: inspect the two-section layout and double-click defaults; audition
all four routes, Volume mute and output matching, low/mid/high Gain, all Boost
modes, repeated section/outer toggles and framework bypass on sustained notes.
Check a clean and driven NAM with calibration enabled/disabled. In the intended
AU/VST3 hosts, confirm latency displays, restart handling, playback transitions
and model changes with the editor closed. No further profile tuning is part
of this milestone; record audition observations for a separate decision.
