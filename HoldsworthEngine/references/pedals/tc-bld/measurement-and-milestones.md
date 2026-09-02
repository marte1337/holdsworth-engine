# Measurement, validation, discrepancies, and milestones

## Two evidence standards

The surviving service scan is unusually valuable, but it is not a unique,
verified production netlist. Its pages span 1982-1985 revision annotations, its
switch drawing is hard to read, its BOM contains later-looking entries, and the
user manual conflicts with it on input impedance and with itself on supply
voltage.

Those limitations do not prevent documentary-nominal work. They require two
standards to remain visibly separate:

- **Nominal/schematic-derived:** an internally node-resolved and independently
  documentary-checked model of an explicitly named service profile. Ambiguities
  are recorded as assumption IDs/variants. Generic tapers and a generic
  4741-family profile are permitted when named and sourced. Supply, input
  boundary, source/interface, and load are declared assumptions. No physical
  pedal is required for M0a-M3.
- **Hardware-calibrated:** a nominal model whose topology and uncertain values
  have subsequently been checked/fitted against a documented vintage unit.
  Physical evidence is mandatory before `calibrated`, `authentic`, or
  unit-specific claims.

Physical calibration distinguishes design intent from unit population,
aging/tolerance, and repair history. It does not retroactively make the nominal
oracle illegitimate; it changes the evidence and claim level.

Use at least one demonstrably unmodified original BLD. Prefer two units from
different date/PCB populations so that a fitted peculiarity of one pedal is not
mistaken for the whole product when M4 begins.

## Unresolved-discrepancy register

| ID | Conflict/open issue | Documentary-nominal treatment | Hardware-calibrated resolution |
|---|---|---|---|
| U01 | Manual prose says external 8-24 VDC and discusses 18/24 V studio use; specification table says 8-18 VDC | Declare a safe nominal supply profile, initially 9 V and optionally 18 V; record documentary protection-drop interpretation; retain 24 V only as a conflict, not a supported profile. | Identify fitted IC2/capacitor ratings and rail connection; seek revision-matched documentation; test only electrically safe voltages. |
| U02 | Manual claims 3.3 MOhm in prose and typically 3 MOhm in specs; service BOM gives input R13 = 1 MOhm | Preserve R13 = 1 MOhm in the P-SM internal profile. Represent the 3-3.3 MOhm claim only as a separate input-boundary/sensitivity variant; do not silently change R13. | Trace R13/reference on the target PCB and measure complex input impedance versus frequency in engaged/bypass states and its loading of known sources. |
| U03 | Manual tone spec is +/-16 dB at 100 Hz/10 kHz; 1982 review reports +/-18 dB at 60 Hz/8 kHz | Use the service topology/values; retain both published responses as separate validation observations, not alternative fitted component values. | Measure magnitude/phase across control travel with recorded source/load/reference definitions. |
| U04 | TC BOM says only `4741 selected (GR1)`; one unit reportedly contains Exar XR-4741; HA-4741 data also circulate | Use an explicitly generic, source-cited 4741-family profile; mark all bandwidth/slew/swing parameters generic. | Photograph the fitted marking and characterize the installed part/stage behavior; fit only supported uncertain parameters. |
| U05 | TC BOM says HBF4007 UBP; one unit reportedly contains Toshiba TC4007UBP; vendor limits differ | Preserve the BOM entry and collapse control logic only under explicit engaged-state assumptions; do not use owner population to rename the nominal part. | Read the fitted IC2 and supply pins; verify switching states and safe voltage for that unit. |
| U06 | Schematic `1501-3`, parts list `1501-06`, layout `1501-6`, PCB artwork `1501-2`, and several dates coexist | Name the exact compound documentary profile; never call it one known production revision. | Build a serial/date/artwork/component revision matrix from actual units. |
| U07 | R33-R36 appear later/hand-entered in the BOM | Use the legible listed values with uncertainty tags and preserve alternative netlist variants if crossings differ. | Check fitted values/copper destinations; compare early/late boards. |
| U08 | Mode-switch contacts/crossings are difficult to read | Select a functionally coherent contact truth table as an explicit assumption ID; independently re-read the document and keep alternate topology tests. | Map all six terminals by power-off continuity and correlate with PCB traces. |
| U09 | Exact XLR pin allocation is hard to read | Omit connector mechanics while retaining C25/R39/R40 and a declared output load; mark pinout unknown. | Check continuity from connector pins to R40/audio return. |
| U10 | Pot curves are specified only as LOG/LIN/NEG.LOG | Use named, documented generic taper curves with explicit orientation/endpoints; do not call them vintage curves. | Measure total/wiper/end resistance versus shaft angle and center detents. |
| U11 | Suppressor scan crossings, JFET curve, and timing remain underdetermined | Keep the block and nominal parts documented; defer or use separately identified assumption variants when its milestone begins. | Verify detector/control netlist and measure control nodes, transfer, and timing. |
| U12 | Manual states 12 mW/1.3 mA; later HA-4741 family data show several mA at their test supply | Keep both facts; use declared ideal/nominal supply impedance and do not infer sag from the conflict. | Measure total current at each safe supply and identify the actual 4741. |
| U13 | Aging/repairs can dominate electrolytics, leakage, and pot behavior | Use service nominal values without invented aging. | Record repair history; separate as-found, safely serviced, and fitted profiles. |

