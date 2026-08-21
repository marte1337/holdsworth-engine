#pragma once

#include "DelayLoopFilter.h"
#include "DelayModulator.h"
#include "FractionalDelayLine.h"

#include <cstddef>
#include <span>

namespace holdsworth::dsp
{

// Normalized audible observation position within the full delay loop. This is
// a DSP value and deliberately has no implicit relationship to Yamaha's
// documented TAP percentage control.
struct TapFraction final
{
  explicit constexpr TapFraction(const double value = 1.0) noexcept
  : value(value)
  {
  }

  double value = 1.0;
};

// Polarity of the audible wet delay signal. This is deliberately distinct
// from ModulationPhaseCycles, which controls oscillator position.
enum class DelaySignalPolarity
{
  normal,
  reverse
};

// One mono-input delay voice with feedback and wet-only stereo output.
//
// prepare() performs all storage allocation. Once prepared, reset(), parameter
// setters, and processBlock() do not allocate. Parameter setters and processing
// must be externally synchronized at an audio-block boundary.
class DelayBand final
{
public:
  using Sample = double;

  static constexpr Sample kMaximumFeedbackCoefficient = 0.99;

  explicit DelayBand(double maximumDelayTimeMs);

  DelayBand(const DelayBand&) = delete;
  DelayBand& operator=(const DelayBand&) = delete;
  DelayBand(DelayBand&&) noexcept = delete;
  DelayBand& operator=(DelayBand&&) noexcept = delete;

  // Allocates delay history and applies a minimum effective delay of one
  // sample. Calling prepare() again discards existing history while preserving
  // parameter values.
  void prepare(double sampleRate, std::size_t maximumBlockSize);

  // Clears delay and feedback history and restores the configured modulation
  // phase without changing parameters.
  void reset() noexcept;

  // The applied delay is clamped to [one sample, maximumDelayTimeMs] after
  // prepare(). Before prepare(), the requested value is clamped only to the
  // configured maximum.
  void setDelayTimeMs(Sample delayTimeMs) noexcept;

  // Normalized coefficient in [0, kMaximumFeedbackCoefficient]. This is not a
  // Yamaha control mapping. Non-finite values are treated as zero.
  void setFeedbackCoefficient(Sample coefficient) noexcept;

  // Linear wet-output gain in [0, 1]. This gain is outside the feedback loop.
  // Non-finite values are treated as zero.
  void setOutputLevel(Sample level) noexcept;

  // Equal-power pan in [-1, 1]: -1 is hard left, 0 is center, and 1 is hard
  // right. Non-finite values are treated as center.
  void setPan(Sample pan) noexcept;

  // Physical modulation parameters. These values are deliberately independent
  // of any future Yamaha SPEED/DEPTH control mapping. Rate and depth are
  // block-rate parameters; phase is normalized cycles in [0, 1) and becomes
  // the phase restored by reset().
  void setModulationRate(ModulationRateHz rate) noexcept;
  void setModulationDepth(ModulationDepthMs depth) noexcept;
  void setModulationPhase(ModulationPhaseCycles phase) noexcept;
  void setModulationWaveform(ModulationWaveform waveform) noexcept;

  // Physical loop-filter configuration. A disengaged cutoff means that
  // section is explicitly OFF. These values have no relationship to Yamaha's
  // documented LOW CUT/HIGH CUT controls.
  void setLoopFilterConfiguration(
    const DelayLoopFilterConfiguration& configuration) noexcept;

  // Audible tap position normalized to [0, 1]. The feedback recurrence keeps
  // using the full loop delay. A non-finite value selects the legacy-safe full
  // loop position (1.0).
  void setTapFraction(TapFraction tapFraction) noexcept;

  // Selects the polarity of the audible, filtered delay/tap signal only. It
  // never changes external input, delay memory, feedback, filtering, TAP
  // timing, or modulation. Invalid enum representations select Normal.
  void setDelaySignalPolarity(DelaySignalPolarity polarity) noexcept;

  // A disabled band emits silence, rejects new external input, and continues
  // advancing its existing feedback state. It does not clear or freeze history.
  void setEnabled(bool enabled) noexcept { mEnabled = enabled; }

