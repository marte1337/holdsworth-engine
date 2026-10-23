# M0 parameter registry contract

`HoldsworthEngine/control/ParameterRegistry.h` is an isolated, header-only
compatibility contract. Nothing in the production plugin includes it. It does
not construct IParams, expose a host parameter, replace serialization, publish a
DSP target or change MIDI configuration. Its first 13 descriptors describe the
actual accepted plugin; descriptors 13–45 record the proposed append-only plan.

## Frozen legacy slots

The legacy host ID equals the numeric IParam index. All 13 currently have host
automation enabled. That existing flag is distinct from a claim of safe,
smoothed updates: the contract records both current exposure and future safety
readiness independently.

| Index | Exact IParam name | IParam type | Range | Default | Unit | Normalized default |
|---:|---|---|---|---:|---|---:|
| 0 | Input | Double (`InitGain`) | −20…20 | 0 | dB | .5 |
| 1 | Threshold | Double (`InitGain`) | −100…0 | −80 | dB | .2 |
| 2 | Bass | Double | 0…10 | 5 | — | .5 |
| 3 | Middle | Double | 0…10 | 5 | — | .5 |
| 4 | Treble | Double | 0…10 | 5 | — | .5 |
| 5 | Output | Double (`InitGain`) | −40…40 | 0 | dB | .5 |
| 6 | NoiseGateActive | Bool | 0…1 | 1 | — | 1 |
| 7 | ToneStack | Bool | 0…1 | 1 | — | 1 |
| 8 | IRToggle | Bool | 0…1 | 1 | — | 1 |
| 9 | CalibrateInput | Bool | 0…1 | 0 | — | 0 |
| 10 | InputCalibrationLevel | Double | −60…60 | 12 | dBu | .6 |
| 11 | OutputMode | Enum | 0…2 | 1 | — | .5 |
| 12 | Slim | Double | 0…1 | 0 | — | 0 |

All 13 use linear normalized coordinates. Finite continuous conversion is
`(value − minimum)/(maximum − minimum)`, with range clamping; inverse conversion
uses the corresponding affine expression. `InitGain` defines dB as the physical
value, not a logarithmic normalized shape. Continuous initialization steps of
`.1` (`.01` for Slim) are display/interaction steps: the existing IParams do not
set `kFlagStepped`. They must not acquire automation quantization accidentally.

Bool and Enum are stepped with step 1. Half-integer ties round upward for their
nonnegative ranges: Boolean `.5` is On; OutputMode normalized `.25` selects
Normalized and `.75` selects Calibrated. OutputMode values remain Raw=0,
Normalized=1, Calibrated=2. Boolean display labels remain `off`/`on`.

The contract's nonfinite-input fallback to the exact default is a proposed
future ingress policy. It does not describe, or modify, legacy IParam behavior
for malformed NaN/Infinity input. Existing finite conversion behavior is the
compatibility obligation.

Legacy serialized doubles and migrations are covered by the companion state
fixtures and codec tests. A semantic registry ID must never be substituted for
an existing framework parameter index in an old state or host project.

## Descriptor and identity conventions

Each `ParameterDescriptor` carries an explicit numeric semantic ID, host index,
readable semantic key, name, unit, type, mapping, physical limits/default, display
step, current host exposure/automation flags, application domain, update-safety
gate, stable enum labels and an optional accepted EQ parameter identifier.

- Numeric semantic IDs are explicit `uint32_t` constants, never hashes or UI
  tags. The high nibble identifies the domain: `0x10000000` legacy NAM,
  `0x20000000` pre-NAM pedals, `0x30000000` EQ, `0x40000000` delay and `0x50000000`
  tuner. Lower bits are explicitly assigned within each domain. Zero and unknown
  IDs have no implicit fallback target. This high range deliberately avoids both
  ordinary host indices and VST3's current bypass/preset/CC IDs `65536…67617`.
  Compile-time and runtime guards require host slots below `65536` and semantic
  IDs at or above `0x10000000`, independently checking the special-ID exclusion.
- Readable keys use lowercase ASCII `he.<domain>.<meaning>`, with units or
  `_position` where necessary. Keys are immutable aliases, not generated IDs.
- Retiring a parameter leaves its ID, key and host slot reserved. Do not compact,
  reorder or reuse them. Append future host slots after the existing maximum.
- Host parameter indices and semantic IDs serve different compatibility needs.
  Preserve both. Display names may be localized separately in the future; the
  existing 13 host names remain unchanged.
- Application domains describe required handoff work. They do not implement a
  coordinator or authorize a new caller to publish to an existing SPSC mailbox.
- `validatedExistingDSPPath` records an accepted processor's existing target and
  smoothing mechanism. It does not certify an unimplemented host/MIDI ingress,
  concurrent publisher arrangement or state transaction.

The tests freeze all 46 explicit IDs, keys and host allocations independently
of the descriptor declarations. They also freeze the original 13 names, types,
units, ranges, defaults, initialization steps and normalized defaults.

## Evaluation of proposed slots 13–45

The reviewed allocation fits without moving the original slots. None of these
33 slots is exposed in M0.

