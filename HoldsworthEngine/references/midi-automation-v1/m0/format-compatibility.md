# M0 format compatibility findings

Inspected application revision: `eaa21571a442bec3bde73b7b917ea272e607376d`.
Vendored iPlug2 revision: `8b7def3150e0b7d2d51c38d5a66833a8753a77d7`.
M0 enables no MIDI flags, changes no identities and modifies no vendored files.

## Identity and parameter invariants

Retain `PLUG_NAME`, manufacturer, bundle identity, `PLUG_UNIQUE_ID='1YEo'`,
`PLUG_MFR_ID='SDAa'`, AU entry/factory/view names and the current VST3 UIDs.
The default VST3 processor UID words are
`F2AEE70D 00DE4F4E 53444161 3159456F`; controller UID words are
`F2AEE70E 00DE4F4F 53444161 3159456F`.
Do not change native-endian byte handling by copying the printed words into a
new implementation. Reuse the existing SDK UID construction.

Regular host parameter IDs equal the iPlug parameter indices. Freeze 0–12 and
append future controls. Under the current non-`IPLUG1_COMPATIBILITY` build:

| Framework parameter | ID |
| --- | ---: |
| Bypass | 65536 |
| Preset | 65537 |
| Virtual MIDI CC base | 65538 |

Keep ordinary indices below 65536. Appending parameters does not shift these
special IDs. The existing one host preset is independent of the five Yamaha
development choices. This document does not establish new host program semantics.

