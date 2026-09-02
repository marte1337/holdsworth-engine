# Source register and evidence policy

Last web/source check: **2026-09-01**.

This register distinguishes what a source can prove. A TC document establishes
documented design intent; a photograph establishes only what is visible in that
unit; an owner post establishes what that owner observed or remembers; and an
engineering deduction remains an inference until checked against the circuit or
hardware.

No currently hosted original-BLD PDF was found on TC Electronic/Music Tribe's
present support site during this search. The original documents below are
third-party mirrors of TC-branded material. Their hashes make the exact reviewed
copies identifiable if a mirror changes.

## Primary BLD documents

### P-UM — original TC user manual

- Title shown in document: *Booster + Line Driver & Distortion*.
- Source URL: <https://www.dropbox.com/s/ya2fg1cqc5houk9/TC%20electronic%20Booster%20linedriver%20and%20distortion%20BLD%20manual.pdf?dl=0>
- Reviewed file: 133,416 bytes, four pages.
- SHA-256: `71c9fa4f719d9b22d8897c1a429f1de206aa1de8beaa58180c2088b087d88b4c`
- Class: **P**, original manufacturer user documentation on a third-party host.
- Establishes: intended operation; control behavior; published impedances,
  levels, ranges, current/power, and supply claims.
- Limitation: it contains a direct supply-range conflict and gives behavioral,
  not node-level, descriptions.

Page-specific facts transcribed for durability:

- Page 2: clean boost/preamplification of +/-30 dB; active Bass/Treble; stated
  3.3 MOhm input; low-impedance jack and XLR outputs; even-harmonic-emphasized
  Distortion; smooth variable-threshold suppressor with claimed improvement up
  to 26 dB; electronic bypass; prose discussion of 18 or 24 VDC studio use.
- Page 3: Boost Gain +/-30 dB and unity between scale marks 2 and 3; in
  Distortion mode Gain is post-distortion output gain; Bass/Treble are active in
  both modes and nominally 0 dB at center; Distortion is fully disabled in Boost
  and bypass; Distortion range from slight (stated 0.1) to sustained, with second
  harmonic dominant and post-distortion filtering; suppressor setup procedure;
  external-supply prose says 8-24 VDC and says 18-24 V permits studio-level
  signals, stated as “max. 18dB” without a voltage reference; external bypass is
  a momentary DC control.
- Page 4: input impedance typically 3 MOhm in and out; output impedance at most
  50 ohm in and out; 10 Hz-40 kHz response; maximum input +6 dBV; maximum output
  +8 dBV; input/output dynamic range 106/110 dB; Gain +/-30 dB; Boost distortion
  0.03%; Treble 10 kHz +/-16 dB; Bass 100 Hz +/-16 dB; Distortion-control range
  40 dB; suppressor threshold -50 to -90 dBV, slope 1:4, and at least 20 dB
  suppression; 9 V battery; 12 mW/1.3 mA; external supply **8-18 VDC**; size
  124 x 98 x 50 mm (31 mm excluding knobs).

“In and out” in the impedance rows appears to mean engaged and bypassed, not
input and output. This reading is consistent with the surrounding table and with
the manual's statement that the line-driver outputs remain active in bypass, but
it is an **I** interpretation of terse typography.

### P-SM — original TC service packet

- Title shown in document: *Service — TC Booster+ Linedriver & Distortion*.
- Primary mirror: <https://www.dropbox.com/s/b5w2b4ny17zceuz/TC%20BLD%20Booster%20Line%20Driver%20Distortion%20service%20manual.pdf?dl=0>
- Alternate public mirror: <https://www.aronnelson.com/DIYFiles/up/TC_DRIVER.PDF>
- Reviewed file: 203,201 bytes, four pages.
- SHA-256: `6d794b2ce05a93f53c579303a5b42682604e1c7922e14c064e30ea9175c0ac62`
- PDF wrapper metadata: title `TC_DRIVER.PDF`, created 2003-02-11 by Microsoft
  Word/PDFWriter. The scanned TC pages themselves are earlier documents.
- Class: **P**, manufacturer service content on third-party hosts.
- Contents:
  1. TC service cover;
  2. schematic, drawing 1501-3, dated 1984-02-22;
  3. parts list, drawing 1501-06, with a partially legible 1984 printed revision
     and a later handwritten entry;
  4. parts layout, drawing 1501-6, whose title block carries entries dated
     1982-07-29, 1984-02-27, and 1985-10-23. The pictured PCB also carries
     `1501-2` artwork text.
- Establishes: topology and designators; nominal values; control taper classes;
  device families; connector/control arrangement; layout intent.
- Limitations: scan contrast and crossing dots are poor; multiple revision dates
  are present; R33-R36 are visibly later/hand-entered in the parts list; a parts
  layout is not proof of the fitted value in every production unit.

## Primary component data

### P-XR4741 — Exar operational-amplifier data book

