# Circuit analysis of the original BLD

> **M0a primary-reread note:** the machine-readable documentary freeze in
> [`m0a/`](m0a/) supersedes this narrative wherever they differ. In particular,
> it resolves `NREF_AUDIO` as the continuous IC1-pin-10/pin-12/Q4-base/Q1-source
> node biased through R18, selects C17 from `OG` to the suppressor return bus,
> and records C3 as a POR/latch coupling capacitor. D17 and four IC2 pin
> relationships remain explicitly unresolved rather than inferred. It also
> resolves schematic callout A as BAT+, the input ring as the switched BAT-
> return, and treats the faint dual-normalling external-power contacts as a
> named medium-confidence variant rather than an adapter-series D1 assumption.

This analysis is derived from the TC service schematic 1501-3 and parts list
1501-06 (**P-SM**), checked against the TC user manual (**P-UM**), period review
(**C-EEM82**), and the limited hardware evidence in `sources.md`. It is a design
analysis, not yet a released simulation netlist.

The scan's unusual switch symbol and a few crossings remain hard to read. Node
relationships below are assigned confidence individually. A documentary-nominal
netlist may select ambiguous readings as explicit, independently checked
assumptions. Power-off PCB continuity remains mandatory before those readings
are called hardware-verified or authentic, not before nominal M0a-M3 work.

## Rail and node notation

The service drawing uses three electrical references:

- `V+` / drawing up-arrow: applied positive analog rail;
- `0 V` / return symbol: battery/external negative and connector return;
- `VREF`: half-supply audio reference made by R2 = R3 = 47 kOhm and C1 =
  22 uF.

IC1 (4741) uses pin 4 at V+ and pin 11 at 0 V. IC2 (4007) uses pins 2 and
14 at V+ and pin 7 at 0 V.
There is no charge pump, analog regulator, or true split supply in P-SM.
Signals inside the AC-coupled analog circuit are biased around VREF. **High**.

Convenient signal-node names used here:

- `O1`: IC1 pin 7, output of the first/input section;
- `E`: mode-switch output that feeds R23/C15 and the Gain section;
- `OG`: IC1 pin 8, output of the Gain section;
- `OUT14`: IC1 pin 14, output of the final active tone/line-driver section.

These labels are local to this document, not names printed by TC.

## Exact engaged path, as far as the evidence permits

### 1. Input and first 4741 section

```text
input jack tip
-> C8 47 nF coupling
-> R13 1 MOhm to VREF
-> R14 2.2 kOhm series
-> IC1 pin 5 (+)

node after R14 -> C9 220 pF -> 0 V

IC1 pin 7 (O1) -> R15 470 kOhm || C10 220 pF -> pin 6 (-)
pin 6 -> C11 1 uF -> Boost/Distortion switch common
D14/D15 1N4148 antiparallel directly between pins 5 and 6
```

**High**, except the physical mode-switch terminal numbering.

C8/R13 establish input AC coupling and bias; R14/C9 provide input/RF isolation.
R15/C10 provide DC and high-frequency feedback. D14/D15 clamp the 4741's
differential/error voltage; they are not a generic pair placed from an op-amp
output to ground. In normal small-signal Boost they should be inactive. **P/I**.

With the service value R13 = 1 MOhm and a low-impedance source, C8/R13 alone
would imply an approximately 3.4 Hz first-order corner and about 1 MOhm midband
input resistance. This is an **I** sanity check, not a substitute for the whole
circuit impedance. It conflicts with P-UM's 3-3.3 MOhm claim; see below.

### 2. Boost-mode switch state

The functionally coherent reading of the two-pole mode switch is:

```text
BOOST:
  C11 lower end -> O1
  E             -> O1
```

**Medium-high**: both connections are supported by the scan and circuit
function, but the switch blades must be continuity-checked.

The first closure makes C11 an AC feedback path from pin 6 to pin 7, turning the
first section into a low-gain/follower input buffer while R15 preserves DC
feedback. The second delivers O1 directly to E. The deliberate Distortion
transfer is therefore bypassed.

