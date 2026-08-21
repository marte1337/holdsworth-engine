#include "DelayModulator.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{
namespace
{

[[nodiscard]] double triangleAtPhase(const double phaseCycles) noexcept
{
  if (phaseCycles < 0.25)
    return 4.0 * phaseCycles;
  if (phaseCycles < 0.75)
    return 2.0 - 4.0 * phaseCycles;
  return 4.0 * phaseCycles - 4.0;
}

// Yamaha names Saw Up and Saw Down for their documented audible pitch
// direction. Because decreasing delay time raises pitch, the provisional
// delay-time offset ramp moves in the opposite numerical direction.
[[nodiscard]] double provisionalNonSineValue(const ModulationWaveform waveform,
                                              const double phaseCycles) noexcept
{
  switch (waveform)
  {
    case ModulationWaveform::triangle:
      return triangleAtPhase(phaseCycles);
    case ModulationWaveform::sawUp:
      return 1.0 - 2.0 * phaseCycles;
    case ModulationWaveform::sawDown:
      return -1.0 + 2.0 * phaseCycles;
    case ModulationWaveform::sine:
      break;
  }

  return 0.0;
}

} // namespace

void DelayModulator::prepare(const double sampleRate)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::invalid_argument("sampleRate must be finite and greater than zero");

  mSampleRate = sampleRate;
  mPrepared = true;
  updateRateDerivedState();
  reset();
}

void DelayModulator::reset() noexcept
{
  mPhaseCycles = mResetPhase.value;
  synchronizeCacheFromAuthoritativePhase();
}

void DelayModulator::setRate(const ModulationRateHz rate) noexcept
{
  mRequestedRate.value = std::isfinite(rate.value) && rate.value >= 0.0 ? rate.value : 0.0;
  updateRateDerivedState();

  // A block-rate rate change must not inherit accumulated quadrature error.
  // Rebuild the cache from logical phase without changing that phase.
  synchronizeCacheFromAuthoritativePhase();
}

void DelayModulator::setDepth(const ModulationDepthMs depth) noexcept
{
  mDepth.value = std::isfinite(depth.value) && depth.value >= 0.0 ? depth.value : 0.0;
}

void DelayModulator::setWaveform(const ModulationWaveform waveform) noexcept
{
  switch (waveform)
  {
    case ModulationWaveform::sine:
    case ModulationWaveform::triangle:
    case ModulationWaveform::sawUp:
    case ModulationWaveform::sawDown:
      mWaveform = waveform;
      return;
  }

  mWaveform = ModulationWaveform::sine;
}

void DelayModulator::setPhase(const ModulationPhaseCycles phase) noexcept
{
  mResetPhase.value = std::isfinite(phase.value) ? wrapPhase(phase.value) : 0.0;
  mPhaseCycles = mResetPhase.value;
  synchronizeCacheFromAuthoritativePhase();
}

DelayModulator::Sample DelayModulator::nextOffsetMs() noexcept
{
  assert(mPrepared && "prepare() must precede nextOffsetMs()");
  if (!mPrepared)
    return 0.0;

  // Preserve the established sine arithmetic and advancement verbatim. In
  // particular, do not recompute sine from logical phase in the hot path.
  if (mWaveform == ModulationWaveform::sine)
  {
    const Sample offsetMs = mDepth.value * mSine;
    advanceOneSample();
    return offsetMs;
  }

  const Sample offsetMs =
    mDepth.value * provisionalNonSineValue(mWaveform, mPhaseCycles);
  advanceOneSample();
  return offsetMs;
}

DelayModulator::Sample DelayModulator::wrapPhase(const Sample phaseCycles) noexcept
{
  const Sample wrapped = phaseCycles - std::floor(phaseCycles);
  // Defend the [0, 1) invariant against an implementation returning a rounded
  // upper endpoint for an extreme finite input.
  return wrapped >= 1.0 ? 0.0 : wrapped;
}

void DelayModulator::updateRateDerivedState() noexcept
{
  if (mPrepared)
  {
    const Sample technicalMaximumRate = mSampleRate * kMaximumTechnicalCyclesPerSample;
    mEffectiveRate.value = std::min(mRequestedRate.value, technicalMaximumRate);
    mPhaseIncrementCycles = mEffectiveRate.value / mSampleRate;
  }
  else
  {
    // No sample-rate-dependent technical bound exists before prepare().
    mEffectiveRate = mRequestedRate;
    mPhaseIncrementCycles = 0.0;
  }

  const Sample angularIncrement = 2.0 * std::numbers::pi_v<Sample> * mPhaseIncrementCycles;
  mSineIncrement = std::sin(angularIncrement);
  mCosineIncrement = std::cos(angularIncrement);
}

void DelayModulator::synchronizeCacheFromAuthoritativePhase() noexcept
{
  const Sample angle = 2.0 * std::numbers::pi_v<Sample> * mPhaseCycles;
  mSine = std::sin(angle);
  mCosine = std::cos(angle);
  mSamplesSinceCacheSynchronization = 0;
}

void DelayModulator::advanceOneSample() noexcept
{
  // Authoritative logical phase advances exactly once; the cache is then
  // rotated to represent that same transition.
  mPhaseCycles += mPhaseIncrementCycles;
  if (mPhaseCycles >= 1.0)
    mPhaseCycles -= 1.0;

  // Rotate the cache by the same known increment. Cache error can never feed
  // back into mPhaseCycles.
  const Sample nextSine = mSine * mCosineIncrement + mCosine * mSineIncrement;
  const Sample nextCosine = mCosine * mCosineIncrement - mSine * mSineIncrement;
  mSine = nextSine;
  mCosine = nextCosine;

  ++mSamplesSinceCacheSynchronization;
  if (mSamplesSinceCacheSynchronization == kCacheResynchronizationInterval)
    synchronizeCacheFromAuthoritativePhase();
}

} // namespace holdsworth::dsp
