#include "MC402CleanBoostProcessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holdsworth::dsp
{
void MC402CleanBoostProcessor::prepare(double sampleRate, std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 768000.0 || maximumBlockSize == 0)
    throw std::invalid_argument("Invalid MC402 clean Boost sample rate/block contract");
  mMaximumBlockSize = maximumBlockSize;
  mRampSamples = static_cast<std::size_t>(std::ceil(kSmoothingSeconds * sampleRate));
  mPrepared = true;
  reset();
}

void MC402CleanBoostProcessor::setBoostDb(double boostDb) noexcept
{
  const double db = std::isfinite(boostDb) ? std::clamp(boostDb, 0.0, kMaximumBoostDb) : kDefaultBoostDb;
  mRequestedGain.store(db == 0.0 ? 1.0 : std::pow(10.0, db / 20.0), std::memory_order_relaxed);
}

void MC402CleanBoostProcessor::reset() noexcept
{
  mGain = mTarget = mRequestedGain.load(std::memory_order_relaxed);
  mRemaining = 0;
  mIncrement = 0.0;
}

void MC402CleanBoostProcessor::processBlock(std::span<const Sample> input, std::span<Sample> output) noexcept
{
  assert(mPrepared && input.size() == output.size() && input.size() <= mMaximumBlockSize);
  if (!mPrepared || input.size() != output.size() || input.size() > mMaximumBlockSize)
  {
    if (input.data() != output.data())
      std::copy_n(input.data(), std::min(input.size(), output.size()), output.data());
    return;
  }
  const double requested = mRequestedGain.load(std::memory_order_relaxed);
  if (requested != mTarget)
  {
    mTarget = requested;
    mRemaining = mRampSamples;
    mIncrement = (mTarget - mGain) / static_cast<double>(mRampSamples);
  }
  for (std::size_t i = 0; i < input.size(); ++i)
  {
    if (mRemaining != 0)
    {
      mGain += mIncrement;
      if (--mRemaining == 0)
        mGain = mTarget;
    }
    const double x = input[i];
    if (!std::isfinite(x))
      output[i] = 0.0;
    else if (mGain == 1.0)
      output[i] = x; // Exact finite identity, including signed zero/subnormals.
    else if (std::abs(x) > std::numeric_limits<double>::max() / mGain)
      output[i] = std::copysign(std::numeric_limits<double>::max(), x);
    else
      output[i] = x * mGain;
  }
}
} // namespace holdsworth::dsp