Important nuance: the O1-to-R21/asymmetric-diode/R22/C14/R20-to-E branch remains
physically connected at both ends. Closing O1 to E bypasses its transfer but does
not erase its nonlinear loading. At sufficiently large O1 amplitude it may draw
current or affect first-stage overload. A faithful large-signal Boost model must
retain the branch or prove its equivalent loading negligible. **P/I, Medium-high**.

### 3. Distortion-mode switch state and nonlinear mechanism

The corresponding reading is:

```text
DISTORTION:
  C11 lower end -> P4 wiper
  E             -> R48 22 kOhm -> P4 high end
  P4 low end    -> C12 1 uF -> Q4 emitter network
```

**Medium-high**, with the same continuity caveat.

The signal also reaches E through:

```text
O1
-> R21 4.7 kOhm
-> asymmetric shunt clamp to VREF: DG1 AA119 opposed by D4 1N4148
-> R22 10 kOhm
-> node with C14 10 nF to reference
-> R20 10 kOhm
-> E
```

Q4 is a BC548-B. Its collector connects to V+, its emitter uses R17 = 33 kOhm
to 0 V and C12 to P4, and its base shares `NREF_AUDIO` with IC1 pins 10/12,
Q1 source, R41, and R44. R18 = 10 kOhm biases that distinct node toward VREF;
`NREF_AUDIO` is not a VREF alias. Q4's base-emitter behavior is therefore part
of the coupled P4 feedback/drive mechanism. **Medium-high**; the long reference
wire is awkward in the scan but is consistent with the PCB placement and stage
operation.

The nonlinear system consequently comprises all of the following, not one
memoryless clipper:

1. D14/D15 differential clamps around the first 4741 section;
2. an intentionally asymmetric AA119 germanium / 1N4148 silicon shunt pair;
3. Q4 base-emitter nonlinearity in the P4 feedback/drive network;
4. first-stage finite output/current/rail behavior;
5. R22/R20/C14 post-clip frequency shaping;
6. mode-dependent loading and the reverse-log P4 law.

This topology supports P-UM's description of soft, even/second-harmonic-rich
distortion and special post-distortion filtering, but that prose cannot select
device equations. P4 is 22 kOhm negative/reverse-log, and P-UM publishes a 40 dB
Distortion-control range. Exact endpoints and the actual AA119/Q4 population
remain hardware-calibration items; a later nominal Distortion model may use
separately named generic device/control assumptions without claiming fitted
vintage behavior.

### 4. Gain/Volume stage

```text
E -> R23 1.5 kOhm -> C15 10 uF -> IC1 pin 9 (-)
IC1 pin 10 (+) -> NREF_AUDIO / Q4-base reference, biased to VREF by R18
IC1 pin 8 (OG) -> P1 47 kOhm LOG rheostat feedback -> pin 9
```

P1's wiper is strapped toward the output side in the service drawing. It is an
active feedback control, not a passive output potentiometer. C17 = 10 nF is
connected at the output/control return and must remain in a node-resolved
nominal netlist under the selected crossing assumption.
**High for topology; Medium for the exact C17 crossing/return in this scan**.

Ignoring residual pot resistance and frequency-dependent loading, maximum
inverting magnitude is:

```text
47 kOhm / 1.5 kOhm = 31.33 = 29.92 dB
```

This directly explains P-UM's +/-30 dB range and why unity lies near scale marks
2-3 on a log control. The real minimum is not mathematical minus infinity: it
depends on residual/wiper resistance, wiring, device behavior, and loading.

P-UM says that in Distortion mode P1 changes output level without changing the
distortion character, except when that output overloads the following amplifier.
Its physical position after the distortion core supports that claim. In Boost it
is the main variable attenuation/boost control. **High**.

The service docs call it `GAIN`; some faceplate/secondary descriptions call it
Volume. Either label refers to P1 and must not be used to infer a passive fader.

### 5. Active Bass/Treble network

OG is AC-coupled by C16 = 2.2 uF into a coupled active network around the final
4741 section:

