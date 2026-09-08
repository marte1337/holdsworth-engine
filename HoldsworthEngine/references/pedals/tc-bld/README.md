# Original TC Electronic Booster + Line Driver & Distortion (BLD)

Research and DSP-design package for a future circuit-faithful BLD processor in
HoldsworthEngine. This directory contains research only: it adds no DSP, plugin
parameter, preset, or UI, and it does not alter the existing Holdsworth delay.

## Status

**M0a documentary IR frozen after a passing fresh-session independent audit;
the intentionally narrow M1 offline engaged-CLEAN-BOOST oracle and M2 golden
references are present under `m1/`; neither is sufficient to claim a
hardware-calibrated vintage BLD.**

The original TC user manual and a four-page TC service packet have been found.
They establish the major topology, nominal parts, controls, published operating
limits, and electronic-bypass arrangement. The scan is not sufficiently clear
to make every switch contact and crossing into a verified simulation netlist,
and the packet itself spans several revision dates. That does not prevent a
nominal netlist and prototype: each ambiguous connection must be assigned an
assumption ID, alternatives must remain separate, and every output must carry
the selected documentary profile. Hardware is required later to replace those
assumptions with evidence and to support calibrated/authentic claims.

The target is the original vintage BLD. The TC Integrated Preamp, later Classic
Booster, PastFX BLD, TC Native plug-in, and neural captures are deliberately not
used as circuit truth. They may eventually be comparison targets, but they may
not fill gaps in the original schematic.

## Two model standards

1. **Documentary nominal:** derived from an explicitly named service-document
   profile, internally node-resolved and independently checked. It may use
   declared generic taper and 4741-family profiles plus explicit supply,
   source, load, and switch assumptions. It may be implemented before a
   physical pedal is available, but must be labelled `nominal`,
   `schematic-derived`, and `not hardware-calibrated`.
2. **Hardware-calibrated vintage BLD:** the nominal model after topology,
   fitted parts, control laws, rails, impedances, nonlinear behavior, and
   dynamics have been checked against a documented physical unit. Only this
   stage may make calibrated/authentic claims, scoped to the measured unit or
   demonstrated unit population.

Nominal service values, alternative readings, owner observations, and fitted
hardware values remain separate data layers throughout.

## Short answer

The BLD's engaged path is not adequately represented by a generic clean boost,
two cookbook shelves, and a waveshaper. The service drawing shows:

```text
input coupling / RF network
-> first 4741 section and mode-dependent feedback/drive network
-> Boost/Distortion routing and the discrete distortion network
-> 4741 Gain stage (P1 is in the active stage, not a passive output divider)
-> active, loaded Bass/Treble network
-> final 4741 summing/line-driver stage, shared with suppression/bypass FETs
-> output coupling capacitor and 47 ohm source resistor
-> paralleled jack/XLR output connection
```

Boost mode bypasses the deliberate distortion network, but it still traverses
the pedal's active input, Gain, tone, suppression/output, coupling, and line
driver circuitry. Its high-level linear response is therefore straightforward,
while its real loading, control laws, finite 4741 behavior, and stage-local
headroom still matter.

The least-complex plausible runtime model is a hybrid: direct/TPT realizations
for isolated RC sections, a small state-space/MNA system for coupled active tone
and loaded-pot networks, circuit-derived local nonlinear solves, and a reduced
4741 model. A full nonlinear MNA/SPICE version should remain the offline oracle.
The complete engaged circuit should be oversampled once nonlinear rail/slew or
Distortion behavior is enabled.

## Pre-ADC source-loading boundary

The physical BLD loads a passive guitar pickup, its controls, and its cable
**before** the ADC. A plug-in receives the waveform after the audio interface
has already imposed its own input resistance/capacitance, gain, noise, and
headroom. Merely simulating the BLD's 1 MOhm or claimed 3-3.3 MOhm input after
conversion cannot retroactively change the pickup resonance or recovered level.

This does not invalidate the BLD core. From a declared voltage at the BLD input
port onward, the internal coupling, active stages, controls, nonlinearities,
supply/headroom, suppressor, and output load remain well-defined modeling
targets. A future source/interface profile can approximate the missing upstream
interaction by describing pickup/cable source impedance and interface input
impedance, then de-embedding the interface load and applying the chosen BLD load.
Without those facts, the nominal model must state its input-boundary assumption.

## Holdsworth constraint

An archived translation of Allan Holdsworth's 1994 *Gitarre & Bass* interview
reports that he used the TC Booster only in clean mode to bring his deliberately
low-output pickups closer to the level of a high-output pickup. A reproduced
2008 *Guitar Player* interview names the Booster + Line Driver Distortion,
repeats clean-Boost/low-output-pickup use, and adds that a booster could push an
amplifier's front end harder. A separate 1999 interview corroborates his reason
for preferring low-output pickups. This establishes role and mode, **not knob
positions**. No exact Holdsworth BLD setting has been found, and this package
invents none.

## Evidence notation

- **P** — primary evidence: original TC documentation or a component-maker data
  book.
- **C** — corroboration: contemporary bench review or a photograph of a vintage
  unit.
- **O** — owner report: useful lead, not design authority.
- **I** — inference: an engineering conclusion from cited evidence, explicitly
  separated from what the source says.

Confidence labels mean:

- **High** — legible primary evidence, with no material conflict found.
- **Medium** — primary evidence exists but revision, drawing, or interpretation
  remains open; or several independent sources converge without a definitive
  hardware check.
- **Low** — ambiguous scan, owner report, or inference that needs hardware.

## Important-fact confidence summary

