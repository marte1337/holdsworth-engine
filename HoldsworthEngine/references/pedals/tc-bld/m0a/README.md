# TC BLD M0a documentary nominal-netlist freeze

Status: **authoring-complete documentary package; fresh-session independent
freeze audit pending**.

This package is a reproducible, node-resolved reading of the exact TC service
packet registered as P-SM. It is not a hardware-calibrated circuit, a claim
about every production revision, an M1 SPICE oracle, or realtime DSP.

The source PDF is identified by hash and byte length in `manifest.json`; it is
not stored in the repository.

## 1. Documentary profile ID

`TC-BLD-DOC-NOMINAL__S1501-3__B1501-06__L1501-6__AS-M0A-001`

This is deliberately a compound documentary identifier:

- schematic 1501-3, dated 1984-02-22;
- parts list 1501-06, whose printed/later handwritten revision fields are not
  collapsed into a single date;
- parts layout 1501-6, with 1982-07-29, 1984-02-27, and 1985-10-23 entries;
- PCB artwork 1501-2 visible in the layout.

It does not mean “production revision 6.” The exact P-SM payload is 203,201
bytes, SHA-256
`6d794b2ce05a93f53c579303a5b42682604e1c7922e14c064e30ea9175c0ac62`.

## 2. Files in the M0a package

| File | Role |
|---|---|
| `manifest.json` | Identity, exact sources, selected state/profiles, scope and M1 handoff |
| `source-evidence.json` | Primary-source policy and page/drawing/region anchors |
| `service-components.json` | Immutable service designators, values, taper classes, parts and population |
| `circuit.json` | Nodes, all component terminals/pins, connectors, switch terminals and contact table |
| `assumptions.json` | Typed selected interpretations and alternatives with evidence and impact matrices |
| `configurations.json` | Mode/bypass/suppressor/supply axes, four-state truth table and named oracle states |
| `generic-profiles.json` | Provisional tapers, 4741, JFET static states, supply and I/O boundaries |
| `documentary-cross-check.json` | Per-designator derived matrix, material check subjects and audit status |
| `schema/tc-bld-documentary-ir-v1.schema.json` | Editor-facing format/enumeration schema |
| `tools/validate_m0a.py` | Dependency-free semantic validator and deterministic M1 materializer |
| `tests/test_validate_m0a.py` | Authoring, freeze-gate and mutation regression tests |

No existing NAM, delay, Holdsworth DSP, integration, UI, or production
parameter source is changed by this package.

## 3. Circuit representation

The normative representation is a layered documentary IR rather than raw
SPICE. `service-components.json` is immutable evidence; topology and terminal
bindings live separately in `circuit.json`; uncertain readings can change only
through the typed variants in `assumptions.json`.

Every populated component terminal has exactly one explicit node. R26 is kept
as `NOT USED` with its depicted layout footprint and has no electrical
terminals. Resistors/capacitors use terminals 1/2, diodes A/K, BJTs C/B/E,
JFETs D/G/S, pots 1/wiper-2/3, and both IC packages expose pins 1-14. Rails,
0V, the real R2/R3/C1 VREF, connector terminals and switch contacts are named.
No global “NC” net shorts unrelated unused terminals together.

The primary reread corrected three material simplifications in the earlier
research narrative:

1. `NREF_AUDIO` is one continuous node joining IC1 pins 10 and 12, Q4 base,
   Q1 source, R18, R41, and R44. It is biased toward VREF through R18; it is not
   an ideal VREF alias.
2. C17 is selected from `OG` to `NRETURN`, the long bus shared by D9 and the
   R33/R34/D10 returns.
3. C3 is the POR/latch coupling capacitor from `POR` to `N_LATCH`, not ordinary
   supply decoupling.

D17 and four unreadable IC2 pin relationships are not guessed. Their distinct
`*_UNRESOLVED` nodes keep the complete population and terminal inventory
machine-visible. The first engaged-Boost oracle freezes Q1/Q2 explicitly, so
it need not invent the unreadable latch wiring.

The supply connectors are also explicit. Schematic callout `A` is the battery
positive net; battery negative reaches 0V through the input-jack ring/sleeve
contact when a mono plug is inserted. The selected external-power reading has
two normally closed contacts when no adapter is present (`A`-VPLUS and
PWR_SENSE-0V); adapter insertion opens both and applies power directly at
VPLUS/0V. D1 therefore isolates the battery in adapter operation rather than
sitting in series with adapter power. The faint jack springs retain a typed
alternative under `A-POWER-JACK-CONTACTS`.

