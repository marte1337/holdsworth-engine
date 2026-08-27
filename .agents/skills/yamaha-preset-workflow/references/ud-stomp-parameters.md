# UD-Stomp Parameter Reference

This is a concise project reference for repeatedly used Yamaha UD-Stomp
concepts.

It does not replace the official Yamaha documentation.

For a new preset or disputed behavior, verify the relevant source using
`official-sources.md`.

Keep documented Yamaha behavior separate from HoldsworthEngine's provisional
physical DSP interpretation.

---

## Delay Time

Yamaha source Delay Time is a documented delay setting.

HoldsworthEngine represents physical delay duration in milliseconds.

Do not assume other Yamaha controls share similarly direct physical units.

GROUP may change the delay resources available to a logical delay and is a
separate architectural concept.

---

## Feedback

Yamaha FEEDBACK controls repeated delay behavior.

HoldsworthEngine uses a normalized physical feedback coefficient.

There is currently no hardware-calibrated general Yamaha FEEDBACK-to-coefficient
mapping.

Preset DSP coefficients therefore remain provisional unless measurement
establishes the mapping.

Feedback remains local to each DelayBand recurrence.

CONNECT must not create cross-band feedback.

---

## TAP

TAP determines where the audible delay signal is taken relative to the configured
delay circuit.

HoldsworthEngine currently represents this as a normalized `TapFraction`.

The current project model separates:

- the full loop delay used by feedback;
- the TAP-selected audible delay output.

For TAP below 100%, the audible signal may occur before the full feedback-loop
delay.

The project's separate TAP-output filter state is provisional and should remain
hardware-verifiable.

Do not silently change feedback timing when changing TAP.

---

## SPEED

Yamaha SPEED controls delay modulation rate.

It is not itself a frequency in Hz.

HoldsworthEngine uses `ModulationRateHz`.

Do not introduce a generic SPEED-to-Hz conversion until physical Magicstomp
measurement establishes one.

Any current SPEED-derived Hz values are explicit provisional audition values.

---

## DEPTH

Yamaha DEPTH controls modulation depth.

It is not itself a delay excursion in milliseconds.

HoldsworthEngine uses `ModulationDepthMs`.

Do not introduce a generic DEPTH-to-ms conversion until physical measurement
establishes one.

Current values are provisional where unmeasured.

---

## WAVE

Documented Yamaha modulation waveform choices are conceptually separate from
modulation rate and depth.

HoldsworthEngine currently supports:

- sine;
- triangle;
- saw up;
- saw down.

Sine is the strongest established reference path.

Exact Yamaha saw direction/origin remains hardware-verifiable.

Do not modify oscillator phase merely because waveform changes.

---

## PHASE

Yamaha PHASE values include:

- NOR;
- REV.

In this context PHASE refers to polarity of the audible delayed signal relative
to the direct/input signal.

It is NOT modulation oscillator phase.

HoldsworthEngine represents this as `DelaySignalPolarity`.

REV currently negates only the audible delayed contribution.

It must not reverse:

- feedback sign;
- delay history;
- filter recurrence;
- TAP timing;
- modulation.

When used with CONNECT, the signed audible delay contribution may affect
downstream audio because CONNECT forwards the upstream input-plus-delay mixture.

Exact hardware placement remains measurable.

---

## SYNC

SYNC concerns modulation oscillator timing relationships between bands.

It is not audio routing.

Keep:

    SYNC    -> modulation timing
    CONNECT -> audio routing
    GROUP   -> delay resource/control grouping

as separate concepts.

HoldsworthEngine resolves SYNC relationships at the engine level.

Each synchronization root retains the authoritative oscillator.

Synchronized slave bands retain their own:

- delay;
- depth;
- waveform;
- feedback;
- TAP;
- filters;
- polarity;
- level;
- pan.

The Yamaha documentation demonstrates a 180-degree synchronized relationship.

The current engine supports direct root/slave synchronization.

