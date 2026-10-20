#include "LegatoCompressorProcessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holdsworth::dsp
{
namespace
{
double bounded(double value, double low, double high, double fallback) noexcept
{ return std::isfinite(value) ? std::clamp(value, low, high) : fallback; }
constexpr double maximumGain = 1.5848931924611136; // 10^(4/20)
}

void LegatoCompressorProcessor::Ramp::snap(double next) noexcept
{ value = goal = next; step = 0.; remaining = 0; }
void LegatoCompressorProcessor::Ramp::target(double next, std::size_t length) noexcept
{
  if (next == goal) return;
  goal = next; remaining = length; step = (goal-value)/static_cast<double>(length);
}
double LegatoCompressorProcessor::Ramp::next() noexcept
{
  if (remaining && --remaining == 0) value = goal;
  else if (remaining) value += step;
  return value;
}

LegatoCompressorControls LegatoCompressorProcessor::sanitizeControls(LegatoCompressorControls c) noexcept
{
  c.calibratedThresholdDb = bounded(c.calibratedThresholdDb, -36., 0., -18.);
  c.normalizedThresholdDb = bounded(c.normalizedThresholdDb, -48., 0., -30.);
  c.attackMs = bounded(c.attackMs, 8., 30., 15.);
  c.releaseMs = bounded(c.releaseMs, 40., 180., 60.);
  c.makeupDb = bounded(c.makeupDb, -6., 4., 3.);
  return c;
}
LegatoCompressorProcessor::Target LegatoCompressorProcessor::makeTarget(LegatoCompressorControls c) const noexcept
{
  c = sanitizeControls(c);
  const double interval = static_cast<double>(mControlSamples)/mSampleRate;
  return {c, std::exp(-interval/(c.attackMs*.001)), std::exp(-interval/(c.releaseMs*.001)),
          c.makeupDb == 0. ? 1. : std::min(maximumGain, std::pow(10., c.makeupDb/20.))};
}
LegatoCompressorProcessor::LegatoCompressorProcessor() noexcept
{ mTarget = makeTarget({}); }
void LegatoCompressorProcessor::setControls(const LegatoCompressorControls& c) noexcept
{
  mMailbox[mWriter] = makeTarget(c);
  mWriter = mMiddle.exchange(mWriter|dirty, std::memory_order_acq_rel)&3U;
}
void LegatoCompressorProcessor::adoptTarget() noexcept
{
  if ((mMiddle.load(std::memory_order_acquire)&dirty) != 0)
  {
    mReader = mMiddle.exchange(mReader, std::memory_order_acq_rel)&3U;
    mTarget = mMailbox[mReader];
  }
}
void LegatoCompressorProcessor::prepare(double fs, std::size_t maximumBlockSize)
{
  if (!std::isfinite(fs) || fs < 8000. || fs > 768000. || maximumBlockSize == 0)
    throw std::invalid_argument("Invalid Legato Compressor rate/block contract");
  adoptTarget();
  mSampleRate = fs;
  mControlSamples = static_cast<std::size_t>(std::max(1., std::round(fs/controlHz)));
  mRampSamples = static_cast<std::size_t>(std::ceil(fs*transitionSeconds));
  mPowerCoefficient = std::exp(-1./(fs*detectorSeconds));
  mMaximumBlockSize = maximumBlockSize;
  mTarget = makeTarget(mTarget.controls);
  mPrepared = true;
  reset();
}
void LegatoCompressorProcessor::reset() noexcept
{
  adoptTarget();
  mEnable.snap(0.); // Requested enable always enters through the 10 ms ramp.
  mCalibratedThreshold.snap(mTarget.controls.calibratedThresholdDb);
  mNormalizedThreshold.snap(mTarget.controls.normalizedThresholdDb);
  mMakeup.snap(mTarget.makeupGain);
  mGain.snap(1.);
  mContext = {};
  mPower = mRequestedReductionDb = mReductionDb = 0.;
  mLevelDb = -180.; mAppliedGain = 1.; mControlPosition = 0;
}
void LegatoCompressorProcessor::beginBlock(LegatoDetectorContext context) noexcept
{
  if (!mPrepared) return;
  adoptTarget();
  context.scale = bounded(context.scale, 1.e-12, 1.e12, 1.);
  if (context.calibrated != mContext.calibrated)
  {
    // Power has incompatible units. Retain smoothed gain/interpolation so this
    // change cannot jump audio gain; the new detector reacquires causally.
    mPower = 0.; mLevelDb = -180.;
  }
  mContext = context;
  const auto& c = mTarget.controls;
  mEnable.target(c.enabled ? 1. : 0., mRampSamples);
  mCalibratedThreshold.target(c.calibratedThresholdDb, mRampSamples);
  mNormalizedThreshold.target(c.normalizedThresholdDb, mRampSamples);
  mMakeup.target(mTarget.makeupGain, mRampSamples);
}
double LegatoCompressorProcessor::reductionForLevel(double levelDb, double thresholdDb) noexcept
{
  if (!std::isfinite(levelDb) || !std::isfinite(thresholdDb)) return 0.;
  const double d = levelDb-thresholdDb;
  if (d <= -kneeDb*.5) return 0.;
  const double q = d < kneeDb*.5 ? .5*(d+kneeDb*.5)*(d+kneeDb*.5)/(2.*kneeDb) : .5*d;
  return std::clamp(q, 0., maximumReductionDb);
}
void LegatoCompressorProcessor::controlTick() noexcept
{
  const double referencePower = mContext.calibrated ? .775*.775 : 1.;
  mLevelDb = std::clamp(10.*std::log10(std::max(mPower/referencePower, 1.e-18)), -180., 180.);
  const double threshold = mContext.calibrated ? mCalibratedThreshold.value : mNormalizedThreshold.value;
  mRequestedReductionDb = reductionForLevel(mLevelDb, threshold);
  const double coefficient = mRequestedReductionDb > mReductionDb ? mTarget.attack : mTarget.release;
  mReductionDb = std::clamp(coefficient*mReductionDb + (1.-coefficient)*mRequestedReductionDb, 0., maximumReductionDb);
  if (mRequestedReductionDb == 0. && mReductionDb < 1.e-6) mReductionDb = 0.;
  mGain.target(mReductionDb == 0. ? 1. : std::pow(10., -mReductionDb/20.), mControlSamples);
}
double LegatoCompressorProcessor::processSample(double input) noexcept
{
  const double x = std::isfinite(input) ? input : 0.;
  if (!mPrepared) return x;
  mCalibratedThreshold.next(); mNormalizedThreshold.next();
  const double makeup = std::clamp(mMakeup.next(), 0., maximumGain);
  const double mix = std::clamp(mEnable.next(), 0., 1.);
  // Detector-only overflow bound, far above audio. No audio clipping threshold.
  const double magnitude = std::abs(x);
  const double detected = magnitude > 1.e6/mContext.scale ? 1.e6 : magnitude*mContext.scale;
  mPower = mPowerCoefficient*mPower + (1.-mPowerCoefficient)*detected*detected;
  if (mPower < 1.e-30) mPower = 0.;
  if (++mControlPosition == mControlSamples)
  { mControlPosition = 0; controlTick(); }
  const double gain = std::clamp(mGain.next()*makeup, 0., maximumGain);
  mAppliedGain = mix == 0. ? 1. : mix == 1. ? gain : 1.+mix*(gain-1.);
  if (mix == 0. || mAppliedGain == 1.) return x; // Exact finite dry identity.
  if (magnitude > std::numeric_limits<double>::max()/mAppliedGain)
    return std::copysign(std::numeric_limits<double>::max(), x); // Numerical overflow only.
  return x*mAppliedGain;
}
void LegatoCompressorProcessor::processBlock(std::span<const double> input, std::span<double> output,
                                            LegatoDetectorContext context) noexcept
{
  assert(mPrepared && input.size() == output.size() && input.size() <= mMaximumBlockSize);
  if (!mPrepared || input.size() != output.size() || input.size() > mMaximumBlockSize)
  {
    if (input.data() != output.data()) std::copy_n(input.data(), std::min(input.size(), output.size()), output.data());
    return;
  }
  beginBlock(context);
  for (std::size_t i = 0; i < input.size(); ++i) output[i] = processSample(input[i]);
}
LegatoCompressorDiagnostics LegatoCompressorProcessor::diagnostics() const noexcept
{ return {mPower, mLevelDb, mRequestedReductionDb, mReductionDb, mAppliedGain, mEnable.value, mContext.calibrated}; }
} // namespace holdsworth::dsp
