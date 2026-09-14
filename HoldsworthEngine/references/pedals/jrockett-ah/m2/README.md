# M2 live AH Boost integration

Profile **`JROCKETT-AH-BOOST-BEHAVIORAL-V1`**, M0/M1 approved; M2 implemented.
The six EQ curves, defaults, 0–20 dB level, 10 ms level smoothing and complete-EQ
mode crossfade remain unchanged. These are **unmeasured behavioral audition
choices**, not calibrated AH hardware responses. Drive remains deferred. No commit.

## Changes and integration

- [DevelopmentPreNAMSelector](../../../../integration/DevelopmentPreNAMSelector.h):
  append `jRockettAH=3`; preserve `off=0`, `tcBld=1`, `mc402Boost=2`. Normalized
  UI centers are 0, 1/3, 2/3, 1, with boundaries 1/6, 1/2, 5/6. Invalid selection
  falls back to Off. Only the selected processor processes audio; readiness,
  maximum block size and AH's explicit product-rate gate are checked first.
- [DevelopmentJRockettAHControls](../../../../integration/DevelopmentJRockettAHControls.h):
  shared normalized mapping/payload validation and six official realtime rates.
- NeuralAmpModeler.h/.cpp: append AH control/message IDs, retain temporary control
  values, prepare/reset AH, route it through the existing pedal bridge and restore
  its UI values on editor opening. No permanent parameters or serialization.
- [DevelopmentPanel](../../../../ui/DevelopmentPanel.h): extend the selector and
  reveal only AH Boost/Type/Emphasis controls when selected. Existing card bounds,
  TC/MC control rectangles/behavior, Delay and NAM panels are retained.
- AH processor header/.cpp: optimize state priming as described below, without
  changing the audible model or crossfade policy. Frozen profile header unchanged.
- Selector tests and AH tests: mapping/routing/calibration/reselection coverage and
  a regression against M1's serial history recurrence. macOS Xcode project: enroll
  AH in the same existing source phases as MC402, including APP/VST3/AU.
- AGENTS and the package index/design/milestones record M2. M1 measurement records
  and hashes remain historical, rather than being rewritten as new M2 evidence.

Signal order is still:

`mono preparation / input trim / calibrated host-to-volts -> selected pedal -> volts-to-NAM -> existing staging / gate trigger / NAM -> tone / cab / DC / delay / output`.

Off uses the original stock arithmetic and invokes no pedal. TC and MC402 retain
reset and processing behavior. AH resets on engagement/disengagement using the
same readiness policy; continued selection does not reset it. Reselection clears
its audio history and adopts its latest settings. Type/emphasis changes while
selected use the M1 crossfade and latest-pending policy, including accepted
approximately 20 ms maximum settling after a final rapid request.

The bridge is unchanged: calibration applies only when enabled and the current
model has input-level metadata; otherwise it retains the post-trim provisional
volts convention. AH is linear, so that convention supplies routing consistency,
not a physical clipping threshold. No edits were made to `_SetInputGain`, the
MC402/TC control-message logic, or downstream processing after the pedal call.
Integration tests use the real selector/control mapping and AH processor with
NAM/gate observations; they do not instantiate a full plugin/UI host. A source
wiring audit supplements those tests and the actual plugin builds.

## Development UI and audition

Selector: **OFF | TC BLD | MC402 | J. ROCKETT AH**. The existing selector rectangle
is retained; the last tab gets extra width and labels use 11 px text so the name
fits. The selected AH view says **Behavioral Boost** and contains:

- Boost slider, 0 to +20 dB; double-click restores 0 dB using the existing slider.
- Type buttons F / C / T, default C.
- Emphasis buttons L / H, default L.

Boost occupies the left part of the existing control row; Type is in the middle
and Emphasis on the right. Tooltips identify the unmeasured behavioral scope and
Fat/Full, Clean and Treble names. AH controls hide for other choices. Existing TC
Gain/Bass/Treble/Focus and MC402 Boost controls retain values and behavior.
There are no Drive controls and no panel redesign.

**Release standalone:**
`/tmp/jrockett-ah-m2/products/NeuralAmpModeler.app`

Built with the Release configuration and `-O3`, then launched from that path.
Installed applications/plugins and AU caches were not replaced or cleared.
Manual inspection/audition remains necessary: check all four tabs, label fit,
control visibility, all six mode combinations, double-click reset, editor
reopening/reselection, and sustained notes under repeated mode changes. Compare
low/high input levels and clean/driven NAM models; check calibration on/off and
normal gate/cab/delay behavior. No screenshot/UI automation or listening result
is claimed by these tests.

