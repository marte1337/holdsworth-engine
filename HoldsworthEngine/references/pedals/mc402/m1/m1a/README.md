> **Historical Overdrive research — rejected for product integration.** The accepted
> M1c review ended development of these provisional nonlinear profiles: insufficient
> evidence plus failure of the approved fidelity envelope even with ideal resampling.
> Overdrive is deferred pending stronger circuit evidence or hardware measurement.
> Current product scope is [MC402-CLEAN-BOOST-V1](../../clean-boost-v1/README.md);
> rejected source and reproduction guidance are in the [archive](../../rejected-overdrive/README.md).
> References below to production, integration blockers or M2 status describe the
> historical milestone only. Its measurements do not describe the new clean Boost.

# MC402 M1a: alias diagnosis and antiderivative experiments

2026-09-11. Frozen profile: **MC402-BOUNDED-V1-PROVISIONAL**.

**Historical M1a full-grid conclusion.** The subsequent
[M1b envelope review](../m1b/README.md) separates PRODUCT fidelity from TORTURE
robustness. The recommendation and “product gate” wording below describe the
original mixed stress envelope; 10 V/near-Nyquist failures alone do not decide
the new PRODUCT proposal. M1b reuses this unchanged experiment and continuous-
time reference, preserving the transfer and all prior measurement artifacts.
M1a validation hashes describe the M1a snapshot; this follow-up note is the only
M1b change within the M1a package.

**Recommendation D: return the frozen saturation/profile requirements to design
review.** No tested ordinary or low-order ADAA strategy with modest oversampling
meets the unchanged full -70 dBc envelope. This is a measured limitation of the
strategies tested here, **not a proof that every possible exact-transfer
antialias algorithm is impractical**. No production solution is selected.

All experiments are offline. The MC402 production processor, numerical profile,
FIR coefficients, tests, project enrollment, NAM integration, TC BLD, Yamaha and
UI are unchanged. No M2 work, circuit archaeology, rig tuning, gate relaxation
or commit occurred. Gain-minimum mute remains an explicitly provisional behavior;
this study adds no hardware claim.

## Read the results

- [Worst three ordinary cases at every rate/factor](worst-cases.md): 72 rows with
  frequency, amplitude, Gain/Tone/Output, factor, absolute and relative levels.
- [Summary and complete mandatory comparison matrix](summary.json).
- [Worst ADAA cases](adaa-worst-cases.json). The complete 3600-case ordinary
  grid (`ordinary-cases.json`) and 16200-case ADAA grid (`adaa-cases.json.gz`)
  are reproducible local outputs, excluded from Git. See the
  [retention and reproduction policy](../../repository-hygiene.md).
- [Stage isolation](stage-localization.json),
  [control-region isolation](stage-control-regions.json),
  [reference convergence](reference-convergence.json),
  [FIR/ideal-resampler strategy comparisons](strategy-probes.json).
- [Frequency response](adaa-frequency-response.json),
  [CPU estimates](cpu-estimates.json), [validation record](validation.json),
  [standalone figure](diagnosis.png).

## 1. Actual worst cases and what positive dBc means

**Level convention:** 0 dBFS means a sinusoid with peak 1 signal unit. In this
isolated volts-domain harness that is **1 provisional V peak**. These are
peak-sinusoid levels, not RMS dBFS or a calibrated hardware/host voltage bridge.
Internal float output may exceed 0 dBFS. Both absolute levels use the same
reference, so `alias dBc = alias dBFS - fundamental dBFS`.

The original M1 stimulus grid is retained: six supported rates; nearest odd
8192-point FFT bins to 100/1000/5000/9500/19000 Hz; peaks
0.005/0.02/0.12/0.5/10 V; Gain 0.1/0.5/1; Tone 0/1; Output 1; Boost off.
The ordinary audit covers 1x/2x/4x/8x. A complete input period is repeated for
at least 0.5 s before analyzing one period with a rectangular window. These are
**observed grid maxima**, not an exhaustive continuous-frequency maximum.

One worst example per supported rate (all: **10 V peak, Gain 1, Tone 0, Output 1,
ordinary 1x**):