To validate and print the deterministic resolved-handoff digest:

```sh
python3 HoldsworthEngine/references/pedals/tc-bld/m0a/tools/validate_m0a.py --gate authoring
```

M1 may materialize a temporary canonical JSON file with
`--emit-resolved /tmp/tc-bld-m0a-resolved.json`. Generated resolved JSON or
SPICE is intentionally not checked into M0a.

Runnable ambiguity variants and named configurations are materialized without
editing the evidence layers, for example:

```sh
python3 HoldsworthEngine/references/pedals/tc-bld/m0a/tools/validate_m0a.py \
  --select A-P2-TAPER=P2-REVERSED-SHAFT-SENSE \
  --configuration STATE-ENGAGED-BOOST-18V-SENSITIVITY \
  --emit-resolved /tmp/tc-bld-m0a-variant.json
```

Variants classified as evidence-acquisition requirements or rejected
interpretations are intentionally refused. The resolved handoff contains the
selected profile definitions, device states, orientation transforms, active
contact closures, and canonical node-equivalence classes—not merely a copy of
the source JSON.

## 4. Component/designator completeness

The service universe is exactly 111 designators:

- R1-R48;
- C1-C31;
- P1-P5;
- Q1-Q6;
- D1-D18 and DG1;
- IC1 and IC2.

There are 110 populated entries. R26 remains the sole `NOT USED` item; its
labeled PCB footprint is retained as layout evidence. The original value,
power/dielectric/tolerance wording that is readable, selected-part notes, and
LOG/LIN/NEG.LOG service classes are preserved separately from model profiles.
The validator hard-codes the full nominal resistor, capacitor and pot contract
so an accidental value edit does not self-validate merely because two JSON
layers were changed together.

## 5. Selected mode/bypass truth table

| Mode | Pedal state | Mode closures | Q1 dry FET | Q2 final-feedback FET | Use |
|---|---|---|---|---|---|
| Boost | engaged | C11 distal-O1; E-O1 | off, branch retained | off/full-level frozen | **First Holdsworth M1 oracle** |
| Distortion | engaged | C11 distal-P4 wiper; E-R48 | off, branch retained | dynamic suppressor | Later documentary oracle |
| Boost | bypass | Boost contacts remain physical | on | on/bypass override | Buffered dry into active line driver |
| Distortion | bypass | Distortion contacts remain physical | on | on/bypass override | Buffered dry into active line driver |

The first configuration ID is
`STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY`. It uses the explicit post-protection
9 V rail profile, the service R13 input boundary, and the generic studio I/O
boundary.

Boost's O1-to-E closure bypasses the intended Distortion transfer but does not
delete the fixed O1-R21-D4/DG1-R22-C14-R20-E branch. That branch remains a
possible large-signal load in every materialized Boost state.

## 6. Unresolved assumptions and variants

Every entry below has a selected interpretation, at least one alternative,
source evidence (or an explicit statement that none exists), confidence,
electrical consequence, and impacts for Boost small signal, Boost large signal,
Distortion, Suppressor, and bypass.

