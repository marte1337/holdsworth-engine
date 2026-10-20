#pragma once

#include "../dsp/LegatoCompressorProcessor.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace holdsworth::integration
{
enum class LegatoControl { calibratedThreshold, normalizedThreshold, attack, release, makeup };
inline double legatoValue(LegatoControl kind, double position) noexcept
{
  const double p = std::isfinite(position) ? std::clamp(position, 0., 1.) :
    kind == LegatoControl::calibratedThreshold ? .5 : kind == LegatoControl::normalizedThreshold ? .375 :
    kind == LegatoControl::makeup ? .9 : kind == LegatoControl::attack ? std::log(15./8.)/std::log(30./8.) :
    std::log(60./40.)/std::log(180./40.);
  switch (kind)
  {
    case LegatoControl::calibratedThreshold: return -36.+36.*p;
    case LegatoControl::normalizedThreshold: return -48.+48.*p;
    case LegatoControl::attack: return 8.*std::pow(30./8., p);
    case LegatoControl::release: return 40.*std::pow(180./40., p);
    case LegatoControl::makeup: return -6.+10.*p;
  }
  return 0.;
}
inline double legatoPosition(LegatoControl kind, double value) noexcept
{
  switch (kind)
  {
    case LegatoControl::calibratedThreshold: return (value+36.)/36.;
    case LegatoControl::normalizedThreshold: return (value+48.)/48.;
    case LegatoControl::attack: return std::log(value/8.)/std::log(30./8.);
    case LegatoControl::release: return std::log(value/40.)/std::log(180./40.);
    case LegatoControl::makeup: return (value+6.)/10.;
  }
  return 0.;
}
inline dsp::LegatoDetectorContext legatoDetectorContext(bool calibrated, double hostToVolts,
                                                       double selectedInputGain) noexcept
{ return {calibrated, calibrated ? hostToVolts/selectedInputGain : 1.}; }

struct LegatoMeterSnapshot final
{
  double levelDb = -180., reductionDb = 0.;
  bool calibrated = false;
};
// One packed publication prevents level/reduction/unit tearing. MillidB is
// presentation precision only; audio diagnostics retain full double precision.
class LegatoMeterTransport final
{
public:
  void publish(const dsp::LegatoCompressorDiagnostics& d) noexcept
  {
    const auto level = static_cast<std::uint64_t>(std::llround(1000.*(std::clamp(d.levelDb,-180.,180.)+180.)));
    const auto reduction = static_cast<std::uint64_t>(std::llround(1000.*std::clamp(d.reductionDb,0.,6.)));
    mValue.store(level | (reduction << 19U) | (static_cast<std::uint64_t>(d.calibrated) << 32U), std::memory_order_relaxed);
  }
  [[nodiscard]] LegatoMeterSnapshot snapshot() const noexcept
  {
    const auto v = mValue.load(std::memory_order_relaxed);
    return {static_cast<double>(v & 0x7ffffU)*.001-180.,
            static_cast<double>((v >> 19U) & 0x1fffU)*.001, (v & (1ULL << 32U)) != 0};
  }
private:
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
  std::atomic<std::uint64_t> mValue{0};
};
} // namespace holdsworth::integration
