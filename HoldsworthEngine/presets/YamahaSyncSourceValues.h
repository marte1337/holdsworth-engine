#pragma once

#include <cstdint>

namespace holdsworth::presets
{

// Yamaha's documented Effect Band numbers. This source-domain identifier is
// deliberately distinct from the DSP engine's band identifiers and indices.
enum class YamahaEffectBandNumber : std::uint8_t
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

enum class YamahaSyncControlState
{
  independentSelf,
  synchronizedToBand
};

// Exact source metadata for Yamaha's numeric SYNC display. Selecting the
// current Effect Band is the documented independent/OFF state; selecting a
// different band synchronizes to that band. No conversion to a DSP phase
// relationship is provided here.
class YamahaSyncControlValue final
{
public:
  [[nodiscard]] static constexpr YamahaSyncControlValue independentSelf(
    const YamahaEffectBandNumber selfBand) noexcept
  {
    return YamahaSyncControlValue{YamahaSyncControlState::independentSelf, selfBand};
  }

  [[nodiscard]] static constexpr YamahaSyncControlValue synchronizedTo(
    const YamahaEffectBandNumber masterBand) noexcept
  {
    return YamahaSyncControlValue{YamahaSyncControlState::synchronizedToBand, masterBand};
  }

  [[nodiscard]] constexpr YamahaSyncControlState state() const noexcept { return mState; }
  [[nodiscard]] constexpr YamahaEffectBandNumber displayedBand() const noexcept
  {
    return mDisplayedBand;
  }

private:
  explicit constexpr YamahaSyncControlValue(const YamahaSyncControlState state,
                                            const YamahaEffectBandNumber displayedBand) noexcept
  : mState(state)
  , mDisplayedBand(displayedBand)
  {
  }

  YamahaSyncControlState mState;
  YamahaEffectBandNumber mDisplayedBand;
};

} // namespace holdsworth::presets
