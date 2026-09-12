#pragma once

#include <atomic>
#include <cstddef>
#include <span>
#include <string_view>

namespace holdsworth::dsp
{
// Accepted clean Boost only. No rejected Overdrive state, filters or latency.
// setBoostDb() may run concurrently with processing. prepare/reset require
// lifecycle synchronization. The single atomic gain is adopted once per block.
class MC402CleanBoostProcessor final
{
public:
  using Sample = double;
  static constexpr std::string_view kProfileId = "MC402-CLEAN-BOOST-V1";
  static constexpr double kDefaultBoostDb = 0.0;
  static constexpr double kMaximumBoostDb = 20.0;
  static constexpr double kSmoothingSeconds = 0.010;

  void prepare(double sampleRate, std::size_t maximumBlockSize);
  // Snap to the latest requested gain, including while deselected. No tail.
  void reset() noexcept;
  // Finite dB clamped to [0,20]; nonfinite controls select 0 dB.
  // pow() is computed here, never in the sample loop.
  void setBoostDb(double boostDb) noexcept;
  void processBlock(std::span<const Sample> input, std::span<Sample> output) noexcept;
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }
  [[nodiscard]] constexpr std::size_t latencySamples() const noexcept { return 0; }

private:
  static_assert(std::atomic<double>::is_always_lock_free);
  std::atomic<double> mRequestedGain{1.0};
  double mGain = 1.0, mTarget = 1.0, mIncrement = 0.0;
  std::size_t mRampSamples = 0, mRemaining = 0, mMaximumBlockSize = 0;
  bool mPrepared = false;
};
} // namespace holdsworth::dsp
