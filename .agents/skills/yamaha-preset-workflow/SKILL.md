---
name: yamaha-preset-workflow
description: >
    Add, transcribe, validate, or modify Yamaha UD-Stomp or Magicstomp presets
    and diagnostic configurations in HoldsworthEngine. Use when a task involves
    implementing a documented Yamaha preset, Allan Holdsworth Yamaha preset,
    Yamaha patch-list values, or creating a DSP audition configuration from
    Yamaha source settings.
---

# Yamaha Preset Workflow

Use this workflow when adding, validating, transcribing, or modifying a
documented Yamaha UD-Stomp / Magicstomp preset or a diagnostic configuration
derived from Yamaha source data.

Follow the repository-wide rules in `AGENTS.md` in addition to this workflow.

## 1. Verify the source identity first

Before modifying code, verify the requested preset against official Yamaha
documentation when an official source is available.

Confirm at least:

- preset number;
- preset name;
- documented author when present;
- enabled/disabled bands;
- all relevant per-band values;
- global values.

Do not assume values supplied in the user prompt are correct.

If the prompt conflicts with the official Yamaha source:

1. do not implement the conflicting data;
2. report the discrepancy;
3. show the verified source values;
4. wait for clarification when the intended preset is ambiguous.

Never silently "fix" the user's requested preset into a different preset.

## 2. Preserve Yamaha source metadata exactly

Treat documented Yamaha values as source-domain data.

Preserve documented values exactly, including concepts such as:

- Delay Time;
- Feedback;
- TAP;
- SPEED;
- DEPTH;
- WAVE;
- PHASE;
- SYNC;
- CONNECT;
- GROUP;
- Low Cut;
- High Cut;
- Pan;
- Level;
- Effect Level;
- Direct Level;
- Direct Pan;
- band ON/OFF state.

Use the project's existing strongly typed Yamaha source representations.

If the source prints a dash or does not document a field, leave that field
unknown/absent rather than inventing a value.

Distinguish explicitly documented OFF from unknown/not documented.

## 3. Keep Yamaha controls separate from DSP values

Never treat Yamaha control-domain values as physical DSP values unless an
existing measured calibration explicitly establishes that relationship.

Examples:

- Yamaha SPEED is not `ModulationRateHz`.
- Yamaha DEPTH is not `ModulationDepthMs`.
- Yamaha FEEDBACK is not automatically a feedback coefficient.
- Yamaha LEVEL is not automatically linear gain.
- Yamaha band numbers are not container indices.

Do not introduce general conversion functions such as:

    yamahaSpeedToHz(...)
    yamahaDepthToMs(...)
    yamahaFeedbackToCoefficient(...)
    yamahaLevelToGain(...)

unless the repository already contains an approved measured calibration for
that control.

Until Magicstomp calibration exists, physical DSP settings derived from Yamaha
controls must be explicitly described as provisional/unmeasured audition data.

## 4. Reuse existing DSP architecture

Before implementing a preset, determine which existing features it requires.

Check for:

- delay;
- feedback;
- modulation;
- modulation waveform;
- TAP;
- loop filters;
- delay-signal polarity;
- SYNC;
- CONNECT;
- GROUP.

Use the existing implementation rather than duplicating DSP behavior inside a
preset.

If the preset requires Yamaha behavior that HoldsworthEngine does not yet
implement, do not fake it with unrelated existing controls.

Stop and report:

1. the missing behavior;
2. what the Yamaha source documents;
3. the smallest DSP architecture required.

Design that DSP feature separately before implementing the preset.

## 5. Construct provisional DSP configuration deliberately

When physical mappings are unmeasured:

- reuse already established provisional literals when the same Yamaha source
  value appears;
- preserve obvious ordering relationships where appropriate;
- do not create an undocumented generic conversion curve merely to generate
  preset data;
- label new assumptions explicitly as provisional.

Do not retune existing established reference presets merely to make a new
preset internally consistent.

Calculate and document required maximum delay capacity from the physical DSP
configuration.

## 6. Preserve reference behavior

Unless the task explicitly requests otherwise, existing established reference
presets must remain unchanged.

At minimum preserve the regression expectations defined in `AGENTS.md`.

Do not alter existing preset DSP values as a side effect of adding a new preset.

## 7. Keep source identity and diagnostic identity separate

A diagnostic/audition variant is not automatically a Yamaha factory preset.

If creating a diagnostic variant:

- preserve the actual Yamaha factory source metadata separately;
- clearly mark the new configuration as diagnostic/provisional;
- do not rewrite factory metadata to match the diagnostic experiment.

For example, if a Yamaha manual instructs temporarily changing a factory
parameter for an audition, retain the factory value in the source record and
describe the temporary change as diagnostic metadata/configuration.

## 8. Add focused tests

For a new preset or diagnostic configuration, test as applicable:

- exact source identity;
- exact Yamaha source metadata;
- exact physical DSP configuration;
- enabled/disabled bands;
- required delay capacity;
- engine configuration round-trip;
- source-type / DSP-type separation;
- relevant routing relationships;
- deterministic rendering for the feature being exercised;
- processing/configuration allocation behavior;
- established reference-preset regressions.

Do not weaken existing regression tests to make the new preset pass.

## 9. Choose validation proportional to the change

Follow the validation tiers in `AGENTS.md`.

Do not automatically run the most expensive DSP/plugin validation matrix for a
metadata-only or temporary audition-UI change.

If implementing or changing realtime DSP architecture, use the full DSP
validation tier.

## 10. Do not expose presets live automatically

Adding a preset definition does not imply adding it to the temporary live
development selector.

Unless live audition is part of the task:

- implement the preset;
- test it;
- leave the live selector unchanged.

Live audition wiring should be a separate small step when useful.

## Completion report

Report concisely:

- verified Yamaha preset identity/source;
- files changed;
- DSP configuration added or changed;
- provisional/unmeasured assumptions;
- tests and validation performed;
- any Yamaha behavior still requiring hardware measurement;
- whether live UI was changed.

Do not create a Git commit unless explicitly requested.
