#pragma once

#include "MC402ProvisionalProfile.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace holdsworth::test
{
struct MC402TestAccess;
}

namespace holdsworth::dsp
{
struct MC402Controls final
{
  bool boostEnabled = false;
  bool overdriveEnabled = false;
  double boostDb = 0.0;
  double gain = MC402ProvisionalProfile::defaultPosition;
  double tone = MC402ProvisionalProfile::defaultPosition;
  double output = MC402ProvisionalProfile::defaultPosition;
};

// Isolated mono, volts-domain behavioral MC402. No NAM, UI or selector coupling.
// Even with both sections off, output is the exact input delayed by latencySamples().
// prepare() requires lifecycle synchronization and rejects unsupported contracts.
// One producer may call setControls() concurrently with processBlock()/reset().
// All audio operations are bounded, noexcept and allocation-free. Diagnostic
// getters (except controls(), which is producer-only) require lifecycle stability.
class MC402BoostOverdriveProcessor final
{
public:
  using Sample = double;
  using Profile = MC402ProvisionalProfile;
  static constexpr auto kProfileId = Profile::id;

  void prepare(double sampleRate, std::size_t maximumBlockSize);
  void reset() noexcept;
  void setControls(const MC402Controls& controls) noexcept;
  [[nodiscard]] MC402Controls controls() const noexcept { return mRequestedControls; }
  // Equal-size spans, length <= prepared maximum; exact in-place is supported.
  // Partial overlap is not supported. Invalid calls assert in Debug and copy
  // equal-size spans in Release without advancing state.
  void processBlock(std::span<const Sample> input, std::span<Sample> output) noexcept;
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] double sampleRate() const noexcept { return mSampleRate; }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }
  [[nodiscard]] std::size_t latencySamples() const noexcept { return mLatency; }
  [[nodiscard]] unsigned oversamplingFactor() const noexcept { return mFactor; }

private:
  friend struct holdsworth::test::MC402TestAccess;
  struct Ramp
  {
    double value = 0.0, target = 0.0, increment = 0.0;
    std::size_t remaining = 0;
    void snap(double next) noexcept;
    void set(double next, std::size_t samples) noexcept;
    double tick() noexcept;
  };
  struct Filter
  {
    double state = 0.0;
    double lowPass(double input, double coefficient) noexcept;
    double highPass(double input, double coefficient) noexcept;
  };
  struct Fir
  {
    std::array<double, Profile::maximumFirLength * 2> history{};
    const double* coefficients = nullptr;
    std::size_t length = 0, cursor = 0;
    void clear() noexcept;
    double tick(double input) noexcept;
  };
  struct Targets
  {
    MC402Controls controls{};
    double gain = 0.0, toneCoefficient = 0.0, output = 0.0, boost = 1.0;
  };

  // SPSC triple buffer: each side owns one slot; exchange transfers the middle
  // slot. Producer coalesces unread snapshots without touching consumer storage.
  static constexpr std::uint32_t kDirty = 4;
  static constexpr std::uint32_t kIndexMask = 3;
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  std::array<Targets, 3> mTargets{};
  std::atomic<std::uint32_t> mMiddle{1};
  std::uint32_t mFront = 0, mBack = 2;
  MC402Controls mRequestedControls{}; // producer-owned; never read by audio
  Targets mAudioTargets{};

  static MC402Controls sanitize(const MC402Controls& controls) noexcept;
  Targets makeTargets(const MC402Controls& controls) const noexcept;
  bool consumeTargets() noexcept;
  void applyTargets() noexcept;
  void clearOverdrive() noexcept;
  static double filterCoefficient(double frequency, double sampleRate) noexcept;
  static double saturate(double value, double swing) noexcept;
  static double safeBoost(double value, double gain) noexcept;
  double core(double value) noexcept;
  template <bool Wire>
  double rateStage(double value, std::size_t stage) noexcept;
  // Test-only access can force rates/factors for offline convergence studies.
  // This is not a product control and is never changed during processing.
  void prepareWithFactor(double sampleRate, std::size_t maximumBlockSize, unsigned factor);

  std::array<Fir, Profile::maximumStages> mUp{}, mDown{};
  std::array<double, Profile::maximumDelaySamples> mDryDelay{};
  std::size_t mDryCursor = 0, mStages = 0, mLatency = 0;
  Filter mInputHighPass{}, mInterstageHighPass{}, mTone{}, mOutputHighPass{};
  double mInputCoefficient = 0.0, mInterstageCoefficient = 0.0, mOutputCoefficient = 0.0;
  Ramp mGain{}, mToneCoefficient{}, mOutput{}, mBoost{}, mOverdriveMix{};
  std::size_t mControlRampSamples = 0, mBoostRampSamples = 0, mFadeSamples = 0, mWarmup = 0;
  bool mOverdriveRunning = false;
  bool mPrepared = false;
  double mSampleRate = 0.0;
  unsigned mFactor = 1;
  std::size_t mMaximumBlockSize = 0;
};
} // namespace holdsworth::dsp
