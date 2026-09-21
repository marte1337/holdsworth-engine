# HoldsworthEngine

A real-time guitar DSP project built in C++ around Neural Amp Modeler, inspired by the signal-processing approaches associated with Allan Holdsworth.

The project combines neural amplifier modeling with custom DSP for multi-delay, modulation, tone shaping and pedal-style processing. It is being developed as a standalone application and audio plugin, with a strong focus on realtime safety, deterministic behavior and validation.

> **Status:** Active development / experimental engineering project.

## Highlights

- Custom **8-band stereo delay engine** with fractional delays, feedback, modulation, filtering, panning, synchronized modulation, serial routing and grouped delay behavior
- **Neural Amp Modeler** integration for amplifier modeling
- Cabinet IR processing and post-amp signal shaping
- Pre-NAM pedal processing including:
  - TC Electronic Booster + Line Driver inspired processing
  - MC402-style clean boost
  - J. Rockett AH-style Boost and behavioral Drive
- Standalone, **VST3** and **Audio Unit** builds
- Realtime-safe parameter transitions and control/audio thread handoff
- Automated DSP, concurrency and latency testing

## Research-driven DSP

A major part of the project is determining what can reasonably be recreated before implementing it.

Research includes manuals, interviews, published equipment information, available circuit evidence and controlled listening comparisons.

Where the original behavior is sufficiently documented, the implementation follows that evidence. Where it is not, I prefer to build an explicitly documented **behavioral model** rather than claim circuit accuracy.

For example, the J. Rockett AH Drive combines the documented controls and Boost → Drive routing of the original pedal with a custom software Drive model:

```text
Bass shaping
    ↓
4× oversampled nonlinear Drive
    ↓
Treble shaping
    ↓
Volume
```

The nonlinear stage is validated against higher-rate offline references before being integrated into the realtime processor.

This research process has also led to features being deliberately dropped when the available evidence or technical results did not justify the complexity.

## Realtime engineering

Audio DSP introduces constraints that differ significantly from typical application development. The project therefore puts particular emphasis on:

- no allocation or locks in realtime processing
- deterministic block processing
- safe concurrent parameter updates
- smooth transitions between DSP states
- very small host callback sizes
- multiple sample rates
- latency and plugin-bypass handling

One recent example involved extending iPlug2's latency handling after dynamic 0/32-sample pedal latency exposed unsafe buffer mutation during rendering. The resulting solution uses preallocated delay storage, atomic state handoff and format-specific AU/VST3 lifecycle handling.

## Validation

DSP features are developed through a staged workflow:

```text
Research
→ Offline prototype
→ Numerical validation
→ Isolated realtime DSP
→ Integration
→ Regression tests
→ Listening evaluation
```

The test suite covers areas including delay behavior, modulation, nonlinear processing, oversampling, block-partition determinism, concurrent control changes, allocation/lock detection, framework bypass and latency transitions.

Validation currently covers:

```text
44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz
```

with callback sizes down to a single sample, using Debug, Release, AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer builds.

## Technologies

- C++17
- realtime audio DSP
- Neural Amp Modeler
- iPlug2
- VST3 / Audio Unit
- FIR / IIR filtering
- fractional delay lines
- nonlinear waveshaping and oversampling
- Python / NumPy / SciPy for offline DSP analysis

## Why I built it

My professional background is primarily in web development, and this project is an opportunity to apply software-engineering principles in a very different domain.

It has given me practical experience with modern C++, realtime programming, concurrency, numerical validation, native build systems and translating incomplete research into explicit technical requirements.

The project is not affiliated with Allan Holdsworth's estate or the manufacturers referenced during research.
