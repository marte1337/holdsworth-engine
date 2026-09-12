> **Historical Overdrive research — rejected for product integration.** The accepted
> M1c review ended development of these provisional nonlinear profiles: insufficient
> evidence plus failure of the approved fidelity envelope even with ideal resampling.
> Overdrive is deferred pending stronger circuit evidence or hardware measurement.
> Current product scope is [MC402-CLEAN-BOOST-V1](../../clean-boost-v1/README.md);
> rejected source and reproduction guidance are in the [archive](../../rejected-overdrive/README.md).
> References below to production, integration blockers or M2 status describe the
> historical milestone only. Its measurements do not describe the new clean Boost.

# MC402 M1c: minimal C² knee experiment

2026-09-11. Offline candidate: **`MC402-BOUNDED-V1-PROVISIONAL-R2`**.
Production remains **`MC402-BOUNDED-V1-PROVISIONAL`**.

**Decision D: the smoother local knee does not qualify. Reconsider the broader
provisional Overdrive model and its antialias realization in design review.**
ADAA2 4x and 8x fail the accepted PRODUCT envelope even with ideal resampling.
The existing FIR also causes a distinct bright-Tone failure, but fixing it
would not qualify this nonlinear core. No production solution is selected.

Only the offline saturation knee changed. Two-stage topology, A1/A2,
H1/H2 = 2.5 provisional V, input/interstage/output filters, Gain/Tone/Output
laws, Boost, section order, calibration convention, controls and software
architecture remain frozen. No NAM, IR, guitar listening or circuit research
was used. Production DSP, NAM, TC BLD, Yamaha DSP and UI remain unchanged.
No M2 or commit. Gain-zero mute remains a provisional named-profile endpoint,
not a hardware-verified MC402 fact.

## Derived transfer and isolated behavior

For `q=|z|/H` and `t=(q−0.9)/0.2`, R2 returns

\[
f(z)=\operatorname{sgn}(z)H\begin{cases}
q,&q\le0.9,\\
0.9+0.2t-0.2t^3+0.1t^4,&0.9<q<1.1,\\
1,&q\ge1.1.
\end{cases}
\]

The six value/slope/curvature boundary conditions yield a **quartic**, with
the quintic coefficient exactly zero. The slope is `1−3t²+2t³`, bounded
between 0 and 1; the second derivative is zero at both joins. The transfer
is odd, exactly linear below the knee and exactly capped at H above it.
[The analytic derivation](derivation.md) includes the minimal-degree proof,
coefficients, exact antiderivatives and robust ADAA divided-difference limits.

| Comparison | Measured or analytic result |
| --- | --- |
| Maximum static difference | **0.00625 H = 15.625 mV**, at `|z|=H`; zero outside the knee |
| Difference formula in positive knee | `R2−V1 = 0.1 t²(1−t)²` |
| Maximum local output-level increase | **0.055519518 dB** |
| Maximum slope difference | `sqrt(3)/18 = 0.096225044865…` |
| Full isolated cascade level sweep | 1,296 CT comparisons at 100/1k/5k/8k Hz; up to 4.5 V; Gain .25/.5/1 and Tone 0/.5/1 |
| Largest fundamental or in-band RMS level change in that sweep | **0.024687133 dB** |
| Largest waveform RMS difference relative to V1 in that sweep | **−36.069025 dB**, about 1.572% RMS; at 100 Hz / 0.371526897 V / Gain 1 / Tone 1 |
| Small-signal old/R2 difference | <= 1e-18 V in native impulse comparisons; same per-method magnitude response and delay |

The waveform result is not a claim of inaudibility. A small local stage change
can propagate through the second nonlinear stage. Single-stage harmonic
spectra were independently integrated for 11 drive peaks from 0.5 H to 40 H,
through harmonic 31. Low-knee harmonics change measurably; heavily driven
low/mid-order harmonics remain very similar. Six matched isolated old/R2
ADAA2 8x FIR renders were generated as double-precision NPZ and floating WAV
(left V1, right R2; unnormalized provisional volts). They were analyzed,
not listened to or tuned against another processor. These raw files are now
excluded from Git; their measured spectra and comparison figures remain.

See [static data and level-sweep extrema](static-comparison.json),
[static figure](static-comparison.png) and [harmonic figure](harmonics.png).
The [full-cascade render spectra](render-spectra.json) and
[comparison figure](render-spectra.png) also show the matched processor
outputs at low, medium and high input drive; intended harmonics are kept
separate from the alias metric.