## Realtime support and the priming correction

Official realtime project rates are **44.1, 48, 88.2, 96, 176.4 and 192 kHz**.
The wrapper explicitly gates AH to these rates; an unsupported rate falls back
to Off audio while retaining the requested UI selection/settings. Preparing an
offline processor at a higher rate does not confer product support. 768 kHz is
not an M2 realtime requirement.

M2's one-sample check exposed the M1 serial priming burst: about 21 us at 192 kHz
versus a 5.208 us callback budget. We replaced only the replay calculation with
an **eight-sample state recurrence**, precomputed during prepare. For the two
filter states, `z'=M*z+B*x`; replay groups eight inputs as
`z[n+8]=M^8*z[n]+sum(M^(7-j)*B*x[n+j])`. Separate pair sums shorten dependencies
without fast-math. Remainders still use the original per-sample recurrence.

The history window, chronological input, filters, outgoing state and crossfade
weights/timing are unchanged. No additional response runs when settled; there
is no coefficient jump, mute, cold-state audible reset, lock or allocation.
Storage rises by 912 bytes to **124,408 bytes**. The optimized states agree with
serial M1 replay within the new 1e-12 regression bound at startup, full history
and ring wrap. A direct comparison against the saved pre-M2 M1 implementation
across 1,290,000 mixed-event samples at six rates measured maximum difference
**1.777e-15 sample units**. This is a numerical regrouping, not a new sonic profile.

[realtime.csv](realtime.csv) measures the selected AH insertion path, including
selector/scalars and full-history transition onset. Release `-O3`, local arm64
Mac, 400 timed callbacks per case; callbacks of **1, 2, 4, 7, 8, 16, 32, 64 and
128 samples**, all six rates, settled and onset cases. Warmup/allocations/control
publication occur outside timing. The priming burst occurs inside timing.

| Rate | One-sample budget | Transition median | Transition p99 | Observed maximum |
| --- | ---: | ---: | ---: | ---: |
| 44.1 kHz | 22.676 us | 0.625 us | 0.750 us | 1.542 us |
| 48 kHz | 20.833 us | 0.458 us | 0.500 us | 0.542 us |
| 88.2 kHz | 11.338 us | 0.833 us | 0.875 us | 0.875 us |
| 96 kHz | 10.417 us | 0.875 us | 1.000 us | 1.084 us |
| 176.4 kHz | 5.669 us | 1.583 us | 1.750 us | 1.792 us |
| 192 kHz | 5.208 us | 1.708 us | 1.917 us | 2.041 us |

All measured cases met their callback budgets at the median and p99, including
transition onset at one sample. The worst p99 used **36.81%** of its budget. One
raw wall-time outlier at 192 kHz/two samples was **15.875 us** against a 10.417 us
budget (that case's p99 was 1.917 us). Timer/scheduling noise is possible but its
cause was not isolated; do not discard it or claim every observed callback met
its deadline. The bounded work, state-equivalence, allocation checks and typical/
p99 timings support AH realtime use at all six rates on the measured Mac;
it is not a hard scheduler guarantee or a CPU guarantee for arbitrary NAM/IR/
delay combinations. User-level host audition still matters. Subsequent timings
can vary with CPU scheduling; the CSV retains the stated run rather than selecting
new runs to improve it.

## Validation and reproduction

See [validation.json](validation.json). Final suites: **260/260 Debug**,
**260/260 Release**, **260/260 ASan+UBSan**. Focused coverage comprises twelve AH
DSP groups, four AH integration groups, and four existing selector regression
groups. Concurrent handoff passes ThreadSanitizer; strict warnings, static
analysis, project lint, Release APP/VST3/AU and diff checks pass.

Coverage includes all control/message combinations; exactly-one routing; Off,
TC and MC402 preservation; calibration enabled/disabled, model present/absent
and metadata present/absent; pre-gate/pre-NAM observation; reselection/state
retention; allocations during repeated mode changes at tiny callbacks; and
optimized replay versus the M1 recurrence. Software response checks still give
maximum magnitude error **2.994e-13 dB** and warm-crossfade error **7.198e-12**.
No nonlinear/alias project was added.

Run `bash HoldsworthEngine/references/pedals/jrockett-ah/m2/validate.sh` to rebuild
and regenerate evidence under `/tmp/jrockett-ah-m2`. [realtime.cpp](realtime.cpp)
reproduces the timing grid. [audit.py](audit.py) checks preserved sources and
source-level integration/message placement. Builds do not install/launch products
or clear AU caches; launch the stated standalone separately for manual audition.
No M3 measurement or Drive work is included.
