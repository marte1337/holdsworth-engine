#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace holdsworth::dsp
{
struct LegatoCompressorControls final
{
  bool enabled = false;
  double calibratedThresholdDb = -18.;
  double normalizedThresholdDb = -30.;
  double attackMs = 15.;
  double releaseMs = 60.;
  double makeupDb = 3.;
};

struct LegatoDetectorContext final
{
  bool calibrated = false;
  double scale = 1.; // Native audio-buffer units -> volts, or normalized units.
};

struct LegatoCompressorDiagnostics final
{
  double power = 0.;
  double levelDb = -180.;
  double requestedReductionDb = 0.;
  double reductionDb = 0.;
  double appliedGain = 1.;
  double enableMix = 0.;
  bool calibrated = false;
};

struct LegatoCompressorTestAccess;

// One control producer and one audio consumer, following the AH triple mailbox.
// prepare requires lifecycle exclusion; reset is audio/lifecycle owned.
// Diagnostics are audio/lifecycle-owned, never a concurrent UI getter.
class LegatoCompressorProcessor final
{
public:
  static constexpr double detectorSeconds = .004;
  static constexpr double controlHz = 2000.;
  static constexpr double transitionSeconds = .010;
  static constexpr double ratio = 2.;
  static constexpr double kneeDb = 6.;
  static constexpr double maximumReductionDb = 6.;
  static constexpr double maximumMakeupDb = 4.;

  LegatoCompressorProcessor() noexcept;
  void prepare(double sampleRate, std::size_t maximumBlockSize);
  void reset() noexcept;
  void setControls(const LegatoCompressorControls& controls) noexcept;
  // beginBlock once per callback; processSample also supports host-bypass history.
  void beginBlock(LegatoDetectorContext context) noexcept;
  [[nodiscard]] double processSample(double input) noexcept;
  void processBlock(std::span<const double> input, std::span<double> output,
                    LegatoDetectorContext context = {}) noexcept;
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }
  [[nodiscard]] constexpr std::size_t latencySamples() const noexcept { return 0; }
  [[nodiscard]] LegatoCompressorDiagnostics diagnostics() const noexcept;
  [[nodiscard]] static double reductionForLevel(double levelDb, double thresholdDb) noexcept;
  [[nodiscard]] static LegatoCompressorControls sanitizeControls(LegatoCompressorControls controls) noexcept;

private:
  friend struct LegatoCompressorTestAccess;
  struct Ramp
  {
    double value = 0., goal = 0., step = 0.;
    std::size_t remaining = 0;
    void snap(double next) noexcept;
    void target(double next, std::size_t length) noexcept;
    double next() noexcept;
  };
  struct Target
  {
    LegatoCompressorControls controls{};
    double attack = 0., release = 0., makeupGain = 1.;
  };
  Target makeTarget(LegatoCompressorControls controls) const noexcept;
  void adoptTarget() noexcept;
  void controlTick() noexcept;
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  static constexpr std::uint32_t dirty = 4;
  std::array<Target, 3> mMailbox{};
  std::atomic<std::uint32_t> mMiddle{1};
  std::uint32_t mWriter = 2, mReader = 0;
  Target mTarget{};
  Ramp mEnable{}, mCalibratedThreshold{}, mNormalizedThreshold{}, mMakeup{}, mGain{};
  LegatoDetectorContext mContext{};
  double mPower = 0., mLevelDb = -180., mRequestedReductionDb = 0., mReductionDb = 0.;
  double mAppliedGain = 1., mPowerCoefficient = 0., mSampleRate = 48000.;
  std::size_t mControlSamples = 24, mControlPosition = 0, mRampSamples = 480, mMaximumBlockSize = 0;
  bool mPrepared = false;
};
} // namespace holdsworth::dsp
