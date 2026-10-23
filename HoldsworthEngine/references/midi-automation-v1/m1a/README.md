# M1a — Production state-extension integration

Branch: `feature/midi-automation-v1`; unchanged HEAD/base:
`eaa21571a442bec3bde73b7b917ea272e607376d`.
All 45 accepted M0 files are preserved byte-for-byte. Their frozen hashes are in
`m0-preservation.json`. No commit, push, merge or branch switch was performed.

## Production scope

The plugin now writes a bounded, independently versioned state container and
reads both that container and legacy NAM state. Production changes are limited to:

- `NeuralAmpModeler/NeuralAmpModeler.cpp`: serialization, validated restore and
  format-specific restore guards.
- `NeuralAmpModeler/NeuralAmpModeler.h`: format overrides and a state-callback-owned
  opaque extension snapshot with its mutex.
- `NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj`:
  enroll the codec in the eight existing macOS plugin targets.
- New `HoldsworthEngine/control/ProductionStateCodec.h` and `.cpp`.

The original 13 IParams retain their indices, metadata, physical values, mappings
and defaults. Plugin identities, version metadata, AU `aufx / 1YEo / SDAa`, MIDI
flags, presets and all DSP code/defaults remain unchanged. No new host parameter,
MIDI mapping, MIDI transport, Learn, coordinator or development-control persistence
is implemented. The default extension is empty. APP's state methods are exercised;
no standalone session-autosave facility is added.

## Wire format and versioning

All integers and binary64 values are little-endian. Strings are byte-preserving
length-prefixed paths without terminators; embedded NUL is rejected.

| Field | Encoding |
| --- | --- |
| Magic | 8 bytes: `HENAMST1` |
| Container schema | uint32, currently 1 |
| Body length | uint32, excluding the 16-byte header |
| Model path | uint32 byte length, path bytes |
| IR path | uint32 byte length, path bytes |
| Frozen legacy values | 13 physical IEEE-754 binary64 values, indices 0–12 |
| Extension length | uint32, includes record count and all records |
| Record count | uint32 |
| Each record | uint32 semantic ID, uint32 wire type, uint32 payload length, payload |

A default state is 136 bytes. The independent three-record test state is 193
bytes. VST3 then appends its unchanged four-byte framework bypass word, giving
140 and 197 bytes respectively. The decoder returns the exact absolute consumed
position, including an arbitrary caller prefix, and never consumes the bypass
word or opaque framework suffix as extension data.

Bounds are 1 MiB per path, 256 KiB for the entire extension, 1,024 records and
2,359,552 bytes for plugin-owned state. The outer VST3 reader additionally bounds
its captured stream to that state limit plus four bytes. Normal small framework
suffixes are accepted within that cap. AU ClassInfo data must contain exactly one
complete plugin state; direct chunk decoding may leave enclosing suffix bytes.

Semantic IDs use M0's explicit high namespace (at least `0x10000000`). Original
legacy IDs `0x10000000`–`0x1000000c` cannot also appear in the extension. Lower IDs,
duplicates, type zero, inconsistent lengths/counts, overflow, nonfinite core
values and malformed records are rejected. Wire type 1 is a finite physical
binary64 with exactly eight payload bytes. Other nonzero wire types are opaque.

**Every extension record is inactive in M1a**, including known proposed EQ IDs.
Unknown IDs/types and record order survive successful load/save byte-for-byte.
No value is applied to a DSP target. Future activation must introduce ownership,
type/range/default rules and state-application tests for that ID. No MIDI-mapping
record format is fabricated here. Unknown container versions fail closed; future
record IDs alone do not require a new container version.

The container version is independent of `PLUG_VERSION_STR` and AU/VST3 identity.
Backward compatibility means new binaries load old state. Loading new `HENAMST1`
state in pre-M1a binaries is unsupported; no downgrade guarantee is claimed.

## Legacy migration and failure behavior

The production codec is separate from M0's frozen prototype. It preserves the
accepted production reader's valid numeric-version dispatch, including its
integer-prefix version parsing, rather than importing M0's narrower version cap:

- Headerless legacy and 0.7.9/10/11: ten stored values; Gate becomes Threshold,
  OutNorm/IRToggle move to their current positions, calibration defaults are
  inserted and missing Slim becomes 1.
- 0.7.12/13: twelve values; missing Slim becomes 1.
- Parsed versions at least 0.7.14: thirteen values, as in the old production code.
- Current startup Slim remains 0. OutNorm 1 remains OutputMode 1, Normalized.

Path bytes and physical values are preserved. The existing IParam constraints,
parameter-reset order, NAM/IR staging, missing-asset behavior and TC/Slim handlers
remain in the accepted `_UnserializeApplyConfig` path. A valid legacy recall
replaces retained extension data with an empty extension.

The complete core and extension are decoded into a private candidate before any
application. The legacy JSON application object is allocated before mutation.
Malformed/truncated input leaves parameters, paths, retained records and framework
bypass unchanged. Finite out-of-range physical legacy values retain the original
IParam clamping; malformed states are intentionally rejected instead of inheriting
old unchecked partial reads, invalid lengths or exceptions.

The direct failure return is zero. A VST3 override captures bounded input, validates
both the state and a complete 0/1 bypass word, then delegates using an immutable
SDK memory stream. This prevents the vendored wrapper from seeking to a failed
decoder result and changing bypass from unrelated bytes. Normal framework
controller-bypass restoration is preserved; processor bypass still follows normal
host parameter delivery.

