# M0 control ownership contract

Inspected baseline: `eaa21571a442bec3bde73b7b917ea272e607376d`.
This is a design contract and isolated evidence, not a coordinator implementation.
M0 does not change a production callback, mailbox, parameter or smoothing law.

## Actual ownership at the baseline

| Path | Current producer / storage | Current audio consumer and limitation |
| --- | --- | --- |
| Registered NAM parameters | iPlug2 `IParam` atomic physical doubles, written by UI/host/state callbacks | `OnParamChange(int)` rereads the parameter and immediately changes wrapper/DSP state. The callback receives neither the original payload nor a transaction. Atomic values do not make arbitrary processor methods safe concurrently. |
| Post-Amp EQ | One development-UI producer owns the non-atomic requested tuple and calls `setControls` | One audio consumer adopts a complete tuple through three fixed slots. Superseded tuples can coalesce. Adding host/MIDI callers directly would violate the single-producer contract. |
| AH Boost / Drive | One UI producer assembles full tuples from wrapper atomics and calls the relevant setter | Each processor has a three-slot SPSC handoff. Individual atomics do not make concurrent read/modify/publish operations coherent. Boost and Drive are separate mailboxes; their adoption is not one cross-processor transaction. |
| TC BLD | One control producer builds coefficients and publishes a complete result through four coefficient slots | Audio selects a complete coefficient set and resets its realization state. Building coefficients is allocation-free but expensive and not an audio control operation. It has no control smoothing. |
| Pedal selection, AH section flags, MC402 | Atomic requested enum/flags/scalar | Existing audio-side selection/fades and gain/section ramps consume them. Atomic visibility is not a whole-rig transaction. |
| Yamaha development controls | Atomic enable, fixed-point wet, preset index | Block-boundary wrapper adoption; no general state transaction or source arbitration. |
| Tuner | Atomic capture enable and preallocated audio-to-analysis SPSC capture | Analysis and UI feedback run off audio in `OnIdle`. Preserve its ownership and inaudible observation path. |

