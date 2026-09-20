#pragma once
#include "JRockettAHBoostProcessor.h"
#include "JRockettAHDriveProcessor.h"
#include <algorithm>
#include <cmath>

namespace holdsworth::dsp
{
// Concrete full AH behavioral pedal. The two frozen processors are unchanged.
// One UI producer; one audio consumer. prepare requires lifecycle exclusion.
// Control mailboxes and section flags remain safe during audio-owned resets.
class JRockettAHPedal final
{
public:
  static constexpr std::size_t latency = JRockettAHDriveProfile::latency;
  static constexpr double sectionFadeSeconds = .010;
  void setBoostControls(const JRockettAHBoostControls& c) noexcept { mBoost.setControls(c); }
  void setDriveControls(const JRockettAHDriveControls& c) noexcept { mDrive.setControls(c); }
  void setSections(bool boost, bool drive) noexcept
  { mSections.store((boost ? 1U : 0U) | (drive ? 2U : 0U), std::memory_order_release); }
  void prepare(double rate, std::size_t maximumBlockSize)
  {
    mBoost.prepare(rate, maximumBlockSize);
    mDrive.prepare(rate, maximumBlockSize);
    mFadeSamples = static_cast<std::size_t>(std::max(1., std::round(sectionFadeSeconds * rate)));
    mMaximumBlockSize = maximumBlockSize;
    reset();
  }
  void reset() noexcept
  {
    mBoost.reset(); mDrive.reset();
    const auto flags = mSections.load(std::memory_order_acquire);
    mBoostMix = {(flags & 1U) ? 1. : 0.};
    mDriveMix = {(flags & 2U) ? 1. : 0.};
    mBoostRunning = (flags & 1U) != 0;
    mDriveRunning = (flags & 2U) != 0;
    mDelay = {}; mWrite = mWarm = 0;
  }
  [[nodiscard]] bool isPrepared() const noexcept { return mDrive.isPrepared() && mBoost.isPrepared(); }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }
  [[nodiscard]] constexpr std::size_t latencySamples() const noexcept { return latency; }
  void processBlock(std::span<const double> input, std::span<double> output) noexcept
  {
    if (!isPrepared() || input.size() != output.size() || input.size() > mMaximumBlockSize) return;
    const auto flags = mSections.load(std::memory_order_acquire);
    const bool boostOn = (flags & 1U) != 0, driveOn = (flags & 2U) != 0;
    if (boostOn && !mBoostRunning) { mBoost.reset(); mBoostRunning = true; }
    if (driveOn && !mDriveRunning)
    { mDrive.reset(); mDriveRunning = true; mWarm = latency; }
    if (!driveOn) mWarm = 0;
    mBoostMix.target(boostOn ? 1. : 0., mFadeSamples);
    if (!mWarm) mDriveMix.target(driveOn ? 1. : 0., mFadeSamples);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
      const double raw = std::isfinite(input[i]) ? input[i] : 0.;
      double boosted = raw;
      if (mBoostRunning) mBoost.processBlock({&raw, 1}, {&boosted, 1});
      const double b = mBoostMix.next();
      const double driveInput = b == 0. ? raw : b == 1. ? boosted : (1.-b)*raw + b*boosted;
      const double aligned = mDelay[mWrite];
      mDelay[mWrite] = driveInput;
      mWrite = (mWrite + 1) % latency;
      double driven = 0.;
      if (mDriveRunning) mDrive.processBlock({&driveInput, 1}, {&driven, 1});
      if (mWarm)
      {
        --mWarm;
        if (!mWarm) mDriveMix.target(1., mFadeSamples);
      }
      const double d = mDriveMix.next();
      output[i] = d == 0. ? aligned : d == 1. ? driven : (1.-d)*aligned + d*driven;
      if (!boostOn && b == 0.) mBoostRunning = false;
      if (!driveOn && d == 0.) mDriveRunning = false;
    }
  }
private:
  struct Mix
  {
    double value = 0., goal = value, step = 0.;
    std::size_t remaining = 0;
    void target(double next, std::size_t length) noexcept
    {
      if (next == goal) return;
      goal = next; remaining = length; step = (goal-value)/static_cast<double>(length);
    }
    double next() noexcept
    {
      const double result = value;
      if (remaining && --remaining == 0) value = goal;
      else if (remaining) value += step;
      return result;
    }
  };
  JRockettAHBoostProcessor mBoost;
  JRockettAHDriveProcessor mDrive;
  std::atomic<std::uint32_t> mSections{1}; // retain the previous Boost-only default
  std::array<double, latency> mDelay{};
  Mix mBoostMix{}, mDriveMix{};
  std::size_t mWrite = 0, mWarm = 0, mFadeSamples = 480, mMaximumBlockSize = 0;
  bool mBoostRunning = false, mDriveRunning = false;
};
}