| Fact | Confidence | Basis and qualification |
|---|---:|---|
| Holdsworth used the clean Boost role, not the Distortion side | High | Independent reproduced 1994 and 2008 artist interviews; no settings claimed. |
| Four sections of one selected 4741-family quad op amp perform input/drive, Gain, suppressor detector, and final tone/line-driver functions | High | TC schematic, BOM, and IC pin table. Exact op-amp manufacturer is not fixed by TC's BOM. |
| Boost retains the active Gain, Bass/Treble, suppressor/output, and line-driver stages | High | TC schematic plus user-manual control descriptions. |
| P1 Gain is 47 kOhm logarithmic and covers a published +/-30 dB | High | TC BOM and user manual. Exact resistance-versus-angle law and residual endpoint resistance are unknown. |
| P2 Bass is 22 kOhm linear; P3 Treble is 100 kOhm linear | High | TC BOM. |
| TC specifies Bass/Treble as +/-16 dB at 100 Hz/10 kHz | High | User-manual specification table. A 1982 review instead reports +/-18 dB at 60 Hz/8 kHz; measurement convention is unknown. |
| P4 Distortion is 22 kOhm reverse/negative-log | High | TC BOM. |
| Distortion is a multi-device, mode-switched circuit, not a generic post-gain waveshaper | High | TC schematic/BOM show silicon diodes, AA119 germanium diode, BC548-B stage, filtering, and differential/feedback interaction. The M0a node-resolved nominal transcription is under `m0a/`; hardware verification is still required for calibrated claims. |
| P5 threshold is 470 kOhm logarithmic; suppressor is a 1:4 downward-expansion/fade system, not a hard gate | High | TC BOM and user manual. Exact time constants and FET transfer require netlist/hardware work. |
| Q1/Q2 are BF245-A JFETs; Q2 is selected | High | TC BOM. Their engaged-state switch/control voltages and parasitics need measurement. |
| Final output uses C25 = 22 uF and R40 = 47 ohm and feeds both output connectors | High | TC schematic/BOM; consistent with the manual's maximum 50-ohm output-impedance claim. XLR pin assignment remains to be checked. |
| Original documentation claims 3 to 3.3 MOhm input impedance | High | User manual. |
| The service BOM instead assigns R13 = 1 MOhm at the input | High | TC BOM/schematic. Whether this denotes a revision or a different impedance definition is unresolved. |
| The analog circuit uses the applied single supply with a half-supply reference; no charge pump or analog regulator is shown | Medium | Schematic interpretation. A nominal model may declare its rail/drop interpretation; hardware must confirm it before calibrated claims. |
| Supported external supply is 8-18 VDC | Medium | TC specification table. The same manual's prose says 8-24 VDC and discusses 18-24 V studio use, so 24 V is a documented conflict, not an endorsed operating point. |
| A vintage example used an Exar 4741 and Toshiba TC4007UBP | Medium for that unit; Low for all units | Owner photographs/report. TC's BOM says only selected "4741" and HBF4007 UBP. |
| Electronic latch, LED timing, footswitch, and remote-switch logic can be omitted from a plugin after replacing their audio switches with the selected engaged state | High at block level | Schematic. The state may be a named documentary assumption in the nominal model and must be hardware-verified for calibrated claims. Q1/Q2 and any residual loading cannot be removed blindly because they touch the final audio/suppressor topology. |

## Package map

- [sources.md](sources.md) — source register, evidence classes, retrieval hashes,
  exclusions, and source limitations.
- [component-register.md](component-register.md) — literal service-BOM
  transcription and control/device notes.
- [circuit-analysis.md](circuit-analysis.md) — engaged path, stage analysis,
  published ranges, bypass exclusions, conflicts, and explicit inferences.
- [dsp-design.md](dsp-design.md) — recommended processor architecture,
  modeling-method comparison, realtime/sample-rate/alias/headroom strategy, and
  pre-NAM level bridge.
- [measurement-and-milestones.md](measurement-and-milestones.md) — hardware
  calibration protocol, validation matrix, staged implementation, and coding
  gates.
- [m0a/README.md](m0a/README.md) — layered documentary IR, assumption variants,
  named profiles, four-state truth table, static checks, and the M1 handoff.
- [m1/README.md](m1/README.md) — dependency-free offline MNA oracle, declared
  small-signal reductions, DC/AC/impedance/control/sensitivity/headroom results,
  reproducible M2 golden data, and focused tests for the frozen primary state.

## Coding and claim gates

- **Before M0a:** name the exact service-document profile and hashes being
  interpreted; commit to nominal-only labeling and an explicit ambiguity
  ledger. No pedal is required.
- **Before M1:** complete an internally node-resolved netlist, independently
  check it against those documents, record every ambiguous node/contact as an
  assumption with alternatives, and freeze separate nominal/generic
  device-control/supply/source/load profiles. The M0a authoring gate now passes;
  the deliberately separate fresh-session freeze audit remains pending. No
  pedal is required.
- **Before M2:** make the M1 nominal oracle solve consistently and produce
  reviewed DC, AC, impedance, and control-grid references; define the generic
  taper laws, generic 4741 profile, input-boundary assumption, and volts-domain
  bridge. No pedal is required.
- **Before hardware-calibrated/authentic status:** document and measure a
  physical vintage unit, verify its PCB/switch topology and component population,
  measure controls/rails/impedances/dynamics, fit only uncertain parameters, and
  validate on withheld measurements. Scope the claim to the evidence.

The detailed gates and revised M0a-M8 sequence are in
`measurement-and-milestones.md`.
