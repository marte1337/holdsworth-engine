# DSP design (no implementation)

## Recommendation

Use a **hybrid circuit-derived processor**:

1. Maintain a complete offline SPICE/MNA model of the chosen original revision
   as the reference oracle.
2. Realize demonstrably isolated linear RC/coupling sections with direct
   analog-derived or topology-preserving-transform (TPT) filters.
3. Realize coupled networks—especially the loaded active Bass/Treble and Gain
   circuitry—as a small state-space/MNA subsystem whose states are physical
   capacitor voltages.
4. Solve only the actual nonlinear clusters implicitly: Distortion devices,
   the suppressor FET where necessary, and reduced op-amp limit behavior.
5. Model each relevant 4741 section with finite bandwidth, slew, output swing,
   and its own rail state; do not add a single global output waveshaper.
6. Run the complete engaged audio core in one oversampled domain when any
   nonlinear behavior is active.

This is the least-complex approach likely to preserve the real control
interactions. A monolithic real-time nonlinear MNA solver would be more general
than needed, while a chain of generic shelves and waveshapers would discard the
very circuit behavior this project is intended to capture.

## Method comparison for this circuit

| Method | Good fit | Problem in the BLD | Decision |
|---|---|---|---|
| Direct transfer functions / TPT | Input/output coupling, RF roll-off, simple buffered RC sections | Independent cookbook shelves do not retain pot loading, active feedback, or switch interactions | Use for proven separable linear sections |
| Small linear state-space / MNA | Coupled tone network, loaded Gain network, finite source/load impedance, capacitor-state continuity | Topology switches need explicit state handling; a needlessly large matrix costs CPU | Preferred for the coupled linear core |
| Full nonlinear MNA in real time | Maximum one-to-one schematic traceability | Stiff op-amp macro-models, larger Newton systems, convergence and CPU complexity | Keep as offline oracle; do not start here for runtime |
| Wave-digital filter | Passive ladders and simple nonlinear one-ports; good passivity properties | Active op-amp feedback, multiports, switches, detector/control paths, and delay-free loops make a whole-circuit WDF disproportionately complex | Not the least-complex whole-BLD choice |
| Circuit-derived local nonlinear solve | Diodes/transistor in their actual feedback/shunt network; FET control element | Requires the correct revision and node topology first | Preferred nonlinear runtime technique |
| Static generic waveshaper | Rapid audition mock-up | Wrong drive impedance, feedback, dynamics, filtering order, rail behavior, and control interaction | Reject as the circuit model |

The nominal oracle should keep the complete engaged netlist, declared nominal
values, explicit assumption variants, strict numerical tolerances, and a high
time-resolution reference. Every runtime reduction should be compared with
oracle DC, AC, impedance, transient, and nonlinear sweeps across the declared
control space—not only at one nominal setting. Hardware comparison is a later,
separate calibration standard.

## Signal-domain architecture

The BLD must be a physical-voltage stage before NAM:

```text
host input
-> current mono/source preparation
-> host/interface reference bridge: normalized sample to volts
-> optional source/interface de-embedding and BLD-load re-embedding
-> BLD circuit in volts, selected documentary engaged order
-> NAM reference bridge: volts to NAM-normalized input
-> existing NAM amp model
-> existing gate-gain/tone/IR/DC-block/post chain
-> existing Holdsworth delay, unchanged
```

At repository commit `fcb5e9630f64262fe2b2eee33aae5c8e26a94d6d`, the
current path applies input staging/calibration before the stock noise-gate
trigger and NAM in `NeuralAmpModeler/NeuralAmpModeler.cpp`. NAM metadata defines
its input level as dBu RMS corresponding to a 0 dBFS-peak 1 kHz sine in
`NeuralAmpModelerCore/NAM/dsp.h`.

Putting the pedal after the existing one-number NAM calibration would expose it
to NAM-normalized units rather than volts and would make its clipping/headroom
depend on the loaded NAM file. The conversion must be split around the pedal.

### Explicit reference-level bridge

For a calibration value `C` in dBu:

```text
V_FS_peak(C) = sqrt(2) * 0.775 * 10^(C/20)
```

where `V_FS_peak` is the analog peak voltage represented by a digital peak of
1.0. With an explicitly defined pre-pedal trim `G_trim`:

```text
v_BLD_in = x_host * 10^(G_trim/20) * V_FS_peak(C_host)
x_NAM    = v_BLD_out / V_FS_peak(C_NAM)
```

`C_host` is the interface/input calibration and `C_NAM` is the model's input
metadata. They are different facts. A BLD output whose normalized value exceeds
`+/-1` must not be digitally clipped before NAM; NAM should receive the physical
voltage translated into its own reference units.

