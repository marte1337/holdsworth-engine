#include "JRockettAHBoostProcessor.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{
JRockettAHBoostProcessor::Coefficients JRockettAHBoostProcessor::shelf(
  double db, double hz, double sampleRate, bool high) noexcept
{
  if (db == 0.0) return {}; // Exact wire, without hidden pole/zero cancellation.
  const double a = std::pow(10.0, db / 20.0), root = std::sqrt(a);
  const double k = std::tan(std::numbers::pi * hz / sampleRate);
  // Substitute s=(1-z^-1)/(1+z^-1); k is prewarped independently per shelf.
  const double pole = high ? k * root : k / root;
  const double zero = high ? k / root : k * root;
  const double scale = high ? a : 1.0;
  return {scale * (1.0 + zero) / (1.0 + pole),
          scale * (zero - 1.0) / (1.0 + pole), (pole - 1.0) / (1.0 + pole)};
}

double JRockettAHBoostProcessor::Response::tick(double input) noexcept
{
  for (std::size_t i = 0; i < state.size(); ++i)
  {
    const auto& c = coefficients[i];
    const double y = c.b0 * input + state[i];
    state[i] = c.b1 * input - c.a1 * y;
    input = y;
  }
  return input;
}

void JRockettAHBoostProcessor::setControls(const JRockettAHBoostControls& controls) noexcept
{
  const double db = std::isfinite(controls.boostDb)
    ? std::clamp(controls.boostDb, 0.0, Profile::maximumBoostDb) : Profile::defaultBoostDb;
  const auto type = static_cast<std::uint32_t>(controls.type);
  const auto emphasis = static_cast<std::uint32_t>(controls.emphasis);
  mMailbox[mWriter] = {std::pow(10.0, db / 20.0),
                      2 * (type < 3 ? type : 1) + (emphasis < 2 ? emphasis : 0)};
  // Producer owns writer; consumer owns reader. Exchange transfers only middle.
  mWriter = mMiddle.exchange(mWriter | kDirty, std::memory_order_acq_rel) & 3U;
}

void JRockettAHBoostProcessor::adoptTarget() noexcept
{
  if ((mMiddle.load(std::memory_order_acquire) & kDirty) != 0)
  {
    mReader = mMiddle.exchange(mReader, std::memory_order_acq_rel) & 3U;
    mTarget = mMailbox[mReader];
  }
}

void JRockettAHBoostProcessor::prepare(double sampleRate, std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate < Profile::minimumSampleRate ||
      sampleRate > Profile::maximumSampleRate || maximumBlockSize == 0)
    throw std::invalid_argument("Invalid J. Rockett AH Boost rate/block contract");
  for (std::size_t i = 0; i < mCoefficients.size(); ++i)
  {
    mCoefficients[i] = {shelf(Profile::shelves[i].lowDb, Profile::lowHz, sampleRate, false),
                       shelf(Profile::shelves[i].highDb, Profile::highHz, sampleRate, true)};
  }
  for (std::size_t mode = 0; mode < mReplay.size(); ++mode)
  {
    const auto& low = mCoefficients[mode][0];
    const auto& high = mCoefficients[mode][1];
    const double r0 = -low.a1, r1 = -high.a1;
    const double d0 = low.b1 - low.a1 * low.b0;
    const double d1 = high.b1 - high.a1 * high.b0;
    auto& block = mReplay[mode];
    double b0 = d0, b1 = d1 * low.b0;
    block.m00 = block.m11 = 1.0; block.m10 = 0.0;
    for (std::size_t i = 0; i < 8; ++i)
    {
      block.low[7-i] = b0; block.high[7-i] = b1;
      b1 = d1 * b0 + r1 * b1; b0 *= r0;
      block.m10 = d1 * block.m00 + r1 * block.m10;
      block.m00 *= r0; block.m11 *= r1;
    }
  }
  mHistoryLength = static_cast<std::size_t>(std::ceil(Profile::historySeconds * sampleRate));
  mFadeSamples = static_cast<std::size_t>(std::ceil(Profile::crossfadeSeconds * sampleRate));
  mRampSamples = static_cast<std::size_t>(std::ceil(Profile::levelSeconds * sampleRate));
  mMaximumBlockSize = maximumBlockSize;
  mPrepared = true;
  reset();
}

