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

// Exact Yamaha WAVE display choices. These source values deliberately do not
// convert to the DSP's provisional ModulationWaveform representation.
enum class YamahaWaveformControlValue
{
  sine,
  triangle,
  sawUp,
  sawDown
};

// Exact Yamaha PHASE display choices for the delay sound. This is deliberately
// unrelated to the oscillator's ModulationPhaseCycles configuration.
enum class YamahaDelaySignalPhaseControlValue
{
  normal,
  reverse
};

} // namespace holdsworth::presets
