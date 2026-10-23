# MIDI / Host Automation M0 — Compatibility Foundation

M0 adds isolated contracts, a legacy-reader prototype, fixtures and tests.
Production parameter registration, state writing/reading, DSP, presets, UI,
latency, MIDI flags and plugin identities remain byte-for-byte unchanged from
accepted main `eaa21571a442bec3bde73b7b917ea272e607376d`. No M1–M4 control
integration, coordinator, MIDI transport or MIDI Learn is implemented.

## Frozen parameter contract

[registry-contract.md](registry-contract.md) lists the exact original 13
IParams, including index, name, type, physical range/unit, default, normalized
default, display step and enum labels. Slots 0–12 remain host-visible and retain
their existing automation flags. Display precision is not added quantization.

`control/ParameterRegistry.h` describes all 46 proposed slots without creating
an IParam. Explicit high-range semantic IDs are separate from host indices and
avoid VST3 special IDs. Compile-time and runtime tests guard both namespaces.
Slots 13–45 follow the reviewed selector/TC/MC/AH/EQ/delay/tuner allocation.
TC remains a position control; AH Volume retains its cubic position law. EQ
uses the accepted canonical converters directly, preserving exact neutral
defaults, remembered cutoffs and logarithmic frequency/Q coordinates.

The native proof checks the actual accepted APP/VST3/AU IParams rather than
trusting a duplicate initializer. It freezes count 13 for this M0 milestone,
names, types, units, limits, bit-exact defaults, steps, flags and enum text.
It compares 1,025 normalized points and their inverse for every parameter in
each format: 39,975 points, maximum measured conversion error **0**.

## State fixtures and boundary proof

[state-fixtures.md](state-fixtures.md) documents the little-endian length/string
and physical-double wire layout, every historical migration, malformed-input
policy and evidence limitations. The independent C++ decoder has no writer,
asset loader, parameter setter or production connection. It belongs off audio.

The fixture set contains 24 payloads:

- 14 independently reconstructed payloads: headerless legacy input, versioned
  0.7.9/10/11/12/13/14 and 0.8.0 schemas, OutNorm Off, current empty paths,
  VST3 bypass 0/1 and an opaque trailing suffix, plus synthetic AU ClassInfo.
- 10 current native payloads captured from accepted APP objects, real VST3
  `IComponent::getState` and real AU ClassInfo APIs: default/custom APP and AU
  inner state, default/custom VST3 with both bypass values, and two AU XML
  property lists. Producer binary hashes match the committed accepted EQ
  integration record. AU XML keys are canonicalized; its inner data is unchanged.

The manifests distinguish reconstruction from native capture. No historical
REAPER project capture is claimed. Paths are controlled nonexistent test paths;
fixtures contain no user model/IR assets or workstation-dependent file contents.

All 13 positive binary reconstruction cases are compared against the actual
production legacy reader in each native format, including physical values,
paths and consumed offsets. The prototype returns the plugin boundary, not the
enclosing input length. Native default/custom boundaries are **147/237 bytes**;
the independent custom reconstruction has a **206-byte** boundary. VST3's next
four bytes remain the framework bypass word. Both actual VST3 framework state
restores are checked through its controller bypass value. The prototype is not
installed as the production framework's reader; equality with the independent
production-reader boundary establishes the compatibility proof at M0.

Both native AU ClassInfo states round-trip through the existing component.
Changing only the test dictionary type to `aumf` is rejected by the `aufx`
component, confirming the identity hazard without changing any shipped identity.

The 19-test codec suite includes 1,506 truncated prefixes, all 39 parameter
NaN/Inf cases, caller offsets, integer/length/version limits, signed zero,
opaque non-UTF8 path bytes and transactional failures. Historical OutNorm 1
remains numeric OutputMode 1 (Normalized), despite the old comment's wording;
old missing Slim becomes 1, while a new current instance defaults to 0.

The joint codec/registry suite passes 15 tests (984 checks) across all 13 binary
reconstructions. It builds a test-only 46-slot snapshot, applies the legacy
physical constraints, and proves that all 33 appended values keep their exact
defaults, including neutral EQ. No runtime state-application API is introduced.

## Resolved and remaining migration work

M0 resolves the old field order, defaults, normalization, migration behavior and
exact plugin/framework boundary for the supported fixtures. It does not replace
the production writer or add a state extension. Newer schemas fail closed in
the prototype; this deliberately differs from the current reader's assumption
that every version >=0.7.14 has 13 values. Other deliberate differences are
transactional rejection of malformed lengths/version syntax/NULs/nonfinite
values and a caller-visible 1 MiB string resource bound.