## Reference-unit intake

Before applying power:

1. Record serial number, enclosure legends, connector types, switch/knob hardware,
   seller/provenance, and all known repairs.
2. Photograph both PCB sides orthogonally at enough resolution to read copper,
   designators, color codes, diode bands, transistor orientation, IC markings,
   capacitor voltage/date codes, and all hand modifications.
3. Record PCB artwork/drawing marks and compare every populated designator with
   `component-register.md`.
4. Map Boost/Distortion switch continuity in both positions.
5. Map bypass and external-bypass switch contacts without guessing from wire
   colors.
6. Identify power polarity and which jack contact switches the battery.
7. Check for shorts, leaking/damaged electrolytics, and unsafe prior repairs.

Do not operate a vintage unit at 24 V merely to investigate the manual conflict.
That requires a separate electrical-safety conclusion based on the fitted parts.

## Source/interface boundary calibration

The physical pedal and an insert plug-in do not see the same upstream circuit by
default. A passive guitar is loaded by the real BLD before conversion; a plug-in
receives a waveform already loaded and possibly colored by the interface. This
boundary is separate from the BLD's intrinsic two-port circuit.

For a future source/interface calibration profile, measure or record:

- pickup open-circuit response where practicable and complex source impedance,
  or a defensible R/L/C pickup model;
- guitar volume/tone settings and network values;
- cable capacitance/resistance used before the pedal or interface;
- interface input resistance and capacitance versus frequency;
- interface gain, headroom, phase, noise, and dBu/full-scale calibration;
- BLD complex input impedance versus frequency for the selected unit/mode;
- the usable frequency range and maximum inverse correction before noise or
  nulls make de-embedding unsafe.

The profile may then de-embed the known interface load and re-embed the selected
BLD input load. It cannot reconstruct clipping, noise, or spectral information
already lost before/during conversion. When these data are absent, label the
input as an already-interface-loaded waveform driven into the nominal BLD from
the declared ideal/source assumption.

For U02, the service-derived 1 MOhm and manual-claimed 3-3.3 MOhm alternatives
must be evaluated at this boundary as well as at the BLD terminals. A passive
pickup can respond differently to them before the ADC. Applying either resistor
inside a plug-in after conversion cannot by itself reproduce that changed
pickup resonance. A buffered or low-impedance reamp source can make the
difference small, but only for that declared boundary condition.

This limitation does not invalidate the internal BLD model: once its input-port
voltage and source boundary are declared, its coupling, control interactions,
active stages, nonlinear behavior, supply/headroom, suppressor, and output load
remain testable independently.

## Measurement metadata

Every capture must include:

- unit ID, PCB/artwork/revision evidence, repair state, and temperature;
- regulated supply voltage at the pedal and supply current;
- source resistance/capacitance and signal-generator output impedance;
- load resistance/capacitance, including whether jack, XLR, or both are loaded;
- converter/interface make, range, measured gain, and dBu/dBV convention;
- sample rate/bit depth or instrument bandwidth;
- switch state;
- each control's measured shaft angle and resistance, not only a faceplate number;
- stimulus RMS/peak level at the pedal input and probe loading.

Use volts at circuit nodes as the shared truth. Digital dBFS files without an
interface calibration cannot calibrate pedal headroom.

## Electrical characterization

### DC, supply, and operating point

- Supply current at 9 V and each other confirmed-safe voltage.
- Voltage after every protection element and at IC1 pins 4/11 and IC2 supply.
- R2/R3/C1 reference voltage, ripple, and source impedance/loading.
- Quiescent output voltage of all four 4741 sections.
- Q1/Q2 drain, source, and gate voltages in engaged Boost, engaged Distortion,
  bypass, and suppressing/non-suppressing states.
- Q3-Q6 DC points where safe and useful.
- Start-up, bypass transition, and recovery from overload.

