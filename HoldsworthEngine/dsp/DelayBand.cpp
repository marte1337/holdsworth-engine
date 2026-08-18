#include "DelayBand.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{
namespace
{

struct StereoGains
{
  double left;
  double right;
};

[[nodiscard]] StereoGains equalPowerPanGains(const double pan) noexcept
{
  if (pan <= -1.0)
    return {1.0, 0.0};
  if (pan >= 1.0)
    return {0.0, 1.0};

  const double angle = (pan + 1.0) * std::numbers::pi_v<double> * 0.25;
  return {std::cos(angle), std::sin(angle)};
}

} // namespace

DelayBand::DelayBand(const double maximumDelayTimeMs)
: mDelayLine(maximumDelayTimeMs)
{
  updatePanGains();
}

void DelayBand::prepare(const double sampleRate, const std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::invalid_argument("sampleRate must be finite and greater than zero");
  if (maximumBlockSize == 0)
    throw std::invalid_argument("maximumBlockSize must be greater than zero");

  double minimumDelayTimeMs = 1000.0 / sampleRate;
  if (minimumDelayTimeMs * sampleRate * 0.001 < 1.0)
    minimumDelayTimeMs = std::nextafter(minimumDelayTimeMs, std::numeric_limits<double>::infinity());
  if (maximumDelayTimeMs() < minimumDelayTimeMs)
    throw std::invalid_argument("maximumDelayTimeMs must accommodate at least one sample");

  mDelayLine.prepare(sampleRate, maximumBlockSize);
  mDelayModulator.prepare(sampleRate);
  mLoopFilter.prepare(sampleRate);
  mMinimumDelayTimeMs = minimumDelayTimeMs;
  mMaximumBlockSize = maximumBlockSize;
  mPrepared = true;
  applyEffectiveDelayTime();
}

void DelayBand::reset() noexcept
{
  mDelayLine.reset();
  mDelayModulator.reset();
  mLoopFilter.reset();
  mCurrentModulatedDelayTimeMs = delayTimeMs();
}

void DelayBand::setDelayTimeMs(const Sample delayTimeMs) noexcept
{
  mRequestedDelayTimeMs = std::isfinite(delayTimeMs)
                            ? std::clamp(delayTimeMs, 0.0, maximumDelayTimeMs())
                            : 0.0;
  applyEffectiveDelayTime();
}

void DelayBand::setFeedbackCoefficient(const Sample coefficient) noexcept
{
  mFeedbackCoefficient = std::isfinite(coefficient)
                           ? std::clamp(coefficient, 0.0, kMaximumFeedbackCoefficient)
                           : 0.0;
}

void DelayBand::setOutputLevel(const Sample level) noexcept
{
  mOutputLevel = std::isfinite(level) ? std::clamp(level, 0.0, 1.0) : 0.0;
}

void DelayBand::setPan(const Sample pan) noexcept
{
  mPan = std::isfinite(pan) ? std::clamp(pan, -1.0, 1.0) : 0.0;
  updatePanGains();
}

void DelayBand::setModulationRate(const ModulationRateHz rate) noexcept
{
  mDelayModulator.setRate(rate);
}

void DelayBand::setModulationDepth(const ModulationDepthMs depth) noexcept
{
  mRequestedModulationDepth.value =
    std::isfinite(depth.value) && depth.value >= 0.0 ? depth.value : 0.0;
  applyEffectiveModulationDepth();
}

void DelayBand::setModulationPhase(const ModulationPhaseCycles phase) noexcept
{
  mDelayModulator.setPhase(phase);
  mCurrentModulatedDelayTimeMs = delayTimeMs();
}

void DelayBand::setLoopFilterConfiguration(
  const DelayLoopFilterConfiguration& configuration) noexcept
{
  mLoopFilter.setConfiguration(configuration);
}

