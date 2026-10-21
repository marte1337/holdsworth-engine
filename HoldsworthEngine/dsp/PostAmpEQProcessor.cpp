#include "PostAmpEQProcessor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{
void PostAmpEQProcessor::Ramp::snap(double value) noexcept
{
  current = target = value;
  increment = 0.0;
  remaining = 0;
}

void PostAmpEQProcessor::Ramp::setTarget(double value, std::size_t samples) noexcept
{
  if (value == target) return;
  target = value;
  increment = (target - current) / static_cast<double>(samples);
  remaining = samples;
}

double PostAmpEQProcessor::Ramp::next() noexcept
{
  if (remaining != 0)
  {
    current += increment;
    if (--remaining == 0) current = target;
  }
  return current;
}

void PostAmpEQProcessor::Section::updateCoefficients() noexcept
{
  // Coupled coefficients are derived from positive g/k; they are never
  // independently interpolated. Simper's two-integrator trapezoidal SVF:
  // https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
  a1 = 1.0 / (1.0 + g.current * (g.current + k.current));
  a2 = g.current * a1;
  a3 = g.current * a2;
}

void PostAmpEQProcessor::Section::clearHistory() noexcept
{
  ic1eq = ic2eq = 0.0;
}

double PostAmpEQProcessor::Section::tick(double input, unsigned kind) noexcept
{
  const bool changing = g.remaining != 0 || k.remaining != 0;
  g.next();
  k.next();
  m.next();
  if (changing) updateCoefficients();
  const double v3 = input - ic2eq;
  const double v1 = a1 * ic1eq + a2 * v3;
  const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
  ic1eq = 2.0 * v1 - ic1eq;
  ic2eq = 2.0 * v2 - ic2eq;
  // Bound exceptional numerical recovery and prevent denormal processing on
  // decayed tails. This is far below the response measurement floor.
  if (!std::isfinite(ic1eq) || !std::isfinite(ic2eq))
  {
    clearHistory();
    return kind == 1 ? input : 0.0;
  }
  if (std::abs(ic1eq) < 1.0e-100) ic1eq = 0.0;
  if (std::abs(ic2eq) < 1.0e-100) ic2eq = 0.0;
  if (kind == 0) return input - k.current * v1 - v2;
  if (kind == 2) return v2;
  // Explicit identity is necessary after gain reaches zero: stale states must
  // never appear in the audible neutral path, including signed zero inputs.
  return m.current == 0.0 ? input : input + m.current * v1;
}

void PostAmpEQProcessor::setControls(const PostAmpEQControls& controls) noexcept
{
  PostAmpEQControls sanitized;
  if (!sanitizePostAmpEQControls(controls, sanitized)) return;
  mMailbox[mWriter] = sanitized;
  mWriter = mMiddle.exchange(mWriter | kDirty, std::memory_order_acq_rel) & 3U;
}

bool PostAmpEQProcessor::adoptControls() noexcept
{
  if ((mMiddle.load(std::memory_order_acquire) & kDirty) == 0) return false;
  mReader = mMiddle.exchange(mReader, std::memory_order_acq_rel) & 3U;
  const auto& controls = mMailbox[mReader];
  if (controls == mControls) return false;
  mControls = controls;
  return true;
}

void PostAmpEQProcessor::configure(bool snap) noexcept
{
  const auto set = [snap](Ramp& ramp, double value, std::size_t samples) noexcept {
    if (snap) ramp.snap(value);
    else ramp.setTarget(value, samples);
  };
  const auto frequency = [this](double hz) noexcept {
    return std::tan(std::numbers::pi * hz / mSampleRate);
  };
  constexpr double butterworthK = std::numbers::sqrt2;
  set(mHighPass.g, frequency(mControls.highPassHz), mParameterRampSamples);
  set(mHighPass.k, butterworthK, mParameterRampSamples);
  set(mHighPass.m, 0.0, mParameterRampSamples);
  set(mLowPass.g, frequency(mControls.lowPassHz), mParameterRampSamples);
  set(mLowPass.k, butterworthK, mParameterRampSamples);
  set(mLowPass.m, 0.0, mParameterRampSamples);
  for (std::size_t i = 0; i < mBands.size(); ++i)
  {
    const auto& control = mControls.bands[i];
    auto& section = mBands[i];
    // RBJ peaking Q convention: boost and cut are reciprocal, center gain is
    // gainDb, and Q defines the half-gain bandwidth after bilinear warping.
    const double a = std::pow(10.0, control.gainDb / 40.0);
    const double k = 1.0 / (control.q * a);
    set(section.g, frequency(control.frequencyHz), mParameterRampSamples);
    set(section.k, k, mParameterRampSamples);
    set(section.m, control.gainDb == 0.0 ? 0.0 : k * (a * a - 1.0), mParameterRampSamples);
    if (snap) section.updateCoefficients();
  }
  if (snap)
  {
    mHighPass.updateCoefficients();
    mLowPass.updateCoefficients();
  }
  set(mEnabled, mControls.enabled ? 1.0 : 0.0, mEnableRampSamples);
  set(mHighPassEnabled, mControls.highPassEnabled ? 1.0 : 0.0, mEnableRampSamples);
  set(mLowPassEnabled, mControls.lowPassEnabled ? 1.0 : 0.0, mEnableRampSamples);
  set(mTrim, mControls.trimDb == 0.0 ? 1.0 : std::pow(10.0, mControls.trimDb / 20.0),
      mTrimRampSamples);
}