```text
upper/treble path:
  C16 node -> R43 2.2 kOhm -> P3 100 kOhm LIN -> R44 1 kOhm -> pin 12
  C29 1 nF spans P3

lower/bass path:
  C16 node -> R42 1.5 kOhm -> P2 22 kOhm LIN -> R41 1.5 kOhm -> pin 12
  C28 100 nF and C27 100 nF span the two P2 sections

wiper/cross path:
  P3 wiper -> C30 6.8 nF
  P2 wiper -> R45 10 kOhm
  common shaping node -> R46 10 kOhm -> pin 13
```

**High** at component/block level. A checked netlist should preserve exact wiper
orientation and junction dots.

This is an active, loaded Baxandall-family feedback network. Bass and Treble are
continuous and coupled; two independent digital shelves will not generally
match intermediate positions, loading, or phase.

P-UM publishes:

- Bass: +/-16 dB referenced at 100 Hz;
- Treble: +/-16 dB referenced at 10 kHz;
- both nominally 0 dB at center and active in Boost and Distortion.

C-EEM82 instead reports +/-18 dB at 60 Hz and 8 kHz. The source does not state
enough setup information to decide whether this is revision, tolerance, or test
definition. Preserve both observations.

P-UM's wording that each tone control is “switched out” at center is not matched
by a separate center switch in P-SM: P2/P3 are ordinary three-terminal linear
pots in the schematic/BOM/layout. The conservative **I** reading is “electrically
flat/neutral at center,” not physically disconnected. Verify center-detent and
continuity on hardware before calibrated claims; a nominal profile may use the
documentary neutral-center interpretation as an assumption.

### 6. Final active stage, suppressor control, and line output

IC1 pins 12 (+), 13 (-), and 14 (OUT14) form the final tone/summing/line-driver
stage. Here and in the tone-network paths above, `pin 12` names a physical
terminal, not a separate electrical net: frozen M0a binds IC1.12, IC1.10,
R41.2, R44.2, Q1.S, and Q4.B to the same `NREF_AUDIO` node. That node is
distinct from `VREF`, with R18 between them. Its feedback/control path includes:

```text
pin 13 -> C24 1 uF -> (R38 1 MOhm in parallel with Q2 drain-source) -> OUT14
```

Q2 is a **selected BF245-A JFET**. Its channel resistance is controlled by the
suppressor and is also overridden by electronic bypass logic. It changes the
final feedback/gain continuously; it is not an independent hard series mute.
**High**.

Output path:

```text
OUT14
-> C25 22 uF
-> node with R39 10 kOhm return
-> R40 47 Ohm series
-> 1/4-inch output and XLR connection
```

**High**. This directly corroborates P-UM's maximum 50-ohm output-impedance
claim. The drawing contains no separate transformer or balanced XLR driver: the
XLR is attached to the same single-ended post-R40 source. The scan appears to
show XLR pin 2 hot, pin 1 return, and pin 3 unused, but that pin assignment is
**Medium** and needs continuity verification.

P-UM's other terminal specifications are preserved rather than treated as
ideal-model constraints:

| Published item | P-UM value |
|---|---:|
| Frequency range | 10 Hz-40 kHz |
| Maximum input | +6 dBV |
| Maximum output | +8 dBV |
| Input dynamic range | 106 dB |
| Output dynamic range | 110 dB |
| Boost distortion | 0.03% |

Source/load, supply, frequency, and distortion measurement conditions are not
fully specified, so these are validation targets with uncertainty.

## Noise Suppressor topology and behavior

The suppressor sidechain senses the first section at the pin-6/feedback node,
upstream of P1 Gain and the tone controls. The fourth 4741 section, pins 1/2/3,
is the detector/threshold amplifier. P5 = 470 kOhm LOG, R24/R25/C18, and
antiparallel D5/D6 establish nonlinear threshold/detector behavior. **High for
tap and component roles; exact polarity still needs a checked netlist**.

Its output proceeds through C19/R31 into a Q6/Q5 and D7-D9/C20/C21/R28-R32
rectifier/control network. D10-D12, C22/C26, and high-value R33-R36 couple that
control to the Q2 gate and combine it with bypass override. **Medium-high**.

The resulting audio action is a smooth, level-dependent reduction of the final
stage's effective feedback/gain through Q2. It is a downward-expansion/fade
system, not a hard gate and not equivalent to HoldsworthEngine's existing NAM
noise gate. At low level Q2 lowers the effective feedback resistance and audio
gain; above threshold the normal R38 path is restored. **P/I, Medium-high**.

