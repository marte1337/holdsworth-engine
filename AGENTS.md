# HoldsworthEngine — Codex Project Guidance

## Project goal

HoldsworthEngine extends the official NeuralAmpModelerPlugin with an
algorithmic stereo multi-delay inspired by Allan Holdsworth's Yamaha
UD-Stomp / Magicstomp usage.

NAM remains responsible for nonlinear amp/preamp modeling.
The Yamaha-style multi-delay is implemented algorithmically in C++.

Physical Magicstomp calibration is planned later but is currently postponed.

## Repository / ownership boundaries

Custom code lives primarily under:

    HoldsworthEngine/

Do not modify vendored code or Git submodules unless explicitly requested.

The live NAM integration is intentionally small and currently exists only to
audition HoldsworthEngine DSP.

## Current live signal path

Conceptually:

    NAM
    -> tone stack
    -> cabinet / IR
    -> DC blocker
    -> HoldsworthDelayEngine
    -> dry + stereo wet
    -> host output

The Holdsworth delay receives the fully processed mono NAM signal and produces
stereo wet output.

Temporary development controls are nonserialized and nonpermanent.

## Implemented DSP architecture

The engine currently supports:

- 8 DelayBands
- FractionalDelayLine
- fractional/interpolated delay reads
- feedback
- per-band output level
- equal-power stereo pan
- per-band enable state
- delay-time modulation
- Sine waveform
- Triangle waveform
- Saw Up waveform
- Saw Down waveform
- TAP
- per-band Low Cut / High Cut loop filters
- delay-signal polarity: Normal / Reverse
- engine-level modulation SYNC

Important components include:

    FractionalDelayLine
    DelayModulator
    DelayLoopFilter
    DelayBand
    HoldsworthDelayEngine
    ModulationSync

## Important DSP semantics

### Feedback

Feedback uses the full loop delay.

Output level and pan remain outside the feedback recurrence.

### TAP

TAP changes the audible observation point but does not replace the full
feedback-loop delay.

For TAP below 100%, both loop and TAP reads occur before the single history
write.

### Loop filters

Low Cut and High Cut affect the delayed signal and feedback recurrence.

When both filters are OFF, the legacy processing path is preserved exactly.

### Modulation

The existing Sine implementation is a validated reference and must not be
numerically changed casually.

The authoritative oscillator phase is normalized cycles in [0,1).

Waveforms:

    Sine
    Triangle
    Saw Up
    Saw Down

Waveform phase conventions are provisional until hardware measurement.

### Yamaha PHASE

Do not confuse Yamaha PHASE NOR/REV with modulation oscillator phase.

DSP:

    DelaySignalPolarity::normal
    DelaySignalPolarity::reverse

Reverse affects only the audible delayed/wet signal.

It must not change:

- feedback sign
- delay memory
- loop-filter recurrence
- TAP timing
- modulation

### Modulation SYNC

SYNC is an engine-level inter-band oscillator relationship.

The root band's existing DelayModulator owns the authoritative clock.

A synchronized root advances exactly once per sample during the SYNC prepass.

Slaves derive phase from the root and never advance an independent oscillator
while synchronized.

A slave retains its own:

- waveform
- depth
- delay
- feedback
- TAP
- filters
- polarity
- pan
- level

SYNC v1 supports direct star/fan-out relationships only.

Chains, self references, cycles and invalid references are rejected
transactionally.

Graph configuration is applied with:

    applyConfiguration(...)
    applyModulationSyncConfiguration(...)

Rejected graph applications must leave the previous engine state untouched.

## Yamaha source data versus DSP data

This distinction is fundamental.

Documented Yamaha control values must remain strongly separated from physical
DSP configuration.

Examples:

    Yamaha SPEED != ModulationRateHz
    Yamaha DEPTH != ModulationDepthMs
    Yamaha FEEDBACK != DSP feedback coefficient
    Yamaha LEVEL != linear gain
    Yamaha TAP source metadata != TapFraction type
    Yamaha band numbers != DSP/container indices

Do not create generic Yamaha-control-to-DSP conversion functions until physical
Magicstomp calibration provides actual measurements.

