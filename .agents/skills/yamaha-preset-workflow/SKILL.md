---
name: yamaha-preset-workflow
description: >
    Add, transcribe, validate, or modify Yamaha UD-Stomp or Magicstomp presets
    and diagnostic configurations in HoldsworthEngine. Use for Yamaha factory
    presets, manual exercises, Holdsworth Yamaha presets, patch-list values, or
    DSP audition configurations derived from Yamaha source settings.
---

# Yamaha Preset Workflow

Use this workflow for Yamaha preset, source-data, and diagnostic tasks.

Follow `AGENTS.md` for repository-wide architecture, realtime, validation,
visual-validation, and Git rules.

## References

Use bundled references only when relevant.

- `references/official-sources.md`
    - official Yamaha source locations;
    - source priority;
    - factory state versus manual-exercise state.

- `references/ud-stomp-parameters.md`
    - established project interpretations of Yamaha parameters and features.

Do not load reference files merely because they exist.

Bundled references do not override official Yamaha documentation.

When exact preset values, preset identity, or disputed Yamaha behavior matter,
verify the relevant official Yamaha source.

If an official source conflicts with a bundled reference, stop and report the
discrepancy.

## Workflow

### 1. Verify the Yamaha source

Before modifying code, establish exactly which preset, patch, or manual exercise
the task refers to.

Verify as applicable:

- preset/group/bank/patch identity;
- name and documented author;
- enabled/disabled bands;
- relevant per-band values;
- relevant global values;
- manual-instructed temporary changes.

Do not assume user-supplied source values are correct.

If the requested data conflicts with official Yamaha documentation, do not
silently correct or substitute it. Report the discrepancy first.

### 2. Preserve source metadata exactly

Treat Yamaha values as source-domain data.

Preserve documented values using existing source types.

Keep these distinct:

- explicit OFF;
- zero;
- unknown;
- absent/not documented;
- not applicable.

Do not invent Yamaha metadata to fill gaps required only by the DSP diagnostic.

### 3. Separate source, exercise, and DSP state

Keep distinct:

1. stored Yamaha factory/source state;
2. temporary state instructed by a Yamaha manual exercise;
3. HoldsworthEngine DSP diagnostic configuration.

A diagnostic change must not rewrite the stored Yamaha source metadata.

### 4. Do not invent physical mappings

Keep Yamaha control values separate from physical DSP values unless approved
measurement establishes a mapping.

In particular, do not assume generic mappings for:

- SPEED -> Hz;
- DEPTH -> milliseconds;
- FEEDBACK -> coefficient;
- LEVEL -> gain;
- PAN law;
- EFFECT LEVEL;
- Direct Level / Direct Pan.

Use explicit provisional diagnostic DSP values where necessary and label them
as provisional/unmeasured.

### 5. Reuse existing DSP architecture

Inspect the existing implementation before creating preset-specific behavior.

When relevant, consult `references/ud-stomp-parameters.md`.

Keep these domains separate:

    SYNC    -> modulation timing
    CONNECT -> audio routing
    GROUP   -> delay resource/control grouping

If the requested Yamaha behavior is not implemented yet, do not fake it using
another feature.

Stop and propose the missing architecture separately.

### 6. Preserve existing reference behavior

Do not change established presets or diagnostics as a side effect of adding a
new one.

Preserve existing regression and bit-exact guarantees required by `AGENTS.md`
and the test suite.

Do not weaken old tests to make new work pass.

### 7. Add focused tests

For a new preset or diagnostic, test the relevant subset of:

- exact source identity;
- exact source metadata;
- source-versus-diagnostic separation;
- exact DSP configuration;
- enabled/disabled bands;
- routing or SYNC relationships;
- required delay capacity;
- configuration round-trip;
- source/DSP type separation;
- allocation-free configuration;
- relevant behavioral timing/rendering;
- established preset regressions.

Reuse existing engine tests rather than duplicating them unnecessarily.

### 8. Keep live audition wiring separate

Adding a preset definition does not automatically mean exposing it in the
temporary HoldsworthEngine Dev selector.

Only change the live selector when auditioning is part of the task.

Keep diagnostic UI temporary and nonserialized unless the project explicitly
moves that behavior into production parameters.

### 9. Validate proportionally

Use the validation tier defined in `AGENTS.md`.

Preset/source/diagnostic changes should normally use focused tests plus the
appropriate Debug suite and relevant standalone build.

Realtime DSP architecture changes require the full DSP validation tier.

Do not automatically run expensive unrelated validation.

## Completion report

Report concisely:

- Yamaha source identity verified;
- official source used when required;
- bundled references used when relevant;
- files changed;
- source metadata added or changed;
- DSP configuration added or changed;
- provisional/unmeasured assumptions;
- tests and validation performed;
- remaining hardware-verification questions;
- whether live UI or DSP architecture changed.

Do not create a Git commit unless explicitly requested.