void DelayBand::processBlock(const std::span<const Sample> monoInput,
                             const std::span<Sample> wetLeft,
                             const std::span<Sample> wetRight) noexcept
{
  const bool outputsAreDistinct = monoInput.empty() || wetLeft.data() != wetRight.data();
  const bool validCall = mPrepared && monoInput.size() == wetLeft.size()
                         && monoInput.size() == wetRight.size()
                         && monoInput.size() <= mMaximumBlockSize && outputsAreDistinct;
  assert(validCall && "prepare() must precede processBlock(), and spans must satisfy the prepared contract");
  if (!validCall)
  {
    std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
    std::fill(wetRight.begin(), wetRight.end(), 0.0);
    return;
  }

  const Sample leftOutputGain = mOutputLevel * mLeftPanGain;
  const Sample rightOutputGain = mOutputLevel * mRightPanGain;

  // Both-filter-OFF is an explicit legacy path. Keep the established static
  // and moving recurrences operationally unchanged: no nominally-neutral
  // filter is called and no extra arithmetic can perturb Lead 121, Chorus 011,
  // or any other existing configuration.
  if (mLoopFilter.isBypassed())
  {
    // Keep the established static-delay recurrence in a dedicated path. The
    // modulator still advances once per sample, but no moving-read arithmetic
    // can perturb a zero-depth band's samples.
    if (mEffectiveModulationDepth.value == 0.0)
    {
      for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
      {
        // Capture input before writing either output so exact input/output
        // aliasing remains safe.
        const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
        const Sample delayed = mDelayLine.readDelayedSample();
        const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
        mDelayLine.pushSample(lineInput);
        static_cast<void>(mDelayModulator.nextOffsetMs());

        if (mEnabled)
        {
          wetLeft[frame] = delayed * leftOutputGain;
          wetRight[frame] = delayed * rightOutputGain;
        }
        else
        {
          wetLeft[frame] = 0.0;
          wetRight[frame] = 0.0;
        }
      }

      mCurrentModulatedDelayTimeMs = delayTimeMs();
      return;
    }

    const Sample baseDelayTimeMs = delayTimeMs();
    const Sample maximumDelay = maximumDelayTimeMs();
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      // DelayModulator advances exactly once for every processed sample, across
      // both block and enabled-state boundaries.
      const Sample modulationOffsetMs = mDelayModulator.nextOffsetMs();
      const Sample movingDelayTimeMs =
        std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

      // The moving tap is read before the conventional external feedback write:
      // lineInput[n] = enabledInput[n] + feedback * delayed[n].
      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample delayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
      const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
      mDelayLine.pushSample(lineInput);
      mCurrentModulatedDelayTimeMs = movingDelayTimeMs;

      if (mEnabled)
      {
        wetLeft[frame] = delayed * leftOutputGain;
        wetRight[frame] = delayed * rightOutputGain;
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    return;
  }

  // Active-filter static recurrence. The first delayed sample is filtered
  // before both output and feedback, so every repeat accumulates filtering.
  if (mEffectiveModulationDepth.value == 0.0)
  {
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample rawDelayed = mDelayLine.readDelayedSample();
      const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
      const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
      mDelayLine.pushSample(lineInput);
      static_cast<void>(mDelayModulator.nextOffsetMs());

      if (mEnabled)
      {
        wetLeft[frame] = filteredDelayed * leftOutputGain;
        wetRight[frame] = filteredDelayed * rightOutputGain;
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    mCurrentModulatedDelayTimeMs = delayTimeMs();
    return;
  }

  // Active-filter moving recurrence. Preserve the established modulation/read
  // order and insert filtering only between the delay read and feedback write.
  const Sample baseDelayTimeMs = delayTimeMs();
  const Sample maximumDelay = maximumDelayTimeMs();
  for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
  {
    // DelayModulator advances exactly once for every processed sample, across
    // both block and enabled-state boundaries.
    const Sample modulationOffsetMs = mDelayModulator.nextOffsetMs();
    const Sample movingDelayTimeMs =
      std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

    // The moving tap is read before the conventional external feedback write:
    // lineInput[n] = enabledInput[n] + feedback * delayed[n].
    const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
    const Sample rawDelayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
    const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
    const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
    mDelayLine.pushSample(lineInput);
    mCurrentModulatedDelayTimeMs = movingDelayTimeMs;

    if (mEnabled)
    {
      wetLeft[frame] = filteredDelayed * leftOutputGain;
      wetRight[frame] = filteredDelayed * rightOutputGain;
    }
    else
    {
      wetLeft[frame] = 0.0;
      wetRight[frame] = 0.0;
    }
  }
}

void DelayBand::applyEffectiveDelayTime() noexcept
{
  const Sample effectiveDelayTimeMs = mPrepared
                                        ? std::max(mRequestedDelayTimeMs, mMinimumDelayTimeMs)
                                        : mRequestedDelayTimeMs;
  mDelayLine.setDelayTimeMs(effectiveDelayTimeMs);
  mCurrentModulatedDelayTimeMs = effectiveDelayTimeMs;
  applyEffectiveModulationDepth();
}

void DelayBand::applyEffectiveModulationDepth() noexcept
{
  if (!mPrepared)
  {
    mEffectiveModulationDepth = ModulationDepthMs{};
    mDelayModulator.setDepth(mEffectiveModulationDepth);
    return;
  }

  // Limit depth symmetrically: the requested base remains the modulation
  // center rather than shifting near a boundary. This also guarantees every
  // moving feedback read remains in the one-sample-to-maximum history range.
  const Sample baseDelayTimeMs = delayTimeMs();
  const Sample capacityBelow = std::max(baseDelayTimeMs - mMinimumDelayTimeMs, 0.0);
  const Sample capacityAbove = std::max(maximumDelayTimeMs() - baseDelayTimeMs, 0.0);
  const Sample maximumSymmetricDepth = std::min(capacityBelow, capacityAbove);
  mEffectiveModulationDepth.value =
    std::min(mRequestedModulationDepth.value, maximumSymmetricDepth);
  mDelayModulator.setDepth(mEffectiveModulationDepth);
}

void DelayBand::updatePanGains() noexcept
{
  const StereoGains gains = equalPowerPanGains(mPan);
  mLeftPanGain = gains.left;
  mRightPanGain = gains.right;
}

} // namespace holdsworth::dsp
