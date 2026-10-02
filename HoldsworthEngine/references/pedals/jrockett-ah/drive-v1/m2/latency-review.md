# Full AH M2: latency inspection and required stop

2026-09-15. M2 implementation has not started. Both approved DSP profiles remain
unchanged. This note records the user's requested latency preflight and stop
condition, not a change to the accepted 0/32-sample policy.

**Current status:** Both the bounded dependency repair and short complementary
outer-alignment crossfades have been authorized. The earlier stops below are
historical, not pending approval requests. See the
[implementation/validation checkpoint](latency-repair.md) for the current
repair and outstanding build-environment gate.

## Finding

The current iPlug2 API cannot safely be called from the selector's UI message
handler while audio callbacks may continue. Moving `SetLatency()` from audio to
`OnIdle()` alone does not solve the problem. A plugin-level callback guard also
does not cover the framework's host-bypass processing.

The VST3 SDK provides a useful deactivate/reactivate protocol, but the current
AU wrapper does not provide an equivalent guarded dynamic-latency update. Thus
the requested APP/VST3/AU integration cannot be completed safely through the
existing shared plugin interface alone. Stop here, as the user requested,
before choosing a broader policy or modifying the dependency.

Inspected iPlug2 revision: `de5a4fb14fd964247f0c70f2c90b32876a3bfc7c`.

## Concrete source evidence

Paths below are relative to the repository root, six levels above this folder.

1. [IPlugProcessor::SetLatency](../../../../../../iPlug2/IPlug/IPlugProcessor.cpp)
   at line 272 writes a plain `int mLatency` and immediately calls
   `mLatencyDelay->SetDelayTime()`. Both fields are private in
   [IPlugProcessor.h](../../../../../../iPlug2/IPlug/IPlugProcessor.h), lines
   288/306; a derived plugin cannot safely substitute atomic storage and its own
   bypass-delay implementation through the public interface.
2. [NChanDelayLine::SetDelayTime](../../../../../../iPlug2/IPlug/Extras/NChanDelay.h)
   at line 26 resizes the buffer, resets its write index and clears its storage.
   `ProcessBlock()` reads/writes those same fields without a handoff. A concurrent
   change can race with buffer access, not merely give a stale latency number.
3. Framework bypass calls `PassThroughBuffers()` outside the plugin's
   `ProcessBlock()`: [VST3 ProcessAudio](../../../../../../iPlug2/IPlug/VST3/IPlugVST3_ProcessorBase.cpp),
   lines 399–405; [AU RenderProc](../../../../../../iPlug2/IPlug/AUv2/IPlugAU.cpp),
   lines 1737–1740. These branches are also outside the surrounding parameter
   mutex regions. Enabling that mutex is neither a sufficient fix nor compatible
   with the requested lock-free realtime path.
4. [IPlugVST3::SetLatency](../../../../../../iPlug2/IPlug/VST3/IPlugVST3.cpp),
   line 244, changes base latency/bypass storage **before** calling
   `restartComponent(kLatencyChanged)`. The vendored
   [VST3 interface contract](../../../../../../iPlug2/Dependencies/IPlug/VST3_SDK/pluginterfaces/vst/ivsteditcontroller.h),
   lines 136–141, says that restart entails host deactivation/reactivation.
   Requesting that cycle first and applying state in the inactive lifecycle is
   a viable VST3-specific direction; the current SetLatency ordering is not it.
5. [IPlugAU::SetLatency](../../../../../../iPlug2/IPlug/AUv2/IPlugAU.cpp), line
   2007, informs property listeners **before** changing the latency value.
   A synchronous listener query can therefore observe the old value. This
   notification itself establishes no exclusion from `RenderProc()` and does
   not safely protect the bypass buffer mutation.
6. `OnReset()` is not a control-thread guarantee. The
   [VST3 SetProcessing implementation](../../../../../../iPlug2/IPlug/VST3/IPlugVST3_ProcessorBase.cpp),
   line 233, calls it on `setProcessing(false)`. The vendored
   [IAudioProcessor contract](../../../../../../iPlug2/Dependencies/IPlug/VST3_SDK/pluginterfaces/vst/ivstaudioprocessor.h),
   lines 332–343, allows this call on the processing thread. Stopped transport
   is distinct from disabled processing and cannot authorize a UI-side resize.

