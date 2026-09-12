#include "MC402BoostOverdriveProcessor.h"
#include "MC402HalfBandCoefficients.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holdsworth::dsp
{
void MC402BoostOverdriveProcessor::Ramp::snap(double next) noexcept
{
  value = target = next;
  remaining = 0;
  increment = 0.0;
}

void MC402BoostOverdriveProcessor::Ramp::set(double next, std::size_t samples) noexcept
{
  if (next == target)
    return;
  target = next;
  remaining = samples;
  increment = samples == 0 ? 0.0 : (next - value) / static_cast<double>(samples);
  if (samples == 0)
    snap(next);
}

double MC402BoostOverdriveProcessor::Ramp::tick() noexcept
{
  if (remaining != 0)
  {
    value += increment;
    if (--remaining == 0)
      value = target;
  }
  return value;
}

double MC402BoostOverdriveProcessor::Filter::lowPass(double input, double coefficient) noexcept
{
  const double v = (input - state) * coefficient;
  const double low = v + state;
  state = low + v;
  if (std::abs(state) < Profile::stateSilenceFloor)
    state = 0.0;
  return low;
}

double MC402BoostOverdriveProcessor::Filter::highPass(double input, double coefficient) noexcept
{
  return input - lowPass(input, coefficient);
}

void MC402BoostOverdriveProcessor::Fir::clear() noexcept
{
  history.fill(0.0);
  cursor = 0;
}

double MC402BoostOverdriveProcessor::Fir::tick(double input) noexcept
{
  history[cursor] = history[cursor + length] = input;
  const double* recent = history.data() + cursor + length;
  const std::size_t middle = length / 2;
  double result = coefficients[middle] * recent[-static_cast<std::ptrdiff_t>(middle)];
  // Exact half-band zeros, symmetry, contiguous mirrored history: no modulo
  // inside the dot product, and no work for the zero taps.
  for (std::size_t tap = 1; tap < middle; tap += 2)
    result += coefficients[tap]
              * (recent[-static_cast<std::ptrdiff_t>(tap)] + recent[-static_cast<std::ptrdiff_t>(length - 1 - tap)]);
  if (++cursor == length)
    cursor = 0;
  return result;
}

double MC402BoostOverdriveProcessor::filterCoefficient(double frequency, double sampleRate) noexcept
{
  const double g = std::tan(std::numbers::pi * frequency / sampleRate);
  return g / (1.0 + g);
}

MC402Controls MC402BoostOverdriveProcessor::sanitize(const MC402Controls& controls) noexcept
{
  auto position = [](double value) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : Profile::defaultPosition;
  };
  return {controls.boostEnabled,
          controls.overdriveEnabled,
          std::isfinite(controls.boostDb) ? std::clamp(controls.boostDb, 0.0, Profile::boostMaximumDb) : 0.0,
          position(controls.gain),
          position(controls.tone),
          position(controls.output)};
}

MC402BoostOverdriveProcessor::Targets MC402BoostOverdriveProcessor::makeTargets(
  const MC402Controls& controls) const noexcept
{
  return {controls, std::pow(controls.gain, Profile::attenuationExponent),
          filterCoefficient(Profile::toneDarkHz * std::pow(Profile::toneFrequencyRatio, controls.tone),
                            mSampleRate * static_cast<double>(mFactor)),
          std::pow(controls.output, Profile::attenuationExponent),
          controls.boostEnabled ? std::pow(10.0, controls.boostDb / 20.0) : 1.0};
}

void MC402BoostOverdriveProcessor::prepare(double sampleRate, std::size_t maximumBlockSize)
{
  const auto rate = std::find(Profile::supportedSampleRates.begin(), Profile::supportedSampleRates.end(), sampleRate);
  if (rate == Profile::supportedSampleRates.end())
    throw std::invalid_argument("MC402 requires a supported host sample rate");
  const auto index = static_cast<std::size_t>(rate - Profile::supportedSampleRates.begin());
  prepareWithFactor(sampleRate, maximumBlockSize, Profile::oversamplingFactors[index]);
}