These measurements define stage-local headroom and reset state. They are more
useful than fitting an arbitrary final clip level.

### Potentiometers and switch states

- Total end-to-end resistance and residual endpoint resistance.
- Wiper-to-each-end resistance versus mechanical angle, with dense samples near
  logarithmic endpoints.
- Center detent position, if present, and electrical flat/unity position.
- Inter-gang question (none is expected, but verify physical construction).
- Switch contact resistance and truth table.

Fit a monotonic taper curve only after retaining raw readings. Do not force a
nominal 10% audio law if the vintage control says otherwise.

### Small-signal audio

At a level at least 20 dB below the first observed nonlinearity:

- complex input impedance versus frequency, engaged/bypassed and both modes;
- complex output impedance versus frequency;
- magnitude and phase from 5 Hz to at least 100 kHz;
- impulse/step response with DC-safe coupling;
- a full physical-control grid including endpoints, center/detent, and multiple
  intermediate positions for Gain, Bass, and Treble;
- interaction sweeps rather than one-control-at-a-time only;
- source/load sensitivity using a low-Z laboratory source, representative
  pickup networks, high-Z amp input, and documented line loads.

For Boost, validate the manual's +/-30 dB, flat-center tone behavior, published
band limits, and the 3 MOhm/1 MOhm impedance discrepancy without making the
published numbers fitting constraints when hardware disagrees.

### Large-signal Boost

- Input/output sweeps at 20 Hz, 100 Hz, 1 kHz, 10 kHz, and near the measured
  closed-loop bandwidth for all relevant supply voltages.
- Stage-node headroom and final output headroom into documented loads.
- THD versus level, individual harmonics, two-tone IMD, and multitone residual.
- Positive/negative asymmetry, transient slew, overload onset, recovery, and
  coupling-capacitor blocking/DC shift.
- Re-run at several Gain/tone settings: an op amp can overload internally even
  when the final output is reduced elsewhere.

### Distortion mode

- Static/quasi-static transfer at the actual nonlinear nodes when probing is
  non-disruptive.
- Harmonic and intermodulation spectra versus level, frequency, supply,
  Distortion angle, Gain, and tone controls.
- Device forward curves or fitted model parameters for installed AA119,
  1N4148s, and Q4 where necessary.
- Post-distortion filter response at low drive and the dynamic response at high
  drive.
- Cross-mode level/phase behavior and switching transient.

Do not fit “even harmonics” as an independent goal. Reproduce the circuit; use
the manual's even/second-harmonic language as a validation observation.

### Noise Suppressor

With internally generated self-noise characterized separately from injected
noise:

- exact detector tap relationship to input and controls;
- threshold versus P5 angle in dBV and volts;
- steady-state input/output curve and slope around/under threshold;
- maximum attenuation and residual feedthrough;
- attack, hold if any, release, and hysteresis using tone bursts;
- response to broadband noise, hum, single notes, chords, and intermodulation;
- Q2 gate/control voltage and effective resistance versus envelope;
- control feedthrough, pumping, modulation products, and supply dependence.

The manual's -50 to -90 dBV, 1:4 slope, and >=20 dB suppression provide sanity
checks. The 1982 review's “below about 5 mV” observation is corroboration only.

## Fitting policy

Keep four layers separate:

1. **Schematic nominal** — literal service values and documented device types.
2. **Documentary assumption** — selected switch/node readings, generic taper and
   4741 profiles, declared supply/source/interface/load, and separately named
   conflict variants used by M0a-M3.
3. **Unit measured** — as-found values, including age and repair state.
4. **Model fitted** — the smallest uncertain parameter set needed to match
   withheld hardware measurements.

Never overwrite a schematic nominal with an assumption, alternative-source
value, or fitted number. The nominal oracle must be reproducible from layers 1
and 2 alone. At M4, fit on one subset of control positions, levels, and
waveforms; validate on positions and stimuli not used for fitting. Report
parameter covariance/non-uniqueness when different components can produce the
same terminal response.

## DSP validation matrix

The nominal implementation needs automated evidence for:

- DC operating points versus independently calculated/SPICE nominal references;
- AC gain/phase and input/output impedance across control grids;
- declared generic-profile control endpoints, center, and intermediate taper
  positions;
- cross-sample-rate equivalence at 44.1, 48, 88.2, 96, and 192 kHz;
- block-partition invariance, including one-sample blocks;
- deterministic reset and mode-change behavior;
- no audio-thread allocation/locks and bounded nonlinear iterations;
- finite handling of silence, denormals, extreme finite input, NaN policy, and
  solver fallback;
