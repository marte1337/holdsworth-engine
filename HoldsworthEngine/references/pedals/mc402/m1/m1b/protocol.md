# M1b protocol, fixed before the product sweep

This is an offline review proposal. It changes neither the frozen DSP profile
nor the historical M1/M1a measurements. The grid and limits are centralized in
[envelope.json](envelope.json). No outcome was used to select these limits.

Propose a **4.5 V peak, 40 Hz–10 kHz** single-tone qualification rectangle at the
post-trim pedal voltage port. This is a deliberately conservative surrogate for
guitar inputs: it permits the entire peak at any one frequency, although a real
pluck distributes energy among partials. The 0.25–2 V / 2–8 kHz region is a
separately reported musical challenge subset, never discarded as torture.

The existing bridge is `v = mono * 10^(trimDb/20) * sqrt(2)*0.775*10^(C/20)`
with active calibration; fallback uses one post-trim unit per provisional volt.
This bounds the actual pedal port after mono preparation and trim, not raw host
dBFS, pickup model, or NAM metadata. It introduces no input limiter or filter.

DiMarzio's passive X2N output rating is 510 mV, but the manufacturer describes
its mV figures as relative loudness indicators, not universal peak guarantees.
EMG's H/HA/58/60/60A/81/85 installation table gives string outputs up to 3.10 V
and strum outputs up to 4.50 V, without an explicit peak/RMS/peak-to-peak
convention in that table. **Do not silently divide those figures by two.**
4.5 V peak is a conservative engineering allowance, not a claim that the table
establishes a measured 4.5 V peak under defined playing conditions. A blanket
2 V ceiling covering all active guitars is not supported by these sources.
9 V is the MC402 supply, not a universal input clipping threshold; 4.5 V peak
is already a 9 V peak-to-peak upper challenge. 10 V peak is 20 V peak-to-peak,
outside the intended bounded overload fidelity claim. Hotter active systems,
external boosters and excessive software trim may exceed the declared domain.

Passive pickup resonances in Seymour Duncan's manufacturer chart extend through
several kHz and above 8 kHz; Fishman also documents active voices with 4–5 kHz
peaks. Therefore 5 kHz / 0.5 V / Gain 1 remains a product challenge, and no
spectral taper is invented to excuse the high-band cases. 10 kHz provides an
upper-band guard around the existing 8 kHz small-signal target. 40 Hz extends
below standard low E to cover low tunings. These are proposed declared fidelity
bounds, **not** proof that every guitar has zero content outside them. Low-level
10–20 kHz tails are reported as sensitivity diagnostics; they are not called
numerical torture merely because they exceed 10 kHz. Passing this finite sine
grid would still require broader arbitrary-input/intermodulation validation.

Sources, checked 2026-09-11 (physical input context, no circuit archaeology):

- [DiMarzio X2N](https://www.dimarzio.com/pickups/high-power/x2n) and
  [meaning of its output figures](https://www.dimarzio.com/node/3362).
- [EMG installation table, p. 1](https://www.emgpickups.com/pub/static/version1696379772/frontend/Magento/emg-m2_v1/en_US/pdfs/top-wiring-diagrams/h__instructions_0230-0106rg.pdf).
- [Seymour Duncan archived manufacturer comparison](https://www.seymourduncan.com/blog/latest-updates/pickup-comparison-chart).
- [Fishman Fluence Classic specifications](https://fishman.com/dp/fluence-classic-humbuckers-6-string-pickups/).
- [Dunlop MC402 power specification](https://www.jimdunlop.com/mxr-cae-boost-overdrive/).

All Gain/Tone/Output positions remain in scope. Grid Gain 0.1/0.25/0.5/0.75/0.9/1,
Tone 0/0.5/1; denser high-Gain follow-up is prescribed in the JSON. Gain 0 is
checked as the frozen **provisional** mute endpoint. Output is a scalar after
the nonlinear chain: evaluate qualification at unity Output, and verify the
law and mute separately. Attenuating Output cannot earn qualification. Boost
is a downstream scalar and receives the same treatment, with absolute levels
shifted by its gain. No hearing threshold or calibrated hardware noise floor
is inferred from provisional dBFS.

## Proposed exact fidelity criteria

Both must pass at unity Output and Boost off, at every nonmuted tested point:

1. Strongest identified 20 Hz–20 kHz alias <= **-70 dBc**, retaining the original
   relative engineering threshold.
2. Sum of identified alias-bin powers <= **-80 dBFS sine-equivalent**. This is
   70.7107 microvolts RMS total, or 100 microvolts peak for one sine. It also
   guarantees no individual identified alias exceeds -80 dBFS peak.

The second is a **new proposed absolute engineering budget**, fixed here before
the sweep. It equals the original relative budget at a 0.316228 V peak
(-10 dBFS) fundamental, and bounds absolute leakage when the output fundamental
is large. It is deliberately an AND rule, with no quiet-fundamental exception.
The existing ratio is retained even when Tone attenuates the fundamental;
absolute values expose the severity. The decision will also be reported using
the original -70 dBc criterion alone, so a new absolute budget cannot manufacture
a failure or conceal a relative failure.

For complex peak Fourier coefficients C, strongest absolute level is
`20*log10(max(abs(C_alias)))`; aggregate is
`10*log10(sum(abs(C_alias)^2))`. Alias RMS volts is
`sqrt(sum(abs(C_alias)^2)/2)`. Total alias dBc uses the same fundamental RMS
reference. Exclude DC and intended unfolded harmonics; alias coincident with
an intended harmonic remains unseparated, so measured energy is a lower bound.
The continuous-time reference has exactly zero coefficients at these identified
nonharmonic bins. Compare full reference harmonics and aligned residuals on the
worst and representative cases; do not label all residual error as aliasing.

Retain <=0.5 dB small-signal magnitude error over 80 Hz–8 kHz and the <=1 ms
added-latency target. Measure actual FIR plus ADAA delay, including fractional
samples. CPU is an offline estimate, with no new acceptance number or live
deadline claim invented for this review.

## Torture / robustness

Retain the historical 0–10 V, 100 Hz–19 kHz grid and add genuinely near-Nyquist
tones at each host rate. Keep 30-second signals, silence recovery, DC, bursts,
huge finite samples, NaN/Inf handling and denormals in the production robustness
suite. The unsanitized M1a experiment is not a replacement for production input
guards. Assess experimental ADAA stability with finite +/-10 V and recovery;
retain the existing M1 production guard evidence separately.

Require finite bounded output and recovery, and report strongest/aggregate
absolute and relative aliases independently of PRODUCT qualification. Flag a
gross artifact for review when both strongest alias >-30 dBc **and** its level
>-40 dBFS at canonical output. This is a proposed triage trigger (31.6x amplitude
ratio / 10 mV peak), not a torture fidelity pass, psychoacoustic claim, or waiver
of a stability defect. No attenuated-fundamental ratio alone decides severity.
A product failure remains a product failure even if torture is stable.