With an ideal-wire pedal and zero extra trim, the bridge reduces exactly to the
existing calibrated gain:

```text
x_NAM = x_host * 10^((C_host - C_NAM)/20)
```

That identity is a mandatory regression test. If NAM input metadata is absent,
use an explicit, documented fallback profile equivalent to current behavior;
never call the fallback a known analog voltage.

This bridge calibrates voltage scale. By itself it does **not** reconstruct the
pickup-loading interaction that occurred before the ADC.

### Pre-ADC pickup, cable, and interface loading

A passive pickup is not an ideal voltage source. Its winding resistance and
inductance, parasitic capacitance, guitar volume/tone network, cable capacitance,
and the next device's input impedance jointly determine level, phase, and the
pickup resonance. In the physical rig, the BLD's input is part of that network
before conversion. In the plug-in rig, the audio interface has already been the
load and its result is baked into the samples.

For a simplified linear Thevenin representation at angular frequency `w`:

```text
V_ADC(w) = V_OC(w) * Z_interface(w) / (Z_source(w) + Z_interface(w))
V_BLD(w) = V_OC(w) * Z_BLD(w)       / (Z_source(w) + Z_BLD(w))
```

If `Z_source(w)` and `Z_interface(w)` are known, a future calibration profile
can estimate the open-circuit source voltage `V_OC` from the captured interface
voltage and then apply the selected BLD input load. A practical profile could
contain:

- a measured complex source impedance or a pickup R/L/C plus guitar-control and
  cable model;
- interface input resistance and capacitance versus frequency;
- analog gain and dBu/full-scale calibration;
- the selected BLD input profile and its evidence class;
- limits for inversion gain/noise amplification and a flag when correction is
  unsafe.

This is de-embedding/re-embedding, not part of the intrinsic BLD circuit. It
cannot recover information lost to interface clipping, noise, an unknown guitar
control/cable state, or a deep/null response, and it cannot be inferred from an
ordinary DI waveform alone. Without a source/interface profile, the plug-in
must state that it applies the BLD to an already interface-loaded signal using a
declared ideal or nominal source boundary.

The 1 MOhm versus 3-3.3 MOhm documentary conflict matters most at this boundary.
For a passive pickup the alternatives can produce different resonance and level
before the ADC; inserting either resistance after conversion does not recreate
that effect. For a low-impedance reamp or buffered source, the difference may be
small, but that must be demonstrated for the stated source.

The nominal internal profile should preserve service R13 = 1 MOhm as the P-SM
component value. A separate alternative input-boundary profile may represent
P-UM's total 3-3.3 MOhm claim for sensitivity analysis; it must not silently
rewrite R13 to 3.3 MOhm without circuit evidence. Hardware calibration later
measures the selected unit's complex input impedance and decides which, if
either, describes it.

What remains modelable from documentary evidence is the BLD two-port from a
declared input-port voltage/source boundary through its internal coupling,
4741 stages, controls, nonlinearities, supply/headroom, suppressor, and loaded
output. Separating the upstream source boundary therefore preserves, rather
than weakens, the validity and reuse of the core circuit model.

| Scope | Digital status before hardware |
|---|---|
| Selected M0a nominal BLD netlist from declared input-port voltage through declared output load | Can be reproduced to stated numerical error relative to the M1 oracle, including internal loading, filter/control interaction, stage-local nonlinearities, supply/headroom assumptions, and state. This is exactness relative to the named nominal model, not proof of an exact vintage pedal. |
| BLD response to a declared ideal, buffered, reamp, or modeled passive source | Can be calculated exactly relative to that declared source model. |
| What an unknown passive guitar would have produced into the BLD instead of the interface | Can only be approximated; requires source/cable/interface de-embedding data. |
| Information already lost through interface noise, clipping, unknown controls/cable, or a response null | Cannot be recovered by the BLD core. |
| Match to an actual vintage BLD's fitted values, tapers, device spread, rails, and dynamics | Requires M4 hardware verification/calibration. |

This design adds no production-facing parameter or UI.

### Placement relative to the two suppressors/gates

The pedal's physical Noise Suppressor is part of the BLD and must remain inside
its circuit. It is not interchangeable with the existing NAM noise gate.

For normal pedal-before-amplifier semantics, the BLD output should feed the
existing stock gate trigger and NAM. The current stock gate may still attenuate
the NAM output using that trigger, but its state and controls remain separate.
Any final integration decision should be regression-tested with the stock gate
both disabled and enabled.

## Proposed processor decomposition

Conceptually, a future mono, double-precision `BldCircuitProcessor` should own:

- an immutable circuit-definition/revision profile;
- separate documentary-nominal, assumption, and later hardware-calibration
  profiles for values, pot laws, device parameters, supply/reference rails,
  source/interface boundary, and load;
- a control snapshot expressed as mode and **mechanical shaft position**, not
  generic EQ/gain coefficients;
- the oversampled circuit and resampler states;
- capacitor, detector/envelope, FET, op-amp, and bounded-solver history owned by
  the audio thread.

These are internal architecture concepts, not a proposal for production
parameters or UI.

Partition only at nodes proven to have sufficiently low impedance that dropping
interstage loading is immaterial. The likely subsystems, pending the selected
and explicitly qualified switch truth table, are:

1. input load, coupling/RF network, and any residual open-switch loading;
2. first 4741 stage plus Boost/Distortion-dependent feedback/drive;
3. selected Boost or Distortion network;
4. active Gain stage;
5. coupled active tone network;
6. suppressor detector/control plus final FET-controlled summing/line-driver;
7. output coupling, 47-ohm source resistor, and defined load;
8. shared supply and half-supply reference.

This list does not assert a new physical order. The final decomposition must be
derived from the resolved original nodes.

## Linear sections and continuous controls

Use physical component values and conductances. For a coupled network, capacitor
voltages should remain the state coordinates while pot position updates matrix
conductances. That keeps stored energy meaningful across parameter changes.

Do not interpolate arbitrary direct-form coefficients through positions that can
be unstable or fail to represent the physical pot. Smooth mechanical shaft
position first, map it through the active profile's taper, then update
resistance. For a nominal model that taper may be an explicitly named and
documented generic LOG/LIN/NEG.LOG curve; for a calibrated model it is the
measured curve. This is especially important for P1 logarithmic Gain and P4
reverse-log Distortion.

For Boost milestone one, an ideal-op-amp small-signal model may temporarily be
used to validate topology and response, but its output must be explicitly marked
“small-signal oracle reduction.” It is not the completed Boost model.

## 4741-family model

Use a reduced closed-loop model rather than integrating a transistor-level 741
macro-model at the audio rate. A documentary-nominal model may start with an
explicitly generic 4741-family profile sourced from the listed Exar/HA data and
label every parameter as family-generic rather than TC-selected. A later
hardware-calibrated profile replaces or fits it. The reduced behavior should
include:

- finite open-loop gain/bandwidth or an equivalent closed-loop pole consistent
  with the actual stage noise gain;
- slew-rate limiting;
- output swing referenced to the local positive rail and reference/negative
  rail, including asymmetry if measured;
- output-current/load limitation only where the hardware operates near it;
- input common-mode limitation only if operating-point analysis shows relevance;
- overload/recovery state if simple rail clamping cannot reproduce measurements.

The family data (roughly 3.5 MHz unity-gain bandwidth and 1.6 V/us slew in the
Exar/HA sheets) are nominal priors, not fitted facts for TC's selected part.
Compare the reduced model first against the nominal full macro-model/oracle and
later against hardware using AC response, steps, overload, recovery, harmonics,
and stage-node headroom.

Each stage clips at its own operating point. A final `tanh`, shared normalized
ceiling, or symmetric global limiter is not circuit-faithful.

## Supply, bias, and headroom

Derive the supply path from the chosen documentary profile:

```text
external/battery voltage
-> socket/protection losses actually in the analog rail
-> effective 4741 rail
-> R2/R3/C1 reference node and its real loading
-> per-stage quiescent point and available swing
```

Initialize reset to the circuit's DC operating point, not zero absolute volts.
Retain coupling-capacitor state so overload can produce real DC shift/blocking
effects. Model ripple or sag only if the schematic and measurement show a
material audio effect; “vintage” is not evidence for invented supply wobble.

For nominal work, declare the adapter/battery voltage, the documentary
interpretation of protection drops, and the resulting rail/VREF; do not imply
they were measured. Treat 9 V and 18 V as separate nominal profiles or sweeps.
Keep the manual's disputed 24 V claim as a conflict and do not expose or endorse
it until the target unit's 4007 variant, capacitor voltage, rail topology, and
safe operation are known. Runtime headroom must follow the selected modeled
effective rail, not a hand-tuned clip threshold.

## Distortion-mode nonlinear strategy

The service evidence shows that Distortion involves multiple real devices and
filters: 1N4148 diodes, an AA119 germanium diode, a BC548-B transistor stage,
mode-dependent feedback/drive, and post-distortion filtering. Once the exact
nodes and orientations are verified:

- diodes inside op-amp feedback require an implicit feedback solve;
- shunt diodes can use a topology-derived monotonic one-port solve;
- the asymmetric silicon/germanium path must retain separate device models;
- Q4 must use the supported BJT topology/operating point rather than a guessed
  even-harmonic polynomial;
- capacitors surrounding the nonlinear devices remain inside that subsystem;
- any 4741 rail overload remains local to the stage that overloads.

A scalar or very small Newton solve seeded from the preceding sample, with a
fixed iteration cap, bracket/safe fallback, and finite-value tests, is likely
sufficient. A lookup table is acceptable only after proving that its dimensions
cover mode, supply, controls, state, and device temperature/tolerance. A single
memoryless table is not the starting assumption.

## Noise Suppressor strategy

Model the suppressor as the documented/derived expander-like control system, not
as a generic gate. Preserve:

- detector tap and any filtering before rectification;
- the 4741 detector section and D5-D9/Q5/Q6 control path as resolved;
- envelope charge/discharge time constants;
- P5's measured logarithmic resistance law and threshold bias;
- knee, residual attenuation, and any hysteresis found in hardware;
- Q2's real JFET resistance/control law in final-stage feedback;
- control feedthrough and modulation distortion where measurable;
- the engaged/bypass override relationship without simulating irrelevant latch
  logic.

The published targets are -50 to -90 dBV threshold, 1:4 slope, and at least
20 dB suppression, but they are not enough to derive attack/release or the FET
curve. Validate detector and audio-control paths separately. Add internal
resistor/op-amp self-noise only after the deterministic transfer is calibrated;
use deterministic seeds in tests if stochastic noise is eventually modeled.

## Oversampling and anti-aliasing

Initial policy, subject to measured convergence:

- process the complete engaged BLD core in a single oversampled domain;
- start at 4x for 44.1/48 kHz hosts;
- at higher host rates, begin with the smallest factor producing approximately
  176.4/192 kHz internally;
- compare with 8x and an offline 16x/32x reference at maximum documented drive,
  all control extremes, rail overload, and suppressor transitions;
- raise the production factor if alias residual or time-domain error fails the
  selected criterion.

Whole-core oversampling keeps clipping, feedback, tone filtering, and blocking
capacitors in the correct order and avoids mode-dependent latency. A strictly
linear, small-signal Boost oracle does not need oversampling, but the completed
Boost model does once rail or slew nonlinearities are active.

The repository's Lanczos resampling container may be evaluated, but reuse is
conditional on nonlinear image rejection, passband/phase behavior, fixed
latency, block invariance, and zero callback allocation. A resampler suitable
for changing a NAM model rate is not automatically sufficient around a
nonlinear circuit. If it fails, prefer cascaded, preallocated 2x half-band stages.

Nominal alias acceptance is numerical: compare against the high-rate oracle
under declared worst cases. After M4, add the physical criterion that residuals
should, where practical, sit below the measured reference-pedal noise floor in
the usable band.

## Realtime-safe change strategy

- Validate/canonicalize a complete settings snapshot off the audio thread.
- Publish immutable snapshots with a lock-free mailbox/double buffer.
- Read one coherent snapshot per block; keep all histories audio-thread-owned.
- Allocate resampling, MNA, solver, and crossfade storage only during prepare.
- Do not lock, allocate, throw, log, or run an unbounded iteration in processing.
- Express smoothing/envelope constants in seconds and derive coefficients from
  the **internal** sample rate.
- Smooth shaft position before taper conversion.
- Recompute the small conductance system directly when a control moves.
- Treat mode/bypass topologies as discrete. Transfer physical capacitor states
  where defined or run a short dual-topology crossfade; never interpolate a
  switch enum into a nonexistent circuit.
- Make one-sample blocks and arbitrary block partitions produce the same stream.
- Define reset, mode-change, sample-rate-change, and supply-change DC behavior.

## Why Boost comes first

Boost is the Holdsworth-relevant path and is the safest way to establish the
infrastructure without prematurely guessing the Distortion/suppressor details.
The first credible Boost milestone still includes:

1. node-resolved engaged I/O loading and static switch states, with unresolved
   contacts recorded as assumption IDs;
2. the selected documentary mode-dependent first-stage configuration;
3. P1 Gain, active Bass/Treble, and the final line driver;
4. explicit generic taper profiles and endpoints, replaced by measured curves
   only at hardware calibration;
5. an explicit generic 4741-family bandwidth/load profile;
6. declared nominal supply, stage-local saturation, and slew assumptions;
7. explicit volts-to-NAM calibration;
8. explicit source/interface boundary, output load, and sample-rate validation.

This yields a **nominal schematic-derived Boost**, not an authentic/calibrated
vintage unit, and it does not include a made-up “Holdsworth setting.”