void PostAmpEQProcessor::prepare(double sampleRate, std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate < minimumSampleRate ||
      sampleRate > maximumSampleRate || maximumBlockSize == 0)
    throw std::invalid_argument("Invalid post-amp EQ rate/block contract");
  mSampleRate = sampleRate;
  mMaximumBlockSize = maximumBlockSize;
  mParameterRampSamples = static_cast<std::size_t>(std::ceil(sampleRate * parameterRampSeconds));
  mEnableRampSamples = static_cast<std::size_t>(std::ceil(sampleRate * enableRampSeconds));
  mTrimRampSamples = static_cast<std::size_t>(std::ceil(sampleRate * trimRampSeconds));
  mPrepared = true;
  reset();
}

void PostAmpEQProcessor::clearHistory() noexcept
{
  mHighPass.clearHistory();
  for (auto& section : mBands) section.clearHistory();
  mLowPass.clearHistory();
}

void PostAmpEQProcessor::reset() noexcept
{
  adoptControls();
  configure(true);
  clearHistory();
}

double PostAmpEQProcessor::blend(double dry, double wet, double amount) noexcept
{
  if (amount == 0.0) return dry;
  if (amount == 1.0) return wet;
  return dry + amount * (wet - dry);
}

void PostAmpEQProcessor::processBlock(std::span<const double> input,
                                     std::span<double> output) noexcept
{
  // Contract failures are bounded transparent fallbacks in every build. Do not
  // consume snapshots or advance filter/ramp states for invalid/empty blocks.
  if (!mPrepared || input.size() != output.size() || input.size() > mMaximumBlockSize)
  {
    if (input.data() != output.data())
      std::copy_n(input.data(), std::min(input.size(), output.size()), output.data());
    return;
  }
  if (input.empty()) return;
  if (adoptControls()) configure(false);
  for (std::size_t i = 0; i < input.size(); ++i)
  {
    const double original = input[i];
    const bool finite = std::isfinite(original);
    if (!finite) clearHistory();
    // Numerical overflow protection only, far outside audio amplitudes. The
    // original finite sample is retained for exact global bypass and neutral.
    constexpr double limit = std::numeric_limits<double>::max() / 65536.0;
    const double x = finite ? std::clamp(original, -limit, limit) : 0.0;
    const double hp = mHighPass.tick(x, 0);
    const double hpMix = mHighPassEnabled.next();
    double y = blend(x, hp, hpMix);
    for (auto& section : mBands) y = section.tick(y, 1);
    const double lp = mLowPass.tick(y, 2);
    const double lpMix = mLowPassEnabled.next();
    y = blend(y, lp, lpMix);
    const double trim = mTrim.next();
    if (trim != 1.0) y *= trim;
    const double enabled = mEnabled.next();
    const bool neutral = hpMix == 0.0 && lpMix == 0.0 && trim == 1.0 &&
      mBands[0].m.current == 0.0 && mBands[1].m.current == 0.0 && mBands[2].m.current == 0.0;
    const double dry = finite ? original : 0.0;
    output[i] = enabled == 0.0 || neutral ? dry : blend(dry, y, enabled);
    if (!std::isfinite(output[i]))
    {
      clearHistory();
      output[i] = 0.0;
    }
  }
}
} // namespace holdsworth::dsp
