#pragma once

#include "FractionalDelayLine.h"

#include <cstddef>
#include <span>

namespace holdsworth::dsp
{

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

  // Clears delay and feedback history without changing parameters.
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
  void updatePanGains() noexcept;

  FractionalDelayLine mDelayLine;
  Sample mRequestedDelayTimeMs = 0.0;
  Sample mMinimumDelayTimeMs = 0.0;
  Sample mFeedbackCoefficient = 0.0;
  Sample mOutputLevel = 1.0;
  Sample mPan = 0.0;
  Sample mLeftPanGain = 0.0;
  Sample mRightPanGain = 0.0;
  std::size_t mMaximumBlockSize = 0;
  bool mEnabled = true;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
