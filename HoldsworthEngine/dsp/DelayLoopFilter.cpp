#include "DelayLoopFilter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{

void DelayLoopFilter::prepare(const double sampleRate)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::invalid_argument("sampleRate must be finite and greater than zero");

  mSampleRate = sampleRate;
  mPrepared = true;
  updateLowCutDerivedState();
  updateHighCutDerivedState();
  reset();
}

void DelayLoopFilter::reset() noexcept
{
  mLowCutState = 0.0;
  mHighCutState = 0.0;
}

void DelayLoopFilter::setConfiguration(
  const DelayLoopFilterConfiguration& configuration) noexcept
{
  setLowCut(configuration.lowCut);
  setHighCut(configuration.highCut);
}

void DelayLoopFilter::setLowCut(std::optional<LowCutFrequencyHz> cutoff) noexcept
{
  cutoff = sanitizeLowCut(cutoff);
  if (sameCutoff(mRequestedConfiguration.lowCut, cutoff))
    return;

  const bool wasEnabled = mRequestedConfiguration.lowCut.has_value();
  mRequestedConfiguration.lowCut = cutoff;

  if (!cutoff.has_value())
  {
    mLowCutState = 0.0;
  }
  else if (!wasEnabled)
  {
    // An OFF section has no dormant history to restore.
    mLowCutState = 0.0;
  }

  updateLowCutDerivedState();
}

void DelayLoopFilter::setHighCut(std::optional<HighCutFrequencyHz> cutoff) noexcept
{
  cutoff = sanitizeHighCut(cutoff);
  if (sameCutoff(mRequestedConfiguration.highCut, cutoff))
    return;

  const bool wasEnabled = mRequestedConfiguration.highCut.has_value();
  mRequestedConfiguration.highCut = cutoff;

  if (!cutoff.has_value())
  {
    mHighCutState = 0.0;
  }
  else if (!wasEnabled)
  {
    // An OFF section has no dormant history to restore.
    mHighCutState = 0.0;
  }

  updateHighCutDerivedState();
}

DelayLoopFilter::Sample DelayLoopFilter::processSample(const Sample input) noexcept
{
  assert(mPrepared && "prepare() must precede processSample()");
  if (!mPrepared)
    return input;

  Sample output = input;
  if (mEffectiveConfiguration.lowCut.has_value())
    output = processHighPass(output, mLowCutCoefficient, mLowCutState);
  if (mEffectiveConfiguration.highCut.has_value())
    output = processLowPass(output, mHighCutCoefficient, mHighCutState);
  return output;
}

std::optional<LowCutFrequencyHz>
DelayLoopFilter::sanitizeLowCut(const std::optional<LowCutFrequencyHz> cutoff) noexcept
{
  if (!cutoff.has_value() || !std::isfinite(cutoff->value) || cutoff->value <= 0.0)
    return std::nullopt;
  return cutoff;
}

std::optional<HighCutFrequencyHz>
DelayLoopFilter::sanitizeHighCut(const std::optional<HighCutFrequencyHz> cutoff) noexcept
{
  if (!cutoff.has_value() || !std::isfinite(cutoff->value) || cutoff->value <= 0.0)
    return std::nullopt;
  return cutoff;
}

bool DelayLoopFilter::sameCutoff(const std::optional<LowCutFrequencyHz>& first,
                                 const std::optional<LowCutFrequencyHz>& second) noexcept
{
  return first.has_value() == second.has_value()
         && (!first.has_value() || first->value == second->value);
}

bool DelayLoopFilter::sameCutoff(const std::optional<HighCutFrequencyHz>& first,
                                 const std::optional<HighCutFrequencyHz>& second) noexcept
{
  return first.has_value() == second.has_value()
         && (!first.has_value() || first->value == second->value);
}

void DelayLoopFilter::updateLowCutDerivedState() noexcept
{
  if (!mRequestedConfiguration.lowCut.has_value())
  {
    mEffectiveConfiguration.lowCut.reset();
    mLowCutCoefficient = 0.0;
    return;
  }

  const Sample effectiveCutoff = mPrepared
                                   ? clampCutoff(mRequestedConfiguration.lowCut->value)
                                   : mRequestedConfiguration.lowCut->value;
  mEffectiveConfiguration.lowCut = LowCutFrequencyHz{effectiveCutoff};
  mLowCutCoefficient = mPrepared ? coefficientForCutoff(effectiveCutoff) : 0.0;
}

void DelayLoopFilter::updateHighCutDerivedState() noexcept
{
  if (!mRequestedConfiguration.highCut.has_value())
  {
    mEffectiveConfiguration.highCut.reset();
    mHighCutCoefficient = 0.0;
    return;
  }

  const Sample effectiveCutoff = mPrepared
                                   ? clampCutoff(mRequestedConfiguration.highCut->value)
                                   : mRequestedConfiguration.highCut->value;
  mEffectiveConfiguration.highCut = HighCutFrequencyHz{effectiveCutoff};
  mHighCutCoefficient = mPrepared ? coefficientForCutoff(effectiveCutoff) : 0.0;
}

DelayLoopFilter::Sample DelayLoopFilter::clampCutoff(const Sample cutoffHz) const noexcept
{
  const Sample minimumCutoff = mSampleRate * kMinimumCutoffToSampleRateRatio;
  const Sample maximumCutoff = mSampleRate * kMaximumCutoffToSampleRateRatio;
  return std::clamp(cutoffHz, minimumCutoff, maximumCutoff);
}

DelayLoopFilter::Sample DelayLoopFilter::coefficientForCutoff(const Sample cutoffHz) const noexcept
{
  const Sample g = std::tan(std::numbers::pi_v<Sample> * cutoffHz / mSampleRate);
  return g / (1.0 + g);
}

DelayLoopFilter::Sample DelayLoopFilter::processLowPass(const Sample input,
                                                        const Sample coefficient,
                                                        Sample& state) noexcept
{
  const Sample v = (input - state) * coefficient;
  const Sample low = v + state;
  state = low + v;
  return low;
}

DelayLoopFilter::Sample DelayLoopFilter::processHighPass(const Sample input,
                                                         const Sample coefficient,
                                                         Sample& state) noexcept
{
  const Sample v = (input - state) * coefficient;
  const Sample low = v + state;
  state = low + v;
  return input - low;
}

} // namespace holdsworth::dsp
