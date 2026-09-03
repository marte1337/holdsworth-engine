# Service component register

This is an electrical service-value transcription of the TC service-packet
parts list (P-SM), normalized to `.` decimals, expanded dielectric words, and SI
suffixes; it is not a typographic facsimile. Source-form strings and editorial
caveats used for the M0a freeze are preserved separately in
[`m0a/service-components.json`](m0a/service-components.json). It is **not** a
claim that every production BLD contains every nominal value. The service
packet contains several revision dates, and the layout must be checked against
the target unit.

`R26 NOT USED`, the selection notes, and taper descriptions are preserved
because they are circuit evidence.

## Resistors

| Ref | Nominal | Ref | Nominal | Ref | Nominal |
|---|---:|---|---:|---|---:|
| R1 | 6.8 MOhm | R17 | 33 kOhm | R33 | 6.8 MOhm |
| R2 | 47 kOhm | R18 | 10 kOhm | R34 | 680 kOhm |
| R3 | 47 kOhm | R19 | 3.3 MOhm | R35 | 2.2 MOhm |
| R4 | 3.3 MOhm | R20 | 10 kOhm | R36 | 6.8 MOhm |
| R5 | 1 MOhm | R21 | 4.7 kOhm | R37 | 1 MOhm |
| R6 | 1 MOhm | R22 | 10 kOhm | R38 | 1 MOhm |
| R7 | 220 kOhm | R23 | 1.5 kOhm | R39 | 10 kOhm |
| R8 | 220 kOhm | R24 | 10 kOhm | R40 | 47 Ohm |
| R9 | 1 MOhm | R25 | 100 Ohm | R41 | 1.5 kOhm |
| R10 | 470 kOhm | R26 | NOT USED | R42 | 1.5 kOhm |
| R11 | 1.5 kOhm, 1/3 W | R27 | 1 MOhm | R43 | 2.2 kOhm |
| R12 | 10 kOhm | R28 | 47 kOhm | R44 | 1 kOhm |
| R13 | 1 MOhm | R29 | 470 kOhm | R45 | 10 kOhm |
| R14 | 2.2 kOhm | R30 | 10 kOhm | R46 | 10 kOhm |
| R15 | 470 kOhm | R31 | 4.7 kOhm | R47 | 10 MOhm |
| R16 | 220 Ohm | R32 | 1 MOhm | R48 | 22 kOhm |

R33-R36 look visibly added/overwritten compared with the surrounding typed
parts list. They are present in the schematic/layout, but the editorial history
is a revision warning.

## Capacitors

| Ref | Nominal / service description | Ref | Nominal / service description |
|---|---|---|---|
| C1 | 22 uF, 16 V electrolytic | C17 | 10 nF plastic |
| C2 | 10 uF, 35 V electrolytic | C18 | 1 uF, 50 V miniature electrolytic |
| C3 | 1 uF, 50 V tantalum | C19 | 100 nF, 20% polyester |
| C4 | 10 nF plastic | C20 | 470 pF plastic |
| C5 | 1 nF plastic | C21 | 10 nF plastic |
| C6 | 33 nF, 20% polyester | C22 | 10 nF plastic |
| C7 | 1 uF, 50 V miniature electrolytic | C23 | 47 nF, 20% polyester |
| C8 | 47 nF, 20% polyester | C24 | 1 uF, 50 V electrolytic |
| C9 | 220 pF plastic | C25 | 22 uF, 16 V electrolytic |
| C10 | 220 pF plastic | C26 | 10 nF plastic |
| C11 | 1 uF, 50 V miniature electrolytic | C27 | 100 nF, 10% polyester |
| C12 | 1 uF, 50 V electrolytic | C28 | 100 nF, 10% polyester |
| C13 | 4.7 uF, 35 V electrolytic | C29 | 1 nF plastic |
| C14 | 10 nF, 10% polyester | C30 | 6.8 nF, 20% polyester |
| C15 | 10 uF, 16 V miniature electrolytic | C31 | 220 pF plastic |
| C16 | 2.2 uF, 50 V electrolytic |  |  |

The original dielectric abbreviations are useful for tolerance/aging work. They
do not by themselves prove parasitic ESR, leakage, or capacitance after decades.

## Potentiometers

| Ref | Nominal | Service taper | Function | What remains unknown |
|---|---:|---|---|---|
| P1 | 47 kOhm | LOG | Gain | Manufacturer law/percentage, orientation, residual and wiper resistance |
| P2 | 22 kOhm | LIN | Bass | Center detent/mechanical tolerance and angle-to-resistance |
| P3 | 100 kOhm | LIN | Treble | Center detent/mechanical tolerance and angle-to-resistance |
| P4 | 22 kOhm | NEG.LOG | Distortion | Exact reverse-log law and endpoint wiring |
| P5 | 470 kOhm | LOG | Suppressor threshold | Exact law/orientation and interaction with detector bias |

These unknown curves do not block a documentary-nominal model. M0a may assign
each pot a named, source-documented generic taper with explicit orientation and
endpoints. Such a profile remains an assumption layer and must not be described
as the vintage control law; M4 replaces it with measured curves.

TC's user manual calls P1 “GAIN” in its instructions and publishes +/-30 dB;
some faceplates/listings call the same user control “VOLUME.” The circuit model
should name the physical control unambiguously, not infer a generic output-fader
topology from either label.

## Semiconductors and ICs

| Ref | Service entry | Evidential note |
|---|---|---|
| Q1 | BF245-A | JFET in the direct/electronic-bypass audio branch |
| Q2 | BF245-A, selected | JFET in final-stage feedback/control; part of the audio/suppressor behavior and cannot simply be deleted |
| Q3 | BC558-B | Bypass/indicator control |
| Q4-Q6 | BC548-B | Q4 participates in Distortion; Q5/Q6 participate in suppressor control/rectification |
| D1 | 1N4001 | Power/control diode |
| D2-D17 | 1N4148 | Individual roles must come from the schematic, not the grouped BOM entry |
| D18 | SBR 3431, red 3 mm | Indicator LED; scan reading should be checked against a clearer original |
| DG1 | AA119 | Germanium diode in the Distortion network |
| IC1 | 4741 selected (GR1) | Quad op amp; manufacturer and TC's selection criterion are unspecified |
| IC2 | HBF 4007 UBP | CMOS transistor array used for electronic-bypass/control logic |

One photographed/reported 1984-era unit has an Exar-marked 4741 and a Toshiba
TC4007UBP. That is a unit observation, not permission to rewrite the TC BOM.

## Drawing/revision anchors

- Schematic: `1501-3`, dated `840222` (1984-02-22).
- Parts list: `1501-06`; printed and handwritten revision fields are partially
  legible and must be re-read from a better scan before assigning exact dates.
- Parts layout: `1501-6`; title block entries `820729`, `840227`, and `851023`.
- PCB artwork visible in the layout: `1501-2`.

Do not merge those numbers into one assumed “revision 6.” They identify
different drawing/artwork documents and a history spanning several years.
