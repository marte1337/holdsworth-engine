# TC BLD milestone M2: realtime CLEAN BOOST

M2 is a documentary-nominal, linear processor for the engaged CLEAN BOOST path
of the frozen M1 configuration. Its source of truth is M1 oracle
`TC-BLD-M1-OFFLINE-MNA-CLEAN-BOOST-V1`, profile
`TC-BLD-DOC-NOMINAL__S1501-3__B1501-06__L1501-6__AS-M0A-001`, and golden
results SHA-256
`d5a8fa92b43650d559538f976b04e4fdc30023812bbebb6c4f03292ec54c2e15`.

## Processor

`HoldsworthEngine/dsp/TCBLDCleanBoostProcessor.{h,cpp}` exposes explicit
`prepare`, `reset`, `setControls`, and `processBlock` operations. Coefficients
start by factoring the fixed 38-unknown M1 linear circuit at prepare time or
when a control changes. That reduction has 24 states derived from 21 unique
capacitor companion states and the three finite-4741 dominant-pole states.

The reviewed M1 equations have right-half-plane small-signal poles at some P1
positions. Direct causal discretization is therefore not a usable realtime
processor: its impulse response grows without bound. M2 uses a fixed-size real
Schur decomposition, reflects only out-of-unit-circle poles, applies the
corresponding unity-magnitude phase factors, and refits the real residues at
128 log-spaced/anchor points. The resulting 24-state spectral equivalent is
strictly stable and preserves M1 magnitude through the fit band. This is a
stability reduction of the named oracle, not rail clipping or manual-range
tuning.

The audio hot path does not solve MNA or run the fit. It only evaluates the
precomputed stable recurrence. Control-side coefficient builds are transferred
through a fixed four-slot, single-producer/single-consumer atomic mailbox; the
audio thread copies only the newest complete set at a block boundary. A
successful control change resets its modal state because the realization
coordinates changed.

The capacitors use the trapezoidal transform without frequency prewarping. The
processor adds no deliberate latency. `processBlock` supports exact in-place
operation and is invariant to block partitioning.

## Controls

All public control positions are inclusive normalized mechanical/electrical
positions in `[0, 1]`:

- Gain is M1 P1. Its 47 kOhm variable resistance is
  `47000 * pow(position, 3.321928094887362)` ohms. The default is the M1 full
  circuit's 1 kHz terminal-unity position `0.13243092421`; its reviewed
  non-monotonic terminal behavior is retained.
- Bass is M1 P2's selected linear electrical direction, default `0.5`.
- Treble is M1 P3's selected linear electrical direction, default `0.5`.

Those P2/P3 values are internal electrical/netlist coordinates. The temporary
development UI reverses both at its wrapper boundary: displayed Bass/Treble
`0` maps to electrical `1` (cut), and displayed `1` maps to electrical `0`
(boost). The standalone processor and M1 golden coordinates remain unchanged.

The P2/P3 resistive segments, shared nodes, capacitors, and following active
stage remain in one circuit reduction. They are not represented as independent
shelves. Exact zero-ohm pot endpoints use explicit branch-current unknowns, not
epsilon resistors.

## Golden comparison

The focused C++ tests transcribe 14 M1 curves: six Gain positions, Bass and
Treble endpoints, and all four combined endpoint corners. They compare
magnitude and wrapped phase at six representative points from 22.1 Hz through
17.7 kHz.

- At 192 kHz, the declared tolerance is 0.35 dB and 2.3 degrees. Measured
  worst cases are 0.317483 dB and 2.26725 degrees, both at the combined
  Bass=0/Treble=1 corner at 17.673 kHz.
- At the live 48 kHz rate, the audition-band criterion runs through 5.125 kHz
  with tolerances of 0.35 dB and 1.9 degrees. Measured worst cases are
  0.322209 dB and 1.77565 degrees.

Magnitude is checked for every representative Gain and tone curve. The phase
tolerances apply to the stable M1 Gain settings and all representative tone
settings. Pole reflection necessarily changes phase at M1's unstable P1
positions; the maximum sampled departure is about 179.9 degrees. Matching that
unstable phase with a causal stable zero-latency processor is not practical,
so the exception is explicit rather than hidden behind loose tolerances.

The increasing top-octave error at lower sample rates is the expected
trapezoidal frequency mapping, not manual-range retuning.

## Live audition bridge

The temporary development path is:

```text
input -> TC BLD CLEAN BOOST -> existing noise-gate trigger -> NAM
      -> existing post-NAM tone/cab/DC-block/delay/output chain
```

The development settings page exposes only BLD on/off, Gain, Bass, and Treble
for this processor. These controls use the existing non-serialized UI-to-DSP
development message convention.

Coefficient changes are intentionally unsmoothed in M2. Each accepted drag
update installs a new fitted realization and clears its coordinate-dependent
state at a block boundary, so a subtle discontinuity or crackle only while a
control is moving is expected. Sending only on mouse-up would trivially reduce
the number of discontinuities but would remove continuous audition feedback;
a proper dezipper/crossfade is outside this focused fix.

When calibrated NAM input metadata is active, the wrapper maps normalized host
samples to peak volts using the configured interface dBu calibration before
the BLD, then maps volts to the loaded NAM model's calibration afterward. With
no applicable calibration it preserves the existing input-gain convention.

M1's fixed 1 kOhm source fixture and 1 MOhm parallel 100 pF load are internal
parts of the named transfer and are retained there. M2 does not add another
pickup, cable, or interface impedance effect after the ADC. Consequently it
cannot reconstruct real pre-ADC pickup/cable/interface loading; that remains a
documented limitation.

## Deliberate exclusions

M2 does not implement Distortion mode, the dynamic Noise Suppressor, electronic
bypass/latch behavior, hardware calibration, vintage tolerances, nonlinear
rail clipping or slew, M3 behavior, or production UI artwork. It does not tune
the model to the manual's +/-16 dB claims or make Gain monotonic.