| ID | Unresolved subject | Selected documentary interpretation |
|---|---|---|
| `A-MODE-CONTACTS` | Nonstandard six-contact mode symbol | Functionally coherent Boost/Distortion table above; physical numbering unknown |
| `A-Q1-STATE` | Engaged/bypass channel and residual loading | Off but complete branch retained when engaged; generic on in bypass |
| `A-Q2-STATE` | Suppressor endpoint and bypass override | First oracle frozen off/full-level; dynamic and bypass-on variants retained |
| `A-C17-RETURN` | Faint crossing | OG to NRETURN |
| `A-R13-BOUNDARY` | Service 1 MOhm versus manual 3-3.3 MOhm | Keep R13=1 MOhm; manual value is a non-default boundary sensitivity only |
| `A-DOCUMENT-COMPOUND` | Drawing IDs/dates | Compound profile, not one production revision |
| `A-P1-TAPER` | LOG law/orientation/residual | Generic 10%-midpoint LOG; P1 wiper/end strapped to OG |
| `A-P2-TAPER` | LIN law/CW sense | Ideal LIN on depicted Bass terminals; reversible shaft sense |
| `A-P3-TAPER` | LIN law/CW sense | Ideal LIN on depicted Treble terminals; reversible shaft sense |
| `A-P4-TAPER` | NEG.LOG law/end numbering | Generic reverse-log on R48/wiper/C12 terminal ordering |
| `A-P5-TAPER` | LOG law/orientation | Generic 10%-midpoint LOG on detector terminals |
| `A-4741-GENERIC` | Manufacturer, GR1 criterion, device behavior | Family-generic 4741 only |
| `A-SUPPLY-VREF` | Protection drop, supply conflicts and reference | Explicit effective 9 V first-oracle rail; solve R2/R3/C1 VREF |
| `A-SOURCE-LOAD` | Undocumented test termination | 1 kOhm source and 1 MOhm || 100 pF output load |
| `A-XLR-PINOUT` | Faint connector pin numbers | Pin 2 hot, pin 1 return, pin 3 NC; quarter-inch path is authoritative for first oracle |
| `A-D17-IC2` | D17 endpoints and IC2 pins 4/5/9/11 | Preserve separate unresolved nodes; static oracle state avoids guessing |
| `A-CAP-POLARITY-SIGNAL` | C11/C12/C13/C15/C16/C24 physical plates | Preserve endpoints but leave +/- plate identity unresolved |
| `A-D14-D15-NUMBERING` | Crowded clamp labels | Opposed pair fixed; selected individual numbering can be swapped with no electrical change |
| `A-CONTROL-DIODE-POLARITY` | Dense D7-D13 cathode bars | Use visible-bar A/K reading, retain verification variant |
| `A-R33-R36-REVISION` | Later/hand-entered BOM values | Populate the literal legible values with revision warning |
| `A-R35-SYMBOL` | Side/strap-like drawing mark | Fixed 2.2 MOhm as the BOM specifies |
| `A-JFET-DS-IDENTITY` | Layout labels only Q1/Q2 gate pads | Use schematic functional D/S identity; keep swap variant |
| `A-POWER-JACK-CONTACTS` | Faint input/power-jack normalling contacts | Battery A-VPLUS and sense-return normally closed without adapter; input ring switches BAT- |
| `A-LAYOUT-READABILITY` | R25/D16 legends and undefined handwritten/callout marks | Preserve unreadable/undefined status; never promote the marks to values or nets |

## 7. Generic profiles

All profiles say `measured: false` and carry evidence and limitations.

| Profile | Selection |
|---|---|
| `TAPER-LIN-IDEAL-V1` | `f(x)=x`; P2/P3 |
| `TAPER-LOG-10PCT-MID-V1` | `f(x)=x^3.321928094887362...`; P1/P5; 10% at midpoint |
| `TAPER-NEGLOG-10PCT-MID-V1` | `f(x)=1-(1-x)^3.321928094887362...`; P4; 90% at midpoint |
| `OPAMP-4741-FAMILY-GENERIC-V1` | Standard quad pinout and condition-tagged family typicals; no TC maker/GR1 inference |
| `JFET-BF245A-DOCUMENTARY-STATIC-V1` | Generic on/off R/C envelope for Q1/Q2 state reduction, not a device fit |
| `SUPPLY-9V-EFFECTIVE-V1` | Selected explicit post-protection VPLUS source; VREF remains dynamic |
| `SUPPLY-9V-ADAPTER-DIRECT-V1` | Non-default adapter-at-VPLUS variant; D1 isolates the battery rather than sitting in adapter series |
| `SUPPLY-9V-BATTERY-SWITCHED-V1` | Non-default battery profile with both power-jack and input-ring contact states explicit |
| `SUPPLY-18V-EFFECTIVE-SENSITIVITY-V1` | Non-default sensitivity only; 24 V remains a conflict, not a runnable profile |
| `INPUT-BOUNDARY-SERVICE-INTERNAL-V1` | Selected; no extra impedance element and R13 remains 1 MOhm |
| `INPUT-BOUNDARY-MANUAL-SENSITIVITY-V1` | Non-default 3-3.3 MOhm behavioral target; never edits R13 |
| `BOUNDARY-STUDIO-GENERIC-V1` | Selected 1 kOhm source, 1 MOhm || 100 pF load, XLR unloaded |
| `BOUNDARY-10K-LOAD-SENSITIVITY-V1` | Non-default output-load sensitivity |

The generic 4741 family numbers come from the registered primary Exar and
Renesas/Intersil component sources. They remain conditional dual-supply family
typicals; M1 must state any mapping to this pedal's single-supply operating
point.

## 8. Uncertainties material to Boost