- URL: <https://www.bitsavers.org/components/exar/_dataBooks/Exar_Operational_Amplifier_Data_Book_198203.pdf>
- Class: **P** for the XR-4741, and **C** for the BLD because TC did not name
  Exar in its BOM.
- Relevant published typicals: four internally compensated 741-like amplifiers;
  +/-2 to +/-20 V supply; about 3.5 MHz unity-gain bandwidth; 1.6 V/us slew rate;
  output swing about +/-13.7 V into 10 kOhm at +/-15 V (about +/-12.5 V into
  2 kOhm); 40 V total absolute-maximum supply.
- Relevance: photographs/owner report identify an Exar 4741 in one vintage BLD.
  These figures are not automatically the parameters of all selected TC parts.

### P-HA4741 — Renesas/Intersil HA-4741 data sheet

- URL: <https://www.renesas.com/en/document/dst/ha-4741-datasheet>
- Class: **P** for HA-4741; family-level corroboration only for an unidentified
  BLD 4741.
- Relevant published behavior: same basic quad-741 pinout and approximately
  3.5 MHz/1.6 V/us family behavior, supply span up to +/-20 V.
- Limitation: a modern/later HA data sheet cannot identify the manufacturer or
  selection criterion of TC's `4741 selected (GR1)`.

### P-TC4007 — Toshiba CMOS data book

- URL: <https://www.bitsavers.org/components/toshiba/_dataBook/1988_Toshiba_TC4000_4500_5000_CMOS_Logic.pdf>
- Class: **P** for Toshiba TC4007UBP; **C** for one photographed/reported vintage
  BLD.
- Establishes: six accessible MOSFETs arranged as complementary pairs/inverter;
  Toshiba specifies a 20 V absolute-maximum VDD-VSS.
- Limitation: TC's service BOM instead names `HBF 4007 UBP`; vendor variants and
  voltage ratings must not be conflated.

An HBF4007 data-sheet mirror cited in the owner discussion is:
<https://www.dropbox.com/s/yduedq2uli4dq1g/HBF4007.pdf?dl=0>. It reportedly gives
an 18 V limit. Because the scan's maker/status and the fitted IC population are
not yet established, it is retained as a lead rather than used to settle the
8-18/8-24 V conflict.

## Corroboration, photographs, and owner reports

### C-EEM82 — contemporary bench/review article

- Dave Goodman, “T.C. Electronic Effects Boxes,” *Electronics & Music Maker*,
  January 1982.
- URL: <https://www.muzines.benhall.co.uk/articles/t-c-electronic-effects-boxes/3666?theme=1>
- Class: **C**, contemporary independent description/bench observation.
- Reports: Boost approximately flat from 20 Hz to 18 kHz; Bass/Treble +/-18 dB
  at 60 Hz/8 kHz; 70 mV RMS input producing 2.25 V RMS at 1 kHz under an
  unstated control setup; soft clipping; suppressor action below about 5 mV;
  electronic latching bypass; LED illumination for about ten seconds.
- Limitations: test settings, loading, tolerances, and measurement definitions
  are incomplete. Its tone figures do not override TC's later/manual figures.

### C-MM85 — later contemporary review

- *Music Maker*, “T.C. FX Units,” 1985.
- URL: <https://muzines.benhall.co.uk/articles/t-c-fx-units/8881>
- Class: **C**.
- Useful mainly as period corroboration of the product and threshold control; it
  adds little node-level evidence.

### C/O-UNIT84 — photo-backed vintage-unit owner/repair report

- Discussion: <https://www.diystompboxes.com/smfforum/index.php?topic=125246.0>
- Class in this package: **O with photographic leads**. The thread contains
  photographs, but its image attachments were not retrievable by the current
  crawler, so their markings were not independently re-read here.
- Reported for that unit: Exar-marked 4741, Toshiba TC4007UBP, and electrolytic
  date codes consistent with 1984. The owner also reports dried electrolytics,
  restoration after recapping, a visibly non-factory resistor repair, and use
  at 18 V.
- Limitations: this is one repaired unit; the photographs do not prove the
  factory population of all revisions, and 18 V operation is not proof that
  24 V is safe.

### C/O-SWITCHREPAIR — electronic-bypass repair testimony

- URL: <https://www.freestompboxes.org/viewtopic.php?t=31116>
- Class: **O/C-repair**, an altered previously broken unit.
- Reports substitution of TL074CN, HEF4007UBP, several 1N914 diodes, and a
  2N3906 for a BC558 in the switching area; a momentary contact to ground was
  used to exercise the footswitch control.
- What it corroborates: the 4007/BC558 area is electronic switching/control and
  the footswitch supplies a control event rather than carrying the main audio.
- What it does not prove: any substitute was an original factory part or that
  every component controlled by the latch can be removed from the engaged audio
  model.

### C/O-FOOTSWITCH — vintage-unit footswitch report/photo attachment

- URL: <https://forum.pedalpcb.com/threads/tc-electronic-booster-line-driver-distortion-broken-footswitch.21492/>
- Class: **O**, with an attachment not exposed by the current crawler.
- Reports a white-wire connection to the footswitch, difficult mechanical
  access under the PCB, and a failure state in which the effect remains
  electronically on. This is consistent with P-SM's latch/control topology but
  adds no audio-node or component-value evidence.

