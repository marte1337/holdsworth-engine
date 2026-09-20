# Latency repair — implementation and validation checkpoint

2026-09-16. The user authorized the narrow dependency patch and clarified that
short complementary alignment crossfades and host-dependent PDC timing are
acceptable. Those design/permission questions are resolved. No commit.

**The isolated gate passed before full M2 resumed.** The user accepted the
Xcode license. Xcode 27 native Debug/Release, ASan/UBSan, TSan and APP/VST3/AU
Release builds passed. The earlier CLT sanitizer startup problem is avoided by
using the Xcode toolchain. Local product builds use a macOS 12 deployment
**command-line override** because Xcode 27 rejects the project's 10.15 setting;
the repository's deployment policy is unchanged.

The resumed full integration is described in [README.md](README.md).

## Implementation

- `LatencyState` separates requested, permitted, audio-target, reported and
  acknowledged configurations. Each shared configuration is one lock-free
  64-bit atomic word containing sample count and a configuration tag. Audio
  owns transition counters. Requests coalesce; an existing fade finishes
  before the next permitted target is adopted.
- The bypass delay reserves capacity outside rendering. Tap changes preserve
  history and use complementary linear weights over 10 ms. Normal processing
  also records input, before in-place DSP can overwrite it, so framework bypass
  receives current history. After a fade only the selected tap is read.
- NAM reserves 131072 + 32 delay samples per input channel in its constructor.
  This covers the existing NAM Lanczos ring capacity plus AH. There is no
  live capacity growth. Requests outside capacity return failure; this is not
  an extension of NAM's supported resampling envelope.
- `_UpdateLatency()` now only caches the model contribution. A framework hook
  publishes it at the next render boundary, including bypass callbacks.
  `OnIdle()` calls the single control-thread service. Model installation and
  removal no longer call a host latency API from the audio callback. No model
  loading, transfer, resampling or downstream DSP calculation was changed.
- Full M2 now publishes model latency plus 32 for AH (zero for Off/TC/MC),
  together with the selector identity. DSP uses the permitted identity, so a
  pending VST3 restart cannot prematurely engage the new pedal.
- Framework bypass enable/disable has its own complementary 10 ms fade, using
  scratch allocated in the existing inactive `SetBlockSize` lifecycle. A rapid
  reversal ramps from the current weight. During fully settled bypass, a small
  plugin hook keeps only the selected pre-NAM stage current; NAM/cab/downstream
  remain bypassed. This prevents a stale outer transition on return from bypass.
  The wet path runs temporarily during a bypass fade and stops afterward.

## Format behavior and guarantees

**VST3:** A latency request while active asks for `kLatencyChanged` before
permitting the new configuration. The actual `setActive` lifecycle publishes
the permitted latency before the host's post-reactivation query; rendering
then owns the short tap fade. Stopped transport and `setProcessing(false)` are
not used as control-thread authorization. A host that ignores/declines the
restart cannot be promised immediate adoption. Playback interruption during a
host restart remains host-dependent.

**AU:** Audio publishes completed state first. The control service invokes the
property notification afterward. Publication stays pinned until notification
returns, so a synchronous property query reads the announced value. AU does
not provide an acknowledged, sample-timed PDC commit; host compensation can
briefly differ during adoption. This patch does not claim seamless AU PDC.
The bypass flag is atomic. Setting AU bypass no longer calls `OnReset()` or
pretends to be an activation lifecycle while rendering can continue.

**APP:** Uses the same request/permit/render handoff without a host notification.

**Settled invariant:** the reported sample count and
the active bypass tap agree exactly. During a crossfade, two temporal
alignments can contribute. During VST3 reactivation the host can query the new
latency before the first resumed render. The combined full-pedal/framework harness also checks exact settled agreement
and rapid normal/bypass changes. The latency framework tests separately cover
nonzero model contributions without asserting a new NAM-model fidelity result.

The local `SetLatency()` API now queues a request. This fork requires capacity
reservation and control-thread `ServiceLatencyUpdates()` for APP/AU/VST3.
Other iPlug2 formats/examples are not validated by this scoped patch.

## Validation

`latency-tests.cpp` compiles the **real `IPlugProcessor.cpp` and bypass delay**
with a small notification/lifecycle host double. It is not an actual DAW test
and does not execute the entire AU/VST3 wrapper. Format ordering is separately
inspected in source and compiled in all three products. Real DAW PDC behavior
still requires manual host inspection.

- Current Debug and optimized native tests pass at 44.1/48/88.2/96/176.4/192 kHz,
  callback sizes 1/2/4/8/32/64/128.
- Exact settled taps, 0→32→0, model totals 29/61/75/107, warm history across
  normal/bypass changes, in-place buffers, transition continuity, rapid
  requests, synchronous queries, delayed restart/reactivation and concurrent
  rendering/control requests pass.
- The tested transition render is bit-identical across all seven partitions.
- Native allocation/free/mutex interposition detects zero calls inside the
  tested render paths. The harness first proves interception with deliberate
  allocation/free calls. Interposers are a separate dylib with static functions;
  an earlier weak-inline interposer failed its self-test and was corrected.
- All existing HoldsworthEngine regression tests pass with the Xcode
  native toolchain. This is not a claim of new full NAM-model fidelity testing.
- Frozen Drive sources match their M1 validation hashes; all tracked legacy
  DSP files remain byte-identical to HEAD.
- Strict test-source warnings pass. The framework static analyzer reports the
  same four raw-pointer/WDL ownership leak warnings on unmodified iPlug2 and
  on the patch; there are no additional diagnostics.
- Repository and dependency `git diff --check` pass.
- Current ASan/UBSan and ThreadSanitizer runs pass the expanded framework suite.
- APP/VST3/AU Release builds pass. No AU caches were cleared and no plugins
  were installed in user or system plugin directories.

Native reproduction: `bash validate-latency.sh` from this folder. It times out
stuck tests and retains failure logs. `AH_NATIVE_CXX` and `AH_NATIVE_SDK` select
a working native toolchain. Logs/artifacts are under `/private/tmp/ah-full-m2`.
No installed plugins or user AU caches are modified by the isolated builds.

## Precise change boundary

Vendored iPlug2 revision remains `de5a4fb14fd964247f0c70f2c90b32876a3bfc7c`.
Exactly these dependency files are changed/added:

1. `IPlug/Extras/LatencyState.h` (new)
2. `IPlug/Extras/NChanDelay.h`
3. `IPlug/IPlugProcessor.h`
4. `IPlug/IPlugProcessor.cpp`
5. `IPlug/AUv2/IPlugAU.h`
6. `IPlug/AUv2/IPlugAU.cpp`
7. `IPlug/VST3/IPlugVST3.h`
8. `IPlug/VST3/IPlugVST3.cpp`

Plugin files: `NeuralAmpModeler/NeuralAmpModeler.h` and `.cpp` (latency plumbing).
Support files here: `latency-tests.cpp`, `rt-audit.h`, `rt-audit.cpp`,
`validate-latency.sh`, this report and the updated historical review. Earlier
`latency-timing-probe.py` is still only a timing-contract illustration.
All prior uncommitted M0/M1 work is preserved. The additional full M2 inventory
is in [README.md](README.md). No commit or dependency revision update.