## Unchanged qualification and coverage

The [preregistered protocol](protocol.md) reuses the accepted
[M1b envelope](../m1b/envelope.json): **40 Hz–10 kHz, up to 4.5 provisional V
peak**, output analysis over **20 Hz–20 kHz**. Full Gain/Tone/Output domains
remain in scope. The finite grid includes dense 125 Hz requests over 2–8 kHz,
0.25–2 V, Gain .5/.6/.75/.8/.9/.95/1 and both Tone extremes, plus the original
broader grid. Actual coherent frequencies, rather than their nominal labels,
are retained in every result.

Each PRODUCT case must satisfy **both**:

- strongest identified alias **<= −70 dBc** relative to its output fundamental;
- total identified alias power **<= −80 dBFS sine-equivalent**.

One provisional volt peak is 0 dBFS here, not a claim about a physical host
calibration or hardware clipping voltage. The aggregate limit is 100 µV
sine-equivalent peak, or 70.7107 µV aggregate RMS. Output is 1 and Boost off
for qualification. Scalars and mute endpoints are checked separately.

There are **80,586 unique PRODUCT coordinates per method/resampler**: 80,388
main+dense and 198 additional historical points inside the product domain.
Five methods × two resamplers give **805,860 PRODUCT evaluations**. The
historical points retain PRODUCT rules even though their source files have a
torture label. Finite-grid failures disprove qualification; passing isolated
examples do not establish qualification of an entire continuous domain.

## Alias results and exact witnesses

Global highest-dBc PRODUCT cases with the unchanged FIR:

| Method | Worst dBc | Alias dBFS | Total alias dBFS-SE | Fs / input Hz / V peak / Gain / Tone |
| --- | ---: | ---: | ---: | --- |
| Ordinary 4x | −7.303753 | −19.751937 | −19.415668 | 48000 / 6626.953125 / 2 / 1 / 0 |
| Ordinary 8x | −11.613990 | −24.812526 | −24.418066 | 48000 / 7248.046875 / 4.5 / 1 / 0 |
| ADAA1 4x | −18.656597 | −31.127986 | −31.108685 | 48000 / 6626.953125 / 2 / 1 / 0 |
| ADAA2 4x | **−30.416951** | **−42.937597** | **−42.925337** | 48000 / 6626.953125 / 2 / .5 / 0 |
| ADAA2 8x | **−33.989853** | **−47.513013** | **−47.498500** | 44100 / 7498.93798828125 / 4.5 / .5 / 0 |

Output = 1 and Boost = 0 dB in all rows. The ADAA2 4x witness has fundamental
**−12.520645 dBFS** and alias at **181.640625 Hz**. Ideal resampling gives
**−30.416883 dBc / −42.925384 dBFS-SE total**. The ADAA2 8x witness has
fundamental **−13.523160 dBFS** and alias at **349.91455078125 Hz**. Ideal
gives **−33.989778 dBc / −47.498541 dBFS-SE total**. These are substantial
absolute aliases, not merely poor ratios against a vanishing fundamental.

The identified frequencies agree with folds of the 29th harmonic about
192 kHz and the 47th about 352.8 kHz respectively. Tone passes those low
frequencies more strongly than the input fundamental. The first-stage knee
is crossed in about **0.2816 µs** in the 4x witness and **0.1104 µs** in the
8x witness—only **0.0541** and **0.0390** internal samples. The unchanged
threshold span and gains preserve this narrow temporal transition despite
its improved continuity. This supports the measured failure; it is not an
independent claim that all conceivable implementations of R2 are impossible.

ADAA2 highest dBc by sample rate:

| Fs | 4x FIR | 4x ideal | 8x FIR | 8x ideal |
| ---: | ---: | ---: | ---: | ---: |
| 44100 | -33.161037 | -33.161103 | -33.989853 | -33.989778 |
| 48000 | -30.416951 | -30.416883 | -34.109464 | -34.109314 |
| 88200 | -32.410800 | -32.410672 | -41.077836 | -41.077761 |
| 96000 | -36.317811 | -36.317734 | -46.217069 | -46.217054 |
| 176400 | -39.598417 | -39.598389 | -58.527199 | -58.527271 |
| 192000 | -41.855630 | -41.855609 | -60.323524 | -60.323365 |

Use [results.md](results.md) for directly generated tables with all five
methods, exact per-rate/control witnesses, absolute levels, musical subset,
failure counts and CPU/latency. Its numbers are generated from the retained
data; [summary.json](summary.json) includes each group's worst three cases.

