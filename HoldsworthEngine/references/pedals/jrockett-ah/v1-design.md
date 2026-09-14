# Frozen bounded v1 design

Status: M0/M1 approved by the user; M2 live integration authorized.
M2 preserves these sonic choices and batches the same history replay recurrence
to meet tiny-callback deadlines; see [M2](m2/README.md).
Profile: `JROCKETT-AH-BOOST-BEHAVIORAL-V1`.
All numerical values below are **V choices, unmeasured**, including 0–20 dB.
Source labels refer to [sources.md](sources.md).

## Boost: smallest useful profile

One mono linear processor: mode/emphasis filtering followed by scalar level.
Expose **Boost level**, **L/H**, **F/C/T**. Retain the hardware-facing letters;
explain F as Fat/Full in documentation. Label the selected processor's scope
“Boost · provisional” in the development card. No claim of circuit exactness.

**A1:** Approximate small-signal Boost by two independent first-order shelves.
**A2:** L/H remains effective in all three types. **A3:** Level does not change
the EQ response. **A4:** F is a gentle low-frequency thickening and T reduces
lows relative to highs. None is determined quantitatively by P1/P2.

Use a single conservative starting profile, not an alternate-profile search:

| Type / emphasis | Low shelf, 250 Hz | High shelf, 2.5 kHz |
| --- | ---: | ---: |
| F / L | +6 dB | 0 dB |
| F / H | +3 dB | +3 dB |
| C / L | +3 dB | 0 dB |
| C / H | 0 dB | +3 dB |
| T / L | -3 dB | +3 dB |
| T / H | -6 dB | +6 dB |

Construction: F adds +3 dB low shelf; C adds no type coloration; T adds -6 dB
low and +3 dB high shelves. L adds +3 dB low; H adds +3 dB high. Combine gains
at each shared shelf frequency. This yields six modest, distinct responses.
“Clean” describes the type basis, **not a flat C/L or C/H promise**. Both remain
linear; no extra neutral emphasis position is invented. Equal level settings
do not imply equal perceived loudness. No automatic normalization hides the
effect of EQ on NAM excitation. These particular curves require audition.

For an unambiguous M1 handoff, shelf frequency means the midpoint in dB between
the low/high asymptotes. Let `a=10^(shelfDb/20)` and `w=2*pi*frequency`.
Analog low shelf: `(s+w*sqrt(a))/(s+w/sqrt(a))`.
Analog high shelf: `a*(s+w/sqrt(a))/(s+w*sqrt(a))`.
Discretize with a bilinear transform prewarped at the stated frequency.
At 0 dB use an exact wire. These formulas define software reference curves,
not an analog AH topology or measured phase response.

Level is `10^(boostDb/20)`, proposed range **0 to +20 dB**, default **0 dB**;
default type **C**, emphasis **L**. Defaults are software startup choices, not
Allan's settings or hardware unity. The level range covers practical pre-NAM
audition and intentionally does not claim a hardware mute/attenuation endpoint.
Including EQ, maximum asymptotic boost is +26 dB, not +20 dB. Do not limit
outputs to normalized ±1 or model a 9 V rail clip. No sag, noise, nonlinear
transfer, oversampling, FIR or added latency.

Approved switching correction: retain 10 ms linear-amplitude level ramps, but
**crossfade between previous and new complete EQ responses over approximately
10 ms**. Keep both response states while fading; do not fade to silence or reset
the audible path. Latest mode requests coalesce coherently in bounded storage.
After transition, only the selected response runs. This is software behavior,
not a hardware switch claim. No parallel processing of different pedals.

M1 realizes this with complementary linear weights over `ceil(0.010*fs)` samples,
including exact old/new endpoints. Finish an ongoing fade before starting toward
the latest pending mode, which bounds storage at two responses and one target.
An incoming response is primed from 20 ms of fixed-ring input history before its
weight becomes nonzero. This suppresses cold-start artifacts without delaying
live audio. See [M1](m1/README.md) for measured residue, CPU onset costs, and the
single-producer coherent handoff contract.

## Drive: evaluated and deferred