  // delayTimeMs() reports the effective applied value. requestedDelayTimeMs()
  // preserves the unclamped-to-one-sample request across sample-rate changes.
  [[nodiscard]] Sample delayTimeMs() const noexcept { return mDelayLine.delayTimeMs(); }
  [[nodiscard]] Sample requestedDelayTimeMs() const noexcept { return mRequestedDelayTimeMs; }
  [[nodiscard]] Sample minimumDelayTimeMs() const noexcept { return mMinimumDelayTimeMs; }
  [[nodiscard]] Sample maximumDelayTimeMs() const noexcept { return mDelayLine.maximumDelayTimeMs(); }
  [[nodiscard]] Sample feedbackCoefficient() const noexcept { return mFeedbackCoefficient; }
  [[nodiscard]] Sample outputLevel() const noexcept { return mOutputLevel; }
  [[nodiscard]] Sample pan() const noexcept { return mPan; }
  [[nodiscard]] ModulationRateHz requestedModulationRate() const noexcept
  {
    return mDelayModulator.requestedRate();
  }
  [[nodiscard]] ModulationRateHz effectiveModulationRate() const noexcept
  {
    return mDelayModulator.effectiveRate();
  }
  [[nodiscard]] ModulationDepthMs requestedModulationDepth() const noexcept
  {
    return mRequestedModulationDepth;
  }
  [[nodiscard]] ModulationDepthMs effectiveModulationDepth() const noexcept
  {
    return mEffectiveModulationDepth;
  }
  [[nodiscard]] ModulationPhaseCycles modulationPhase() const noexcept
  {
    return mDelayModulator.currentPhase();
  }
  [[nodiscard]] ModulationPhaseCycles modulationResetPhase() const noexcept
  {
    return mDelayModulator.resetPhase();
  }
  [[nodiscard]] ModulationWaveform modulationWaveform() const noexcept
  {
    return mDelayModulator.waveform();
  }
  [[nodiscard]] DelayLoopFilterConfiguration requestedLoopFilterConfiguration() const noexcept
  {
    return mLoopFilter.requestedConfiguration();
  }
  [[nodiscard]] DelayLoopFilterConfiguration effectiveLoopFilterConfiguration() const noexcept
  {
    return mLoopFilter.effectiveConfiguration();
  }
  [[nodiscard]] TapFraction tapFraction() const noexcept { return mTapFraction; }
  [[nodiscard]] DelaySignalPolarity delaySignalPolarity() const noexcept
  {
    return mDelaySignalPolarity;
  }

  // Diagnostic value: the delay used by the most recently processed sample.
  // Before processing after prepare/reset/a parameter change, it is the
  // effective unmodulated base delay.
  [[nodiscard]] Sample currentModulatedDelayTimeMs() const noexcept
  {
    return mCurrentModulatedDelayTimeMs;
  }
  [[nodiscard]] bool isEnabled() const noexcept { return mEnabled; }
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }

  // Overwrites both output spans with this band's wet-only stereo signal.
  // All spans must have equal lengths no greater than maximumBlockSize. Exact
  // aliasing between monoInput and either one output is supported; wetLeft and
  // wetRight must be distinct and non-overlapping. Partial overlap is not
  // supported.
  void processBlock(std::span<const Sample> monoInput,
                    std::span<Sample> wetLeft,
                    std::span<Sample> wetRight) noexcept;

private:
  void applyEffectiveDelayTime() noexcept;
  void applyEffectiveModulationDepth() noexcept;
  void updatePanGains() noexcept;

  FractionalDelayLine mDelayLine;
  DelayModulator mDelayModulator;
  DelayLoopFilter mLoopFilter;
  DelayLoopFilter mTapOutputFilter;
  Sample mRequestedDelayTimeMs = 0.0;
  Sample mMinimumDelayTimeMs = 0.0;
  ModulationDepthMs mRequestedModulationDepth{};
  ModulationDepthMs mEffectiveModulationDepth{};
  Sample mCurrentModulatedDelayTimeMs = 0.0;
  Sample mFeedbackCoefficient = 0.0;
  Sample mOutputLevel = 1.0;
  Sample mPan = 0.0;
  Sample mLeftPanGain = 0.0;
  Sample mRightPanGain = 0.0;
  TapFraction mTapFraction{};
  DelaySignalPolarity mDelaySignalPolarity = DelaySignalPolarity::normal;
  std::size_t mMaximumBlockSize = 0;
  bool mEnabled = true;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
