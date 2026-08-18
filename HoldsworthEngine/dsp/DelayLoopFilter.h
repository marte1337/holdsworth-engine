#pragma once

#include <optional>

namespace holdsworth::dsp
{

// Strong physical-unit types keep loop-filter DSP values distinct from future
// Yamaha control values, whose mappings have not yet been measured.
struct LowCutFrequencyHz final
{
  explicit constexpr LowCutFrequencyHz(const double value = 0.0) noexcept
  : value(value)
  {
  }

  double value = 0.0;
};

struct HighCutFrequencyHz final
{
  explicit constexpr HighCutFrequencyHz(const double value = 0.0) noexcept
  : value(value)
  {
  }

  double value = 0.0;
};

// A disengaged cutoff is explicitly OFF. Engaged finite positive values are
// requested physical cutoffs; technical sample-rate clamping is reported
// separately by effectiveConfiguration().
struct DelayLoopFilterConfiguration final
{
  std::optional<LowCutFrequencyHz> lowCut;
  std::optional<HighCutFrequencyHz> highCut;
};

// A provisional non-resonant delay-loop filter consisting of a first-order
// TPT high-pass (Low Cut) followed by a first-order TPT low-pass (High Cut).
// It owns no buffers and performs no allocation.
class DelayLoopFilter final
{
public:
  using Sample = double;

  // These are discrete-time implementation guards, not musical ranges and
  // not Yamaha control mappings. Each enabled cutoff is clamped independently.
  static constexpr Sample kMinimumCutoffToSampleRateRatio = 1.0e-6;
  static constexpr Sample kMaximumCutoffToSampleRateRatio = 0.49;

  // Establishes the sample rate, derives effective cutoffs and coefficients,
  // and clears both section states. Invalid sample rates throw.
  void prepare(double sampleRate);

  // Clears both section states without changing configuration or coefficients.
  void reset() noexcept;

  // A non-finite or nonpositive engaged cutoff sanitizes to OFF. Switching a
  // section OFF clears only that section's state; switching it ON starts from
  // zero state. Changing one active cutoff retains that section's state.
  void setConfiguration(const DelayLoopFilterConfiguration& configuration) noexcept;
  void setLowCut(std::optional<LowCutFrequencyHz> cutoff) noexcept;
  void setHighCut(std::optional<HighCutFrequencyHz> cutoff) noexcept;

  [[nodiscard]] DelayLoopFilterConfiguration requestedConfiguration() const noexcept
  {
    return mRequestedConfiguration;
  }

  [[nodiscard]] DelayLoopFilterConfiguration effectiveConfiguration() const noexcept
  {
    return mEffectiveConfiguration;
  }

  [[nodiscard]] bool isBypassed() const noexcept
  {
    return !mRequestedConfiguration.lowCut.has_value()
           && !mRequestedConfiguration.highCut.has_value();
  }

  // Applies Low Cut first, then High Cut. With both sections OFF this returns
  // input directly, without running an identity-filter arithmetic path.
  [[nodiscard]] Sample processSample(Sample input) noexcept;

private:
  static std::optional<LowCutFrequencyHz>
  sanitizeLowCut(std::optional<LowCutFrequencyHz> cutoff) noexcept;
  static std::optional<HighCutFrequencyHz>
  sanitizeHighCut(std::optional<HighCutFrequencyHz> cutoff) noexcept;

  static bool sameCutoff(const std::optional<LowCutFrequencyHz>& first,
                         const std::optional<LowCutFrequencyHz>& second) noexcept;
  static bool sameCutoff(const std::optional<HighCutFrequencyHz>& first,
                         const std::optional<HighCutFrequencyHz>& second) noexcept;

  void updateLowCutDerivedState() noexcept;
  void updateHighCutDerivedState() noexcept;
  [[nodiscard]] Sample clampCutoff(Sample cutoffHz) const noexcept;
  [[nodiscard]] Sample coefficientForCutoff(Sample cutoffHz) const noexcept;

  static Sample processLowPass(Sample input, Sample coefficient, Sample& state) noexcept;
  static Sample processHighPass(Sample input, Sample coefficient, Sample& state) noexcept;

  DelayLoopFilterConfiguration mRequestedConfiguration{};
  DelayLoopFilterConfiguration mEffectiveConfiguration{};
  Sample mSampleRate = 0.0;
  Sample mLowCutCoefficient = 0.0;
  Sample mHighCutCoefficient = 0.0;
  Sample mLowCutState = 0.0;
  Sample mHighCutState = 0.0;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
