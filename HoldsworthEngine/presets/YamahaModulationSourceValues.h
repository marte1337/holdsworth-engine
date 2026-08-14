#pragma once

namespace holdsworth::presets
{

// Values transcribed from Yamaha's modulation controls. They are source
// metadata only: no SPEED-to-hertz or DEPTH-to-milliseconds mapping is known.
struct YamahaSpeedControlValue final
{
  explicit constexpr YamahaSpeedControlValue(const double controlValue = 0.0)
  : value(controlValue)
  {
  }

  double value = 0.0;
};

struct YamahaDepthControlValue final
{
  explicit constexpr YamahaDepthControlValue(const double controlValue = 0.0)
  : value(controlValue)
  {
  }

  double value = 0.0;
};

} // namespace holdsworth::presets
