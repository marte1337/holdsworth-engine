# J. Rockett AH Drive: bounded behavioral v1 proposal

2026-09-14 initial design record. **Design and audition renders approved.**
Profile: `JROCKETT-AH-DRIVE-BEHAVIORAL-V1`.
The approved architecture is **pre-drive Bass, 4× oversampled asinh compression,
post-drive Treble, then Volume**. [Isolated M1 is complete](m1/README.md), with
the user's exact-mute Volume correction. The earlier Volume proposal and
pre-implementation review language below are retained as historical context;
the M1 report and C++ profile define the current taper and implementation.
No live Drive, UI, NAM or Boost integration changes; no commit.

The user intentionally authorized an educated behavioral Drive without a verified
circuit. This supersedes the earlier Drive-deferral decision in the Boost M0
package, not its frozen Boost curves or evidence limits. All Drive numbers and
the transfer below are **unmeasured software choices**.

## Evidence and audition context

The [manufacturer manual, page 1](https://images.thomann.de/pics/atg/atgdata/document/manual/allan_holdsworth.pdf)
was rechecked: Drive has Volume, Gain, Bass and Treble; sections work independently
or in the fixed Boost → Drive order. It states development with Allan for live
and studio needs. These facts justify the interface, routing and project context.
They do not specify EQ order, tapers, ranges, clipping, symmetry or sensitivity.
The earlier [source register](../sources.md) contains the bounded circuit leads;
no new trace or hardware measurements are claimed here. No Allan settings inferred.

User-reported Boost M2 audition passed: clean switching, imperceptible transition
delay, a current preference for F/H with **amp** Bass at 0, meaningful Rockett/TC
differences, and possibly conservative adjacent F/C/T distinctions. These are
the user's listening observations, not hardware data or an Allan preset. Boost
remains frozen; its default stays C/L. They motivate useful Drive interaction,
not a retune of Boost.

| Evidence category | Applies here |
| --- | --- |
| Documented product facts | Four Drive controls, independent sections, Boost → Drive, Allan collaboration |
| Circuit evidence | Earlier limited component observations; no verified topology used |
| Technical inference | Upstream level/EQ changes nonlinear excitation; downstream EQ cannot undo intermodulation already generated |
| Assumptions | Symmetric memoryless nonlinearity is sufficient for first audition; no modeled bias, sag, hysteresis or pickup loading |
| Provisional choices | Every equation, frequency, range, default, filter and smoothing proposal below |
| Unknown | Real pedal transfer/dynamics, stage count, EQ positions/coupling, voltage thresholds, revision differences |

## Topology decision

```text
A, recommended: input → Bass shelf → 4× / gain + asinh / ÷4 → Treble shelf → Volume
B, compared:   input → 4× / gain + asinh / ÷4 → Bass shelf → Treble shelf → Volume
```

| Layout | Useful behavior | Tradeoff |
| --- | --- | --- |
| A | Bass adjusts low-frequency excitation: cut for cleaner/tighter attacks; boost for thicker, more compressed low notes. Treble adjusts brightness after harmonics form | Bass interacts with Gain; +6 dB does not promise +6 dB final bass under saturation |
| B | Both tone controls reshape the output directly, making tonal level matching more predictable | Bass cut cannot reduce saturation/intermodulation that low notes already caused |

Choose A for the requested dynamic lead role and meaningful control interaction.
This is a design judgment supported by the offline behavior, not a listening or
hardware-fidelity verdict. Both are identical with Bass at software noon.
Neither borrows a named pedal circuit, fixed mid hump, diode pair or feedback topology.

At ~110 Hz, 0.2 software V peak, Gain 0.75, Treble noon, A's measured THD rises
**2.54 → 5.53 → 9.70%** across Bass min/noon/max. B yields **7.65 → 5.53 → 4.01%**:
its changed harmonic ratios are post-EQ weighting, not changed nonlinear excitation.
The matched stereo render compares A on the left and B on the right.

## Transfer, gain and exact constants

For normalized Gain `g ∈ [0,1]`, pre-shaped input `x`, and `Vref = 1 software V`:

```text
D(g) = 10^(24 g / 20)
m(g) = D(g)^(-0.35)
u    = D(g) x / Vref
y    = Vref m(g) asinh(u)
```

`asinh` is odd and smooth at every finite real input. Its derivative is
`1/sqrt(1+u²)`: exactly 1 at zero, positive throughout, decreasing smoothly with
amplitude. It has no finite plateau, derivative splice, hard knee or dead zone.
Its logarithmic growth preserves some attack-level distinction even at high
drive. This is soft compression/saturation without a modeled supply rail;
output is deliberately not bounded to ±1 or a voltage rail. There is no envelope
detector or sustain generator: cleanup and apparent sustain arise from instantaneous
compression of attacks relative to decaying notes.

Tanh was a credible alternative, but flattens more strongly at large arguments.
`u/sqrt(1+u²)` is also smooth, but had worse high-drive alias residue in this
comparison. Plain `u/(1+abs(u))` was excluded because its second derivative is
discontinuous at zero. The selected function avoids that issue from the start.
At ~4 kHz, 0.5 V peak, Gain max and neutral tones, 4× residual versus each
candidate's own 32× reference was **−91.18 dBr asinh, −80.07 tanh, −67.67 rational**.
This does not imply asinh wins at every signal or matches the pedal.

The explicit `m(g)` compensation limits gain-related loudness changes without
automatic RMS normalization or cancelling the change in curvature. Net small-signal
gain is `D^0.65`: **0 / +7.8 / +15.6 dB** at Gain 0 / 0.5 / 1. At Gain 0 the
stage is unity in the small-signal limit; it still compresses sufficiently hot
input, including an upstream Boost. Increasing Gain changes harmonics and cleanup,
not just amplitude. Volume can then match levels independently.

Measured THD at ~440 Hz, neutral tones, 48 kHz (harmonics 2–31):

| Input peak, software V | Gain 0 | Gain 0.5 | Gain 1 |
| --- | ---: | ---: | ---: |
| 0.05 | 0.0104% | 0.1623% | 2.0853% |
| 0.20 | 0.1638% | 2.1013% | 10.6406% |

The reduced-input rows demonstrate cleanup with fixed controls. This model is
deliberately moderate at middle Gain; high Gain plus Bass/Boost can saturate more.

All sonic constants are centralized in [profile.json](profile.json):

| Constant / control | Proposed value |
| --- | --- |
| Gain | normalized 0–1; internal 0–24 dB; default 0.5 |
| Sensitivity reference / compensation | 1 software V; `D^(-0.35)` |
| Bass | first-order low shelf, 250 Hz, −6…+6 dB, linear dB mapping |
| Treble | first-order high shelf, 2.5 kHz, −6…+6 dB, linear dB mapping |
| Tone noon | normalized 0.5 = exact wire, **software convention only** |
| Volume | −24…+12 dB, default 0 dB; multiplier `10^(volumeDb/20)` after all Drive processing |
| Normalized Volume, if needed later | `volumeDb = −24 + 36v`; 0 dB occurs at `v = 2/3` |
| Proposed control ramps | 10 ms; not exercised by the settled offline oracle |
| Oversampling | fixed 4× at all six supported rates |
| Each FIR | 129 taps; Kaiser β = 8.6; cutoff = 0.5 × base sample rate |

Shelf frequency is the midpoint in dB between asymptotes. For `a=10^(dB/20)`
and `ω=2πf`, analog definitions are `(s+ω√a)/(s+ω/√a)` for low shelf and
`a(s+ω/√a)/(s+ω√a)` for high shelf. Use first-order DF2-transposed realizations
with bilinear prewarping at each center, at the base rate. At ±6 dB the center
is ±3 dB. The reuse of Boost's transparent shelf definition/frequencies is a
software economy; it is not a claim about the Drive circuit.

Volume has no effect on the nonlinear stage or tone filters. It is a pure post
scalar, with 36 dB of matching range; minimum is attenuation, not mute. No
Gain-dependent tone compensation, dry blend, asymmetry, noise, sag, hidden
limiter, extra fixed high-pass/low-pass tone filter or auto loudness matching.

## Antialiasing and the bounded fidelity envelope

Use a symmetric polyphase FIR interpolator and FIR decimator around the sole
nonlinearity. For factor `R`, the prototype uses `firwin(32R+1, 1/R,
window=('kaiser',8.6))`; interpolation multiplies the DC-normalized taps by R,
decimation does not. The chosen R=4 therefore gives 129 taps each way. See the
primary [SciPy FIR](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.firwin.html)
and [polyphase resampling documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.resample_poly.html)
for the offline tool's conventions. This is an implementable FIR design, not an
ideal brick-wall resampler hidden in the product proposal.

The 44.1 kHz single-stage passband error over 20 Hz–10 kHz is at most
**0.000216 dB**; stopband maximum from 0.625 × base rate is **−88.89 dB**.
The top octave is not promised transparent: the FIR transition is intentional.
The reference uses the same normalized FIR construction at 32×, with a 64×
convergence check and a separate ideal Fourier-resampling check at the worst case.
No ADAA, hard-knee patch, adaptive oversampling or expanding alias project proposed.

**Product design envelope:** all six project rates (44.1/48/88.2/96/176.4/192 kHz);
ordinary guitar input roughly 0.02–0.5 software V peak at the Drive input before
Bass, including signals supplied by Boost. Main note content is 80 Hz–4 kHz;
quieter overtones extend through 8 kHz. This is a declared software operating
target, not a claim about all pickups or all possible full-pedal settings.
Fidelity is assessed over **20 Hz–10 kHz**, without relying on a cabinet to hide
errors. Proposed gate: total in-band residual ≤ **−75 dBr** against the high-rate
reference. The residual includes numerical/filter differences as well as aliases;
it is not advertised as an isolated alias-floor measurement or hardware error.

Retained product grid: 9 frequency/amplitude pairs × 3 Gain positions × neutral
and all four Bass/Treble corners × 6 rates = **810 cases per factor**. Pairs are
110 Hz/.02 V, 440/.05, 440/.2, 110/.5, 1000/.5, 2000/.5, 4000/.5,
6000/.05 and 8000/.05; frequencies are moved slightly to coherent odd FFT bins.

| Base rate | Worst 4× product residual |
| --- | ---: |
| 44.1 kHz | −90.99 dBr |
| 48 kHz | −96.49 dBr |
| 88.2 kHz | −109.51 dBr |
| 96 kHz | −111.76 dBr |
| 176.4 kHz | −109.50 dBr |
| 192 kHz | −109.32 dBr |

4× passes all 810; 2× fails 10, worst **−58.06 dBr**. Choose 4× throughout for
one stable implementation. At the worst 4× point, 32× versus 64× differs by
−151.15 dBr; 4× versus the independent ideal 64× reference is −87.39 dBr.
The common FIR contribution is therefore explicitly checked, not hidden by
comparing only similarly filtered paths.

Additional measurements cover a declining-harmonic guitar multitone, actual DI
at three levels/Gain positions and the tone corners, and all six frozen Boost
modes at 0/6/12/20 dB feeding Drive. Their sampled success does not certify every
possible broadband waveform or every high-level Boost/Gain combination.
The 13 DI comparisons at 48 kHz have worst residual **−117.25 dBr**; the 24
Boost cascades have worst **−116.75 dBr** (maximum sampled Drive input peak
0.601 software V). The settled Boost transcription agrees with retained M1
production response measurements within **5.13 × 10⁻¹² dB**; no Boost transition
behavior is reimplemented or retuned in these offline cascade renders.

Separate overload observations: a 2 V guitar multitone remains finite with small
reference residue; a **10 V, 0.45 × sample-rate sine fails fidelity**, with full-band
residual about −25.3…−23.3 dBr. It is finite, but audibly unreliable territory.
Its in-band dBr can be positive because the reference fundamental is outside the
analysis band. Neither this torture failure nor its tiny in-band denominator
defines the normal fidelity gate. Stacking all maxima on a hot pickup is also
outside the guaranteed design envelope; lower Boost/Gain is the intended control.

## CPU, latency and later implementation boundaries

The causal FIR pair adds exactly **32 base samples**: 0.726/0.667/0.363/0.333/
0.181/0.167 ms at the six ascending rates. The zero-centered offline renders
remove that delay for comparison; a direct causal `upfirdn` realization matches
them exactly after shifting 32 samples. Do not implement a callback-sized delay
or falsely report zero latency.

Conservative unsymmetrized polyphase cost is about **258 FIR multiply-accumulates
plus four asinh evaluations per input sample**, two first-order shelves and a
few scalar multiplies. At 192 kHz that is ~49.5 million FIR MAC/s and 768,000
asinh evaluations/s; at 48 kHz ~12.4 million and 192,000. This is an operation-count
estimate, **not a measured realtime CPU percentage**. A small fixed state allocation
(well below 8 KiB excluding existing Boost and optional block scratch) is sufficient.
Use native `asinh` initially; any faster approximation would need its own error
bound against this oracle. Benchmark the actual causal C++ path at 1/2/4/8/32/64/
128-frame callbacks and all rates during isolated implementation.

Future Gain/Volume ramps can linearly ramp precomputed positive D, compensation
and output scalars over `ceil(.010 fs)` base samples, with smooth interpolation
across their four internal samples. During a ramp D and compensation need not
obey the settled power law exactly; endpoints do. Bass/Treble may ramp their
normalized first-order coefficients over the same interval, preserving states;
the real pole remains between stable endpoints. No filter resets on continuous
tone changes. Use a complete control-tuple handoff, latest target wins; do not
recompute exponentials/tangents in the audio callback. These transition details
are a proposal for M1 tests, not an implemented or verified property here.

Keep one concrete AH owner with Boost and Drive members and fixed routing:

| Boost enabled | Drive enabled | Later internal route |
| --- | --- | --- |
| Yes | No | Existing Boost, unchanged |
| No | Yes | Drive |
| Yes | Yes | Existing Boost → Drive |
| No | No | Local bypass |

Retain the outer four-choice selector and run only its selected pedal. Existing
Boost mode crossfades remain inside Boost; their smoothly varying signal naturally
feeds Drive. Boost is not normalized away before Drive and Volume belongs after
Drive. Full +20 dB Boost is useful with lower input/Gain; it need not produce a
proportional output-level increase when both sections run.

Reuse the calibrated pre-NAM input scale at a later integration boundary, but
**1 V here is an arbitrary software sensitivity anchor, not measured AH headroom**.
With the existing uncalibrated fallback, one post-trim sample unit is one software
volt. A calibrated input-scale change will now change Drive saturation, as it
should for the defined nonlinear model; do not infer a 9 V clipping threshold.
No extra normalization based on Boost mode or loaded NAM is proposed.

Latency policy needs review **before live integration**: the existing Off/Boost/TC/
MC402 paths have zero added latency. Prefer keeping those paths intact and reporting
32 samples only while Drive runs, with a host-safe control-thread latency update
and an aligned section-toggle transition. If the host cannot support that reliably,
constant whole-plugin compensation would alter bypass timing and needs separate
approval. The isolated Drive design does not silently choose that broader change.
No section bypass automation or host latency negotiation is prototyped here.

## Compact deliverables, reproduction and review

- [profile.json](profile.json): exact proposed constants.
- [prototype.py](prototype.py): independent offline oracle and focused assertions.
- [results.json](results.json): machine-readable summary, input/source hashes, audio cue times.
- [measurements.csv](measurements.csv): one combined numeric table, including harmonics and failed stress cases.
- [overview.png](overview.png): one four-panel plot.
- [gain-cleanup-boost.wav](gain-cleanup-boost.wav): mono, 48 kHz/16-bit, 24.75 s. Dry; Gain 0/0.5/1 each at .2/.05 V input; then F/H at Boost 0/6/12/20 dB into Gain 0.5 using .05 V source. Two-second segments with .25 s gaps. Common ×0.85 export scale preserves dynamics and relative levels.
- [tone-topology-AB.wav](tone-topology-AB.wav): stereo, 48 kHz/16-bit, 13.5 s. Bass min/noon/max then Treble min/noon/max, Gain .75. Left A/right B; each channel separately matched to RMS .12 for timbre comparison. Isolate channels to compare the layouts. These are not stereo pedal designs.

Renders use the local `REAPER/Guitar DI.wav` excerpt at 9–11 seconds, deterministically
selected for energy, tapered over 10 ms, rescaled to the documented software peaks.
Its original physical voltage is unknown. There is no NAM, cabinet, EQ sweetening
or limiting in the renders. Full-precision measurements precede 16-bit export;
the WAVs are for listening, not −90 dB numerical analysis. No listening verdict
is claimed by the agent. Generated audio occupies about 5 MB total, in two files.

From the repository root, in a temporary environment with NumPy/SciPy/Matplotlib:

```sh
python3 -m venv /private/tmp/ah-drive-design-venv
/private/tmp/ah-drive-design-venv/bin/pip install numpy==2.0.2 scipy==1.13.1 matplotlib==3.9.4
/private/tmp/ah-drive-design-venv/bin/python HoldsworthEngine/references/pedals/jrockett-ah/drive-v1/prototype.py
```

Use `--out /private/tmp/ah-drive-recheck` to avoid replacing retained results.
Only settled controls are prototyped. Assertions cover the declared product and
DI/cascade gates, reference convergence, exact Volume scaling, odd symmetry,
silence, small-signal slope, causal-delay correspondence and the offline Boost
transcription against retained M1 measurements. Existing DSP source is unchanged;
no production builds or full live test matrix are warranted by this reference-only task.

**Review before implementation:** approve A's interactive Bass, the asinh character,
1 V sensitivity/24 dB gain staging and output matching range by listening to the
short renders. Approve this profile's 4×/32-sample design and fidelity envelope.
Then isolated Drive M1 should implement the causal path with focused numerical,
control-ramp, finite/nonfinite recovery, reset/reprepare, allocation/lock and block
partition tests plus real CPU measurement. Live M2 follows only after isolated
approval and a latency-policy decision. Hardware measurements or tone refinement
remain optional M3 work prompted by a specific question. Stop here; no live Drive.
