#pragma once
#include "DevelopmentPreNAMSelector.h"
#include <algorithm>

namespace holdsworth::integration
{
// AH-specific extension of the existing four-choice selector, not a pedal
// framework. Zero-latency legacy switches retain their original exact path.
// Cross a raw-wire intermediate so only one pedal executes per input sample.
class DevelopmentAHOuterTransition final
{
public:
  using Choice = DevelopmentPreNAMProcessor;
  void prepare(double rate) noexcept
  { mHalfFade = static_cast<std::size_t>(std::max(1., std::round(rate*.005))); reset(); }
  void reset() noexcept { mSelector.reset(); mPhase = Phase::stable; mDestination = Choice::off; }
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
      if (current == requested || (current != Choice::jRockettAH && requested != Choice::jRockettAH))
        mSelector.beginBlock(requested, frames, tc, mc, ah, supported);
      else
      {
        mDestination = requested;
        mFadeLength = (current == Choice::off || requested == Choice::off) ? 2*mHalfFade : mHalfFade;
        mPosition = 0;
        if (current == Choice::off)
        {
          mSelector.beginBlock(requested, frames, tc, mc, ah, supported);
          mPhase = Phase::warm; mWarm = ah.latencySamples();
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
    if (!mBlockTransition) { mSelector.processSelected(samples, voltsToNam, tc, mc, ah); return; }
    for (double& sample : samples)
    {
      const double wire = sample * voltsToNam;
      double processed = sample;
      if (applied() == Choice::off) processed = wire;
      else mSelector.processSelected({&processed, 1}, voltsToNam, tc, mc, ah);
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
          if (mDestination == Choice::off) mPhase = Phase::stable;
          else if (mDestination == Choice::jRockettAH) { mPhase = Phase::warm; mWarm = ah.latencySamples(); }
          else mPhase = Phase::enter;
        }
        else mPhase = Phase::stable;
      }
    }
  }
private:
  enum class Phase { stable, leave, warm, enter };
  DevelopmentPreNAMSelector mSelector;
  Choice mDestination = Choice::off;
  Phase mPhase = Phase::stable;
  std::size_t mHalfFade = 240, mFadeLength = 480, mPosition = 0, mWarm = 0, mFrames = 0;
  bool mBlockTransition = false, mSupported = true;
};
}