Current physical values are fixed provisional audition data.

Clearly label them as provisional/unmeasured.

When transcribing Yamaha presets, verify values against official Yamaha source
documents when possible. Do not trust prompt transcriptions blindly. If source
data conflicts with the request, stop and report the mismatch before
implementation.

## Existing reference presets

Important existing presets include:

    lead121UnmodulatedProvisional()
    chorus011ProvisionalV1()
    chorus031ProvisionalV1()

These are established reference renders and must remain bit-for-bit unchanged
unless a task explicitly changes them.

Yamaha 922 diagnostic configurations also exist:

    sync922IndependentDiagnosticV1()
    sync922BaselineProvisionalV1()
    sync922HalfCycleDiagnosticV1()

The 922 configurations are diagnostic/reference configurations rather than
Holdsworth presets.

## Current audition observations

These are subjective references, not DSP specifications.

Chorus 011:

- pleasantly wide
- smooth
- clear / pleasantly smeared chords
- spatial character

Chorus 031:

- more immediate and denser than Chorus 011
- clear attacks/chords
- smooth
- no unpleasant early-TAP artifacts

922 SYNC:

- Sync OFF slightly asymmetric
- Sync 0 degrees centered/moderately wide
- Sync 180 degrees clearly widest and preferred
- 180 degrees pleasant/lush on chords
- no obvious DSP artifacts

Do not retune established provisional presets merely because another
configuration sounds different.

## Not implemented yet

Do not assume these exist:

- CONNECT routing
- GROUP behavior
- calibrated Yamaha control mappings
- Magicstomp measurement system

SYNC, CONNECT and GROUP are separate concepts:

    SYNC    -> oscillator timing relationships
    CONNECT -> audio routing relationships
    GROUP   -> delay/control grouping topology

Do not collapse them into one generic band graph without a strong reason.

## Realtime rules

Inside audio processing:

- no allocation
- no locks
- no file I/O
- no logging
- no exceptions
- no unnecessary per-sample transcendental calculations

Allocate scratch/state during prepare where possible.

Preserve deterministic block-partition behavior.

## Regression philosophy

When extending the engine, preserve existing legacy paths explicitly rather
than relying on mathematically equivalent rewrites when bit-exact behavior
matters.

Important existing regressions include:

- Lead 121 bit exact
- Chorus 011 bit exact
- Chorus 031 bit exact
- no-SYNC path bit exact

## Development workflow

For uncertain DSP architecture:

    inspect -> design -> review -> implement

Do not implement an ambiguous Yamaha behavior silently.

For isolated preset/UI wiring on settled architecture, implementation can be
more direct.

Do not create Git commits unless explicitly requested.

Keep changes small and report deviations from an approved design.

## Validation tiers

Do not automatically run the most expensive validation matrix for every tiny
development-UI change.

For DSP architecture changes:

- full Debug tests
- full Release tests
- ASan + UBSan
- strict warnings
- static analyzer
- APP/VST3/AU builds
- project lint
- git diff --check

For isolated preset/configuration changes:

- relevant focused tests
- full Debug suite
- Release suite when appropriate

For temporary development UI/audition wiring:

- relevant focused tests
- full Debug suite
- standalone APP build and launch

Run the complete validation matrix before major DSP milestones are merged or
when the task materially changes realtime DSP behavior.

## Development UI

The current Holdsworth development controls inside the NAM Settings overlay are
temporary.

The layout is already cramped and partially overlaps existing NAM Settings
content.

Do not keep expanding this area indefinitely.

Prefer removing obsolete audition-only choices or later creating a dedicated
HoldsworthEngine development panel rather than continually adding buttons.

### Visual UI validation on macOS

Do not attempt automated screen capture, Accessibility inspection, WindowServer
queries, UI scraping, or similar visual automation unless explicitly requested.

For temporary development UI work:

- build and launch the standalone;
- validate labels, mappings and control behavior from code/tests;
- report the expected visual layout.

The user will manually inspect the application and can provide screenshots when
visual verification is needed.