## Existing plugin latency work that must be included

[NeuralAmpModeler::_ApplyDSPStaging](../../../../../../NeuralAmpModeler/NeuralAmpModeler.cpp)
already calls `_UpdateLatency()` when a model is installed/removed from inside
the realtime callback (around lines 966/982). `_UpdateLatency()` calls the host
API directly (line 1343 onward). These are pre-existing calls, not AH additions.
Adding an AH-only idle notification while retaining them would not satisfy the
user's prohibition on realtime host-latency calls, and would leave competing
latency writers.

The later total must be **existing NAM resampling latency + selected pedal's
additional latency**. Off/TC/MC402 contribute zero; AH contributes 32 regardless
of its internal enables. Model staging should publish its latency contribution
without host calls; a control-thread coordinator should own host notifications.
This is latency bookkeeping only, not authorization to change NAM audio behavior.

## Alternatives examined

| Approach | Assessment |
| --- | --- |
| Call SetLatency in the selector message or OnIdle | Still races with framework bypass delay/storage; AU notification order is stale |
| Guard only NeuralAmpModeler::ProcessBlock | Cannot exclude the framework bypass path; that path does not call it |
| Trust stopped transport or a quiet callback interval | Neither prevents the next callback; explicitly disallowed by the task |
| Update in every OnReset | OnReset can run on the processing thread; notifications/resizes there violate the requested contract |
| Use the parameter mutex | Bypass is outside it; introducing realtime locks is disallowed |
| VST3 restart first, then inactive lifecycle update | Promising for compliant VST3 hosts, but does not solve AU or all existing latency writers |
| Permanently add 32 samples to every outer choice | Broader behavior change explicitly withheld; not chosen |
| Standalone-only full pedal | Does not fulfill the requested multi-format M2; not substituted silently |

## Recommended bounded next scope

Keep the requested latency policy. Authorize a narrowly scoped iPlug2
latency/bypass-state fix, reviewed independently of the frozen pedal DSP:

- Separate control-thread host notification from DSP/bypass state adoption.
  Latency publication needs a race-free value visible to host property queries;
  publish the new value before AU listener notification.
- Give the bypass delay audio-owned indices and preallocated storage with a
  bounded configuration handoff. Any capacity growth must occur in an actual
  host-inactive lifecycle, never concurrently with rendering or in audio code.
  Capacity must account for existing model resampling latency as well as AH's 32.
- Coordinate selector adoption and reported total latency through the format's
  lifecycle: use the documented VST3 restart cycle; establish and test the AU
  update/compensation behavior rather than assuming VST3 semantics apply there.
- Replace the plugin's existing realtime host-latency calls with contribution
  publication and a single control-thread notification path. No NAM transfer or
  resampler changes.
- Test notification-thread affinity, synchronous latency queries, bypass renders
  concurrent with requests, genuine inactive/active cycles, rapid coalesced
  requests and model-latency contributions before finishing AH M2. Then retain
  the normal APP/VST3/AU builds and manual host inspection.

This is a proposed repair scope, not a claim that a patch has been implemented
or proven. It changes dependency/lifecycle code and needs explicit authorization:
[AGENTS.md](../../../../../../AGENTS.md) states, “Do not modify vendored code or
Git submodules unless explicitly requested.” No dependency, processor, UI or
project build file was changed during this M2 preflight. Only this note was
added. No new full-pedal standalone was built, and no commit was created.

## Authorized follow-up: timing contract

The authorized scope requires no time discontinuity during framework bypass
latency changes, while also requiring the bypass delay and reported latency to
represent the same adopted state. A literal uninterrupted, host-compensated
timeline is stronger than memory safety and eventual compensation convergence.
This distinction needs resolution before adopting a transition policy.