- nonlinear transfer, harmonics, IMD, transient overload, slew, and blocking;
- suppressor curve, timing, modulation, and control feedthrough when M6 begins;
- oversampling convergence and in-band alias residual versus the high-rate
  oracle;
- correct latency reporting;
- ideal-wire reference bridge identity with current NAM input calibration;
- unchanged current Holdsworth-delay reference renders when BLD is disabled.

At M4, add hardware evidence for DC nodes, terminal AC/impedance, measured
control curves, nonlinear spectra/dynamics, suppressor behavior, source-loading
sensitivity, and the reference-pedal noise floor. Keep nominal-oracle agreement
and hardware agreement as separate reported metrics.

## Implementation milestones

No milestone below authorizes implementation in this research task.

### M0a — documentary nominal-netlist freeze

- Assign a unique documentary profile ID that names the exact schematic,
  parts-list/layout artifacts, hashes, and date/revision ambiguity. Do not
  collapse `1501-3`, `1501-06`, `1501-6`, and PCB `1501-2` into an invented
  production revision.
- Produce a complete node-resolved netlist from P-SM and independently check
  every node, crossing, component designator, polarity, and switch connection
  against the documentary images.
- Record every unclear mode/bypass contact or node as an assumption ID, its
  selected interpretation, rationale/evidence class, affected outputs, and any
  alternative topology that must remain testable.
- Preserve literal service values separately from manual claims, review data,
  owner reports, and alternate assumptions.
- Define named generic LOG/LIN/NEG.LOG taper profiles with explicit equations or
  tables, orientation, endpoints, and provenance.
- Define a named generic 4741-family profile from cited primary data, clearly
  distinct from TC's unknown `selected (GR1)` population.
- Declare nominal supply/protection interpretation, VREF, source/interface input
  boundary, output load, and Q1/Q2 engaged-state assumptions.

**Exit:** two independent documentary checks agree on all unambiguous nodes;
every remaining ambiguity is traceable in a structured register to a named
assumption or variant. No PCB or physical pedal is required. The result is
labelled `documentary nominal`, not a verified production netlist.

### M1 — offline nominal full-circuit SPICE/MNA oracle

- Instantiate the complete M0a nominal circuit, including explicit static
  engaged bypass assumptions and nominal suppressor/control circuitry.
- Establish stable DC operating points and produce AC/phase, input/output
  impedance, control-grid, transient, overload, and declared-safe-supply sweeps.
- Run sensitivity/variant comparisons for assumptions that affect Boost,
  especially mode contacts, 1 MOhm versus claimed total input impedance, Q1
  off-state loading, generic tapers, 4741 family limits, and rail drops.
- Store golden data with solver version/settings, documentary profile, assumption
  set, generic-device profiles, source/interface boundary, and load identity.
- Cross-check key gains, poles, reference voltages, and limiting behavior with an
  independent formulation or hand calculation.

**Exit:** the oracle is internally consistent, reproducible, numerically
convergent over its declared domain, and hides no ambiguous documentary choice.
It remains a nominal oracle and requires no physical pedal.

### M2 — nominal small-signal Boost processor

- Implement the volts-domain bridge and complete engaged Boost topology selected
  by M0a, with assumption/profile identity preserved in tests and outputs.
- Implement service-nominal coupling/loading, active Gain and coupled tone
  networks, final line driver, and declared source/output boundaries.
- Use named generic taper profiles; do not call their angle response vintage or
  measured.
- Validate magnitude, phase, impedance, control grids, sample-rate independence,
  realtime safety, and block invariance against M1.
- Test the ideal-wire dBu/NAM bridge identity and explicitly label whether input
  is already interface-loaded or source/interface-corrected.

**Exit:** response and impedance meet declared error criteria against M1 across
the nominal control grid. This is a nominal schematic-derived Boost prototype,
not a hardware-calibrated BLD. No physical pedal is required.

### M3 — nominal large-signal/headroom Boost processor

- Add the named generic 4741-family bandwidth, slew, output swing, and overload
  behavior.
- Add declared nominal supply/reference/headroom behavior and retain coupling
  capacitor/blocking state.
- Add oversampling and prove alias/transient convergence against a high-rate M1
  oracle over explicit worst cases.
- Report sensitivity to generic-device, rail, Q1-loading, source, and load
  assumptions rather than tuning them to an imagined pedal.

**Exit:** stage-node and terminal errors meet declared nominal-oracle criteria
over the supported assumption profiles. The result remains nominal.

### M4 — physical vintage-BLD verification and calibration

- Intake at least one documented original unit using the preserved plan above;
  prefer a second unit to separate one-unit tolerance/aging from product
  population.
