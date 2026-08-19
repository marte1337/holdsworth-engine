#pragma once

#include "../dsp/HoldsworthDelayPresets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace holdsworth::integration
{

// Session-local choices exposed by the temporary development UI. These are
// deliberately not plugin parameters and have no serialized representation.
enum class DevelopmentDelayPreset : std::uint32_t
{
  lead121 = 0,
  chorus011 = 1,
  chorus031 = 2
};

inline constexpr std::uint32_t kDevelopmentDelayPresetCount = 3;

// Invalid wire values fail safely to the proven Lead 121 default.
[[nodiscard]] constexpr DevelopmentDelayPreset developmentDelayPresetFromIndex(
  const std::uint32_t index) noexcept
{
  switch (index)
  {
    case static_cast<std::uint32_t>(DevelopmentDelayPreset::chorus011):
      return DevelopmentDelayPreset::chorus011;
    case static_cast<std::uint32_t>(DevelopmentDelayPreset::chorus031):
      return DevelopmentDelayPreset::chorus031;
    case static_cast<std::uint32_t>(DevelopmentDelayPreset::lead121):
    default:
      return DevelopmentDelayPreset::lead121;
  }
}

// Temporary tab controls communicate a normalized value. With three choices,
// the exact wire values are 0.0, 0.5, and 1.0.
[[nodiscard]] inline DevelopmentDelayPreset developmentDelayPresetFromNormalizedControlValue(
  const double normalizedValue) noexcept
{
  if (!std::isfinite(normalizedValue))
    return DevelopmentDelayPreset::lead121;

  constexpr double maximumIndex = static_cast<double>(kDevelopmentDelayPresetCount - 1U);
  const double scaledIndex = std::clamp(normalizedValue, 0.0, 1.0) * maximumIndex;
  return developmentDelayPresetFromIndex(static_cast<std::uint32_t>(std::lround(scaledIndex)));
}

[[nodiscard]] constexpr double developmentDelayPresetNormalizedControlValue(
  const DevelopmentDelayPreset preset) noexcept
{
  constexpr double maximumIndex = static_cast<double>(kDevelopmentDelayPresetCount - 1U);
  return static_cast<double>(static_cast<std::uint32_t>(preset)) / maximumIndex;
}

[[nodiscard]] inline const dsp::HoldsworthDelayPresetDefinition& developmentDelayPresetDefinition(
  const DevelopmentDelayPreset preset) noexcept
{
  switch (preset)
  {
    case DevelopmentDelayPreset::chorus031:
      return dsp::presets::chorus031ProvisionalV1();
    case DevelopmentDelayPreset::chorus011:
      return dsp::presets::chorus011ProvisionalV1();
    case DevelopmentDelayPreset::lead121:
    default:
      return dsp::presets::lead121UnmodulatedProvisional();
  }
}

// Applies the selected existing DSP configuration without resetting delay
// memory. Switching during an active tail can therefore produce a temporary
// hybrid/morphing tail, which is intentional for this audition milestone.
inline void applyDevelopmentDelayPreset(dsp::HoldsworthDelayEngine& engine,
                                        const DevelopmentDelayPreset preset) noexcept
{
  engine.applyConfiguration(developmentDelayPresetDefinition(preset).dspConfiguration);
}

} // namespace holdsworth::integration