AU's virtual state method is private in the base class. The project override
validates ClassInfo types, numeric identity fields, a UTF-8 name fitting the
framework's 64-byte buffer, and bounded copied data before reproducing its public
`RestorePreset` -> `UnserializeState` -> `OnRestoreState` sequence. It preserves
valid-state behavior and prevents malformed payloads from first restoring a
named preset. Wrong AU identities are rejected. No vendored file is modified.

## Ownership and realtime boundary

State callbacks exclusively own the whole opaque record vector. One recursive
mutex serializes save/load and accommodates AU's existing preset reentry. A
validated complete vector is published with a no-throw swap. No development
atomics are sampled to invent a requested DSP transaction. Audio processing,
parameter callbacks and DSP mailboxes never acquire the new mutex or access the
extension. Encoding, decoding, string/vector allocation and asset loading are
non-realtime state operations.

This guarantee covers complete extension snapshots and concurrent state callbacks.
It does not make the original 13 independently atomic parameters, UI path edits
or audio-side model clearing a coherent transaction during concurrent automation
or asset editing. Those pre-existing paths require source/lifecycle ownership in
M1b; hosts are not assumed always to suspend audio for saves. The native APP test
performs 1,000 recalls concurrently with 1,000 saves and accepts only either
complete requested state, with concurrent automation/path editing explicitly
excluded. No full coordinator is introduced to overstate that guarantee.

## Validation and evidence

Exact commands, exit statuses, measurements, source/product hashes and raw logs
are retained in `validation.json` and `validation-logs.tar.gz`. New native capture
payloads and their provenance are under `native-captures/`.

- The production codec passes Debug, Release and ASan/UBSan: eight test groups,
  60,727 checks, 2,650 truncated prefixes and 10,000 deterministic mutations per
  run. Its independent encoder oracle checks the byte format. Clang analysis
  reports no diagnostics for codec and codec tests.
- Frozen M0 registry (195,388 checks), legacy codec (19 tests) and joint migration
  (15 tests / 984 checks) pass in all three modes. The M0 files are not rewritten.
- Newly built APP, VST3 and AU state APIs each load all 13 historical binary
  reconstructions and all eight native M0 binary captures. AU additionally loads
  two actual native ClassInfo plist captures. These are **not saved DAW projects**.
- Each format checks 13,325 normalization points and inverses; maximum absolute
  error is zero. There remain exactly 13 host parameters.
- New empty/populated/unknown records round-trip; malformed, truncated, oversized,
  duplicate-ID and nonfinite states reject transactionally. VST3 missing/invalid
  bypass words reject before mutation. AU also rejects wrong CF types, missing
  dictionary fields, oversized UTF-8 names and identity mismatch.
- Each format verifies 25 direct consumed offsets and 30,962 bit-exact stereo
  frames across all six supported sample rates, irregular/one-sample callbacks,
  plus real NAM/IR assets and normal/host-bypass processing. Reported latency is
  unchanged at 32 samples in these no-model / 48 kHz fixture-model cases.
- Existing DSP regressions are freshly compiled and pass **329/329** in Debug,
  Release and ASan/UBSan.
- Release APP/VST3/AU products pass build, plist, signature and architecture checks.
  Production APP state methods and the codec are additionally recompiled with
  ASan/UBSan and pass the native functional/concurrency suite. Other framework/DSP
  objects remain Release in that instrumented APP probe; AU/VST3 bundles are
  tested as actual Release products. ASan leak detection is disabled.

The native realtime audit retains pre-existing first-use work rather than hiding
it: at each rate, legacy-state and extension-state instances each report 48
intercepted first-use calls; the real NAM/IR case reports 55 each. Subsequent
callbacks report zero intercepted allocation/free/lock/wait calls. The interposer
self-test verifies interception before testing. These counts are instrumented
calls, not exclusively allocation counts or timing measurements.

Initial sandbox/Xcode cache failure, a strict test-loop warning, the AU private-base
compile error, missing interposer setup and the first-use audit finding are kept
in the raw evidence. Combining the dyld realtime interposer with ASan aborted
before useful diagnostics; the same instrumented production test passes with
sanitizer interception alone. Realtime auditing therefore runs in the separate
Release pass, with a visible `rt_audit_enabled` flag. Functional tests and response
thresholds are unchanged. Inherited native compiler/toolchain warnings remain in
logs; expected missing-asset fixture messages are not codec failures.

## Reproduction

Run from the repository root. Products stay under `/private/tmp`, with installation
and AU cache clearing disabled by the existing builder:

```sh
TUNER_BUILD=/private/tmp/nam-midi-automation-m1a bash HoldsworthEngine/references/tuner-v1/package-products.sh
python3 HoldsworthEngine/references/midi-automation-v1/m1a/validate-contracts.py
python3 HoldsworthEngine/references/midi-automation-v1/m1a/validate-contracts.py --engine
python3 HoldsworthEngine/references/midi-automation-v1/m1a/validate-native.py
python3 HoldsworthEngine/references/midi-automation-v1/m1a/validate-native.py --formats APP --sanitize-production-app
```

Xcode builds need normal cache/service access; actual AU tests need registrar
access. No plugin installation or GUI automation is used. The M0 audit intentionally
asserts its original disconnected-production milestone and should not be rewritten
or presented as an M1a production audit.

## M1b readiness

M1a provides the validated state envelope and backwards loading needed to begin
M1b. M1b must establish actual value-carrying ingress and ownership before activating
extension records or exposing additional host parameters. AU payload capture,
legacy concurrent-control coherence, ready-control transition tests and the TC BLD /
Slim safety exceptions remain explicit gates. This work does not certify broad
automation, MIDI routing, Program Change or real REAPER project reopening.
