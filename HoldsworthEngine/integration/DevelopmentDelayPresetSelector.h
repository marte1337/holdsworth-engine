#pragma once

#include "../dsp/HoldsworthDelayPresets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace holdsworth::integration
{

// Session-local choices exposed by the temporary development UI. These are
// deliberately not plugin parameters and have no serialized representation.
// The 922, 9.13 CONNECT, and GROUP 900 diagnostics remain available as DSP
// presets but are intentionally absent from this milestone's live selector.
enum class DevelopmentDelayPreset : std::uint32_t
{
  lead121 = 0,
  chorus011 = 1,
  chorus031 = 2,
  holdsworth223 = 3,
  group12Rhythm1200 = 4
};

inline constexpr std::uint32_t kDevelopmentDelayPresetCount = 5;

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
    case static_cast<std::uint32_t>(DevelopmentDelayPreset::holdsworth223):
      return DevelopmentDelayPreset::holdsworth223;
    case static_cast<std::uint32_t>(DevelopmentDelayPreset::group12Rhythm1200):
      return DevelopmentDelayPreset::group12Rhythm1200;
    case static_cast<std::uint32_t>(DevelopmentDelayPreset::lead121):
    default:
      return DevelopmentDelayPreset::lead121;
  }
}

// Temporary tab controls communicate a normalized value. With five choices,
// the exact wire values progress from 0.0 through 1.0 in 0.25 steps.
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
    case DevelopmentDelayPreset::group12Rhythm1200:
      return dsp::presets::group12Rhythm1200DiagnosticV1();
    case DevelopmentDelayPreset::holdsworth223:
      return dsp::presets::holdsworth223ProvisionalV1();
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
inline dsp::HoldsworthDelayConfigurationApplyResult applyDevelopmentDelayPreset(
  dsp::HoldsworthDelayEngine& engine,
  const DevelopmentDelayPreset preset) noexcept
{
  return engine.applyConfiguration(developmentDelayPresetDefinition(preset).dspConfiguration);
}

} // namespace holdsworth::integration
