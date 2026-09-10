#pragma once

#include <algorithm>
#include <cstdint>

namespace holdsworth::integration
{
inline constexpr double kDevelopmentGainDefault = 0.132430924210101;
inline constexpr double kDevelopmentToneDefault = 0.5;
// Existing wrapper startup value, in millionths. Keep Wet reset and startup shared.
inline constexpr std::uint32_t kDevelopmentWetDefaultEncoded = 100'000;
inline constexpr double kDevelopmentWetDefault = kDevelopmentWetDefaultEncoded / 1'000'000.0;

// Editor-local range state. The real value is stored separately from slider
// position so changing ranges never introduces a conversion round-trip.
struct DevelopmentGainRange
{
  double value = kDevelopmentGainDefault;
  bool focus = true;

  double minimum() const noexcept { return focus ? 0.10 : 0.0; }
  double maximum() const noexcept { return focus ? 0.30 : 1.0; }
  double position() const noexcept { return (value - minimum()) / (maximum() - minimum()); }
  void setPosition(double position) noexcept
  {
    position = std::clamp(position, 0.0, 1.0);
    value = position == 1.0 ? maximum() : minimum() + position * (maximum() - minimum());
  }
  bool setFocus(bool requested) noexcept
  {
    focus = requested;
    const double previous = value;
    value = std::clamp(value, minimum(), maximum());
    return value != previous;
  }
  void reset() noexcept { value = kDevelopmentGainDefault; focus = true; }
};
} // namespace holdsworth::integration