**Dynamic AU latency is supported.** Apple's
[Audio Unit Programming Guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnit/TheAudioUnit.html)
describes changing the latency property and notifying listeners. This should
not be represented as an AU prohibition on dynamic latency. However, the
documented property-change notification is not an acknowledged, sample-timed
commit of the host's compensation graph. That missing coordination is the
limitation on the requested guarantee, not permission to edit iPlug2.

Additional local evidence:

- In the installed macOS SDK, `AUComponent.h`, lines 1070–1091,
  `AudioUnitPropertyListenerProc` receives the unit, property, scope and element;
  it returns `void`. It supplies neither a future render boundary nor a PDC
  completion acknowledgement. The inspected header is under
  `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/`.
- `AudioUnitUtilities.h` describes event delivery on run loops/dispatch queues
  with notification intervals. Its event host time records an event; it is not
  an agreement to switch audio/compensation on a specified future sample.
  Also, `AUEventListenerNotify` is explicitly for parameter/gesture changes,
  not an alternative API for issuing an AU's property changes externally.
- The current iPlug2 `InformListeners()` simply invokes property callbacks.
  Callback return does not prove the host has stopped rendering or changed PDC.
- The VST3 SDK's `kLatencyChanged` restart explicitly entails a host lifecycle
  cycle. `IAudioProcessor::getLatencySamples` also warns that compensation
  recomputation can interrupt playback. Even VST3 should not be advertised as
  a guarantee of uninterrupted playback across a latency change.
- AU's `kAudioUnitProperty_BypassEffect` setter currently calls `OnReset()`
  immediately after `SetBypassed()`. That is another reset-while-rendering risk
  that the eventual scoped bypass repair must cover; a bypass property write
  is not proof of a genuinely inactive lifecycle.

### What a bounded repair can and cannot promise

A preallocated history with audio-owned indices, a bounded request handoff,
and atomic publication can make the plugin's delay state and synchronous
latency queries race-free. AU notification can then run on the control thread
after publication. That does not synchronize the host's compensation change
with the audio adoption. Neither notifying first nor notifying afterward closes
this gap. This is an inference from the API contracts, not a DAW measurement.

Switching an exact delay from 0 to 32 while continuously rendering necessarily
changes which input time produces the next output sample. A short crossfade
avoids a hard tap switch but temporarily contains both delays. During that
transition it cannot equal a single exact integer-sample delay. Waiting for a
genuine host-inactive cycle avoids modifying a running route, but AU supplies
no equivalent mandatory latency-restart request; an indefinitely deferred
selection would not satisfy normal live operation.

The compact [timing probe](latency-timing-probe.py) reproduces these distinctions
offline using no dependencies or generated artifacts:

- At 48 kHz, a 750 Hz sine of peak amplitude 0.25 has an ordinary sample step of
  0.0012038183 at the chosen boundary. Changing the exact tap from 0 to 32 gives
  a step of 0.4987961817, even with perfectly available history and no reset.
- A half-way two-tap crossfade has two nonzero impulse taps, so it is not an
  exact 0- or 32-sample delay. For this half-cycle-separated sine the two taps
  cancel at the midpoint. This is a counterexample, not a proposed transition
  curve or a pedal fidelity measurement.
- A scheduling example for seven callback sizes and three model contributions
  keeps published latency and the plugin's adopted bypass delay equal while
  host compensation is deferred. Plugin state agreement alone does not imply
  host timeline agreement.

All probe assertions passed. **These are mathematical/scheduling examples,
not framework tests, concurrency validation or tests in an actual host.** No
latency implementation has passed the user's isolated repair gate yet.

The user has been asked whether outer 0/32 switches may use a short transition
with host-dependent compensation timing, or whether the timeline must remain
uninterrupted. Do not silently choose the former, impose global 32-sample
latency, or implement the full-pedal M2 while that requirement is unresolved.
The frozen internal AH 32-sample policy is unaffected by this question.

Follow-up change inventory: this note and `latency-timing-probe.py` only.
Vendored iPlug2 files changed: **none**. Plugin/NAM/DSP/UI files changed in this
follow-up: **none**. No new APP/VST3/AU builds or full-pedal Release standalone
were produced. Prior uncommitted M0/M1 files remain intact. No commit.