Sources: [IParam storage](../../../../iPlug2/IPlug/IPlugParameter.h#L337),
[current callback](../../../../NeuralAmpModeler/NeuralAmpModeler.cpp#L824),
[development tuple assembly](../../../../NeuralAmpModeler/NeuralAmpModeler.cpp#L866),
[EQ mailbox](../../../dsp/PostAmpEQProcessor.h#L14),
[AH Boost mailbox](../../../dsp/JRockettAHBoostProcessor.h#L21),
[AH Drive mailbox](../../../dsp/JRockettAHDriveProcessor.h#L20),
[TC coefficient ownership](../../../dsp/TCBLDCleanBoostProcessor.h#L29),
[TC coefficient/reset behavior](../../../dsp/TCBLDCleanBoostProcessor.h#L59).

The current development helper calls `OnMessage` and then
`SendArbitraryMsgFromUI`; the latter calls `OnMessage` again in these native
builds. M1 must migrate each control to one parameter ingress rather than retain
both paths. M0 leaves this accepted behavior untouched.
See [helper](../../../ui/DevelopmentPanel.h#L319) and
[framework dispatch](../../../../iPlug2/IPlug/IPlugAPIBase.cpp#L207).

## Proposed single audio-side owner

One coordinator will own audible requested state and the ordering of ordinary
control changes at each processing boundary. Source callbacks capture values;
they will not publish directly into a processor mailbox or modify audio-owned
state. This is a future implementation requirement.

- UI ingress carries `(semantic ID, double payload, source sequence, generation)`
  from the original event. It performs normal host gestures off audio.
- Host ingress captures the actual format payload and available offset. Do not
  reconstruct an event by rereading an `IParam` that another writer can replace.
- MIDI ingress will eventually carry bounded raw events or compiled mapping
  results; there is no MIDI ingress implementation in M0.
- Independent scalar latest-value requests can coalesce only where losing
  intermediate points matches the declared timing semantics. Switch edges and
  transactions need a separate fixed-capacity representation.
- Every SPSC queue has exactly one producer. UI, host and MIDI cannot be treated
  as one producer merely because they address one parameter. Actual thread
  domains must be verified per format; use separate producer channels or a
  separately validated bounded mechanism if callbacks can be concurrent.
- The coordinator builds complete EQ, Boost and Drive targets from its one
  canonical physical snapshot, preserving all accepted transition timings. It
  becomes the sole publisher for those ordinary processor handoffs; changing
  producer ownership requires quiescent lifecycle transfer, not concurrent old
  and new publishers.
- Coefficient/model preparation is a different application domain. TC must retain
  one off-audio preparation publisher and a defined adoption protocol. A coherent
  rig transaction involving TC cannot be claimed until its prepared coefficients
  can be staged independently and activated with the transaction. Do not call
  TC `setControls` from the audio-side coordinator as a shortcut.

Lifecycle prepare/reset and destruction remain externally excluded from all
publishers/consumers. No new sample buffering or latency is implied.

## Coherent transactions and conflicts

A future transaction must contain a complete validated group, a transaction ID,
and a state epoch. Validate ranges, enums, graph constraints and referenced assets
off audio. Publish a fixed-size snapshot or a preallocated handle; never pass a
callback-owned pointer. Audio either adopts the whole ready group or retains the
old group. Publication, adoption and retirement must have explicit ownership.

Proposed deterministic same-boundary order is: ready state transaction as base,
UI requests, MIDI events, host automation last. A host event wins a conflict at
that boundary; it does not suppress a later MIDI or UI event forever. Retain a
defined ordering for equal-offset events. Old-epoch work must not overwrite a
newer state restore. Define cancellation and host-bypass behavior before use.

This precedence is a specification, not a verified property of current callbacks.
VST3 currently uses only each queue's last point; AU currently drops offsets.
M1 must not advertise sample accuracy. The conservative guarantee is bounded
block-boundary target adoption followed by the processor's own smoothing.
Read/Touch/Write envelope behavior remains host-controlled.

## UI and host feedback

Audio publishes applied-state values and generations to preallocated feedback
storage. The control thread refreshes parameter-linked UI and controller mirrors.
No UI, host gesture, formatting or filesystem call may originate on audio.
Requested state, applied state and smoothed instantaneous DSP values are distinct.
Do not serialize ramp intermediates as requested parameter values.

Track pending gesture/request generations so a delayed feedback frame cannot
undo a newer UI gesture. Mark feedback as feedback to prevent reentering the
control ingress. A generation alone does not establish thread ordering; its
publication payload and release/acquire ownership must be tested together.

`SendParameterValueFromAPI` is not a proven uniform bridge: it queues parameter
feedback, but the VST3 `OnTimer` branch does not drain that parameter queue.
See [API feedback](../../../../iPlug2/IPlug/IPlugAPIBase.cpp#L132).

## Payload interception gates

`OnParamChange(idx, source, offset)` adds source/offset but no value payload;
the wrapper currently overrides only its one-argument version. Migrating to the
three-argument callback alone does not solve a concurrent payload reread.

- The UI setter's argument can be captured before shared storage is changed.
- VST3 processing supplies a concrete double and offset in its queue. A narrow
  project adapter can capture them before base processing; do not double-apply.
- AU's static `SetParamProc` has the concrete value, then writes `IParam` and calls
  `OnParamChange` without that value. A project factory bridge or narrowly scoped
  framework hook must be proven before strict source priority is promised.
  M0 does not modify iPlug2 or assume this hook exists.

See [UI setter](../../../../iPlug2/IPlug/IPlugAPIBase.cpp#L115),
[VST3 queue consumption](../../../../iPlug2/IPlug/VST3/IPlugVST3_ProcessorBase.cpp#L275)
and [AU setter](../../../../iPlug2/IPlug/AUv2/IPlugAU.cpp#L1565).

## Safety exceptions and M1 gates

**TC BLD:** the accepted control change rebuilds fitted coordinates and resets
state on adoption. No dezippering exists. Registering a descriptor does not make
these controls safe for unrestricted automation or MIDI sweeps. A separately
validated transition/preparation protocol and deterministic offline policy are
required. See [accepted limitation](../../pedals/tc-bld/m2/README.md#L98).

**Slim:** preserve legacy host slot 12 and existing state semantics. Its handler
calls `SetSlimmableSize` on the active and staged NAM models. The WaveNet
implementation allocates vectors, extracts weights, constructs/reset a model and
uses atomic shared-pointer publication, whose operations are not guaranteed
lock-free. Adoption can release old shared-owned resources on audio. Moving the
existing call to a worker touching the live model does not establish safe
ownership. Isolated preparation, bounded adoption, non-realtime retirement and an
offline policy are separate validation gates. M0 does not change automation flags
or claim this legacy path is safe.
See [wrapper handler](../../../../NeuralAmpModeler/NeuralAmpModeler.cpp#L1233),
[construction/staging](../../../../NeuralAmpModelerCore/NAM/wavenet/slimmable.cpp#L390),
[adoption](../../../../NeuralAmpModelerCore/NAM/wavenet/slimmable.cpp#L454)
and [parameter update](../../../../NeuralAmpModelerCore/NAM/wavenet/slimmable.cpp#L484).

Other current gain/tone/gate/IR/delay paths have no new smoothing guarantee in M0.
M1 can begin with compatibility-preserving, independently tested ready controls;
neither complete automation coverage nor unrestricted TC/Slim control is approved
by this foundation alone.

## Isolated evidence

`tests/compatibility/ControlOwnershipContractTests.cpp` contains two deterministic
counterexamples using test-only controlled threads:

1. A shared atomic parameter is changed after a source event but before its
   callback rereads it; the callback sees another source's value. Capturing the
   event payload retains the intended value.
2. Two writers each load an old tuple and publish a complete replacement; the
   later publication loses the other writer's update despite every shared field
   being atomic. Applying the captured patches under one owner preserves both.

These tests demonstrate prerequisites. They do not prove a future coordinator,
production callback serialization, queue bounds or realtime behavior. Their
thread creation and test-only synchronization are intentionally outside DSP.
