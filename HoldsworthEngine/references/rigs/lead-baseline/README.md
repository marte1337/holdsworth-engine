# Holdsworth Lead Baseline

Status: working development baseline, not final production preset.

This document records the current HoldsworthEngine lead-tone reference so the
audition setup can be reproduced without relying on memory.

The goal is not to claim an exact historical Allan Holdsworth rig. It is the
current musical baseline against which Boost/Drive, NAM, cabinet and delay work
can be evaluated.

## Current baseline — 2026-09-10

Signal path:

Guitar
→ TC BLD Clean Boost
→ NAM
→ cabinet IR
→ Holdsworth delay
→ output

### TC BLD Clean Boost

State: ACTIVE

Gain: 0.172
Bass: 0.500
Treble: 0.500

Gain is currently operated in the UI FOCUS range.

The BLD is intended primarily to push the amplifier rather than provide the
whole distortion sound itself.

Current listening impression:

- tightens the response;
- reduces dynamics somewhat as Gain increases;
- increases sustain;
- makes legato lines easier;
- complete Holdsworth lead chain has so far been preferred with BLD enabled.

### NAM amplifier

Current model:

MBDR-RevF-Orange-Crunchy-TR_Out.nam

Current NAM controls:

Input: 0.0 dB
Noise Gate: ACTIVE
Threshold: -59.5 dB

EQ: ACTIVE
Bass: 1.3
Middle: 5.0
Treble: 5.0

Output: 0.0 dB

This Orange Rev F capture is the current WORKING amp baseline, not a final
historical or product choice.

The previously tried MellowPunky capture was hotter than expected and left less
useful headroom for evaluating the BLD. Crunchy currently works better as the
boostable baseline.

### Cabinet IR

Current IR:

MC-90 Black Shadow 57 close 42_dc.wav

Source pack:

Mesa Boogie Full Back 4x12
MC-90 Black Shadow
SM57 / MPT pack

Current impression:
close 42 gives a useful balance between articulation and smoothing the
Rectifier's aggressive upper-mid character.

### MC-90 IR shortlist

Do not audition the entire pack every time. Start with these references.

57 close 35_dc

- smooth and balanced;
- retains useful 2–3 kHz articulation;
- reduced upper-mid/fizz region;
- first alternative if 42 needs to be smoother.

57 close 42_dc

- CURRENT BASELINE;
- slightly more open/present than 35;
- good balance of articulation and smoothness.

57 close 48_dc

- warmer, thicker and darker;
- stronger high-frequency suppression;
- try when the Rectifier still sounds too aggressive.

57 close 44_dc

- darkest/smoothest reference of the shortlist;
- strongly reduced presence/fizz;
- useful as an extreme smooth alternative;
- may become too veiled for lead articulation.

57 close 27_dc

- brighter/more neutral comparison;
- useful sanity reference when the smoother IRs become too dark.

IR preference order is not frozen. These are audition shortcuts rather than
historical claims about microphone position.

### Delay

State: ACTIVE

Current preset:

Holdsworth 122

Wet: 21%

Lead 121 remains an important alternate lead reference.

122 currently provides the complete working tone shown in the development
baseline, while 121 should continue to be used as a comparison rather than
discarded.

## Historical rationale

### Amplifier

Allan Holdsworth is strongly documented using Mesa/Boogie Dual Rectifiers for
lead/solo sounds in the 1990s.

Documented mid-1990s Rectifier settings include approximately:

Gain: 2 o'clock
Treble: 2 o'clock
Middle: 2 o'clock
Bass: fully off
Presence: 10 o'clock
Master: according to required level

His signal path was not simply Dual Rectifier → ordinary cabinet.

He used the amplifier's speaker output into his own load/line-level system and
then into a solid-state power amplifier before the speaker cabinets. He also
described the Rectifier as somewhat too bold for his music and used this system,
sometimes together with EQ, to tame it.

Therefore a raw Rectifier NAM plus conventional IR should not automatically be
assumed to reproduce his complete historical amplifier system.

### Orange versus Red

Dual Rectifier use itself has strong evidence.

Use of the Vintage/Orange channel is supported by secondary reports and is
musically plausible, but the currently reviewed primary interviews do not
establish Orange versus Red strongly enough to treat it as settled fact.

Current project policy:

- Orange Crunchy = working baseline.
- Red remains a required future comparison.
- Do not tune the complete suite around Orange until controlled Orange/Red
  comparisons have been made.
- Exact Rectifier revision/channel authenticity is a later refinement, not a
  blocker for development.

## Future controlled amp comparison

When revisiting the amplifier choice, keep these fixed:

TC BLD settings
cabinet IR
NAM input level
guitar
pickup
delay preset/wet level
monitoring level

Then compare a small set of candidates at approximately matched perceived
loudness.

At minimum include:

- current Rev F Orange Crunchy;
- a suitable comparable Rev F Red capture;
- optionally one other historically relevant lower-gain Mesa reference.

Do not change amp and IR simultaneously during this comparison.

## Status

The present baseline is good enough for continued HoldsworthEngine development.

It is not yet:

- a final factory preset;
- a claim of exact historical signal-chain reproduction;
- the final selected NAM;
- the final selected IR;
- hardware-calibrated TC BLD behavior.

Update this file whenever a new lead baseline clearly replaces the current one.