## Previous borderline and resampler distinction

The nominal **5 kHz / 0.5 V / Gain 1 / Output 1** case remains rate/Tone
dependent. Selected R2 measurements with the existing FIR:

| Fs / actual input Hz | Tone | Method | Alias dBc | Alias dBFS | Total alias dBFS-SE |
| --- | ---: | --- | ---: | ---: | ---: |
| 44100 / 5001.08642578125 | 0 | ADAA2 4x | −54.790195 | −64.826861 | −64.475442 |
| 44100 / 5001.08642578125 | 1 | ADAA2 4x | −63.224387 | −54.615948 | −50.897986 |
| 44100 / 5001.08642578125 | 0 | ADAA2 8x | −67.416085 | −77.415614 | −77.412006 |
| 44100 / 5001.08642578125 | 1 | ADAA2 8x | −62.347908 | −53.726837 | −53.724041 |
| 48000 / 4998.046875 | 0 | ADAA2 4x | −66.736442 | −76.759559 | −74.305272 |
| 48000 / 4998.046875 | 1 | ADAA2 4x | −68.524747 | −59.911630 | −56.941605 |
| 48000 / 4998.046875 | 0 | ADAA2 8x | −100.982639 | −110.975108 | −107.624797 |
| 48000 / 4998.046875 | 1 | ADAA2 8x | −97.267204 | −88.644095 | −87.610045 |

At 44.1 kHz, 8x ideal resampling passes this exact 5 kHz case at both Tone
extremes: dark **−100.206907 dBc / −108.373962 total**, bright
**−95.522350 dBc / −85.646398 total**. This preserves the separate FIR
transition-band problem diagnosed in M1b. The unchanged first 65-tap bank
has its transition between 17.64 and 26.46 kHz at 44.1 kHz host rate, allowing
some above-Nyquist harmonics to fold into the protected band.

At **4871.88720703125 Hz / 0.5 V / Gain 1 / Tone 1 / 44100 Hz**, R2 ADAA2
8x gives **−50.671785 dBc**, alias **−41.987737 dBFS** at
**19740.56396484375 Hz**, total **−41.987589 dBFS-SE**. Ideal gives
**−96.763074 dBc / −86.697666 total**, a pass for this case alone.

A separate plausible case still fails ideal resampling:
**7248.046875 Hz / 0.5 V / Gain .6 / Tone 0 / Output 1 / 48000 Hz**.
R2 ADAA2 8x FIR has fundamental **−13.214405 dBFS**, alias at
**146.484375 Hz**, **−71.045583 dBFS / −57.831178 dBc**, total
**−71.034133 dBFS-SE**. Ideal gives **−57.831950 dBc / −71.035375 total**.
This rules out decision C for the product envelope. At 2 V the failures are
larger still. No FIR taps were redesigned or substituted.

## Reference and measurement validation

The established M1a continuous-time solver is imported into a separate module
instance with only its saturation callable replaced by R2. It locates knee
events, solves the periodic interstage state, integrates smooth intervals,
and applies the frozen analog Tone/output response. Across **76 selected
R2 cases**, doubling Gauss–Legendre order changes any in-band coefficient by
at most **1.735e-15 V**. This is numerical convergence evidence, not hardware
accuracy or a claim that every physical error is at that floor.

For six worst 8x cases, independent ideal-resampled ADAA2 128x/256x runs
approach that CT reference. Intended-harmonic residuals decrease by about
12 dB when the factor doubles; at 256x they are **−124.2 to −148.7 dBFS-SE**.
These factors establish reference convergence only, not realtime proposals.

The full sweep uses exact periodic solutions of the same feed-forward digital
filters and native nonlinear kernels. **315 native/periodic comparisons**
cover both profiles, all methods/rates, sine and multitone cases, plus ideal
projection checks. Maximum sample error is **2.918e-13 V**. Twelve worst
witnesses repeated at N=8192/32768 and three phases remain stable; maximum
phase/window dBc spread is **2.124e-7 dB**.

The alias metric excludes DC and intended *unfolded* integer harmonics in
20 Hz–20 kHz. It does not classify ordinary nonlinear harmonic distortion as
aliasing, nor call the entire CT residual an alias. Aliases coincident with
intended harmonics cannot be separated and make this a lower bound on alias
power. Reference harmonic errors are reported separately. Absolute alias
levels and aggregate energy prevent a dark Tone/fundamental ratio from being
the only evidence. See [reference checks](continuous-reference-checks.json),
[high-rate checks](highrate-reference-checks.json),
[periodic checks](periodic-checks.json) and [phase checks](window-phase-checks.json).