Sources: [identity/configuration](../../../../NeuralAmpModeler/config.h#L1),
[UID construction](../../../../iPlug2/IPlug/IPlug_include_in_plug_src.h#L105),
[reserved IDs](../../../../iPlug2/IPlug/IPlugConstants.h#L51).

## APP: existing entry points, not enabled product MIDI control

`PLUG_DOES_MIDI_IN=0` and `PLUG_DOES_MIDI_OUT=0` remain unchanged. Framework
transport code exists; it is not evidence that the product already implements
MIDI CC mapping or Learn.

- `IPlugAPPHost::InitMidi` constructs RtMidi input/output and registers
  `MIDICallback`; RtMidi supplies the macOS CoreMIDI backend.
- Device selection is `SelectMIDIDevice`. Endpoint names and channel preferences
  persist in the APP settings INI.
- `MIDICallback` copies short messages to the private callback queue; SysEx uses
  a separate bounded payload. Its `deltatime` argument is not used. Short messages
  therefore carry no meaningful event sample offset into audio.
- `IPlugAPP::AppProcess` drains callback MIDI/SysEx and editor MIDI, then processes
  buffers. Its `while (Pop)` loops are not bounded to a snapshot of events present
  at entry: a concurrently refilling producer can extend the drain. Queue
  capacity alone does not bound this loop. Callback `Push` failures are ignored.
- Stored `mMidiInChan` is not enforced by this callback. Future mapping must apply
  explicit channel/Omni handling instead of assuming the settings UI filters it.

A future transport milestone needs an independently reviewed bounded-drain and
observable-overflow plan. An event budget inside the plugin's `ProcessMidiMsg`
cannot cap work in the outer framework loop. No framework change is made in M0.
Parameter mutex macros in framework paths are conditional; their presence in
source is not proof of runtime locking in the accepted build.

Sources: [APP MIDI creation](../../../../iPlug2/IPlug/APP/IPlugAPP_host.cpp#L780),
[callback](../../../../iPlug2/IPlug/APP/IPlugAPP_host.cpp#L894),
[drain](../../../../iPlug2/IPlug/APP/IPlugAPP.cpp#L122),
[settings](../../../../iPlug2/IPlug/APP/IPlugAPP_host.cpp#L139),
[conditional mutex macros](../../../../iPlug2/IPlug/IPlugPlatform.h#L47).

## VST3: ordinary parameters and virtual CC transport

The current wrapper implements `IMidiMapping`. MIDI input is product-disabled;
the default `VST3_NUM_MIDI_IN_CHANS` and `VST3_NUM_CC_CHANS` are both one. Future
Omni/all-channel support must deliberately expose the required channels.

`getMidiControllerAssignment` encodes a controller as
`65538 + channel * 130 + controllerNumber`. Controller numbers 0–127 are CC;
128 and 129 represent channel aftertouch and pitch bend. Incoming CC therefore
arrives through VST3 parameter queues, not as an assumed generic MIDI event.

`ProcessParameterChanges` consumes only `getPoint(numPoints - 1, ...)` for each
queue. It sets an ordinary `IParam` and invokes `OnParamChange(idx,kHost,offset)`,
or decodes a virtual controller and invokes `ProcessMidiMsg`. Intermediate points,
including a same-block switch press/release, are currently lost. Ordinary DSP
application remains block-oriented; the supplied final offset does not make the
current NAM one-argument handler sample-accurate.

Future MIDI switch semantics need an edge-preserving bounded adapter before the
base wrapper collapses virtual-CC queues. Avoid duplicate decoding in the base.
Define tie ordering across different CC queues with equal offsets.

Program Change input is not decoded by this implementation. SDK
`kCtrlProgramChange=130` is for legacy MIDI CC **output** events; requesting 130
as an input CC aliases the next channel's controller zero with this encoding.
Do not use it as a Program Change workaround.

Sources: [channel defaults](../../../../iPlug2/IPlug/VST3/IPlugVST3_Defs.h#L1),
[mapping](../../../../iPlug2/IPlug/VST3/IPlugVST3.cpp#L171),
[last-point handling](../../../../iPlug2/IPlug/VST3/IPlugVST3_ProcessorBase.cpp#L275),
[SDK controller numbers](../../../../iPlug2/Dependencies/IPlug/VST3_SDK/pluginterfaces/vst/ivstmidicontrollers.h#L103).

VST3 framework bypass is serialized after plugin-owned state. The future codec
must return its exact consumed offset; never consume or interpret bypass bytes as
an optional parameter extension. See [state wrapper](../../../../iPlug2/IPlug/VST3/IPlugVST3_Common.h#L69).

## Existing AU: preserve `aufx`

The installed component's identity is `aufx / 1YEo / SDAa`. Preserve its plist,
runtime component type and subtype/manufacturer. Enabling MIDI in the current
effect wrapper changes `GetAUPluginType()` from `aufx` to `aumf`; the resource
definition makes the same distinction. Its `SetState` rejects a mismatched type,
subtype or manufacturer. A blanket MIDI flag change risks opening existing
REAPER projects with a missing or incompatible component.

The existing AU can receive appended ordinary host automation without changing
its type. REAPER's host parameter Learn/MIDI Link may drive those parameters;
that route is distinct from receiving MIDI messages inside the plugin.

`SetParamProc` receives a physical float and `offsetFrames`, but writes `IParam`
then calls `OnParamChange(idx,kHost)` without passing the offset or value.
`DoScheduleParameters` processes immediate events only; ramp events are ignored.
An exact-value callback bridge is an unresolved M1 ingress gate. Do not claim
sample-accurate ramps or deterministic source attribution from the current generic
callback alone.

`DoMIDIEvent` calls `ProcessMidiMsg` only when `DoesMIDIIn` is true; otherwise it
returns `badComponentSelector`. No direct-MIDI Learn is promised for the existing
component by M0.

Sources: [current plist](../../../../NeuralAmpModeler/resources/NeuralAmpModeler-AU-Info.plist#L21),
[runtime type](../../../../iPlug2/IPlug/IPlugProcessor.cpp#L452),
[resource type](../../../../iPlug2/IPlug/AUv2/IPlugAU.r#L85),
[state identity check](../../../../iPlug2/IPlug/AUv2/IPlugAU.cpp#L1470),
[setter](../../../../iPlug2/IPlug/AUv2/IPlugAU.cpp#L1565),
[scheduled events](../../../../iPlug2/IPlug/AUv2/IPlugAU.cpp#L2417),
[MIDI selector](../../../../iPlug2/IPlug/AUv2/IPlugAU.cpp#L2449).

## Possible AU MIDI companion and host routing

A separately identified `aumf` target can share DSP, the registry and state codec
while leaving the accepted `aufx` component available. A distinct target/component
identity and a defined import path for old AU state need explicit approval and
tests; no such target is introduced in M0. If a companion uses another subtype,
its normal framework state identity check will reject old `aufx` state unless an
explicit, validated import operation is provided.

APP routing will select a CoreMIDI endpoint. VST3 routing requires REAPER to send
track MIDI to the effect's enabled event bus. A MIDI-capable AU requires the host
to route MIDI to that component. REAPER host Learn uses control MIDI; MIDI Link
and plugin-owned Learn use different paths. A controller enabled only for host
control is not evidence its messages reach the plugin. These are future transport
acceptance checks, not M0 runtime changes.

Host bypass can suspend processing and thus control adoption. Future code must
bound pending work and reconcile requested/applied generations when resumed.
M0 preserves existing bypass behavior and the accepted pedal-domain latency;
there is no new audio buffering or latency declaration.