void MC402BoostOverdriveProcessor::prepareWithFactor(double sampleRate, std::size_t maximumBlockSize, unsigned factor)
{
  if (!std::isfinite(sampleRate) || sampleRate < Profile::supportedSampleRates.front()
      || sampleRate > Profile::supportedSampleRates.back() * 64.0 || maximumBlockSize == 0 || factor == 0 || factor > 64
      || (factor & (factor - 1)) != 0)
    throw std::invalid_argument("MC402 invalid prepare contract");
  mPrepared = false;
  mSampleRate = sampleRate;
  mMaximumBlockSize = maximumBlockSize;
  mFactor = factor;
  mStages = 0;
  mLatency = 0;
  for (unsigned divisor = 2; divisor <= factor; divisor *= 2)
  {
    const auto length = Profile::firLengths[mStages];
    const auto* taps =
      length == mc402_detail::kHalfBand65.size() ? mc402_detail::kHalfBand65.data() : mc402_detail::kHalfBand33.data();
    mUp[mStages].coefficients = mDown[mStages].coefficients = taps;
    mUp[mStages].length = mDown[mStages].length = length;
    mLatency += (length - 1) / divisor;
    ++mStages;
  }
  assert(mLatency < mDryDelay.size());
  const double internalRate = sampleRate * static_cast<double>(factor);
  mInputCoefficient = filterCoefficient(Profile::inputHighPassHz, internalRate);
  mInterstageCoefficient = filterCoefficient(Profile::interstageHighPassHz, internalRate);
  mOutputCoefficient = filterCoefficient(Profile::outputHighPassHz, internalRate);
  mControlRampSamples = static_cast<std::size_t>(std::ceil(Profile::smoothingSeconds * internalRate));
  mBoostRampSamples = static_cast<std::size_t>(std::ceil(Profile::smoothingSeconds * sampleRate));
  mFadeSamples = static_cast<std::size_t>(std::ceil(Profile::sectionFadeSeconds * sampleRate));
  mFront = 0;
  mBack = 2;
  mMiddle.store(1, std::memory_order_relaxed);
  mAudioTargets = makeTargets(mRequestedControls);
  mTargets.fill(mAudioTargets);
  mPrepared = true;
  reset();
}

void MC402BoostOverdriveProcessor::setControls(const MC402Controls& controls) noexcept
{
  mRequestedControls = sanitize(controls);
  if (!mPrepared)
    return;
  mTargets[mBack] = makeTargets(mRequestedControls);
  mBack = mMiddle.exchange(mBack | kDirty, std::memory_order_acq_rel) & kIndexMask;
}

bool MC402BoostOverdriveProcessor::consumeTargets() noexcept
{
  if ((mMiddle.load(std::memory_order_acquire) & kDirty) == 0)
    return false;
  mFront = mMiddle.exchange(mFront, std::memory_order_acq_rel) & kIndexMask;
  mAudioTargets = mTargets[mFront];
  return true;
}

void MC402BoostOverdriveProcessor::clearOverdrive() noexcept
{
  for (auto& fir : mUp)
    fir.clear();
  for (auto& fir : mDown)
    fir.clear();
  mInputHighPass = {};
  mInterstageHighPass = {};
  mTone = {};
  mOutputHighPass = {};
  mWarmup = 0;
}

void MC402BoostOverdriveProcessor::reset() noexcept
{
  (void)consumeTargets();
  clearOverdrive();
  mDryDelay.fill(0.0);
  mDryCursor = 0;
  mGain.snap(mAudioTargets.gain);
  mToneCoefficient.snap(mAudioTargets.toneCoefficient);
  mOutput.snap(mAudioTargets.output);
  mBoost.snap(mAudioTargets.boost);
  mOverdriveRunning = mAudioTargets.controls.overdriveEnabled;
  mOverdriveMix.snap(mOverdriveRunning ? 1.0 : 0.0);
}

void MC402BoostOverdriveProcessor::applyTargets() noexcept
{
  mGain.set(mAudioTargets.gain, mControlRampSamples);
  mToneCoefficient.set(mAudioTargets.toneCoefficient, mControlRampSamples);
  mOutput.set(mAudioTargets.output, mControlRampSamples);
  mBoost.set(mAudioTargets.boost, mBoostRampSamples);
  if (mAudioTargets.controls.overdriveEnabled)
  {
    if (!mOverdriveRunning)
    {
      clearOverdrive();
      mOverdriveRunning = true;
      mWarmup = 2 * mLatency; // Full round-trip FIR support before blending.
    }
    if (mWarmup == 0)
      mOverdriveMix.set(1.0, mFadeSamples);
  }
  else
  {
    mWarmup = 0;
    mOverdriveMix.set(0.0, mFadeSamples);
  }
}