The following can materially change the first or later Boost oracle:

- mode-switch contacts determine both first-stage feedback and the O1-to-E
  route;
- battery/input/power-jack contacts determine the physical supply path; the
  first oracle avoids that uncertainty with a named effective-VPLUS fixture;
- Q2's selected full-level state determines final-stage feedback/gain;
- the real `NREF_AUDIO` coupling, supply/VREF impedance, generic 4741 limits,
  P1/P2/P3 laws, and source/load boundaries change linear response;
- Q1 off-state loading and C17/NRETURN coupling may change small signal and can
  matter more at large signal;
- P4 orientation and the still-connected Distortion branch chiefly affect
  large-signal Boost loading;
- supply voltage/protection interpretation and 4741 swing/slew/current limits
  are directly material to large-signal headroom;
- signal-electrolytic plate orientation is immaterial to ideal linear C but may
  matter once leakage/nonlinearity is modeled;
- D17/IC2 and D7-D13 affect Boost only through correct control-state selection.
  The first oracle makes that dependency explicit by freezing Q1/Q2 instead of
  solving the unreadable latch.

R33-R36 revision risk is material to a dynamic suppressor run, but not to the
first oracle while Q2 is frozen. Swapping identical D14/D15 reference numbers
has no electrical effect.

## 9. Documentary consistency checks

The authoring pass directly re-read all four pages of the exact hashed
P-SM rather than transforming `circuit-analysis.md`. It then cross-checked:

- every BOM designator/value and the 111/110/R26 inventory contract;
- every audio/control component endpoint readable on schematic 1501-3;
- IC1 pins, readable IC2 pins, diode bars, BJT/JFET terminals, pot wipers,
  mode contacts, rails/reference, all coupling/filter capacitors, and output;
- layout/artwork placement, IC orientation, Q1/Q2 gate marks, D17 presence,
  R26 footprint, connector labels, mirrored viewing side, and revision dates;
- the resulting JSON graph for missing/duplicate terminals, undefined nodes,
  accidental resistor shorts, invalid switch contacts, missing assumption
  impacts/evidence, forbidden value mutations, truth-table coverage, taper
  endpoints/monotonicity, and selected-profile consistency.

`documentary-cross-check.json` expands explicit, source-anchored observation
groups to exactly one status for every designator against schematic, BOM, and
layout. The dimensions are deliberately separate: BOM establishes values,
schematic establishes electrical endpoints, and layout establishes placement
only. It prevents R25/D16 legends, D7-D15 identity/polarity, D17 endpoints, and
R26 population from being overstated.

The regression suite covers service-value and terminal mutations, duplicate
JSON keys, accidental shorts, observation overlaps, variant-classification
gaps, deterministic resolution, executable alternatives, refused
evidence-acquisition variants, power-contact alternatives, dynamic-state
readiness, and stale audit digests. The `freeze` gate deliberately fails until
a distinct fresh-session audit record for the exact source and package is
added.

## 10. What prevents an independent M0a audit

Nothing in the repository prevents the requested fresh-session audit. The
auditor needs access to the exact P-SM bytes identified by hash; the PDF is not
vendored here. Scan-limited items are already isolated as variants rather than
hidden blockers.

M0a should be called frozen only after an auditor who is not this authoring
session reviews the source and package. The auditor can generate the exact
record skeleton and source/package/resolved/validator/file digests with
`--print-audit-template`, replace its identity fields, append it under
`independent_freeze_audits`, and set `freeze_gate_status` to
`passed_fresh_session_audit`. The final check is:

```sh
python3 HoldsworthEngine/references/pedals/tc-bld/m0a/tools/validate_m0a.py --gate freeze
```

## 11. Future M1 input

M1 consumes:

1. `manifest.json` and the exact P-SM identity;
2. the immutable service component/value layer;
3. canonical nodes, terminal bindings, connectors, mode contacts and retained
   full-circuit control topology;
4. the selected typed assumptions (or one explicitly named alternative set);
5. `STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY`;
6. the selected generic taper, op-amp, Q1/Q2 state, supply, source, load and
   input-boundary profiles;
7. every unresolved site and the resolved-handoff SHA-256 emitted by the
   validator.

M1 must add its own explicitly sourced/provisional nonlinear equations where
needed, map conditional 4741 family data to the single supply, and report any
variant it selects. It must not reinterpret the service schematic, change R13,
delete the Boost-mode Distortion loading branch, idealize VREF silently, or
start realtime DSP.
