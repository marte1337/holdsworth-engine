#pragma once
#include "JRockettAHDriveProfile.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace holdsworth::dsp
{
struct JRockettAHDriveControls final
{
  double gain = JRockettAHDriveProfile::defaultGain;
  double bass = JRockettAHDriveProfile::defaultTone;
  double treble = JRockettAHDriveProfile::defaultTone;
  double volume = JRockettAHDriveProfile::defaultVolume; // normalized; zero mutes
};
struct JRockettAHDriveTestAccess;

// Concrete isolated mono Drive. No Boost, host latency negotiation, NAM or UI.
// One control producer and one audio consumer. prepare/reset require lifecycle
// exclusion from both threads. Exact in-place spans supported; partial overlap not.
class JRockettAHDriveProcessor final
{
public:
  using Profile = JRockettAHDriveProfile;
  JRockettAHDriveProcessor() noexcept;
  void prepare(double sampleRate, std::size_t maximumBlockSize);
  void reset() noexcept;
  void setControls(const JRockettAHDriveControls& controls) noexcept;
  void processBlock(std::span<const double> input, std::span<double> output) noexcept;
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }
  [[nodiscard]] constexpr std::size_t latencySamples() const noexcept { return Profile::latency; }
  [[nodiscard]] static double volumeAmplitude(double normalized) noexcept;

private:
  friend struct JRockettAHDriveTestAccess;
  // D, compensation, Bass b0/b1/a1, Treble b0/b1/a1, Volume amplitude.
  using Values = std::array<double, 9>;
  struct Target { JRockettAHDriveControls controls{}; Values values{}; };
  [[nodiscard]] Target makeTarget(const JRockettAHDriveControls& controls) const noexcept;
  void adoptTarget() noexcept;
  void startRamps() noexcept;
  void advance(std::size_t group) noexcept;
  void makeFIR() noexcept;
  static double shelfTick(double x, const double* coefficients, double& state) noexcept;
  static constexpr std::array<std::size_t, 5> kOffsets{0,2,5,8,9};
  static constexpr std::size_t kPhaseTaps = (Profile::firTaps + Profile::oversampling - 1) / Profile::oversampling;
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  static constexpr std::uint32_t kDirty = 4;
  std::array<Target, 3> mMailbox{};
  std::atomic<std::uint32_t> mMiddle{1};
  std::uint32_t mWriter = 2, mReader = 0;
  Target mTarget{};
  Values mCurrent{}, mGoal{}, mStep{};
  std::array<std::size_t, 4> mRemaining{};
  std::array<std::array<double, kPhaseTaps>, Profile::oversampling> mPhases{};
  std::array<double, Profile::firTaps> mFIR{};
  // Duplicated fixed rings provide contiguous dot products without modulo per tap.
  std::array<double, 2*kPhaseTaps> mInputHistory{};
  std::array<double, 2*Profile::firTaps> mNonlinearHistory{};
  std::size_t mInputIndex = 0, mNonlinearIndex = 0;
  double mBassState = 0., mTrebleState = 0., mSampleRate = 48000.;
  std::size_t mRampSamples = 480, mMaximumBlockSize = 0;
  bool mPrepared = false;
};
} // namespace holdsworth::dsp
