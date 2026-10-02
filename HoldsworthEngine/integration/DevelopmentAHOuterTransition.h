#pragma once
#include "DevelopmentPreNAMSelector.h"
#include <algorithm>
#include <array>

namespace holdsworth::integration
{
// AH-specific extension of the existing four-choice selector, not a pedal
// framework. Every route has 32 host samples of latency. Cross an aligned dry
// intermediate so only one pedal executes per input sample, even during fades.
class DevelopmentAHOuterTransition final
{
public:
  using Choice = DevelopmentPreNAMProcessor;
  static constexpr std::size_t latency = 32;
  void prepare(double rate) noexcept
  { mHalfFade = static_cast<std::size_t>(std::max(1., std::round(rate*.005))); reset(); }
  void reset() noexcept
  {
    mSelector.reset(); mPhase = Phase::stable; mDestination = Choice::off;
    mDryDelay = {}; mSelectedDelay = {}; mDryWrite = mSelectedWrite = 0;
    mBlockTransition = false;
  }
  [[nodiscard]] constexpr std::size_t latencySamples() const noexcept { return latency; }
  [[nodiscard]] Choice applied() const noexcept { return mSelector.applied(); }
  [[nodiscard]] bool transitioning() const noexcept { return mPhase != Phase::stable; }
  template<class TC, class MC, class AH>
  void beginBlock(Choice requested, std::size_t frames, TC& tc, MC& mc, AH& ah, bool supported = true) noexcept
  {
    mFrames = frames;
    mSupported = supported;
    if (requested == Choice::jRockettAH && (!supported || !ah.isPrepared() || frames > ah.maximumBlockSize()))
      requested = Choice::off;
    const auto current = applied();
    if (mPhase == Phase::stable)
    {
      if (current == requested)
        mSelector.beginBlock(requested, frames, tc, mc, ah, supported);
      else
      {
        mDestination = requested;
        mFadeLength = (current == Choice::off || requested == Choice::off) ? 2*mHalfFade : mHalfFade;
        mPosition = 0;
        if (current == Choice::off)
        {
          mSelector.beginBlock(requested, frames, tc, mc, ah, supported);
          mSelectedDelay = {}; mSelectedWrite = 0;
          mPhase = Phase::warm; mWarm = latency;
        }
        else mPhase = Phase::leave;
      }
    }
    mBlockTransition = transitioning();
  }
  [[nodiscard]] double inputGain(double stockGain, double hostToVolts) const noexcept
  { return mBlockTransition ? hostToVolts : mSelector.inputGain(stockGain, hostToVolts); }
  template<class TC, class MC, class AH>
  void processSelected(std::span<double> samples, double voltsToNam, TC& tc, MC& mc, AH& ah) noexcept
  {
    if (!mBlockTransition)
    {
      // Record dry history before in-place processing. Preserve the original
      // settled calibration/DSP arithmetic, then delay only the legacy routes.
      for (double sample : samples)
        delay(mDryDelay, mDryWrite, applied() == Choice::off ? sample : sample * voltsToNam);
      mSelector.processSelected(samples, voltsToNam, tc, mc, ah);
      if (applied() != Choice::jRockettAH)
        for (double& sample : samples) sample = delay(mSelectedDelay, mSelectedWrite, sample);
      return;
    }
    for (double& sample : samples)
    {
      const double wire = delay(mDryDelay, mDryWrite, sample * voltsToNam);
      double processed = sample;
      if (applied() == Choice::off) processed = wire;
      else
      {
        mSelector.processSelected({&processed, 1}, voltsToNam, tc, mc, ah);
        if (applied() != Choice::jRockettAH)
          processed = delay(mSelectedDelay, mSelectedWrite, processed);
      }
      // Off uses the aligned dry history during transitions. Keep the settled
      // Off ring current as well, including a fade ending inside this block.
      if (applied() == Choice::off) delay(mSelectedDelay, mSelectedWrite, sample * voltsToNam);
      const double weight = static_cast<double>(mPosition) / static_cast<double>(mFadeLength);
      if (mPhase == Phase::leave) sample = (1.-weight)*processed + weight*wire;
      else if (mPhase == Phase::enter) sample = (1.-weight)*wire + weight*processed;
      else sample = mPhase == Phase::warm ? wire : processed;
      if (mPhase == Phase::warm)
      {
        if (--mWarm == 0) { mPhase = Phase::enter; mPosition = 0; }
      }
      else if ((mPhase == Phase::enter || mPhase == Phase::leave) && ++mPosition == mFadeLength)
      {
        if (mPhase == Phase::leave)
        {
          mSelector.beginBlock(mDestination, mFrames, tc, mc, ah, mSupported);
          mPosition = 0;
          if (mDestination == Choice::off)
          {
            mSelectedDelay = mDryDelay; mSelectedWrite = mDryWrite;
            mPhase = Phase::stable;
          }
          else
          {
            mSelectedDelay = {}; mSelectedWrite = 0;
            mPhase = Phase::warm; mWarm = latency;
          }
        }
        else mPhase = Phase::stable;
      }
    }
  }
private:
  static double delay(std::array<double, latency>& history, std::size_t& write, double input) noexcept
  {
    const double result = history[write];
    history[write] = input;
    write = (write + 1) % latency;
    return result;
  }
  enum class Phase { stable, leave, warm, enter };
  DevelopmentPreNAMSelector mSelector;
  Choice mDestination = Choice::off;
  Phase mPhase = Phase::stable;
  std::size_t mHalfFade = 240, mFadeLength = 480, mPosition = 0, mWarm = 0, mFrames = 0;
  bool mBlockTransition = false, mSupported = true;
  std::array<double, latency> mDryDelay{}, mSelectedDelay{};
  std::size_t mDryWrite = 0, mSelectedWrite = 0;
};
}