| Candidate | Benefit | Evidence/cost judgment |
| --- | --- | --- |
| Generic soft clip with Gain, Bass, Treble, Volume | Full familiar UI and audible cascade | Transfer, knee, headroom, EQ positions and coupling all guessed. Antialiasing would validate a chosen algorithm, not AH fidelity. Reject for v1 |
| Linear Gain + Bass/Treble + Volume | Cheap; retains tone knobs | Gain and Volume collapse to one scalar around linear EQ at settled controls. It loses the drive-versus-output distinction; shelves and noon-flat positions also lack evidence. Reject as an AH Drive section |
| Drive EQ/Volume only, Gain omitted | Potentially useful extra pre-NAM EQ | Could be a separately labeled tone tool, but has no stronger AH curve evidence and expands this Boost v1. Defer |
| Measured behavioral Drive or verified trace reduction | Meaningful control and cascade behavior | Best later route if a small evidence acquisition becomes worthwhile; no forensic reconstruction required now |

A future Drive may retain Gain/Bass/Treble/Volume and its own enable, after
Boost, but must first establish small-signal EQ/control curves and at least
level-dependent behavior. Do not freeze pre-clipping Bass, post-clipping Treble,
a Tube Screamer/Blue Note/Tim Pierce topology, a diode threshold or a `tanh`
curve. The present parts observations cannot settle them.

**V decision:** Boost only in M1/M2; no dummy Drive knobs, no both-on mode that
silently substitutes an EQ. This reduces the implementation, not the documented
hardware. Once Drive has support, the four states in [README.md](README.md)
apply, with fixed Boost -> Drive order; no routing framework or order switch.
NAM supplies downstream nonlinearity in v1, which can make the Boost EQ useful,
but is not a substitute claim for the pedal's Drive circuit.

## Architecture and calibrated-volts boundary

Extend the concrete [DevelopmentPreNAMSelector](../../../integration/DevelopmentPreNAMSelector.h)
later with one `jRockettAH` choice. Preserve enum identities 0/1/2 and append 3;
update the temporary UI's normalized four-choice mapping and focused tests
together. Do not leave the current three-choice threshold mapping in place.
The final selector labels are **Off / TC BLD / MC402 Boost / J. Rockett AH**.
One concrete AH processor, two complete EQ paths during transitions and fixed
input-history storage, plus gain/transition state; no
generic pedal base class, registry, pedal chain or generic routing graph.

```text
existing mono input preparation / trim / input-scale bridge
 -> exactly one selected pre-NAM processor
 -> existing NAM normalization -> gate trigger -> NAM
 -> unchanged tone / cabinet / DC blocker / Holdsworth delay / output
```

Use the existing mono double-buffer/process-span lifecycle, readiness and
maximum-block fallback, block-boundary control publication and reset-on-selection
policy. Publish the AH control tuple coherently; audio processing may not
allocate, lock, log, do I/O or throw. Only the selected pedal processes audio;
no crossfade between TC, MC402 and AH. Off preserves stock arithmetic and calls
none. Keep development controls nonserialized, retain their values across
selector changes, and append new message IDs rather than repurpose existing IDs.

The current bridge, documented in
[MC402 Clean Boost v1](../mc402/clean-boost-v1/README.md), uses
`F(C)=sqrt(2)*0.775*10^(C/20)` peak volts per full-scale sample when calibration
and model input metadata apply. It multiplies by host `F(C)` and input trim
before the pedal, then divides by model `F(C)` after. Otherwise one post-trim
sample unit remains one provisional volt and the post scalar is 1.

Reuse that path for integration consistency. **AH v1 itself is scale invariant
at settled controls**: its dB gains and EQ require no voltage calibration, and
no physical clipping threshold is inferred from the bridge or 9 V adapter.
No supply/headroom control is useful here. Absolute volts become physically
relevant only for a future evidenced nonlinear model. Existing two-scalar bridge
arithmetic need not be bit-exact to Off; preserve TC's existing arithmetic.
As with TC, software after the ADC cannot reconstruct unspecified pickup/cable
loading that already occurred at the interface.
