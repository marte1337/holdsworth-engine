# MC402 evidence register

Accessed 2026-09-10. **F** = documented product fact; **R** = direct repair/trace
report; **C** = our circuit-derived inference; **A** = assumption;
**V** = provisional v1 decision; **M** = later measurement. Reports and copied
drawings are not manufacturer service data. Confidence applies to each claim,
not to everything on the source's website.

## Primary documentation

### P1: Dunlop MC402 manual, 92503004778 Rev B

[Official two-page PDF](https://www.jimdunlop.com/content/manuals/MC402.pdf).
Page 2 was downloaded and visually inspected, including its specification table,
control diagram and sample LEDs. SHA-256 of inspected PDF:
`efb20795482e0d97ea6952666f6d81e33a5e77e828ee365e16eb574e28c74249`.

**F, high confidence:** Boost spans 0–20 dB. Output adjusts Overdrive level;
Tone clockwise brightens; Gain clockwise increases distortion. Separate Boost
and Overdrive switches/LEDs are identified. Sample illustrations show both LEDs
lit, supporting simultaneous use. Bypass is specified as true hardwire.
Supply is 9 V DC/battery; current is below 5.5 mA. Output impedance is below
150 ohms with Boost on and below 15 kohms with Overdrive on/Boost off.
SNR exceeds 87 dB A-weighted at midpoint controls. These are published specs,
not measurements made here.

**Conflicts:** the Overdrive directions incorrectly name the BOOST footswitch;
the labeled control list resolves this as an apparent editorial error. The
printed input impedance really is **1 kohm**, not merely a text-extraction
mistake. Do not silently change it to 1 Mohm. The partial trace has a 1 Mohm
pulldown but an inverting input network, so that resistor alone does not prove
terminal impedance. Carry the impedance discrepancy as unresolved.

No numerical Overdrive gain range, Tone cutoff/range, Output taper, clipping
threshold, internal order, maximum clean signal swing or internal rail is given.

### P2: Dunlop product description and CAE designer usage

[Dunlop MC402](https://www.jimdunlop.com/mxr-cae-boost-overdrive/) identifies an
independent clean Boost/Line Driver circuit alongside Overdrive. **F, high:**
product relationship and separate functions. Descriptions of warmth, punch,
transparency, dynamics and cleanup are **marketing performance claims**, not
filter coefficients or a clipping topology.

[Bob Bradshaw's CAE pedalboard account](https://www.customaudioelectronics.com/custom_shop?id=4&view=article)
describes engaging MC402 Boost at unity as a buffer with no audible change in
that rig. **Primary listening account:** useful support for a flat v1 Boost;
neither a measured frequency response nor evidence for the internal section order.

## Circuit evidence and its limits

### R1: original MC402 repair/trace discussion, 2010–2023

[Freestompboxes page 1](https://www.freestompboxes.org/viewtopic.php?t=10060)
contains Nikifena's July 2010 report of tracing only Boost during a repair.
It also links an MC401-derived drawing; that is not a complete MC402 schematic.
[Page 3](https://www.freestompboxes.org/viewtopic.php?t=10060&start=40) contains
TheKoala's 2020 through-hole Rev E trace report: OP275 and LF442CN. Later posts
correct R3/R24 to 470 kohms and report a TLC2262CP-equipped unit and discrepancies
with the drawing. **R, medium:** real tracing exists, but revisions/values need
qualification. Original attachments required forum permissions; only indexed
post text was accessible in this session. No restricted attachment was accessed.

Repair posts discuss reduced supply voltage and a Black Cat OD-1 relationship.
These are not controlled healthy-unit measurements or proof of intentional sag.

### R2: accessible partial Overdrive drawing

[Inspected drawing](https://www.pasqualerobustini.com/wp-content/uploads/2021/03/mc402od.jpeg),
mirrored on [this owner's page](https://www.pasqualerobustini.com/borntoplayguitar/english-effetti-a-pedale/cae-mxr-mc-402-boost-overdrive/).
Image SHA-256:
`6672c1aadf0336d76a9fe9321e22a790865a9fb8b9d394a251fbd305c7f3ed44`.
The mirror attributes it only to a modder forum; authorship, board revision and
identity with R1's later attachment are **not established**. Medium-low confidence
as a partial trace, enough to motivate a provisional structure.

**Observed:** two inverting op-amp stages; first input 47 nF/22 kohms, feedback
470 kohms; interstage coupling and a 47 kohm Gain pot; second 22 kohm input and
1 Mohm feedback as drawn. Some capacitors have question marks. No signal-clipping
diodes are drawn; the 1N4xxx diode is on the supply. Tone, Output pot, Boost and
switch wiring are absent. The drawing cannot prove the complete signal order.

**C:** a cascade of saturating gain stages is more directly supported by this
drawing than an assumed Tube Screamer feedback-diode model. The first ideal
midband gain is -470/22, about -21.36. The second would be -45.45 with the drawn
1 Mohm, or -21.36 with R1's reported 470 kohm correction. Interstage loading means
these ratios do not specify the complete terminal/control response.

The owner's prose includes an explicitly AI-generated analysis calling both
stages non-inverting and describing a feedback Gain pot. Those statements
disagree with the image and are excluded. The owner's reported Dunlop exchange
is not treated as an independently retrieved manufacturer statement.

### R3: conflicting earlier repair account

[Freestompboxes 2013 discussion](https://www.freestompboxes.org/viewtopic.php?t=22560)
includes mictester's firsthand report of a Tube Screamer-like circuit and an
observed version with shunt diodes; another participant reports shunt diodes too.
It also contains an explicitly modified Boost schematic with a voltage doubler.
**R, low-to-medium:** conflict to retain, not a netlist to combine with R2.
Do not import the doubler, generic silicon thresholds, or a Tube Screamer mid hump
into v1. The available evidence does not resolve whether conflicting observations
reflect revisions, identification errors or incomplete tracing.

### P3: OP275 manufacturer data

[Analog Devices OP275 Rev C](https://www.analog.com/media/en/technical-documentation/data-sheets/OP275.pdf),
electrical-characteristics table on page 2. **F, high for the part, conditional
for the pedal:** specified supply range starts at **±4.5 V**, i.e. 9 V total,
not 4.5 V total. Most swing/distortion specifications are at ±15 V unless stated
otherwise. They do not calibrate behavior on a reduced MC402 rail. Do not
extrapolate a guaranteed clipping voltage or distortion curve from them.

## Order and bypass conflicts

[Firsthand owner review by Yu Hagiwara](https://xn--8mro61ayx1a.com/archives/9458.html)
explicitly describes Boost after distortion. [Blank Generation's 2014 technical
owner account](https://dinkiest.blog96.fc2.com/blog-entry-113.html) likewise states
fixed Overdrive -> Boost order and discusses switching FETs. Indexed text was
available; direct page opens failed. **Medium confidence together**, not official
confirmation. P1's lower output impedance with Boost engaged is consistent with
a final line driver, but is not by itself proof of order.

[Audiofanzine owner reports](https://en.audiofanzine.com/overdrive-pedal/mxr/mc402-boost-overdrive/user_reviews/)
include claims of the reverse order. These are conflicting firsthand impressions
without a displayed routing test. A downstream amp's extra distortion does not
locate Boost inside the pedal. **V:** use Overdrive -> Boost, retain this uncertainty,
and do not offer an invented order switch.

FET switching reports do not automatically disprove the official hardwire-bypass
specification: transistor function and complete switching connections must be
known. **V:** logical section bypass, without simulating switches, LEDs or pops.

## Historical source and research stop

H1 is the [2008 Guitar Player interview reproduction](https://allanholdsworth.info/ahwiki/index.php/The_Man_Who_Changed_Guitar_Forever_%28Guitar_Player_2008%29),
Barry Cleveland, already registered as H-AH08 under TC BLD. Primary artist
statements in a secondary transcription; medium-high confidence for reported
studio use. The [package introduction](README.md) records the careful scope of
the section-use statement. No original page scan was independently verified.

This bounded search found official operating documentation, original trace/repair
reports and one inspectable partial Overdrive image. It did **not** establish a
complete trustworthy factory/service schematic. This is an access/evidence limit,
not a claim that no complete drawing exists anywhere. No need to reconstruct all
revisions before an explicitly provisional v1.
