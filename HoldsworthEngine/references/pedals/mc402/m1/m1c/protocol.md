# M1c preregistered experiment

Candidate: `MC402-BOUNDED-V1-PROVISIONAL-R2`, offline only. V1 is retained.
The only model change is its normalized positive knee, with
`t = (q - 0.9) / 0.2`: `0.9 + 0.2 t - 0.2 t^3 + 0.1 t^4`.
The degree-five Hermite coefficient is zero. All other constants, filter
recurrences, control laws, stage order and FIR coefficients remain frozen.

Use the accepted M1b envelope and exact main/dense case coordinates: 40 Hz–10 kHz,
up to 4.5 provisional V peak, all reviewed Gain/Tone coverage, Output 1, Boost 0.
Keep the dense 2–8 kHz / 0.25–2 V coverage, historical product witnesses, and
the 5 kHz / 0.5 V / Gain 1 case at both Tone extremes. Output and Boost remain
scalar laws; do not use attenuation to manufacture qualification.

At every product case require BOTH strongest identified alias <= -70 dBc and
total identified alias power <= -80 dBFS sine-equivalent. Analysis band is
20 Hz–20 kHz; one peak provisional volt defines 0 dBFS. Report fundamental,
strongest alias and aggregate absolute levels. Exclude DC and intended,
unfolded integer harmonics. Coincident aliases cannot be separated by this
single-tone metric, so measured alias power is a lower bound, not a full error
measurement. Continuous-time intended-harmonic comparisons are separate.

Evaluate ordinary 4x, ordinary 8x, ADAA1 4x, ADAA2 4x and ADAA2 8x, each with
(1) the actual unchanged production FIR taps and (2) ideal interpolation and
projection to host Nyquist. Ideal projection is diagnostic, not a causal
realtime resampler proposal. No FIR design changes in this experiment.

Use the M1a continuous-time event-split reference with only its saturation
callable replaced by R2. Check quadrature convergence again for selected R2
witnesses. Validate an exact periodic feed-forward renderer against the native
sample-by-sample clone before using it for the large sweep. This FFT solution
removes redundant startup warmup, not any filter or nonlinear stage.

Static comparisons use analytic transfers and isolated periodic renders,
without NAM, IR, guitar listening or circuit research. Keep the historical
10 V / near-Nyquist torture grid separate. Finite state, recovery and gross
alias flags there are robustness diagnostics, not product fidelity waivers.

Measure native CPU and small-signal response. FIR delays remain 40/44 host
samples at 4x/8x; two-stage ADAA1 adds 1/factor and ADAA2 adds 2/factor samples.
Check those predictions experimentally. Existing 0.5 dB small-signal and 1 ms
latency targets remain visible; neither alias failure nor latency is waived.

Decision after measurement: A (R2/ADAA2 4x qualifies), B (8x qualifies), C
(ideal nonlinear core qualifies but FIR needs revision), or D (local redesign
still fails; broader provisional Overdrive design needs review). No chosen
solution is implemented in production and no M2 or commit is authorized.