double MC402BoostOverdriveProcessor::saturate(double value, double swing) noexcept
{
  const double magnitude = std::abs(value);
  if (magnitude <= Profile::kneeStart * swing)
    return value;
  if (magnitude >= Profile::kneeEnd * swing)
    return std::copysign(swing, value);
  const double u = (magnitude / swing - Profile::kneeStart) / Profile::kneeWidth;
  return std::copysign(swing * (Profile::kneeStart + Profile::kneeWidth * u - Profile::kneeQuadratic * u * u), value);
}

double MC402BoostOverdriveProcessor::safeBoost(double value, double gain) noexcept
{
  if (gain == 1.0)
    return value;
  const double limit = std::numeric_limits<double>::max() / gain;
  if (std::abs(value) > limit)
    return std::copysign(std::numeric_limits<double>::max(), value);
  return value * gain;
}

double MC402BoostOverdriveProcessor::core(double value) noexcept
{
  const double gain = mGain.tick();
  const double tone = mToneCoefficient.tick();
  const double output = mOutput.tick();
  double result =
    saturate(Profile::stage1Gain * mInputHighPass.highPass(value, mInputCoefficient), Profile::stage1SwingVolts);
  result = mInterstageHighPass.highPass(result * gain, mInterstageCoefficient);
  result = saturate(Profile::stage2Gain * result, Profile::stage2SwingVolts);
  result = mTone.lowPass(result, tone);
  result = mOutputHighPass.highPass(result, mOutputCoefficient);
  return output == 0.0 ? 0.0 : result * output;
}

template <bool Wire>
double MC402BoostOverdriveProcessor::rateStage(double value, std::size_t stage) noexcept
{
  if (stage == mStages)
  {
    if constexpr (Wire)
      return value;
    else
      return core(value);
  }
  const double even = rateStage<Wire>(mUp[stage].tick(value) * 2.0, stage + 1);
  const double result = mDown[stage].tick(even); // Always decimate on the even phase.
  const double odd = rateStage<Wire>(mUp[stage].tick(0.0) * 2.0, stage + 1);
  (void)mDown[stage].tick(odd);
  return result;
}
// Offline friend access instantiates the same resampler with a wire core.
template double MC402BoostOverdriveProcessor::rateStage<true>(double, std::size_t) noexcept;

void MC402BoostOverdriveProcessor::processBlock(std::span<const Sample> input, std::span<Sample> output) noexcept
{
  const bool valid = mPrepared && input.size() == output.size() && input.size() <= mMaximumBlockSize;
  assert(valid && "MC402 prepare and block contract required");
  if (!valid)
  {
    if (input.size() == output.size() && input.data() != output.data())
      std::copy(input.begin(), input.end(), output.begin());
    return;
  }
  if (consumeTargets())
    applyTargets();
  for (std::size_t i = 0; i < input.size(); ++i)
  {
    const double value = std::isfinite(input[i]) ? input[i] : 0.0;
    double dry = value;
    if (mLatency != 0)
    {
      dry = mDryDelay[mDryCursor];
      mDryDelay[mDryCursor] = value;
      if (++mDryCursor == mLatency)
        mDryCursor = 0;
    }
    double wet = 0.0;
    if (mOverdriveRunning)
    {
      wet = rateStage<false>(std::clamp(value, -Profile::stateInputLimit, Profile::stateInputLimit), 0);
      if (mWarmup != 0 && --mWarmup == 0)
        mOverdriveMix.set(1.0, mFadeSamples);
    }
    else
    {
      // Keep smoothing time independent of section state without running DSP.
      for (unsigned j = 0; j < mFactor; ++j)
      {
        (void)mGain.tick();
        (void)mToneCoefficient.tick();
        (void)mOutput.tick();
      }
    }
    const double mix = mOverdriveMix.tick();
    const double combined = mix == 0.0 ? dry : (mix == 1.0 ? wet : (1.0 - mix) * dry + mix * wet);
    output[i] = safeBoost(combined, mBoost.tick());
    if (mOverdriveRunning && !mAudioTargets.controls.overdriveEnabled && mix == 0.0)
    {
      clearOverdrive();
      mOverdriveRunning = false;
    }
  }
}
} // namespace holdsworth::dsp
