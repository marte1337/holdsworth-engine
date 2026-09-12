> **Historical Overdrive research — rejected for product integration.** The accepted
> M1c review ended development of these provisional nonlinear profiles: insufficient
> evidence plus failure of the approved fidelity envelope even with ideal resampling.
> Overdrive is deferred pending stronger circuit evidence or hardware measurement.
> Current product scope is [MC402-CLEAN-BOOST-V1](clean-boost-v1/README.md);
> rejected source and reproduction guidance are in the [archive](rejected-overdrive/README.md).
> References below to production, integration blockers or M2 status describe the
> historical milestone only. Its measurements do not describe the new clean Boost.

# MC402 bounded v1 DSP and integration proposal

All numerical choices below marked **V** are reviewable engineering starting
points, not measured pedal specifications. Source labels refer to
[the evidence register](sources.md). M0 is approved and this profile is frozen as
`MC402-BOUNDED-V1-PROVISIONAL`. The [isolated M1 implementation report](m1/README.md)
records the exact realization and measurements. The alias gate fails; the
integration proposal below remains deferred. The approved transfer is unchanged.

## Physical interpretation

The product has two independent effects. Our medium-confidence order conclusion
is input -> Overdrive -> Boost -> output. R2 supports two inverting gain stages
with AC coupling and interstage Gain attenuation; their two inversions preserve
midband polarity. The complete Tone/output/switching topology is unavailable.
No single combined netlist is asserted.

| Boost enabled | Overdrive enabled | V1 transfer before NAM |
| --- | --- | --- |
| No | No | Wire (MC402 latency policy below applies). |
| Yes | No | Boost scalar. |
| No | Yes | Overdrive, Tone, Output. |
| Yes | Yes | Overdrive, Tone, Output, then Boost scalar. |

Boost therefore raises the completed Overdrive output. It does not change the
internal Overdrive saturation in v1, though it can drive the following NAM into
more distortion. Output belongs only to Overdrive; it must not attenuate the
Boost-only route. There is no parallel mix between physical sections.

## Boost

**V:** `y = x * 10^(boostDb/20)`, with `boostDb` in [0,20]. Compute gain targets
outside the sample loop; use an explicit identity operation at settled 0 dB.
Use dB-linear control travel as a software convention, not a verified pot law.

Flat, linear and no oversampling. P1/P2 justify a clean level function; neither
provides a reliable audible EQ curve or overload threshold. Do not add shelves,
pickup resonance, noise, slew or rail clamps just to sound analog. Large signals
can exceed a physical 9 V pedal's headroom: this is a deliberate v1 limitation,
especially with both sections at maximum. Do not clamp to normalized +/-1.

## Overdrive: small, explicit behavioral reduction

**V:** retain the topology suggested by R2 without inventing diode circuitry:

```text
upsample
 -> input high-pass -> inverting gain A1 -> local saturation S1
 -> interstage Gain attenuation -> coupling high-pass
 -> inverting gain A2 -> local saturation S2
 -> Tone low-pass -> output coupling/DC removal -> Output attenuation
 -> downsample
 -> optional Boost at host rate
```

Initial named constants for review:

| Item | V1 starting point | Basis / limit |
| --- | --- | --- |
| A1 | -470/22 = -21.3636... | C: ideal ratio from R2. |
| A2 | -470/22 = -21.3636... | V: adopt R1's reported correction provisionally; original R2 drawing instead yields -45.4545... |
| Input high-pass | First order, `1/(2*pi*22k*47n)` = about 154 Hz | C: ideal stiff-source/inverting-stage reduction of R2. Not a pickup-load simulation. |
| Interstage coupling high-pass | First order, initially 154 Hz | V: simplified approximation; omits pot-dependent loading and the second coupling capacitor's full interaction. |
| Gain law | `a(g) = g^3.321928094887362`, g in [0,1] | V: audio-style attenuation, 0.1 at midpoint; not a measured taper. |
| Saturation swing | H1 = H2 = 2.5 provisional V about bias | V: effective headroom parameter, not a derived OP275 rail specification. |
| Tone | First-order low-pass, `fc(t) = 500 * 16^t` Hz | V: 500 Hz dark, 2 kHz midpoint, 8 kHz bright; no documented numerical range. |
| Output coupling | First-order 10 Hz high-pass | V: remove DC/very slow offsets locally; does not replace stock post-NAM DC blocker. |
| Output law | `a(o) = o^3.321928094887362`, o in [0,1] | V: attenuation from mute to unity; no added makeup-gain control. |

