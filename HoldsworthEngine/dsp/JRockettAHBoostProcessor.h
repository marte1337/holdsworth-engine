#pragma once
#include "JRockettAHBoostProfile.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace holdsworth::dsp
{
enum class JRockettAHBoostType : std::uint32_t { full = 0, clean = 1, treble = 2 };
enum class JRockettAHEmphasis : std::uint32_t { low = 0, high = 1 };
struct JRockettAHBoostControls final
{
  double boostDb = JRockettAHBoostProfile::defaultBoostDb;
  JRockettAHBoostType type = JRockettAHBoostType::clean;
  JRockettAHEmphasis emphasis = JRockettAHEmphasis::low;
};
struct JRockettAHBoostTestAccess;

// Isolated linear Boost only. No Drive, calibration, selector or plugin wiring.
// Exactly one control producer may call setControls concurrently with processing.
// A three-slot SPSC mailbox publishes the complete gain/mode tuple losslessly.
// prepare/reset require external lifecycle synchronization with both threads.
class JRockettAHBoostProcessor final
{
public:
  using Profile = JRockettAHBoostProfile;
  void prepare(double sampleRate, std::size_t maximumBlockSize);
  void reset() noexcept;
  void setControls(const JRockettAHBoostControls& controls) noexcept;
  void processBlock(std::span<const double> input, std::span<double> output) noexcept;
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }
  [[nodiscard]] constexpr std::size_t latencySamples() const noexcept { return 0; }

private:
  friend struct JRockettAHBoostTestAccess;
  struct Target { double gain = 1.0; std::size_t mode = Profile::defaultMode; };
  struct Coefficients { double b0 = 1.0, b1 = 0.0, a1 = 0.0; };
  struct Response
  {
    std::array<Coefficients, 2> coefficients{};
    std::array<double, 2> state{};
    double tick(double input) noexcept;
  };
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  static constexpr std::uint32_t kDirty = 4;
  std::array<Target, 3> mMailbox{};
  std::atomic<std::uint32_t> mMiddle{1};
  std::uint32_t mWriter = 2, mReader = 0;
  Target mTarget{};
  void adoptTarget() noexcept;
  void startTransition() noexcept;
  void remember(double input) noexcept;
  static Coefficients shelf(double db, double hz, double sampleRate, bool high) noexcept;

  // Eight-sample state recurrence for fast, mathematically equivalent history
  // replay. Prepared once; does not change live filters or transition timing.
  struct ReplayBlock
  {
    std::array<double, 8> low{}, high{};
    double m00 = 0.0, m10 = 0.0, m11 = 0.0;
  };
  void replay(const double* samples, std::size_t count) noexcept;
  std::array<ReplayBlock, 6> mReplay{};
  std::array<std::array<Coefficients, 2>, 6> mCoefficients{};
  Response mCurrent{}, mIncoming{};
  std::size_t mCurrentMode = Profile::defaultMode, mIncomingMode = Profile::defaultMode;
  std::array<double, Profile::maximumHistorySamples> mHistory{};
  std::size_t mHistoryLength = 0, mHistoryCount = 0, mHistoryWrite = 0;
  std::size_t mFadeSamples = 0, mFadePosition = 0;
  bool mFading = false;
  double mGain = 1.0, mGainTarget = 1.0, mGainIncrement = 0.0;
  std::size_t mRampSamples = 0, mRampRemaining = 0, mMaximumBlockSize = 0;
  bool mPrepared = false;
};
} // namespace holdsworth::dsp
