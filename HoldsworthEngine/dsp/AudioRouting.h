#pragma once

#include "DelayBandIdentity.h"

#include <array>
#include <optional>

namespace holdsworth::dsp
{

// The destination band is identified by this relationship's array position.
// sourceBand identifies the one upstream Effect Band whose mono routed output
// becomes that destination's input.
struct ConnectedBandAudioInput final
{
  DelayBandId sourceBand = DelayBandId::band1;
};

// inputs[0] describes Band 1's input, inputs[1] Band 2's input, and so on.
// std::nullopt represents Yamaha CONNECT IN: direct engine input. A source may
// feed several destinations, while each destination has exactly one input.
struct AudioRoutingConfiguration final
{
  std::array<std::optional<ConnectedBandAudioInput>, kHoldsworthDelayBandCount> inputs{};
};

enum class AudioRoutingApplyResult
{
  applied,
  invalidSourceReference,
  selfReference,
  cycleDetected
};

} // namespace holdsworth::dsp