Implement frequency-defined filters with stable, prewarped first-order sections
at the internal sample rate. The high-pass reductions and Tone are approximations
to a loaded analog circuit, not independent components claimed to match it.
Do not add an assumed Tube Screamer mid hump. Tone clockwise removes less treble;
bright does not mean bypass or added treble gain.

For S1/S2, propose a cheap symmetric clip with a narrow quadratic knee. With
`q = abs(z)/H`, use unity for q <= 0.9, cap magnitude at H for q >= 1.1, and
between them use `H*(0.9 + 0.2*u - 0.1*u*u)`, where `u=(q-0.9)/0.2`; restore
the input sign. This has continuous value/slope and tends to harder clipping
at high drive. It is an **assumed op-amp saturation shape**, not an OP275 model.
Two local saturators preserve a meaningful difference between changing input
level and the interstage Gain control. Do not replace them with a final limiter.

At Gain zero, the proposed interstage attenuation mutes steady-state Overdrive;
it is not an automatic clean bypass. Existing coupling/filter histories may
decay first. This endpoint is provisional, inferred from the partial pot drawing,
and must be reviewed/auditioned explicitly. At low input amplitude both stages
become linear; at high input the first can clip even with low Gain. No automatic
loudness normalization is applied. Output zero must mute after smoothing settles.

No asymmetry, dynamic sag, phase reversal, overload recovery, bias pumping,
device capacitance or component tolerance in v1. A fixed effective swing is
enough for an initial audition; the reduced-rail repair reports do not establish
that a time-varying supply simulation is necessary.

## Oversampling, latency and bypass

**V:** preallocated cascaded 2x FIR half-band stages scoped to this processor:
4x at 44.1/48 kHz, 2x at 88.2/96 kHz, 1x at 176.4/192 kHz, subject to the
[aliasing acceptance test](milestones.md). Both saturation stages, their coupling
filters and Tone stay in one rate domain. No resampling of NAM, TC BLD or delay
is added. No processing-time factor changes. Filter lengths are an M1 numerical
design result, not a research question; target <=1 ms round-trip added latency.

The existing `AudioDSPTools/dsp/ResamplingContainer/ResamplingContainer.h` is a
Lanczos model-rate adapter, with `std::function` passed by value, callback error
throws/logging, and startup buffering. Do not assume it meets the new processor's
strict hot-path contract or alias target. Leave this vendored utility unchanged.
A small MC402-owned resampler is justified; a generic analog framework is not.

**V latency policy:** while MC402 is selected, all its section combinations have
the same measured integer delay L. With Overdrive off, use a plain delay copy,
not an identity trip through the resampler. Boost-only also uses that delay.
Both-off output is bit-exact input delayed by L for finite samples; it is
amplitude-transparent, **not zero-latency hardware bypass**. L is zero at 1x.
Choose FIR lengths/alignment with integer host-sample delay; measure rather than
guess L. An engaged 0 dB Boost preserves the corresponding wire/delay samples.

Off and TC BLD retain their existing zero-added-latency paths and are not padded.
Add L to existing NAM latency only when MC402 is selected. To avoid a new dynamic
latency system, the initial development selector changes processors only while
audio is stopped/suspended, followed by lifecycle reset and host latency update.
Do not infer that a stopped DAW transport means callbacks have stopped; coordinate
the existing host lifecycle. Standalone can stop/restart its audio engine.
This is a deliberate M2 audition limitation, not physical pedal behavior.

Inside selected MC402, switches can change during audio: use a short (5 ms)
crossfade between aligned dry and Overdrive output, and ramp Boost gain to/from
unity. Only MC402 runs. Reset dormant Overdrive history on enable; transition
time must include FIR warmup before blending. On disable, finish the fade then
clear its states. Both-off settled operation can be a delayed copy with no
nonlinear processing. Arbitrary processor-to-processor seamless switching is M3
only if needed; never temporarily run TC BLD and MC402 together.

## Existing input convention and provisional headroom

Inspected integration: `NeuralAmpModeler::ProcessBlock` (~line 395) chooses input
gain, `_ProcessInput` collapses to mono, then TC BLD processes `mInputPointers[0]`
in place before the stock gate trigger and NAM. DAW input channels are averaged;
standalone inputs are summed. `_SetInputGain` (~line 1019) and the TC BLD voltage
helpers define:

```text
F(C) = sqrt(2) * 0.775 * 10^(C/20) volts peak per normalized sample unit
calibration active: v = monoInput * 10^(trimDb/20) * F(hostCalibrationDbu)
                   NAM input = pedal(v) / F(modelCalibrationDbu)
fallback:          v = monoInput * 10^(trimDb/20)
                   NAM input = pedal(v)
```

