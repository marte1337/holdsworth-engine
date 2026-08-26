#pragma once

#include <cstddef>
#include <cstdint>

namespace holdsworth::dsp
{

inline constexpr std::size_t kHoldsworthDelayBandCount = 8;

// One-based DSP band identity. This type is deliberately distinct from any
// Yamaha source-document band-number type and does not implicitly convert to
// a zero-based container index.
enum class DelayBandId : std::uint8_t
{
  band1 = 1,
  band2,
  band3,
  band4,
  band5,
  band6,
  band7,
  band8
};

} // namespace holdsworth::dsp