P-UM documents:

- P5 threshold: -50 to -90 dBV;
- expansion slope: 1:4;
- suppression: at least 20 dB;
- claimed S/N improvement: up to 26 dB;
- a smooth fade rather than gate-like on/off action.

The scan is not sufficient to claim attack, release, knee, hysteresis, Q2
resistance curve, or exact D10-D12 control polarity. R33-R36 also have visible
revision/editorial history. A nominal suppressor may later use explicit
documentary/generic variants; verified timing and vintage behavior require
bench data.

All four 4741 sections therefore matter in the complete engaged BLD:

| IC1 pins | Function |
|---|---|
| 5, 6, 7 | Input buffer and Boost/Distortion core |
| 8, 9, 10 | active Gain/Volume |
| 12, 13, 14 | active Bass/Treble, suppressor-controlled final gain, and line driver |
| 1, 2, 3 | suppressor detector/threshold amplifier |

## Electronic bypass, remote, and XLR: what can be excluded

The pedal uses active/electronic bypass. P-UM explicitly says its line driver
remains active while bypassed, so “bypass” is not true-bypass wiring.

Q1 (BF245-A) is the dry/bypass audio FET:

```text
O1 -> C13 4.7 uF -> Q1 drain
Q1 drain -> R19 3.3 MOhm bias/reference
Q1 source -> final pin-12/tone node
Q1 gate -> D13 || R47 10 MOhm control, with C31 220 pF drain-to-gate
```

In effect-engaged operation Q1 is off; in bypass it passes the buffered O1 dry
signal into the final active stage. **High at block level; gate voltage/polarity
and parasitic feedthrough need measurement**.

For an engaged-only plugin model:

| Physical circuitry | Treatment |
|---|---|
| 4007 latch state machine, momentary footswitch, external remote-switch contact | Exclude after replacing them with the selected static engaged control state. That state may be a named documentary assumption for nominal work and must be hardware-verified for calibrated work. |
| Q3, D18 LED, LED timer/pulse network | Exclude; no intentional engaged audio function. |
| Input-jack battery mechanical switching and connector mechanics | Exclude; provide a defined effective analog supply instead. |
| XLR connector mechanics | Exclude; retain the shared C25/R39/R40 output and defined external load. There is no separate balanced audio circuit to emulate. |
| Q1 dry branch | Do not blindly delete. Nominal work should retain the service topology with a named generic BF245/open-state profile, or use an explicitly documented equivalent plus sensitivity test. A calibrated profile requires measured/verified off-state loading. |
| Q2 and associated suppressor audio control | Retain. Q2 is in the final audio feedback path and is the suppressor gain element as well as a bypass override target. |
| First input section and final line driver | Retain. They are part of both engaged and bypassed audio. |

This reduction removes UI/control machinery without changing the engaged analog
path. It does not model the pedal's bypass sound, which would require both Q1/Q2
states and final-stage reconfiguration.

## Power-supply topology and headroom

P-SM shows an unregulated single-supply design. R2/R3/C1 make VREF; C2 bypasses
VPLUS; D1/D3 and the external-power/battery contacts provide isolation,
selection, and protection; D2/R10/C3 participate in POR/latch control. The M0a
selection reads no-adapter contacts as A-VPLUS and PWR_SENSE-0V, with adapter
power applied directly at VPLUS/0V after those contacts open. An alternate
battery-through-D1 spring reading remains explicit. No voltage doubler,
inverter, charge pump, or analog regulator is present. **High for architecture;
Medium for the faint mechanical-contact interpretation**.

Consequences:

- the 4741's effective rail and VREF move with applied supply;
- linear headroom grows with rail voltage and stage load/output-swing limits;
- 1N4148, AA119, and base-emitter forward voltages do not scale with supply;
- clipping balance and Distortion behavior can therefore change with supply;
- C1/reference impedance may move under nonlinear/control current and must be
  assessed rather than replaced automatically by an ideal zero-ohm midpoint;
