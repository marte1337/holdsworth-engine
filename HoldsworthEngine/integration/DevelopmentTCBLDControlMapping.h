#pragma once

#include "../dsp/TCBLDCleanBoostProcessor.h"

#include <algorithm>

namespace holdsworth::integration
{

// The temporary UI follows the conventional left=cut, right=boost direction.
// M1 P2/P3 use the opposite electrical/netlist coordinate, so only the
// wrapper mapping is reversed. Gain already follows the desired UI direction.
[[nodiscard]] constexpr dsp::TCBLDCleanBoostControls
tcBldControlsFromDevelopmentUI(const double gain,
                               const double bass,
                               const double treble) noexcept
{
  const double userGain = std::clamp(gain, 0.0, 1.0);
  const double userBass = std::clamp(bass, 0.0, 1.0);
  const double userTreble = std::clamp(treble, 0.0, 1.0);
  return {userGain, 1.0 - userBass, 1.0 - userTreble};
}

} // namespace holdsworth::integration
