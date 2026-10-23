# M0 legacy state boundary and fixture evidence

The new `control/LegacyStateCodec` is an isolated, non-audio compatibility
prototype. Production `SerializeState`, `UnserializeState`, parameter application,
plugin identities and framework code remain unchanged. There is no future state
writer or appended-parameter exposure in M0.

## Frozen byte layout

On the supported Intel/Apple Silicon macOS ABI, `IByteChunk::PutStr` writes a
little-endian signed 32-bit byte length followed by that many string bytes, with
no terminating NUL. It does not serialize a URL, bookmark, file contents or
normalized path. Parameter serialization writes ordered raw IEEE-754 binary64
**physical** values, not normalized coordinates.

The current plugin state is:

```
string "###NeuralAmpModeler###"
string PLUG_VERSION_STR (currently "0.8.0")
string NAM path
string IR path
13 physical doubles in host index order 0..12
```

The strings in the fixtures contain UTF-8, spaces, an absolute model path and a
Windows-style IR path. The codec preserves these bytes without filesystem
access, Unicode conversion or path normalization. Empty paths remain empty.

VST3 `IPlugVST3State::GetState` adds a signed 32-bit framework bypass word after
the plugin state. `SetState` seeks to the exact offset returned by plugin
unserialization, then reads that word. The codec therefore returns the plugin
end position, never the end of the enclosing input span. For the custom current
fixture it returns **206**, before bytes 206..209 containing bypass; for the
current empty-path/default fixture it returns **147**.

AU ClassInfo puts the same plugin bytes in the `data` entry of a CF property
list. Its enclosing dictionary holds `version`, `type`, `subtype`,
`manufacturer` and `name`. The current identity is `aufx` / `1YEo` / `SDAa`;
`version` is `PLUG_VERSION_HEX` (`0x00000800`). The AU wrapper verifies type,
subtype and manufacturer when restoring. This codec deliberately decodes only
the inner plugin bytes and makes no AU identity decision.

The APP does not add a distinct state envelope to `SerializeState`. Automatic
APP last-session state persistence is separate future work.

## Historical reconstruction coverage

`fixtures/generate-fixtures.py` independently constructs frozen bytes using
Python `struct`, rather than a writer shared with the candidate C++ decoder.
`fixtures/manifest.json` contains exact byte counts, SHA-256 hashes, stored
values, expected migrated physical values, paths and plugin-end offsets.

These fixtures are **synthetic reconstructions from the inspected accepted
source at eaa21571**, not historical DAW-project captures. A versioned fixture
demonstrates a supported reader schema; it does not establish which historical
released writer emitted that header. The original source itself describes
headerless input as assumed 0.7.3–0.7.9. There are no distinguishable version
bytes in that headerless format.

| Fixture family | Stored physical fields | Migration to current 13 slots |
|---|---:|---|
| Headerless pre-0.7.10 | 10 | `Gate` → `Threshold`; old slots 8/9 are OutNorm/IRToggle; add calibration Off, level 12 dBu and Slim 1 |
| Header `0.7.9` | 10 | Same legacy ordering and defaults |
| Headers `0.7.10`, `0.7.11` | 10 | `OutNorm` → numeric `OutputMode`; add calibration Off, level 12 dBu and Slim 1 |
| Headers `0.7.12`, `0.7.13` | 12 | Current order through OutputMode; add Slim 1 |
| Header `0.7.14` | 13 | Preserve all stored physical values |
| Header `0.8.0` | 13 | Current accepted schema |

For ten-field states, the stored order is:

```
Input, Gate/Threshold, Bass, Middle, Treble, Output,
NoiseGateActive, ToneStack, OutNorm, IRToggle
```

Current `IRToggle` is index 8; historical OutNorm is moved to current
`OutputMode` index 11. OutNorm `1.0` becomes numeric enum value **1**, which is
currently **Normalized**, despite an incorrect old migration comment calling
it Calibrated. Both OutNorm states have explicit coverage, with differing IR
values to detect accidental swapping. No boolean reinterpretation is applied
to arbitrary finite old doubles; eventual IParam constraints remain the
application stage's responsibility.

Other frozen synthetic cases include both VST3 bypass values, an opaque suffix
after bypass, empty paths/current defaults, and an AU property-list envelope.
The AU synthetic dictionary has an explicit synthetic name and is not presented
as an actual framework capture. Actual current APP/VST3/AU writer evidence, when
present under `fixtures/native-current`, is separately identified by its native
producer and manifest.

## Candidate decoder safety and deliberate differences

The prototype checks all lengths and arithmetic before access, preserves caller
start offsets, accepts only finite doubles, and publishes output only after a
complete successful decode. Errors leave both the caller's state and consumed
offset unchanged. It does not clamp physical ranges, invoke DSP/model callbacks
or open model/IR files. It allocates strings and belongs strictly off audio.

The current parser treats every version `>= 0.7.14` as 13 doubles. That is unsafe
for a future appended state layout because it would seek VST3 bypass into an
extension. The prototype accepts known legacy layouts through current `0.8.0`
and returns `unsupportedVersion` for a newer version instead of guessing.
Versions `0.7.14` through `0.7.x` retain the inspected 13-field layout. A future
writer must introduce an explicit version/extension contract and update this
selection before being connected to production.

Malformed version syntax, negative/oversized lengths, embedded NULs and NaN/Inf
are rejected more strictly than the old reader. The configurable default
string-size limit is 1 MiB per string. This is a resource bound for the proposed
decoder, **not** an existing IByteChunk contract; future integration must keep
the limit explicit. Path bytes otherwise remain opaque, including non-UTF8
bytes. Old invalid input is not passed to the production parser for comparative
tests because its bounds/version handling can fail outside a transactional
error path.

M0 does not claim compatibility with arbitrary corrupt historical chunks,
unknown future schemas, big-endian historical platforms, or a changed AU type.
Real saved REAPER projects remain a separate integration acceptance gate.

## Isolated test coverage and reproduction

The standalone suite has 19 named tests. It covers all eight schema fixtures,
both OutNorm migrations, exact current defaults, bypass/suffix boundaries,
caller prefixes of 1/7/64 bytes, every truncated prefix of all eight schema
fixtures (**1,506 prefixes**), invalid caller offsets, all four string-field
negative/oversized/NUL cases, path limits/opaque non-UTF8 bytes, all 39 parameter NaN/Inf cases,
out-of-range finite values/signed zero, malformed/overflowing versions and
unknown schemas.

From the repository root:

```sh
python3 HoldsworthEngine/references/midi-automation-v1/m0/fixtures/generate-fixtures.py --check
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -O2 HoldsworthEngine/control/LegacyStateCodec.cpp HoldsworthEngine/tests/compatibility/LegacyStateCodecTests.cpp -o /private/tmp/holdsworth-m0-legacy-state-tests
/private/tmp/holdsworth-m0-legacy-state-tests HoldsworthEngine/references/midi-automation-v1/m0/fixtures
```

The codec is not added to the production Xcode target. Its tests are separate
from the existing DSP test main, so this foundation cannot change DSP output,
filter defaults, presets, MIDI flags, latency or shipped state writing.