Before M1 exposure, implement a separately versioned, length-delimited extension
and a reader that consumes it exactly before VST3 bypass. Keep old readers and
all original 13 meanings. Do not expand `SerializeParams` alone. Downgrade
loading of new state in an old plugin is not established. Real existing REAPER
project reopening remains a later host acceptance check.

The reviewed allocation is contiguous. Exposing EQ at 27–41 cannot leave
13–26 uninitialized: initialize and route or explicitly gate every preceding
registered slot. The descriptor array is not authorization to publish blank
host placeholders or unsafe TC/Slim handlers.

## Ownership, formats and safety gates

[control-ownership.md](control-ownership.md) specifies one proposed audio-side
coordinator, exactly one publisher per existing DSP mailbox, value-carrying
ingress, state transaction epochs, source conflicts and feedback generations.
No coordinator is implemented. Two isolated counterexamples, each across 128
controlled schedules, demonstrate why atomic scalar storage cannot recover an
overwritten callback payload or make a multi-writer tuple update coherent.

[format-compatibility.md](format-compatibility.md) records actual APP entry
points, VST3 IDs/CC queue behavior and AU dispatch/type restrictions. APP MIDI
flags stay Off. The accepted AU remains `aufx / 1YEo / SDAa`; a separate `aumf`
companion is a future option, not a new M0 target. VST3 retains its identity,
ordinary parameter IDs and framework bypass ID.

TC BLD fitting/reset behavior and Slim structural-model changes remain safety
exceptions. M0 does not enable either from a new source. Strict source-priority
claims require captured payloads, not a later IParam reread; AU interception
needs proof. Strict APP MIDI drain bounds/observable overflow and VST3 retained
CC edges remain future transport work. No vendored files were modified.

M1 can begin with the versioned codec and narrowly scoped, ready-control
adapters. Broad exposure is not yet safe: state-extension integration, ingress
proof, transition checks and TC/Slim restrictions must remain explicit gates.

## Validation and reproduction

`validation.json` and `validation-logs.tar.gz` retain exact results, commands,
source/product fingerprints and resolved harness/environment failures. The
source audit checks all 202 tracked production files, unchanged vendor worktrees,
all fixture hashes and zero production inclusion of the new contracts.

`validate.py` builds Debug/Release/ASan-UBSan standalone contracts with strict
warnings, runs the controlled ownership tests under TSan, runs Clang analysis,
then links the APP proof and loads actual VST3/AU bundles. Native assertions stay
enabled despite imported Release flags. Native compiler logs retain existing
vendor deprecation warnings and warnings from unused renamed legacy test entry
points; strict new standalone source checks are clean.

Final Debug, Release and ASan/UBSan runs each pass 195,388 registry checks,
19 codec tests, 15 joint migration tests, and the two ownership counterexamples
across 128 schedules each. The ownership TSan run passes; Clang analysis of all
five isolated translation units reports no diagnostics. All native format
checks pass. Leak detection was disabled for ASan; this is not a leak audit.

The native harness reuses accepted EQ integration products by default. If they
are absent, build isolated products from the unchanged accepted production tree:

```sh
EQ_BUILD=/private/tmp/nam-midi-m0-products bash HoldsworthEngine/references/post-amp-eq-v1/package-products.sh
M0_PRODUCT_BUILD=/private/tmp/nam-midi-m0-products python3 HoldsworthEngine/references/midi-automation-v1/m0/validate.py
python3 HoldsworthEngine/references/midi-automation-v1/m0/audit-foundation.py
```

Without an override, product root is `/private/tmp/nam-eq-git-integration`;
test output defaults to `/private/tmp/nam-midi-automation-m0` (`M0_BUILD` can
override it). AU tests need registrar access but do not install a component or
clear user caches. `--native-only --capture-native` writes candidate captures
only to the temporary output; normal validation compares frozen fixtures and
never rewrites them. Fixture replacement is a separate intentional review step.

The existing Release regression executable was also rerun: 329 tests passed.
Native paired rendering remains bit-exact and reports 32 samples with no NAM
model loaded. Source identity proves the existing model-dependent latency policy
and all DSP/preset arithmetic remain unchanged. M0 does not constitute new
musical, REAPER UI or complete host-automation acceptance.

The exact new-file inventory is in [files-added.txt](files-added.txt). All M0
work remains uncommitted; no branch was changed and nothing was pushed.