Do not invent unsupported synchronization chains from generic graph theory.

---

## CONNECT

CONNECT controls which audio signal enters an Effect Band.

`CONNECT IN` means the band receives the UD-Stomp input directly.

When a selected destination band is connected to another band, the displayed
band is the source feeding that destination.

Yamaha documentation demonstrates:

- independent parallel bands;
- serial chains;
- mixed parallel/serial structures;
- fan-out;
- routing from higher-numbered bands to lower-numbered bands.

The manual describes a connected source band's output as its input/original
signal mixed with its delay sound.

HoldsworthEngine therefore models CONNECT as a separate engine-level one-parent
acyclic routing graph.

Current provisional routing concept:

    routedSignal =
        bandInput +
        perBandLevel * signedFilteredTapSelectedDelay

This routing signal is mono.

PAN is applied only to the band's final stereo wet presentation.

The HoldsworthDelayEngine final output remains wet-only; internal routed copies
of the direct/input signal are not summed directly to final output.

Current disabled-band CONNECT behavior is provisional bypass:

    routedSignal = bandInput

while the disabled DelayBand retains its existing muted-tail semantics.

Do not reinterpret CONNECT as cross-band feedback.

---

## GROUP

GROUP is NOT CONNECT.

CONNECT creates audio relationships between distinct Effect Bands.

GROUP combines Effect Band resources/control into a larger logical delay
structure.

Do not model GROUP as a CONNECT chain.

GROUP architecture must be derived separately from Yamaha documentation and,
where required, later physical measurement.

---

## Low Cut / High Cut

Yamaha provides low-cut and high-cut controls associated with delay behavior.

HoldsworthEngine currently uses provisional first-order filters in the delay
loop.

The current implementation applies filtering to the first delayed output and
subsequent repeats.

The exact Yamaha filter topology, slopes and control mapping remain
hardware-verifiable.

Do not claim the current TPT implementation is a measured Yamaha circuit model.

---

## LEVEL

Yamaha per-band LEVEL controls delay-output level.

HoldsworthEngine uses an explicit physical output level.

No general hardware-calibrated Yamaha LEVEL-to-gain mapping exists yet.

In the current provisional CONNECT model, per-band level affects:

- the band's audible wet contribution;
- the delayed component forwarded through CONNECT.

It does not scale the direct/input portion of the CONNECT routing signal.

This ordering remains hardware-verifiable.

---

## PAN

Yamaha PAN positions the delay sound in stereo.

HoldsworthEngine currently treats CONNECT routing as mono and applies PAN only
for final stereo wet output.

Therefore PAN does not alter downstream CONNECT input.

This interpretation is strongly consistent with documented behavior but remains
subject to physical verification if necessary.

---

## EFFECT LEVEL

Do not equate Yamaha EFFECT LEVEL with HoldsworthEngine
`globalWetOutputLevel`.

The Yamaha documentation indicates EFFECT LEVEL participates in signals sent to
connected/grouped Effect Bands.

The exact gain mapping and internal placement are not yet calibrated.

HoldsworthEngine's current `globalWetOutputLevel` remains a post-sum engine wet
control.

A future measured Yamaha EFFECT LEVEL implementation should be represented
explicitly rather than silently changing the meaning of that existing control.

---

## Direct Level / Direct Pan

These are Yamaha source-domain global controls.

The current NAM integration has its own external dry/wet architecture.

Do not automatically map Yamaha Direct Level or Direct Pan to the integration
mixer without first designing and validating that relationship.

---

## Calibration status

The following mappings remain intentionally unmeasured:

- SPEED -> Hz;
- DEPTH -> milliseconds;
- FEEDBACK -> coefficient;
- LEVEL -> gain;
- PAN law;
- EFFECT LEVEL;
- Direct Level / Direct Pan;
- detailed filter response;
- exact waveform behavior;
- exact internal placement of some TAP / PHASE / CONNECT operations.

Physical Magicstomp measurement is the authority for replacing these
provisional relationships.