- a nominal profile may declare its documentary protection-drop interpretation;
  a calibrated profile measures the effective rail after the actual drop.

P-UM's supply statements conflict:

| Location | Claim |
|---|---|
| Operating prose | 8-24 VDC |
| Studio-use prose | 18 or 24 VDC; “max. 18dB” level claim without reference |
| Technical specification table | 8-18 VDC |
| Battery | 9 V |

Additionally, a reported Toshiba TC4007UBP unit and the HBF4007 data-sheet lead
raise 20 V/18 V device-limit questions. The analog 4741 family alone does not
settle system safety. Treat 24 V as a documentation conflict, not a recommended
operating condition or default model profile.

## Impedance discrepancy

P-UM says “very high input impedance (3.3 MOhms)” in prose and typically 3 MOhm
in its specification table for both engaged and bypassed operation. P-SM gives
R13 = 1 MOhm and places it from the C8-coupled input node to VREF. Unless a PCB
revision has another bias path/value, the schematic suggests approximately
1 MOhm rather than 3 MOhm through much of the audio band.

Confidence is **High that the documents disagree; Low on the reason**. Plausible
causes—revision, documentation error, fitted-value difference, or a measurement
definition—remain hypotheses. The P-SM documentary-nominal internal profile may
use its literal R13 = 1 MOhm while retaining the conflict label. P-UM's 3-3.3
MOhm claim belongs in a separate total-input/boundary sensitivity profile; it
must not silently change R13 without circuit evidence.

The impedance conflict also crosses the ADC boundary. In the physical rig,
either input impedance loads the passive pickup/cable network and can change its
level and resonance before conversion. In the plug-in, the audio interface has
already been that load. Applying a 1 MOhm or 3.3 MOhm model after the ADC cannot
retroactively recreate the pickup interaction. Exact internal BLD behavior is
still definable from a declared input-port voltage; reconstructing what that
voltage would have been requires a known pickup/cable source impedance and
interface impedance, and is otherwise an explicitly labelled approximation.
See `dsp-design.md` for the future de-embedding/re-embedding profile.

Output impedance has no equivalent conflict: R40 = 47 Ohm is consistent with
the manual's maximum 50 Ohm, subject to the coupling capacitor and op-amp loop at
frequency extremes.

## Schematic versus parts/layout status

No securely readable schematic-value versus service-BOM-value conflict was
found in this scan—the schematic mostly uses designators rather than printed
values. That does **not** establish that the pages represent one production
revision:

- schematic 1501-3 is dated 1984-02-22;
- layout 1501-6 records 1982-07-29, 1984-02-27, and 1985-10-23 work;
- PCB artwork in the layout says 1501-2;
- parts-list R33-R36 visually differ from the surrounding typeset entries;
- the user-manual input value conflicts with R13;
- P-UM's literal “switched out” tone-center wording has no explicit switch in
  the schematic/layout.

An owner/forum statement that unspecified schematic/layout discrepancies exist
is only an **O** lead and is not promoted here into invented component changes.
A photo-backed owner report names some installed IC markings, but the image
attachments were not independently retrievable during this research and no
complete two-sided trace/value audit is available. Therefore the exact
schematic-to-PCB discrepancy list remains an explicit acquisition task.

## Facts requiring assumptions in nominal code and hardware for calibrated code

The following may be encoded in M0a-M3 only through named documentary/generic
assumptions and sensitivity variants. None may be presented as measured vintage
fact until M4:

- physical terminal numbering and continuity of both mode-switch poles;
- exact P1/P2/P3/P4/P5 angle laws, orientation, and endpoint resistance;
- revision-specific R13 and input impedance;
- exact 4741 manufacturer and TC `GR1` selection criterion;
- exact HBF/Toshiba 4007 population and safe system supply ceiling;
- Q1 off-state capacitance/leakage and the correct engaged control voltage;
- Q2 transfer/control curve and final-stage feedback behavior through the full
  suppressor range;
- suppressor attack/release/knee/hysteresis;
- XLR pin allocation;
- effective rail drops and VREF movement under signal/control load;
- which electrolytic/parasitic aging characteristics belong to design nominal
  versus only a particular vintage unit.