### C-EXTERIOR — archived vintage listing photographs

- URL: <https://www.soundpure.com/p/tc-electronics-booster-line-driver-distortion-used/26841>
- Class: **C** for enclosure, control, and connector appearance only.
- No PCB/component evidence; marketplace copy and user-attribution claims are
  not used.

### C-RE2025 — incomplete community redraw

- Discussion: <https://forum.pedalpcb.com/threads/help-removing-the-buffer-from-a-tc-boost-line-driver-distortion.25659/>
- Image page: <https://postimg.cc/LgXwjR7s>
- Class: **C/I**, low-confidence readability aid.
- Useful point: the thread explains how the unusual paired mode-switch contacts
  are mechanically connected.
- Limitation: the author calls the redraw incomplete/uncertain, and the drawing
  contains suspect rail, op-amp-polarity, value, and switching interpretations.
  It must never be imported as a netlist or used to overrule P-SM.

No complete, hardware-verified, high-quality reverse-engineered netlist of the
original BLD was found in this search. The 2025 redraw is the only public
component-level redraw located, and its own author marks it incomplete. The
node trace in `circuit-analysis.md` therefore returns to P-SM rather than
laundering that redraw's assumptions into new “evidence.” This absence prevents
hardware-verified/authentic claims, but it does not prevent an independently
documentary-checked nominal netlist whose ambiguities are explicit assumptions.

### O-RIG — owner comparison reports

- URL: <https://www.rig-talk.com/forum/threads/t-c-electronics-integrated-preamplifier.226495/>
- Class: **O**.
- Useful only as a warning that owners who have both pedals report the BLD and
  Integrated Preamp as different circuits/behaviors. Subjective tone and knob
  reports are not circuit evidence.

A separate Rig-Talk discussion with a service-packet attachment contains a
secondhand statement that schematic/layout discrepancies exist, but names no
designator or value:
<https://www.rig-talk.com/forum/threads/t-c-electronics-integrated-pre-amp.274741/>.
It is retained as an acquisition lead, not promoted to a circuit fact.

## Holdsworth historical evidence

### H-AH94 — reproduced/translated artist interview

- URL: <https://allanholdsworth.info/ahwiki/index.php/Individualist_%26_Musician_%28Gitarre_%26_Bass_1994%29>
- Source lineage: interview published in *Gitarre & Bass* (1994), archived in
  translated/transcribed form.
- Class: **O/P-history** — primary artist statement in a secondary reproduction.
- Establishes: Holdsworth says the TC Booster was used only in clean mode to
  compensate for the low output of his pickups and bring level toward that of a
  high-output pickup.
- Does **not** establish: knob positions, nominal supply, exact pedal revision,
  or that Distortion/suppressor were physically disabled beyond the mode choice.

### H-AH99 — reproduced artist interview on pickup design

- URL: <https://allanholdsworth.info/ahwiki/index.php/Allan_Holdsworth_Remembers:%22In_The_Dead_Of_Night%22_(Guitar_and_guitar_shop_1999)>
- Class: **O/P-history**.
- Corroborates the reason: Holdsworth preferred low-output pickups to reduce
  magnetic string pull. It does not add a BLD setting.

### H-AH08 — reproduced Guitar Player artist interview

- URL: <https://allanholdsworth.info/ahwiki/index.php/The_Man_Who_Changed_Guitar_Forever_%28Guitar_Player_2008%29>
- Source lineage: *Guitar Player* interview, 2008, archived/transcribed by the
  Allan Holdsworth Information Center.
- Class: **O/P-history** — primary artist statements in a secondary
  reproduction.
- Establishes more specifically: Holdsworth names old TC pedals including the
  Booster + Line Driver Distortion, says he uses only their clean-boost side
  mostly to compensate for low-output pickups, and explains that a booster can
  raise gain and push an amplifier's front end harder.
- Does **not** establish: exact control settings, voltage, serial/revision, or a
  single fixed operating level.

## Explicitly excluded as circuit ground truth

- TC Electronic Integrated Preamp service material or clone schematics.
- Later TC Classic Booster/Distortion service drawing, including
  <https://service-tcgroup.tcelectronic.com/files/tech_service/classic_pedals/booster_distortion.pdf>.
- PastFX or other modern clone layouts and marketing.
- TC's current Native BLD-inspired plug-in and its graphics/parameterization.
- Neural captures, NAM files, videos, and subjective A/B clips.
- Marketplace descriptions and unattributed “used by” lists.

These can become validation comparisons only after the original circuit and
physical reference level are independently fixed.

## Preservation policy

The original PDFs are not copied into this repository. This package instead
stores their URLs, exact reviewed-file hashes, page-level factual transcription,
drawing identifiers, and literal BOM. If redistribution permission is obtained,
the PDFs may later be archived beside this register without changing evidence
IDs.