| Host Hz | Input Hz | Fundamental dBFS | Alias Hz | Alias dBFS | Alias dBc |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 44100 | 18997.668457 | -32.020684 | 683.679199 | -11.426308 | +20.594376 |
| 48000 | 19001.953125 | -29.030883 | 990.234375 | -10.851723 | +18.179160 |
| 88200 | 19003.051758 | -23.030863 | 6815.258789 | -26.807157 | -3.776294 |
| 96000 | 18996.093750 | -22.770584 | 1019.531250 | -11.049423 | +11.721161 |
| 176400 | 19013.818359 | -21.888709 | 5275.634766 | -29.557981 | -7.669272 |
| 192000 | 19007.812500 | -21.831421 | 210.937500 | -30.754953 | -8.923532 |

The next worst cases usually retain that frequency/Tone but use Gain 0.5 or
0.5 V input at Gain 1. They are individually listed in the 72-row appendix.
Positive dBc also occurs at **48 kHz/2x**, 19001.953125 Hz, 10 V, Gain 1,
Tone 0, Output 1: fundamental **-22.773998 dBFS**, alias 990.234375 Hz at
**-10.846221 dBFS**, or **+11.927777 dBc**.

The positive values are real. For the 44.1 kHz/1x example, the seventh harmonic
folds to `7*18997.668457 - 3*44100 = 683.679199 Hz`. With Tone bypassed only
for diagnosis, the **same tracked component** is -6.845907 dBFS and the
fundamental is +10.057002 dBFS. Tone attenuates the fundamental by **42.077686 dB**
and that alias by only **4.580401 dB**. It moves their ratio from -16.902909 dBc
to +20.594376 dBc. The low-rate bilinear Tone response has particularly strong
attenuation close to Nyquist. This is not just a harmless denominator near zero:
the alias is about **0.268 V peak** at the output.

The corresponding lowest matching folded odd harmonics at the six rates are
7, 5, 5, 5, 9 and 101. The nonlinear reference being unconverged compromises
reference-residual interpretation; it does **not** invalidate these independently
identified nonharmonic bins. Tone bypass changes which alias is strongest, so
comparisons above track a fixed component rather than subtracting unrelated
maxima.

## 2. Measurement-harness audit

The detector excludes DC and every *unfolded* in-band harmonic of the input.
Any other significant coherent bin is a folding component for these steady,
odd, time-invariant single-tone systems. It cannot separate an alias that lands
exactly on an intended harmonic; consequently this is a **lower bound** on total
alias contamination. It does not falsely call every reference mismatch an alias.

Known-result tests at 48 kHz:

| Test | Input Hz | Fundamental dBFS | Strongest nonharmonic bin dBFS | dBc |
| --- | ---: | ---: | ---: | ---: |
| Identity, 0.5 peak | 1001.953125 | -6.020600 | -287.600606 | -281.580006 |
| Boost scalar +20 dB | 1001.953125 | +13.979400 | -267.600992 | -281.580392 |
| Frozen linear filters, saturators bypassed | 1001.953125 | +46.908002 | -235.043432 | -281.951435 |
| `x + 0.2*x^3`, 0.5 peak, no folding | 1001.953125 | -5.700838 | -287.231748 | -281.530910 |
| Same polynomial, known folded third | 19001.953125 | -5.700838 | **-44.082400** | **-38.381562** |
| Frozen saturator, 2.75 V peak | 87.890625 | +8.463763 | -134.692293 | -143.156056 |

The polynomial's intended third harmonic is excluded when below Nyquist. When
it folds, the detector finds **9005.859375 Hz** and **0.00625 peak**, agreeing
with the analytic cubic coefficient. Its fundamental is **0.51875 peak**.
The low-frequency frozen saturator's relevant harmonics are in band and are
excluded correctly; its tiny remaining high-order fold is well below the gate.
The extreme negative levels in the linear tests are floating-point/FFT residue,
not claims about an analog noise floor.

The offline ordinary C++ model was compared with the actual separately compiled
production processor on seeded input at all six rates and 1x/2x/4x/8x. All
24 comparisons pass a 1e-12 sample-error limit. No measurement-error explanation
rescues M1. Full values are in [harness-sanity.json](harness-sanity.json).

## 3. Where the aliases originate

In isolation tests, “S1 only” means S1 saturation remains and S2 saturation is
replaced by identity; its gain and coupling filter remain. “S2 only” makes the
opposite diagnostic substitution. The production topology is untouched.
Tone bypass removes only that low-pass from the offline model. S1-only output
can be much larger because its following S2 gain is then unclipped.

Representative 44.1 kHz/4x, 18997.668457 Hz, 10 V, Gain 1, Tone 0, Output 1:

| Diagnostic configuration | Strongest alias dBc |
| --- | ---: |
| S1 only, Tone bypassed | -19.043 |
| S2 only, Tone bypassed | -19.023 |
| Both, Tone bypassed | -19.023 |
| S1 only, Tone applied | -7.870 |
| S2 only, Tone applied | -5.173 |
| Both, Tone applied | -5.189 |

Both stages can generate substantial folding. At high Gain, S2 dominates the
completed hard-saturated result and can convert errors from S1 into further
nonlinear distortion. Aliases already folded to the audio band cannot be
removed by the following downsampling FIR. Tone often makes their ratio to the
high-frequency fundamental worse.

The additional **432-render control-region matrix** proves two useful boundaries:

- At Gain 0.1, both-stages output is **bit-exact to S1-only** in every tested
  region: S2 stays linear after the very small interstage attenuation.
- At input peak 0.02 V, both-stages output is **bit-exact to S2-only**: S1 remains
  below its knee. Increasing Gain then drives S2 into saturation.

For high-frequency small signals, the approximate input knees are 0.1053 V at
S1, and `0.00493 / Gain^3.321928094887362` V at S2 while S1 is linear. These are
consequences of the frozen gains/headroom, not hardware measurements. At Gain 1,
the second stage is already near its knee around 5 mV input; at Gain 0.5 the
interstage attenuation is 0.1, and at Gain 0.1 it is about 0.000477.

First-order ADAA at S1 alone or S2 alone leaves the representative 4x failure at
-6.86/-7.08 dBc; both together improve it to -20.82 dBc. Second-order divided-
difference ADAA at S1 alone/S2 alone gives -6.85/-7.01 dBc; both give -44.14 dBc.
A single-stage fix is insufficient for the full cascade.

## 4. Trustworthy reference and convergence

The reduced set contains the ordinary worst regions at every rate, the additional
44.1 kHz/4x ADAA-worst Gain-0.5 region, and four guitar-band diagnostics: **12 cases**.
Direct ordinary sampling is tested at 8/16/32/64/128/256x; 512x is added when the
256x result has not met a -90 dB reference margin. Ideal physical sine input and
ideal FFT output-band projection remove production FIR uncertainty.

Raw sampling is still not a trustworthy reference for the hardest cases at 512x.
For 44.1 kHz, 18997.668457 Hz, 10 V, Gain 1, Tone 0, Output 1:

| Direct factor | Strongest alias dBc |
| ---: | ---: |
| 8 | -14.44 |
| 16 | -14.50 |
| 32 | -30.72 |
| 64 | -32.02 |
| 128 | -32.02 |
| 256 | -45.14 |
| 512 | -45.14 |

At 512x, the other primary worst cases still have aliases of approximately
-66.08 dBc (44.1 kHz/9.5 kHz input), -53.65 (48 kHz), -66.06 (88.2 kHz),
-65.04 (96 kHz), -87.19 (176.4 kHz) and -87.61 (192 kHz). All raw and successive
complex-band differences are retained in [reference-convergence.json](reference-convergence.json).

A separate **continuous-time periodic reference** resolves the ambiguity:

1. Apply the analytic input high-pass response to the sine.
2. Locate every S1 knee crossing in phase.
3. Integrate the interstage first-order filter with its periodic steady-state
   boundary condition, splitting at S1's knees.
4. Locate every S2 knee crossing and split the Fourier integrals there as well.
5. Integrate each smooth interval, then apply the analytic Tone/output filters
   to the intended audio-band harmonics.

This evaluates the same frozen transfer and analog filter reduction without
uniformly sampling the sharply clipped waveform. It is an offline reference for
periodic sines, not a proposed realtime processor. Increasing Gauss-Legendre
order from 64 to 128 changes the reference coefficients only at floating-point
roundoff on this set. Twelve separate small-signal checks agree with the
independent analytic linear cascade. This does not claim hundreds of decibels
of physical accuracy.

As a cross-check with a different numerical procedure, high-rate **ADAA1** with
known linear delay removed approaches this reference. At 512x its complex-band
error for the primary extremes is **-108.98 to -134.68 dB**. The hardest 44.1 kHz
case is -86.48 dB at 256x and -108.98 dB at 512x; therefore **256x-to-512x alone
would not establish a -90 dB convergence bound there**. The independent
quadrature comparison establishes the usable reference. No higher uniform factor
than 512x was used. Ordinary 256x already qualifies for the four milder guitar-
band diagnostics, so their final protocol does not require 512x.

