#pragma once

namespace holdsworth::presets
{

// A TAP percentage transcribed from Yamaha source material. This is metadata
// only and deliberately provides no conversion to the normalized DSP
// TapFraction type.
struct YamahaTapPercentValue final
{
  explicit constexpr YamahaTapPercentValue(const double percent = 100.0) noexcept
  : value(percent)
  {
  }

  double value = 100.0;
};

} // namespace holdsworth::presets
