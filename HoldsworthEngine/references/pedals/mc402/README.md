# MXR / CAE MC402: product v1 clean Boost

Accepted production profile: **`MC402-CLEAN-BOOST-V1`**.

Product v1 includes only the well-supported flat clean Boost: **0 to +20 dB**,
pre-NAM, no modeled clipping, EQ, sag/noise, oversampling or added latency.
The concrete processor is `MC402CleanBoostProcessor`; the existing development
insertion point selects exactly one of **Off / TC BLD / MC402 Boost**.
See [implementation and validation](clean-boost-v1/README.md).

**The provisional Overdrive model was rejected for product integration.**
Available evidence did not justify further model speculation, and the tested
V1/R2 nonlinear models failed the approved fidelity envelope even with ideal
resampling. Overdrive is deferred pending stronger circuit evidence or hardware
measurement. No further alias experiments or circuit research are part of this work.

`MC402-BOUNDED-V1-PROVISIONAL` and `MC402-BOUNDED-V1-PROVISIONAL-R2` remain
historical experimental profiles only. Their source moved out of production
DSP/tests into the [rejected Overdrive archive](rejected-overdrive/README.md).
Their measurements were not rewritten as results for the accepted Boost.

## Read the package

- [Accepted Boost processor and M2 integration](clean-boost-v1/README.md).
- [Historical sources/confidence](sources.md) and [original M0 design](dsp-design.md).
- [Historical milestones](milestones.md) and [M1 isolated experiment](m1/README.md).
- [M1a alias diagnosis](m1/m1a/README.md).
- [M1b product/torture envelopes](m1/m1b/README.md).
- [M1c R2 C² knee experiment](m1/m1c/README.md).
- [Archived source and offline reproduction](rejected-overdrive/README.md).

## Holdsworth context

In Barry Cleveland's March 2008 *Guitar Player* interview, reproduced by the
[Allan Holdsworth Information Center](https://allanholdsworth.info/ahwiki/index.php/The_Man_Who_Changed_Guitar_Forever_%28Guitar_Player_2008%29),
Holdsworth names the MXR/CAE Boost/Overdrive and describes using it in the studio.
This is the historical reason for inclusion, also consistent with the existing
TC BLD source register's H-AH08 entry.

The answer then discusses old TC pedals and using their clean-boost side to
compensate for low-output pickups. The wording about “those pedals” could be
read more broadly, but does not separately identify the MC402 switch state.
Record his general boost preference; do not turn it into an MC402-only usage
claim. No knob positions, supply setting, exact unit revision, recording-specific
chain, or simultaneous-section preference are established. V1 defaults are
development defaults, not an Allan preset.

The historical record supports inclusion of the MC402 pedal, **not a claim
that Allan specifically used only its Boost section**. Product v1's Boost-only
scope is an engineering/evidence decision, not a reconstruction of his settings.

TC BLD DSP, Yamaha DSP and NAM/cab processing remain unchanged. Development UI
v2 gains only the processor selector and MC402 Boost control in its existing
Boost / Drive card. No J. Rockett work or commit is included.

## Repository retention after the Boost-only decision

See the [repository hygiene audit](repository-hygiene.md) for the complete retained-file
inventory, excluded generated outputs, historical hash-manifest scope and
reproduction order. Reports and qualification decisions are preserved; bulk
experiment grids are regenerated locally. Historical validation hashes/counts
describe their original run, not the curated checkout.