All candidate comparisons use this independent reference. They align the known
FIR and ADAA linear-regime delay. Both raw aligned and complex wire-response-
compensated residuals are recorded. Residuals include dynamic-filter/BLT effects
and nonlinear input-filter interaction; they are **not** labelled alias levels.

## 5. Exact antiderivatives and ADAA experiments

Let `q=x/H`, `a=0.9`, `b=1.1`, `w=0.2`, `c=2.5`, and let `s(q)` be the frozen
normalized odd saturation. For `q >= 0`:

```
s(q)  = q                              q <= a
        q - c*(q-a)^2                  a < q < b
        1                              q >= b

F1(q) = q^2/2 - c*max(q-a,0)^3/3       q <= b
        F1(b) + (q-b)                  q >= b

F2(q) = q^3/6 - c*max(q-a,0)^4/12      q <= b
        F2(b)+F1(b)*(q-b)+(q-b)^2/2    q >= b
```

Extend F1 evenly and F2 oddly. `F1(b)=0.5983333333333333...`, `F2(b)=0.2215`.
Volts-domain antiderivatives scale by H²/H³. Their derivatives recover the exact
frozen transfer; no H, knee, Gain, Tone, Output or stage-order change is involved.

The first-order experiment evaluates `H*F1[q[n],q[n-1]]`, where brackets denote
a divided difference. This follows the continuous-time rectangular-kernel
construction in [Parker, Zavalishin and Le Bivic, DAFx 2016](https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf).

