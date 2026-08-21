#pragma once

#include "YamahaSyncSourceValues.h"

#include <optional>

namespace holdsworth::presets
{

// Exact Yamaha Effect Band switch state. For a documented OFF row, the other
// source fields may remain unknown when Yamaha prints dashes for them.
enum class YamahaEffectBandSwitchState
{
  off,
  on
};

enum class YamahaConnectControlState
{
  input,
  effectBand
};

// Source-only representation of Yamaha's CONNECT display. This does not
// configure audio routing in HoldsworthDelayEngine.
class YamahaConnectControlValue final
{
public:
  [[nodiscard]] static constexpr YamahaConnectControlValue input() noexcept
  {
    return YamahaConnectControlValue{YamahaConnectControlState::input, std::nullopt};
  }

  [[nodiscard]] static constexpr YamahaConnectControlValue fromEffectBand(
    const YamahaEffectBandNumber sourceBand) noexcept
  {
    return YamahaConnectControlValue{YamahaConnectControlState::effectBand, sourceBand};
  }

  [[nodiscard]] constexpr YamahaConnectControlState state() const noexcept { return mState; }
  [[nodiscard]] constexpr std::optional<YamahaEffectBandNumber> sourceBand() const noexcept
  {
    return mSourceBand;
  }

private:
  explicit constexpr YamahaConnectControlValue(const YamahaConnectControlState state,
                                                const std::optional<YamahaEffectBandNumber>
                                                  sourceBand) noexcept
  : mState(state)
  , mSourceBand(sourceBand)
  {
  }

  YamahaConnectControlState mState;
  std::optional<YamahaEffectBandNumber> mSourceBand;
};

// Exact source-domain GROUP range printed as firstBand->lastBand. An
// individual Yamaha band is represented by equal endpoints (for example,
// Band 2 is 2->2). No GROUP DSP behavior is implied.
class YamahaGroupControlValue final
{
public:
  [[nodiscard]] static constexpr YamahaGroupControlValue individual(
    const YamahaEffectBandNumber band) noexcept
  {
    return YamahaGroupControlValue{band, band};
  }

  [[nodiscard]] static constexpr YamahaGroupControlValue range(
    const YamahaEffectBandNumber firstBand,
    const YamahaEffectBandNumber lastBand) noexcept
  {
    return YamahaGroupControlValue{firstBand, lastBand};
  }

  [[nodiscard]] constexpr YamahaEffectBandNumber firstBand() const noexcept
  {
    return mFirstBand;
  }

  [[nodiscard]] constexpr YamahaEffectBandNumber lastBand() const noexcept
  {
    return mLastBand;
  }

private:
  explicit constexpr YamahaGroupControlValue(const YamahaEffectBandNumber firstBand,
                                              const YamahaEffectBandNumber lastBand) noexcept
  : mFirstBand(firstBand)
  , mLastBand(lastBand)
  {
  }

  YamahaEffectBandNumber mFirstBand;
  YamahaEffectBandNumber mLastBand;
};

} // namespace holdsworth::presets
