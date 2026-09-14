# J. Rockett Allan Holdsworth Signature OD/Boost

M0 and M1 approved by the user; M2 live integration implemented.
Profile: **`JROCKETT-AH-BOOST-BEHAVIORAL-V1`**. Curves remain unmeasured behavioral
audition choices. Drive remains deferred. No commit.
See the [M1 report](m1/README.md) for the frozen profile and historical measurements,
and the [M2 report](m2/README.md) for live integration, realtime checks and audition.

Recommend a **linear Boost with Level, L/H emphasis and F/C/T type**. Defer Drive
and therefore live cascaded operation. This retains useful tone shaping before
NAM without inventing and then antialiasing an unsupported distortion circuit.
A useful bounded v1 is possible without hardware **as an AH-inspired behavioral
Boost**; a strong claim of pedal-response fidelity is not yet possible, even
for Boost. Its EQ curves and level range would be explicit audition choices.

## Why it belongs

The manufacturer's operating manual says the product was developed with Allan
for his live and studio needs ([P1/P2](sources.md)). This directly establishes
the collaboration and intended role in HoldsworthEngine. It does not establish
which section, switch setting, knob position, amplifier, recording or tour he
used. No Allan preset is proposed. Product illustration knob positions are not
artist settings; forum claims that he used only Clean Boost are not adopted.

## What the primary manuals establish

Both manual editions were downloaded and their relevant first pages visually
inspected. P1 contains the annotated diagram; P2 adds the power specification.

| Section | Exact documented controls | Confidence / limit |
| --- | --- | --- |
| Drive | Volume (faceplate VOL), Gain, Bass, Treble; DRIVE footswitch | High for names and section assignment; no numerical control ranges |
| Boost | Boost level (faceplate BOOST); L/H two-position emphasis; F/C/T three-position type; BOOST footswitch | High for controls and choices; no numerical gain/EQ curves |
| L/H | L emphasizes lows; H emphasizes highs | High qualitatively; magnitude, bandwidth and type-dependent interaction unspecified |
| F/C/T | F = Fat in P1's diagram, Full in both editions' instructions; C = Clean; T = Treble | Preserve the Fat/Full wording discrepancy; it does not define two F modes |
| Operation | Each section can work alone, or Boost can feed Drive | High; no reverse-order selector documented |
| Supply | 9 V battery or standard 9 V DC adapter; 5.5 mm outer / 2.1 mm inner connector, center negative | High, P2; supply voltage is not a clipping/headroom specification |

The documented four logical hardware states are:

| Boost | Drive | Signal operation |
| --- | --- | --- |
| Off | Off | Logical bypass; bypass electronics unspecified here |
| On | Off | Boost alone; Drive controls do not affect it |
| Off | On | Drive alone; Boost controls do not affect it |
| On | On | Input -> Boost -> Drive -> output |

“Independent” means separately usable sections in this pedal. It does not
establish separate I/O pairs, a parallel blend, two plugin slots, or reversible
ordering. The manual's repeated cascade exceptions describe the combined
signal being affected; they are not evidence of controls electrically migrating
between circuits.

## Evidence boundaries and decision

| Layer | Established or proposed content |
| --- | --- |
| Documented product facts | Controls, qualitative emphasis/type labels, independent/cascaded operation, Boost -> Drive, 9 V supply, artist collaboration |
| Circuit evidence | Firsthand Rev 8 component observations; a Rev 7 recreation-in-progress report. No verified complete trace retrieved |
| Technical inference | Upstream boost/EQ can change what reaches Drive and NAM; exact Drive Gain/Volume interaction requires its nonlinear transfer and topology |
| Assumptions | Linear Boost, two separable shelves, L/H active in all three types, no loading or level-dependent EQ; not established hardware behavior |
| Provisional v1 choices | One fixed six-combination Boost profile, 0–20 dB level, no pedal clipping, zero added latency; Drive deferred |

The TC BLD has service-document support for its coupled linear circuit and
24-state reduction. AH does not. MC402's accepted flat 0–20 dB Boost has a
documented range; AH does not. Neither circuit nor range can be borrowed as AH
evidence. MC402's rejected nonlinear work provides the stop rule: better
antialiasing cannot establish the authenticity of a guessed transfer.

Package: [ranked sources and gaps](sources.md), [frozen design](v1-design.md),
[milestones and acceptance](milestones.md), [M1 report](m1/README.md). Source
URLs/hashes replace a bulky manual archive; M1 retains a compact software
measurement table and reproduction tools.