The second-order experiment evaluates `2*H*F2[q[n],q[n-1],q[n-2]]`, using an
exact triangular B-spline integral over sorted argument knots. Higher-order
antiderivative methods are discussed in
[Bilbao et al., IEEE SPL 2017](https://doi.org/10.1109/LSP.2017.2675541).
We also tested Parker's distinct triangular **time** kernel on the reduced set;
it is labelled mode 3 and is not conflated with the second divided difference.

To avoid subtracting large nearly equal primitives, the implementation splits
at the four knee boundaries and evaluates the primitive differences in local
polynomial form. On each segment, integrating a quadratic times a linear weight
is analytic. Normalized interval weights avoid cancellation and reciprocal
blow-up near coincident nodes. Zero-width and repeated-node limits are explicit.
There are **no lookup tables or quadrature evaluations inside the experimental
ADAA kernel**. Quadrature and 80-digit decimal arithmetic are independent tests.

600 integral comparisons have maximum absolute error 6.66e-16. The 2210
80-digit primitive/Hermite comparisons have maximum errors 2.22e-16 (ADAA1)
and 4.44e-16 (ADAA2), including repeated outer nodes, knee crossings and almost
identical inputs. Constant-input limits reproduce the unchanged static curve.

The mandatory full-grid comparison below uses ADAA at **both** stages. Numbers
are worst identified alias dBc; **every entry fails -70 dBc**.

| Host Hz | Ordinary 1x | Ordinary 2x | Ordinary 4x | ADAA1 1x | ADAA1 2x | ADAA1 4x | ADAA2 1x | ADAA2 2x | ADAA2 4x |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 44100 | +20.59 | -3.32 | -5.19 | +29.04 | -14.83 | -20.56 | +23.71 | -16.97 | -33.04 |
| 48000 | +18.18 | +11.93 | -13.74 | +19.87 | -1.92 | -29.06 | +29.55 | -8.98 | -39.05 |
| 88200 | -3.78 | -6.46 | -13.05 | -14.93 | -20.54 | -24.39 | -17.05 | -33.00 | -34.79 |
| 96000 | +11.72 | -11.55 | -17.63 | -2.15 | -29.04 | -34.47 | -9.20 | -39.04 | -44.37 |
| 176400 | -7.67 | -12.34 | -12.40 | -20.37 | -26.85 | -23.76 | -32.78 | -37.27 | -34.15 |
| 192000 | -8.92 | -9.14 | -18.62 | -29.09 | -31.00 | -29.82 | -39.06 | -41.64 | -40.49 |

ADAA was also tested independently at S1 and S2 throughout this same grid.
The full 16200-case dataset contains all six placements/orders at 1x/2x/4x.
The worst both-stage ADAA2/4x case is **44.1 kHz, 18997.668457 Hz, 10 V peak,
Gain 0.5, Tone 0, Output 1**: fundamental **-22.670398 dBFS**, alias
**5420.983887 Hz at -55.709224 dBFS**, or **-33.038827 dBc**. ADAA1/4x at the
same settings gives fundamental -22.259350 dBFS and that alias -42.819963 dBFS,
or -20.560613 dBc. ADAA1's improved average behavior does not guarantee a better
worst bin at every factor; folds can move into a more strongly passed region.

FIR effects are real but are not the entire problem. For the 44.1 kHz,
18997.668457 Hz, 10 V, Gain 1/Tone 0 example with ADAA2 at both stages:

| Factor | Existing M1 FIR dBc | Ideal sine input + ideal output projection dBc |
| ---: | ---: | ---: |
| 4 | -44.14 | -49.48 |
| 8 | -41.61 | -41.63 |
| 16 | -41.32 | -41.17 |
| 32 | -43.80 | -59.73 |
| 64 | -43.80 | -64.60 |

The high-factor difference implicates resampler images/rejection and their
nonlinear interaction. But even the ideal-resampler diagnostic misses the gate
at these factors. Improving FIR stopbands alone cannot establish a passing
modest-factor solution. No deliberate prefiltering or sound retuning was used
to reduce the stimulus entering the saturators.

## 6. Guitar-band behavior, cost, state and latency

These are synthetic, rig-independent diagnostics, not assertions about a
particular guitar's spectrum or calibrated output. At **48 kHz/4x, Tone 1,
Output 1**, applying ADAA to both stages gives:

| Input / Gain | Method | Alias dBc | Alias dBFS |
| --- | --- | ---: | ---: |
| 1001.953125 Hz, 0.12 V, Gain 0.5 | Ordinary | -109.42 | -99.76 |
| same | ADAA1 | -137.31 | -127.66 |
| same | ADAA2 | -159.68 | -150.03 |
| 4998.046875 Hz, 0.5 V, Gain 1 | Ordinary | -31.21 | -22.57 |
| same | ADAA1 | -57.72 | -49.10 |
| same | ADAA2 | -68.52 | -59.91 |
| same | Triangular time kernel | -73.49 | -64.88 |

The triangular kernel passes this one demanding guitar-band example but fails
the extreme cases; it is not a qualified production choice. Neither that result
nor the very clean 1 kHz example supports reducing the existing FIR oversampling
under the full unchanged gate.

Measured median CPU on this arm64 macOS 26.3.1 machine, **48 kHz**, both stages,
existing FIRs, unoptimized experimental implementation:

| Method | 1x ns/sample | 2x | 4x | 8x | 4x audio-time budget |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ordinary | 10.2 | 48.7 | 95.8 | 194.6 | 0.460% |
| ADAA1 | 34.0 | 117.8 | 232.2 | 466.5 | 1.115% |
| ADAA2 | 62.2 | 170.7 | 326.1 | 657.5 | 1.565% |
| Triangular time kernel | 46.4 | 128.6 | 252.3 | 506.9 | 1.211% |

Each number is the median of three 2-second audio-length runs on a precomputed
0.5 V/997 Hz table, Gain/Tone/Output 1. Includes core and FIR work; excludes host
callback, handoff and live dry-path alignment. These are aggregate CPU estimates,
not realtime deadline guarantees. No optimization pass was performed.

ADAA1 needs one previous normalized stage input; ADAA2 needs two. The uniform
experimental stage struct stores two doubles plus a mode integer/padding,
**24 bytes per stage**; mathematical history requirements are 8/16 bytes.
The triangular time variant also uses two history samples. FIR histories remain
unchanged.

Measured small-signal kernels per stage are:

- ADAA1: `(1+z^-1)/2`, adding 0.5 internal sample.
- ADAA2: `(1+z^-1+z^-2)/3`, adding 1 internal sample.
- Triangular time: `(1+4*z^-1+z^-2)/6`, adding 1 internal sample.

Thus both stages add **1/factor** host sample for ADAA1 or **2/factor** for
ADAA2/triangular time, in addition to the FIR delay. These are verified linear-
regime group delays, not a newly implemented host-latency policy. At 4x, that
is 0.25/0.5 host sample; fractional dry alignment would require review.

An unchanged static curve does not imply unchanged dynamic response. At
44.1 kHz/4x, maximum additional 80 Hz–8 kHz attenuation is **0.176899 dB** for
ADAA1 and **0.473351 dB** for ADAA2. Including the frozen filters and FIRs, the
worst analog-reference errors across Tone 0/0.5/1 become **0.235427 dB** and
**0.531879 dB** respectively. The latter slightly fails the existing 0.5 dB
small-signal tolerance. At 8x, those total errors are 0.058538/0.132158 dB,
but the alias failures remain. At 1x/2x the added filtering is substantially
larger. No compensation EQ or makeup gain was introduced.

## 7. Review gate and remaining limits

| Option | Finding |
| --- | --- |
| A: original measurement flawed | Rejected: known-result tests and absolute levels validate the observed failures. |
| B: revised filters/factors sufficient | Not demonstrated: even ideal-resampler modest-factor diagnostics fail. Raw 512x remains unconverged on extremes. |
| C: ADAA + modest oversampling preferred | Promising for selected signals, but no tested candidate passes the full gate; a production choice is not justified. |
| **D: return to design review** | **Recommended within the tested strategy space.** The frozen high-gain, narrow-knee cascade and full stress envelope have no demonstrated practical passing implementation here. |

For scale, near a zero crossing of the 19 kHz/10 V/Gain-1 stress input,
the locally linear two-stage slew is approximately
`(470/22)^2 * 10 * 2*pi*19000 = 5.45e8 V/s` before S2 limits it.
Traversing one 0.5 V knee interval then takes roughly **0.92 ns**.
A 44.1 kHz/512x time sample is about **44.3 ns**. This estimate explains why
ordinary uniform sampling can remain unreliable even at that offline factor;
it is a local slope estimate, not a hardware slew-rate claim.

The antiderivatives are not the blocker: they are analytic, inexpensive and
numerically stable. The problem is the very sharp, strongly driven cascade,
including interactions between stage sampling, Tone's weighting of folded
components and finite resampler rejection. A design review should decide whether
to pursue another exact-transfer antialias method or explicitly revisit the
profile/operating requirements. This study does not authorize either change,
and does not weaken -70 dBc to obtain a pass.

The periodic reference is trustworthy for these single-tone cases; it is not
a general arbitrary-input realtime solution. The stress grid is finite, the
metric misses aliases coincident with intended harmonics, and no hardware
calibration or listening validation is claimed. Production implementation,
fractional latency handling and M2 remain deferred.

## Validation, files and reproduction

New files are contained in this `m1a/` directory: two offline C++ files, eight
Python tools, `reproduce.sh`, this report, the generated worst-case appendix,
JSON/compressed JSON measurements, validation hashes and the figure. The parent
M1 README gains only a link to this report. All pre-existing production and
integration files are verified byte-for-byte unchanged.

Validation performed:

- Known identity/Boost/filter/polynomial/saturator spectral sanity cases.
- Production/offline ordinary equivalence at 24 rate/factor combinations.
- 600 analytic-integral comparisons and 2210 80-digit primitive/Hermite checks.
- Repeated/near-equal nodes, knee boundaries, denormals and finite output.
- Standalone ASan + UBSan checks over 96 rate/factor/mode configurations,
  with whole versus 7-sample partition equality; no diagnostics.
- Strict Clang warnings and static analyzer on the offline C++: pass.
- 3600 ordinary and 16200 ADAA single-tone cases; stage/control-region,
  reference-convergence, ideal-resampler and small-signal matrices.
- Python syntax/format, shell syntax, local links, JSON integrity, new-file
  whitespace and `git diff --check`; production file hash audit.

The offline diagnostics pass as diagnostics; **the product alias gate still
fails**. No plugin target was changed or enrolled with this code. The production
validation matrix remains the separately recorded M1 result.

Use the pinned Python packages in [the M1 requirements](../tools/requirements.txt).
From the repository root:

```sh
MC402_M1A_PYTHON=/path/to/venv/bin/python \
  sh HoldsworthEngine/references/pedals/mc402/m1/m1a/reproduce.sh
```

The script builds fresh ordinary/experimental libraries, executes all numerical
matrices, updates the local result artifacts, regenerates the summaries/figure,
and runs the offline sanitizer executable. `MC402_M1A_BUILD` defaults to
`/tmp/mc402-m1a`; the full Xcode Clang toolchain is required. The C++ reference
flags are `-std=c++20 -O3 -DNDEBUG -fno-fast-math -ffp-contract=off`.
Sanitizer checks use `-O1 -g -fsanitize=address,undefined`; LeakSanitizer is not
supported on this platform and is disabled. Results were obtained with Apple
Clang 21.0.0, Python 3.9.6, NumPy 2.0.2 and SciPy 1.13.1.