Calibration is active only with a loaded model containing input-level metadata
and the existing input-calibration toggle enabled. **V:** preserve that convention
for MC402. In fallback, explicitly interpret one post-trim unit as one provisional
volt. This assumption affects clipping; it was inaudible as a voltage choice
in a purely linear TC BLD transfer. Do not silently use the UI's default +12 dBu
as a verified interface calibration when calibration is inactive.

At +12 dBu, F is about 4.36 V peak/unit. Thus 2.5 V is about 0.573 normalized
units in that domain, but is a **stage-output** swing, not the pedal input's
clipping threshold. A1 gain, filtering, interstage attenuation and trim determine
when clipping begins. Neither 9 V supply nor digital full scale is a valid
universal clipping ceiling. Calibrated and fallback audition levels can differ.

Input trim intentionally changes pedal drive. Model calibration maps the output
to NAM; it must not be applied as an additional pre-pedal gain. Under an ideal
wire, the bridge reproduces the stock input scaling within floating-point
tolerance. Off must keep the original arithmetic path for bit-exact regression.
MC402 with both sections off should use the stock input scaling plus its pure
latency delay, avoiding a needless volts round trip.

No input de-embedding or source/load reconstruction: the ADC has already seen
the guitar, cable and interface load. The model assumes a stiff voltage source
and high-impedance destination. It cannot recreate a passive pickup's interaction
with the physical pedal by applying the disputed input resistance after the ADC.
No dependency on a particular NAM or IR is introduced.

## Small software architecture and controls

**V:** add a concrete `MC402BoostOverdriveProcessor` under `HoldsworthEngine/dsp`
with double samples, `prepare`, `reset`, `setControls`, `processBlock`, and
`latencySamples`. Follow the existing span/in-place/block-size contracts. Keep
its filters, resampler, smoothing and section switching private.

At integration, introduce `DevelopmentBoostDriveSelection { off, tcBld, mc402 }`
and a single validated selector value. Prepare both concrete members during the
existing reset lifecycle; process through one switch at the current TC BLD site.
The selected branch runs exactly once, or none for Off. Reject invalid selectors;
an unprepared selection takes the stock Off path with coherent gain/latency
state. Do not keep an independently enabled TC BLD chain behind the selector.
Retain that processor's controls and existing behavior untouched.

Keep UI messages temporary and nonserialized, matching the current development
message/control-tag pattern. M2 may add a tiny selector/mapping helper under
`HoldsworthEngine/integration` if needed for direct integration tests. No base
class, virtual dispatch, registry, factory or generic parameter store is needed.
An eventual J. Rockett addition requires another concrete member and switch case.
This proposal does not implement or redesign Development UI v2.

| Future development control | Range | Default when first selecting MC402 |
| --- | --- | --- |
| Processor | Off / TC BLD / MC402 | Off at application startup |
| Boost enabled | Boolean | Off |
| Boost | 0–20 dB | 0 dB |
| Overdrive enabled | Boolean | Off |
| Gain | Normalized 0–1 | 0.5 |
| Tone | Normalized 0–1, dark -> bright | 0.5 |
| Output | Normalized 0–1 | 0.5 |

No supply, clipping-type, calibration, mix, order or quality knob. All numerical
internal choices belong to the named provisional profile. Reuse the existing
voltage helper calculations at the wrapper boundary without changing TC BLD
DSP; their current class location does not justify a broad refactor.

`DelayLoopFilter` is a Yamaha delay-loop abstraction; do not mutate or couple it
to this pedal. Its first-order stable-filter approach is useful precedent, but
MC402 can own a few simple private filter states.

## Realtime contract

One control producer publishes a complete fixed-size snapshot through a bounded
SPSC handoff, following TC BLD's proven ownership pattern without extracting a
framework. Audio owns all histories. Convert pot laws and filter targets outside
the callback; apply snapshots at block boundaries. Smooth gain and stable
first-order filter parameters sample by sample over a fixed 10 ms interval.
Do not interpolate arbitrary higher-order IIR coefficient sets or reset filters
on every drag. Snap exactly to endpoints when ramps finish.

Prepare all scratch/delay/FIR memory, coefficients and callbacks outside audio.
No allocation, locks, I/O, logging, exceptions, unbounded solves or per-sample
pow/tan/exp calls. Reset is bounded and does not allocate. Clamp finite controls
to their ranges; non-finite controls select documented defaults. Engaged processing
replaces non-finite input with zero and prevents state contamination; finite
extreme-value arithmetic must avoid intermediate overflow. Off remains untouched.
Sample-rate changes reprepare/reset, with clear bounded rejection of unsupported
rates/block contracts. Block invariance includes FIR phase, smoothing and fades;
control-event comparisons must apply the same events at the same sample offsets.
