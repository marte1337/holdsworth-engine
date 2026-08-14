#pragma once

#include "../dsp/HoldsworthDelayPresets.h"

#include <cstdint>

namespace holdsworth::integration
{

// Session-local choices exposed by the temporary development UI. These are
// deliberately not plugin parameters and have no serialized representation.
enum class DevelopmentDelayPreset : std::uint32_t
{
  lead121 = 0,
  chorus011 = 1
};

// Invalid wire values fail safely to the proven Lead 121 default.
[[nodiscard]] constexpr DevelopmentDelayPreset developmentDelayPresetFromIndex(
  const std::uint32_t index) noexcept
{
  return index == static_cast<std::uint32_t>(DevelopmentDelayPreset::chorus011)
           ? DevelopmentDelayPreset::chorus011
           : DevelopmentDelayPreset::lead121;
}

[[nodiscard]] inline const dsp::HoldsworthDelayPresetDefinition& developmentDelayPresetDefinition(
  const DevelopmentDelayPreset preset) noexcept
{
  switch (preset)
  {
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
