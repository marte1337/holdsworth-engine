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
  mMinimumDelayTimeMs = minimumDelayTimeMs;
  mMaximumBlockSize = maximumBlockSize;
  mPrepared = true;
  applyEffectiveDelayTime();
}

void DelayBand::reset() noexcept
{
  mDelayLine.reset();
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

  for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
  {
    // Capture input before writing either output so exact input/output aliasing
    // remains safe.
    const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
    const Sample delayed = mDelayLine.readDelayedSample();
    const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
    mDelayLine.pushSample(lineInput);

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
}

void DelayBand::applyEffectiveDelayTime() noexcept
{
  const Sample effectiveDelayTimeMs = mPrepared
                                        ? std::max(mRequestedDelayTimeMs, mMinimumDelayTimeMs)
                                        : mRequestedDelayTimeMs;
  mDelayLine.setDelayTimeMs(effectiveDelayTimeMs);
}

void DelayBand::updatePanGains() noexcept
{
  const StereoGains gains = equalPowerPanGains(mPan);
  mLeftPanGain = gains.left;
  mRightPanGain = gains.right;
}

} // namespace holdsworth::dsp