| Slot(s) | Proposed controls | Physical values/defaults | Application and safety |
|---|---|---|---|
| 13 | Pre-NAM Processor | Off=0, TC BLD=1, MC402=2, J. Rockett AH=3; default Off | Existing exclusive selector target/transition |
| 14–16 | TC BLD Gain/Bass/Treble | Shaft positions 0…1; `.132430924210101`, `.5`, `.5` | Coefficient preparation; safe update mechanism required |
| 17 | MC402 Boost | 0…20 dB; 0 | Existing 10 ms scalar ramp |
| 18–20 | AH Boost/Type/Emphasis | 0…20 dB, F=0/C=1/T=2, L=0/H=1; defaults 0/C/L | Existing coherent Boost tuple and ramps/crossfade |
| 21–22 | AH Boost/Drive Enable | Boolean; On/Off | Existing section transitions |
| 23 | AH Drive Gain | Position 0…1; `.5` | Existing Drive tuple and ramps |
| 24–25 | AH Bass/Treble | −6…6 dB / −9…9 dB; 0/0 dB | Affine conversion to existing DSP positions |
| 26 | AH Volume | Position 0…1; `.6309573444801932` | Preserve cubic law, exact mute, 0 dB default, +12 dB maximum |
| 27 | EQ Enable | Boolean; Off | Existing complete EQ snapshot |
| 28–30 | EQ Band 1 Frequency/Gain/Q | 80…8000 Hz / −12…12 dB / .35…4; 250 Hz / 0 dB / .7 | Existing canonical mappings and transitions |
| 31–33 | EQ Band 2 Frequency/Gain/Q | Same ranges; 1000 Hz / 0 dB / .7 | Existing canonical mappings and transitions |
| 34–36 | EQ Band 3 Frequency/Gain/Q | Same ranges; 3500 Hz / 0 dB / .7 | Existing canonical mappings and transitions |
| 37–38 | EQ HP Enable/Frequency | Off; 20…200 Hz, remembered 80 Hz | Existing canonical mappings and transitions |
| 39–40 | EQ LP Enable/Frequency | Off; 3000…16000 Hz, remembered 8000 Hz | Existing canonical mappings and transitions |
| 41 | EQ Trim | −24…6 dB; 0 | Existing canonical conversion, including exact-default snap |
| 42–44 | Delay Enable/Wet/Preset | Off; 0…1 default `.1`; five-state preset default 0 | Transition validation required |
| 45 | Tuner Enable | Boolean; Off | Existing capture-enable request; no audible path change |

The delay enum follows actual DSP selection values: Lead 121=0, Chorus 011=1,
Chorus 031=2, Holdsworth 223=3, Holdsworth 122=4. The panel's visual grouping is
different and must not redefine those values. AH Emphasis is a two-value enum,
not an On/Off parameter: L/H labels and meaning matter even though its numeric
mapping matches a Boolean.

TC Gain has no monotonic dB interpretation. Keep full shaft position and keep
Focus/Full as presentation state. TC Bass/Treble use conventional user positions;
only the existing electrical adapter reverses them. No separate UI/MIDI
interpretation is introduced.

AH Volume remains a position axis because zero is exact mute. Its gain display
can use the existing cubic law without inventing a finite minimum dB value.
AH physical Bass/Treble descriptors map linearly to the exact accepted 0…1 DSP
positions. Profile constants are reused, not retuned.

EQ mappings delegate directly to `PostAmpEQControls.h`: frequency and Q retain
their logarithmic mappings; gains/trim retain linear dB mappings. Every exact
default survives a normalized round-trip bit for bit, including Trim normalized
`.8` returning positive exact zero after previously non-neutral values. M0
does not alter that accepted header or processor.

## Safety exceptions and extension

Slim remains an existing host-automatable parameter in the factual inventory.
Its descriptor also marks structural-model application and requires a validated
safe replacement mechanism. M0 does not change its existing host flags or call
its unsafe update path from a new source.

TC BLD coefficient preparation/reset semantics do not establish safe continuous
automation. Its three descriptors retain the separate coefficient-update gate.
Delay enable/wet/preset and the stock unsmoothed paths retain explicit transition
validation requirements. A future milestone must resolve these gates before
claiming general click-free realtime control.

Future accepted compressor parameters can receive a new explicit ID domain and
append host indices after 45. No placeholder host exposure is necessary. The
experimental compressor branch is neither read nor imported by this contract.

## Isolated verification

The standalone test source is
`HoldsworthEngine/tests/compatibility/ParameterRegistryTests.cpp`. It requires
C++20 and no iPlug2 or production plugin linkage. It covers frozen allocation,
unique IDs/keys, descriptor boundaries/defaults, continuous and stepped mappings,
nonfinite policy, inverse/monotonic conversions, actual selector/AH mode mapping,
TC electrical coordinate conventions, AH position/gain conventions, delay enum
order and bit-identical accepted EQ conversion/restoration.

Example strict build from the repository root:

```sh
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -O2 \
  HoldsworthEngine/tests/compatibility/ParameterRegistryTests.cpp \
  -o /private/tmp/nam-m0-registry-tests
/private/tmp/nam-m0-registry-tests
```

The companion native IParam proof compares the first 13 descriptors against the
actual production initialization. Keeping that proof independent is necessary:
an isolated registry test alone cannot demonstrate what the plugin registers.

`LegacyRegistryCompatibilityTests.cpp` independently composes the prototype
decoder with a test-only 46-value snapshot for all 13 frozen binary fixtures.
It applies legacy physical constraints directly, preserving nonstepped values
and signed zero without a normalized round-trip. Every appended value retains
its exact registry default, including canonical neutral EQ; historical Slim=1
remains distinct from current startup Slim=0. The returned plugin boundary still
points to the VST3 bypass word and untouched opaque suffix. This test adds no
production migration/application API or coordinator.