void JRockettAHBoostProcessor::reset() noexcept
{
  adoptTarget();
  mCurrentMode = mIncomingMode = mTarget.mode;
  mCurrent = {mCoefficients[mCurrentMode], {}};
  mIncoming = {};
  mGain = mGainTarget = mTarget.gain;
  mGainIncrement = 0.0;
  mRampRemaining = mFadePosition = 0;
  mFading = false;
  // Count invalidates history without clearing the large array in realtime.
  mHistoryCount = mHistoryWrite = 0;
}

void JRockettAHBoostProcessor::startTransition() noexcept
{
  mIncomingMode = mTarget.mode;
  mIncoming = {mCoefficients[mIncomingMode], {}};
  // Reconstruct the incoming response's recent history before it becomes audible.
  // At startup all available history is replayed. Later, the omitted IIR tail
  // is below the measured transition-error bound; this is not an audio delay.
  const std::size_t index = (mHistoryWrite + mHistoryLength - mHistoryCount) % mHistoryLength;
  const auto first = std::min(mHistoryCount, mHistoryLength - index);
  replay(mHistory.data() + index, first);
  replay(mHistory.data(), mHistoryCount - first);
  mFadePosition = 0;
  mFading = true;
}

void JRockettAHBoostProcessor::replay(const double* samples, std::size_t count) noexcept
{
  const auto& b = mReplay[mIncomingMode];
  std::size_t i = 0;
  for (; i + 8 <= count; i += 8)
  {
    // Independent pair sums shorten dependency chains without fast-math.
    const auto dot = [samples, i](const std::array<double, 8>& w) {
      const double p0 = w[0]*samples[i] + w[1]*samples[i+1];
      const double p1 = w[2]*samples[i+2] + w[3]*samples[i+3];
      const double p2 = w[4]*samples[i+4] + w[5]*samples[i+5];
      const double p3 = w[6]*samples[i+6] + w[7]*samples[i+7];
      return (p0+p1)+(p2+p3);
    };
    const double low = b.m00 * mIncoming.state[0] + dot(b.low);
    const double high = b.m10 * mIncoming.state[0] + b.m11 * mIncoming.state[1] + dot(b.high);
    mIncoming.state = {low, high};
  }
  for (; i < count; ++i) mIncoming.tick(samples[i]);
}

void JRockettAHBoostProcessor::remember(double input) noexcept
{
  mHistory[mHistoryWrite] = input;
  if (++mHistoryWrite == mHistoryLength) mHistoryWrite = 0;
  if (mHistoryCount < mHistoryLength) ++mHistoryCount;
}

void JRockettAHBoostProcessor::processBlock(std::span<const double> input,
                                         std::span<double> output) noexcept
{
  assert(mPrepared && input.size() == output.size() && input.size() <= mMaximumBlockSize);
  if (!mPrepared || input.size() != output.size() || input.size() > mMaximumBlockSize)
  {
    if (input.data() != output.data())
      std::copy_n(input.data(), std::min(input.size(), output.size()), output.data());
    return;
  }
  if (input.empty()) return;
  adoptTarget();
  if (mTarget.gain != mGainTarget)
  {
    mGainTarget = mTarget.gain;
    mRampRemaining = mRampSamples;
    mGainIncrement = (mGainTarget - mGain) / static_cast<double>(mRampSamples);
  }
  for (std::size_t i = 0; i < input.size(); ++i)
  {
    // Finish an uninterrupted fade, then start toward the latest pending mode.
    // No growing mixture/queue, starvation, cold restart or discontinuous retarget.
    if (!mFading && mCurrentMode != mTarget.mode) startTransition();
    // Purely numerical protection for extreme doubles, not modeled clipping.
    constexpr double limit = std::numeric_limits<double>::max() / 1024.0;
    const double x = std::isfinite(input[i]) ? std::clamp(input[i], -limit, limit) : 0.0;
    double y = mCurrent.tick(x);
    if (mFading)
    {
      const double next = mIncoming.tick(x);
      // Complementary linear weights: exact old/new endpoints, no equal-power bump.
      const double weight = static_cast<double>(mFadePosition) / static_cast<double>(mFadeSamples - 1);
      y = (1.0 - weight) * y + weight * next;
      if (++mFadePosition == mFadeSamples)
      {
        mCurrent = mIncoming;
        mCurrentMode = mIncomingMode;
        mFading = false;
      }
    }
    remember(x);
    if (mRampRemaining != 0)
    {
      mGain += mGainIncrement;
      if (--mRampRemaining == 0) mGain = mGainTarget;
    }
    output[i] = y * mGain;
  }
}
} // namespace holdsworth::dsp
