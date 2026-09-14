#pragma once
#include "../dsp/JRockettAHBoostProcessor.h"
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace holdsworth::integration
{
// Product realtime support; the wider isolated-DSP prepare range is not support.
inline constexpr std::array<double, 6> kDevelopmentAHRealtimeRates{
  44100.,48000.,88200.,96000.,176400.,192000.};
inline bool ahRealtimeRateSupported(double rate) noexcept
{
  return std::find(kDevelopmentAHRealtimeRates.begin(),kDevelopmentAHRealtimeRates.end(),rate)
         != kDevelopmentAHRealtimeRates.end();
}
inline dsp::JRockettAHBoostControls ahControlsFromDevelopmentUI(double boost, double type, double emphasis) noexcept
{
  const double level = std::isfinite(boost) ? std::clamp(boost,0.,1.)*20. : 0.;
  const auto t = !std::isfinite(type) || type<0. || type>1. ? 1U : type<.25 ? 0U : type<.75 ? 1U : 2U;
  const auto e = !std::isfinite(emphasis) || emphasis<0. || emphasis>1. ? 0U : emphasis<.5 ? 0U : 1U;
  return {level,static_cast<dsp::JRockettAHBoostType>(t),static_cast<dsp::JRockettAHEmphasis>(e)};
}
// Common payload validation used by the actual plugin message cases and tests.
inline bool ahReadControlMessage(int ctrlTag,int expectedTag,int size,const void* data,double& value) noexcept
{
  if(ctrlTag!=expectedTag || size!=static_cast<int>(sizeof(double)) || data==nullptr) return false;
  std::memcpy(&value,data,sizeof(value));
  return true;
}
} // namespace holdsworth::integration