- Verify PCB/component population, mode/bypass continuity, Q1/Q2 states, input
  impedance, XLR/output wiring, supply rails/VREF, and safe operating voltage.
- Measure pot laws, source/load sensitivity, DC/AC behavior, headroom, harmonics,
  IMD, slew/overload recovery, suppressor behavior, and self-noise.
- Resolve or retain revision-specific variants; never rewrite the M0a nominal
  record.
- Fit only uncertain parameters, validate on withheld points, and preserve
  nominal, as-found, serviced, fitted-unit, and population profiles separately.

**Exit:** state whether evidence supports one measured unit, a named revision,
or a unit population. Only then may the relevant profile be called
hardware-calibrated/authentic, and only within the measured source/interface,
load, control, supply, level, and bandwidth domain.

### M5 — Distortion

- Implement the verified nonlinear cluster and P4 law.
- Validate nonlinear spectra, dynamics, controls, and alias behavior.

### M6 — Noise Suppressor

- Implement detector/envelope and Q2 audio-control behavior.
- Validate curve, timing, attenuation, modulation, and bypass override.

### M7 — complete integration

- Place BLD before stock gate trigger/NAM with explicit level references.
- Keep stock NAM gate separate.
- Handle topology switching, latency, lifecycle, and fallback metadata.
- Prove the existing post-NAM chain and Holdsworth delay are unchanged when BLD
  is disabled.

### M8 — cross-unit and performance validation

- Compare additional original units/revisions.
- Establish CPU, memory, iteration, latency, and alias budgets.
- Decide which revision/calibration profiles, if any, are appropriate for later
  product work. No UI or production-parameter decision is part of this package.

## Entry gates by claim/milestone

### Required before M0a

- Reviewed P-UM/P-SM source copies identified by the URLs, hashes, drawing IDs,
  and evidence classes in this package.
- A declared documentary-profile naming scheme and `nominal / not
  hardware-calibrated` claim policy.
- An assumption/variant register format that never upgrades ambiguity to fact.

These prerequisites are present in the research package; M0a documentary work
may begin without a physical BLD.

Use a compound identifier such as
`TC-BLD-DOC-NOMINAL__S1501-3__B1501-06__L1501-6__AS-<set-id>`. It names the
source documents and assumption set; it deliberately does not assert that they
are one known production revision.

### Required before M1

- The complete M0a node-resolved netlist and independent documentary check.
- Explicit mode/bypass truth-table assumptions and alternatives.
- Frozen service-nominal value layer plus separate conflict/alternative layer.
- Named generic pot and 4741-family profiles.
- Declared supply/protection/VREF, source/interface boundary, output load, and
  engaged Q1/Q2 assumptions.
- A scope statement identifying which parts of the full circuit are resolved,
  assumed, or deferred.

No physical BLD, measured pot, or identified installed 4741 is required.

### Required before M2

- M1 DC convergence and reviewed DC/AC/phase/impedance/control-grid golden data.
- M1 sensitivity results for assumptions material to Boost.
- A specific assumption/profile set selected for the prototype, while
  alternatives remain separately reproducible.
- Explicit generic taper equations/tables and generic 4741 small-signal behavior.
- Explicit nominal supply, source/interface boundary, output load, host dBu
  reference, NAM metadata interpretation, and fallback behavior.
- Declared numerical error criteria and realtime/sample-rate test plan.

No hardware measurement is required. Passing this gate authorizes only a
nominal schematic-derived Boost prototype.

### Required before M3

- M2 validation against the nominal small-signal oracle.
- Named generic large-signal 4741, effective-rail, and overload assumptions.
- High-rate oracle cases and numerical alias/transient acceptance criteria.

### Required before hardware-calibrated/authentic status

- Provenance, serial/date/artwork, repair history, and readable two-sided PCB
  evidence for the target unit.
- Hardware continuity verification of documentary switch/node assumptions and
  fitted component/device population.
- Measured pot laws, rails/VREF/current, complex input/output impedances,
  headroom, AC/phase, nonlinear spectra/dynamics, suppressor behavior, and noise.
- Recorded pickup/source/cable/interface/load/calibration conditions, including
  pre-ADC source-loading limitations.
- Fitting confined to uncertain parameters and validation on withheld data.
- Explicit claim scope: one unit, named revision, or supported population and
  the measured supply/control/level/frequency domain.

Additional prerequisites before M5/M6 remain a documentary-resolved nonlinear
and detector/control topology plus suitable nominal device models. Hardware
data from M4 is required for calibrated Distortion and suppressor claims, but
the nominal record must remain independently reproducible.