## CPU, latency, small signal and robustness

FIRs are the unchanged 65/33-tap cascade at 4x and 65/33/33 at 8x,
symmetric linear-phase half-band FIRs with even-phase decimation. Measured
round-trip FIR delays are **40** and **44 host samples**, at all six rates.
Two stages of ADAA1 add `1/F`, ADAA2 add `2/F`. Total delays are therefore:

| Method | Host samples | ms at 44.1 kHz | ms at 48 kHz | Typical / worst ns per host sample at 48 kHz |
| --- | ---: | ---: | ---: | ---: |
| Ordinary 4x | 40 | 0.907029478 | 0.833333333 | 95.820 / 95.275 |
| Ordinary 8x | 44 | 0.997732426 | 0.916666667 | 195.140 / 193.193 |
| ADAA1 4x | 40.25 | 0.912698413 | 0.838541667 | 206.634 / 221.436 |
| ADAA2 4x | 40.5 | 0.918367347 | 0.843750000 | 304.161 / 327.554 |
| ADAA2 8x | 44.25 | 1.003401361 | 0.921875000 | 605.081 / 627.656 |

CPU uses serial native C++ benchmarks on this arm64 Mac, with five warmed
repetitions per workload. At 48 kHz the worst measured 8x workload uses about
**3.01% of one core**; at 192 kHz typical 8x uses about **11.49%**. These
are estimates, not callback deadline guarantees. Ideal FFT resampling has no
finite causal realtime delay/CPU proposal. Its ADAA component alone adds the
same fractional delay; it does not justify deleting the real resampler.

State is unchanged from M1a: each offline antialias struct is 24 bytes (two
double histories, mode and padding); ADAA1 needs one history and ADAA2 two
per stage. No lookup tables or new states were added. Work per sample is
bounded by the four fixed knee boundaries. Native processing uses no
allocation, locks or I/O; whole versus seven-sample block renders are
bit-exact across 96 offline configurations. Production handoff/smoothing and
section-transition code was untouched; M1c does not requalify live integration.

The R2 small-signal response is unchanged from V1 for each method.
At 44.1 kHz maximum 80 Hz–8 kHz magnitude errors are **0.531934239 dB**
for ADAA2 4x and **0.132171705 dB** for 8x. Thus 4x also misses the existing
0.5 dB target, while 8x exceeds the 1 ms latency target by about 0.0034 ms
at that rate. No host integer-latency or live selection policy is implemented.
[All six rates are tabulated](results.md) and [raw timings retained](performance.json).

TORTURE retains ±10 V and near-Nyquist tones separately. The historical
grid supplies **6,840 out-of-product evaluations** across the methods and
resamplers; in-domain rows remain PRODUCT. Gross-artifact flags remain
visible in [results.md](results.md), without applying the product fidelity
gate outside its scope. In **60 native 30-second stress runs**, all observed
outputs and core states stayed finite. Maximum output was **3.266564185 V**,
maximum observed core state magnitude **155.428637894** (mixed V and
normalized-history units), and recovery after one second of silence was at
most **1.194e-29 V**. Stable output does not imply acceptable torture aliasing.

## Reproduction, scope and review

Use [reproduce.sh](reproduce.sh) with the existing Python environment or the
versions in [requirements.txt](requirements.txt). The full datasets, detailed
`level-sweeps.json.gz`, paired renders and generated `files.json` inventory are
local outputs excluded from Git. Reports, summaries, spectra and plots remain.
See the [retention inventory and reproduction order](../../repository-hygiene.md);
regenerate M1b datasets in an isolated archive export before running M1c.
Strict warnings, optimized native checks,
ASan/UBSan, static analysis, independent quadrature/Decimal tests, reference
convergence, scalar endpoints, block partitions, stress/recovery and source
preservation checks passed; see [validation.json](validation.json).

All reproduction components were executed during this task; the complete
shell wrapper was syntax-checked, not rerun end-to-end after those successful
component runs. Plugin targets/full plugin regressions were not rerun for
this offline-only experiment. Prior M1 production build/test evidence remains
in the parent package.

The only polynomial-degree difference from the request's suggested quintic
is mathematically required minimality: the fifth-degree coefficient vanishes.
No other model or gate changed. R2 remains an archived, unqualified offline
candidate. The next action is design review of the broader provisional
Overdrive/antialias approach; no production implementation step or M2 is
authorized by this result.
