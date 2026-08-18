#include "FractionalDelayLine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace holdsworth::dsp
{

FractionalDelayLine::FractionalDelayLine(const double maximumDelayTimeMs)
: mMaximumDelayTimeMs(maximumDelayTimeMs)
{
  if (!std::isfinite(maximumDelayTimeMs) || maximumDelayTimeMs < 0.0)
    throw std::invalid_argument("maximumDelayTimeMs must be finite and non-negative");
}

void FractionalDelayLine::prepare(const double sampleRate, const std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::invalid_argument("sampleRate must be finite and greater than zero");
  if (maximumBlockSize == 0)
    throw std::invalid_argument("maximumBlockSize must be greater than zero");

  const double maximumDelayInSamples = mMaximumDelayTimeMs * sampleRate * 0.001;
  const double maximumBufferOffset = static_cast<double>(mBuffer.max_size() - 1);
  if (!std::isfinite(maximumDelayInSamples) || maximumDelayInSamples > maximumBufferOffset)
    throw std::length_error("requested delay storage exceeds vector capacity");

  // One additional element represents the current sample. The rounded-up
  // history length provides the older neighbor needed for linear interpolation.
  const auto historyLength = static_cast<std::size_t>(std::ceil(maximumDelayInSamples));
  std::vector<Sample> preparedBuffer(historyLength + 1, 0.0);

  mBuffer.swap(preparedBuffer);
  mSampleRate = sampleRate;
  mMaximumBlockSize = maximumBlockSize;
  mWriteIndex = 0;
  mPrepared = true;
  updateDelayInSamples();
}

void FractionalDelayLine::reset() noexcept
{
  std::fill(mBuffer.begin(), mBuffer.end(), 0.0);
  mWriteIndex = 0;
}

void FractionalDelayLine::setDelayTimeMs(const double delayTimeMs) noexcept
{
  mDelayTimeMs = std::isfinite(delayTimeMs) ? std::clamp(delayTimeMs, 0.0, mMaximumDelayTimeMs) : 0.0;
  updateDelayInSamples();
}

void FractionalDelayLine::processBlock(const std::span<const Sample> input,
                                       const std::span<Sample> output) noexcept
{
  const bool validCall = mPrepared && input.size() == output.size() && input.size() <= mMaximumBlockSize;
  assert(validCall && "prepare() must precede processBlock(), and block sizes must satisfy the prepared contract");
  if (!validCall)
  {
    std::fill(output.begin(), output.end(), 0.0);
    return;
  }

  const auto wholeSampleDelay = static_cast<std::size_t>(mDelayInSamples);
  const double fractionalDelay = mDelayInSamples - static_cast<double>(wholeSampleDelay);

  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    // Capture input before writing output so exact in-place processing is safe.
    const Sample currentInput = input[frame];
    mBuffer[mWriteIndex] = currentInput;

    output[frame] = interpolateHistory(wholeSampleDelay, fractionalDelay);

    ++mWriteIndex;
    if (mWriteIndex == mBuffer.size())
      mWriteIndex = 0;
  }
}

FractionalDelayLine::Sample FractionalDelayLine::readDelayedSample() const noexcept
{
  const bool validRead = mPrepared && mDelayInSamples >= 1.0;
  assert(validRead && "readDelayedSample() requires a prepared delay of at least one sample");
  if (!validRead)
    return 0.0;

  return readHistoryAtDelayInSamples(mDelayInSamples);
}

FractionalDelayLine::Sample
FractionalDelayLine::readDelayedSampleAtDelayTimeMs(const double delayTimeMs) const noexcept
{
  const double maximumDelayInSamples =
    mPrepared ? mMaximumDelayTimeMs * mSampleRate * 0.001 : 0.0;
  const bool validRead = mPrepared && maximumDelayInSamples >= 1.0;
  assert(validRead &&
         "readDelayedSampleAtDelayTimeMs() requires prepared capacity for at least one sample");
  if (!validRead)
    return 0.0;

  // This read is used by an external feedback loop, so the zero/sub-one-sample
  // feed-forward region is deliberately unavailable. Clamp in sample units to
  // make the lower bound exact despite millisecond conversion roundoff.
  const double requestedDelayInSamples =
    std::isfinite(delayTimeMs) ? delayTimeMs * mSampleRate * 0.001 : 1.0;
  const double effectiveDelayInSamples =
    std::clamp(requestedDelayInSamples, 1.0, maximumDelayInSamples);

  return readHistoryAtDelayInSamples(effectiveDelayInSamples);
}

FractionalDelayLine::Sample FractionalDelayLine::readDelayedSampleAtDelayTimeMs(
  const double delayTimeMs,
  const Sample currentSample) const noexcept
{
  const bool validRead = mPrepared;
  assert(validRead && "readDelayedSampleAtDelayTimeMs() requires a prepared delay line");
  if (!validRead)
    return 0.0;

  const double maximumDelayInSamples = mMaximumDelayTimeMs * mSampleRate * 0.001;
  const double requestedDelayInSamples =
    std::isfinite(delayTimeMs) ? delayTimeMs * mSampleRate * 0.001 : 0.0;
  const double effectiveDelayInSamples =
    std::clamp(requestedDelayInSamples, 0.0, maximumDelayInSamples);

  if (effectiveDelayInSamples < 1.0)
  {
    const Sample previousSample = mBuffer[indexBehindWriteHead(1)];
    return currentSample + (previousSample - currentSample) * effectiveDelayInSamples;
  }

  return readHistoryAtDelayInSamples(effectiveDelayInSamples);
}

FractionalDelayLine::Sample
FractionalDelayLine::readHistoryAtDelayInSamples(const double delayInSamples) const noexcept
{
  const auto wholeSampleDelay = static_cast<std::size_t>(delayInSamples);
  const double fractionalDelay = delayInSamples - static_cast<double>(wholeSampleDelay);
  return interpolateHistory(wholeSampleDelay, fractionalDelay);
}

FractionalDelayLine::Sample
FractionalDelayLine::interpolateHistory(const std::size_t wholeSampleDelay,
                                        const double fractionalDelay) const noexcept
{
  const Sample newerSample = mBuffer[indexBehindWriteHead(wholeSampleDelay)];

  if (fractionalDelay == 0.0)
    return newerSample;

  const Sample olderSample = mBuffer[indexBehindWriteHead(wholeSampleDelay + 1)];
  return newerSample + (olderSample - newerSample) * fractionalDelay;
}

void FractionalDelayLine::pushSample(const Sample sample) noexcept
{
  assert(mPrepared && "prepare() must precede pushSample()");
  if (!mPrepared)
    return;

  mBuffer[mWriteIndex] = sample;
  ++mWriteIndex;
  if (mWriteIndex == mBuffer.size())
    mWriteIndex = 0;
}

std::size_t FractionalDelayLine::indexBehindWriteHead(const std::size_t sampleOffset) const noexcept
{
  const std::size_t wrappedOffset = sampleOffset % mBuffer.size();
  return (mWriteIndex + mBuffer.size() - wrappedOffset) % mBuffer.size();
}

void FractionalDelayLine::updateDelayInSamples() noexcept
{
  mDelayInSamples = mPrepared ? mDelayTimeMs * mSampleRate * 0.001 : 0.0;
}

} // namespace holdsworth::dsp
