# HoldsworthEngine — Codex Project Guidance

## Project goal

HoldsworthEngine extends the official NeuralAmpModelerPlugin with DSP for an
Allan Holdsworth-inspired guitar signal chain. Current work includes a
documentary-nominal TC BLD Clean Boost or MC402 Clean Boost before NAM and an
algorithmic stereo multi-delay inspired by Holdsworth's Yamaha UD-Stomp /
Magicstomp usage after NAM.

NAM remains responsible for nonlinear amp/preamp modeling.
The TC BLD / MC402 Clean Boost processors and Yamaha-style multi-delay are
implemented algorithmically in C++.

Physical TC BLD and Magicstomp calibration is deferred.

## Repository / ownership boundaries

Custom code lives primarily under:

    HoldsworthEngine/

Do not modify vendored code or Git submodules unless explicitly requested.

The live NAM integration is intentionally small and currently exists only to
audition HoldsworthEngine DSP.

## Current live signal path

Conceptually:

    host input
    -> selected pre-NAM processor: Off / TC BLD / MC402 Clean Boost
    -> NAM
    -> tone stack
    -> cabinet / IR
    -> DC blocker
    -> HoldsworthDelayEngine
    -> dry + stereo wet
    -> host output

The selector processes mono input through at most one pedal before NAM.
The existing input trim and volts/calibration bridge apply to either pedal.
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
- inter-band CONNECT audio routing
- contiguous-band GROUP shared delay circuits
- transactional validation of SYNC, CONNECT, GROUP and their composition
- realtime TC BLD Clean Boost with Gain, Bass and Treble controls
- realtime MC402 Clean Boost V1 with a 0 to +20 dB Boost control and zero latency

Important components include:

    FractionalDelayLine
    DelayModulator
    DelayLoopFilter
    DelayBand
    HoldsworthDelayEngine
    ModulationSync
    AudioRouting
    DelayGrouping
    GroupedDelayCircuit
    TCBLDCleanBoostProcessor
    MC402CleanBoostProcessor
    DevelopmentPreNAMSelector

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

### CONNECT

CONNECT is engine-level mono audio routing. Each band receives either direct
engine input or one upstream band's routed output. Chains in either numerical
direction and source fan-out are supported; self references, invalid references
and cycles are rejected transactionally.

The current routed output is the band's direct input plus its signed, audible
delayed signal scaled by per-band output level. It is pre-pan and excludes the
engine global wet gain. A disabled band bypasses its input for routing while its
local delay state continues to advance. Each band's stereo wet contribution is
still summed exactly once.

This send composition is a provisional Yamaha CONNECT interpretation pending
hardware measurement.

### GROUP

GROUP replaces an ascending, contiguous, non-singleton band range with one
shared delay history, feedback recurrence and loop filter. Ranges must be
disjoint and fit both documented limits where available and the separately
prepared physical capacity.

The group head supplies input, base delay, feedback and loop-filter settings.
Member band identities retain per-output enable, TAP observation, modulation
waveform/depth/clock, polarity, level and pan. Feedback remains at the
unmodulated group base delay; the current modulation/TAP observation timing is
provisional pending hardware measurement.

CONNECT may compose with GROUP, but a grouped non-head cannot be a CONNECT
destination. Invalid composed topology and collapsed cycles are rejected
transactionally. A changed GROUP topology clears affected delay/filter
histories while preserving modulation clocks; an unchanged topology preserves
history.

### TC BLD Clean Boost

`TCBLDCleanBoostProcessor` is a realtime engaged-Clean-Boost reduction of the
frozen M1 documentary-nominal circuit. It uses a precomputed stable 24-state
recurrence and block-boundary coefficient handoff for Gain, Bass and Treble.

It is not hardware calibrated and does not implement Distortion, dynamic Noise
Suppressor behavior, bypass electronics, component tolerances, clipping or slew
behavior.

### MC402 Clean Boost

`MC402CleanBoostProcessor` implements `MC402-CLEAN-BOOST-V1`: flat scalar gain,
0 to +20 dB (default 0 dB), with 10 ms gain smoothing and zero added latency.
It uses the existing pre-NAM calibration convention without modeled clipping,
EQ, FIR or oversampling. The provisional Overdrive profiles were rejected and
archived under `HoldsworthEngine/references/pedals/mc402/rejected-overdrive`;
they are not enrolled in production targets. Overdrive remains deferred pending
stronger circuit evidence or hardware measurement.

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
    holdsworth111ProvisionalV1()
    holdsworth122ProvisionalV1()
    holdsworth223ProvisionalV1()
    holdsworth231ProvisionalV1()

Lead 121, Chorus 011 and Chorus 031 are established bit-exact reference renders
and must remain unchanged unless a task explicitly changes them. The additional
Holdsworth preset definitions are also covered by focused source/DSP and render
regressions.

Yamaha 922 diagnostic configurations also exist:

    sync922IndependentDiagnosticV1()
    sync922BaselineProvisionalV1()
    sync922Band1ReverseDiagnosticV1()
    sync922HalfCycleDiagnosticV1()

The 922 configurations are diagnostic/reference configurations rather than
Holdsworth presets.

CONNECT and GROUP diagnostic definitions also exist:

    connect913ParallelDiagnosticV1()
    connect913SerialDiagnosticV1()
    group12Rhythm1200DiagnosticV1()
    group12Rhythm900DiagnosticV1()

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

## Implemented and deferred status

Implemented:

- modulation SYNC
- CONNECT routing, including chains and fan-out
- GROUP shared-history processing and CONNECT/GROUP composition
- provisional physical-DSP presets and diagnostics
- realtime TC BLD Clean Boost reduction
- MC402 Clean Boost V1 and the exclusive Off / TC BLD / MC402 development selector

Deferred or intentionally incomplete:

- calibrated Yamaha control-to-DSP mappings
- physical Magicstomp measurement and calibration
- hardware-calibrated TC BLD behavior
- the TC BLD features explicitly excluded from the Clean Boost reduction
- production parameter serialization and a permanent product UI

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

The macOS development build uses the dedicated Development UI v2. It presents
separate Boost / Drive and Delay cards beside the existing NAM control module in
an enlarged, resizable editor.

The Boost / Drive card selects Off, TC BLD or MC402. TC BLD exposes its existing
Gain, Bass and Treble controls; MC402 exposes only Boost (0 to +20 dB). The Delay
card exposes bypass, a separate Wet control and the five live audition presets:
Lead 121, Holdsworth 122, Chorus 011, Chorus 031 and Holdsworth 223. Their visual
grouping does not change preset indices or DSP identity.

NAM Settings remains the existing calibration, model-info and about utility;
it is not the Holdsworth development-control container. Development controls
remain temporary and nonserialized. Keep them in the dedicated panel and avoid
turning audition-only controls into permanent product parameters implicitly.

### Visual UI validation on macOS

Do not attempt automated screen capture, Accessibility inspection, WindowServer
queries, UI scraping, or similar visual automation unless explicitly requested.

For temporary development UI work:

- build and launch the standalone;
- validate labels, mappings and control behavior from code/tests;
- report the expected visual layout.

The user will manually inspect the application and can provide screenshots when
visual verification is needed.
