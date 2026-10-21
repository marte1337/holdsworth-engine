#pragma once

#include "PostAmpEQControls.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace holdsworth::dsp
{
struct PostAmpEQTestAccess;

// Independent, host-rate mono EQ: HP -> three bells -> LP -> trim.
// Exactly one producer calls setControls while exactly one audio consumer calls
// processBlock. Three fixed slots publish a coherent latest controls snapshot;
// superseded snapshots may be coalesced. No allocation, locks, waits or retries.
// prepare/reset require external lifecycle synchronization with both threads.
class PostAmpEQProcessor final
{
public:
  static constexpr double minimumSampleRate = 44100.0;
  static constexpr double maximumSampleRate = 192000.0;
  static constexpr double parameterRampSeconds = 0.020;
  static constexpr double enableRampSeconds = 0.010;
  static constexpr double trimRampSeconds = 0.010;

  void prepare(double sampleRate, std::size_t maximumBlockSize);
  void reset() noexcept;
  void setControls(const PostAmpEQControls& controls) noexcept;
  void processBlock(std::span<const double> input, std::span<double> output) noexcept;
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }
  [[nodiscard]] constexpr std::size_t latencySamples() const noexcept { return 0; }

private:
  friend struct PostAmpEQTestAccess;
  struct Ramp final
  {
    double current = 0.0, target = 0.0, increment = 0.0;
    std::size_t remaining = 0;
    void snap(double value) noexcept;
    void setTarget(double value, std::size_t samples) noexcept;
    double next() noexcept;
  };
  struct Section final
  {
    Ramp g{}, k{}, m{};
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double ic1eq = 0.0, ic2eq = 0.0;
    void updateCoefficients() noexcept;
    // kind: 0 = HP, 1 = bell, 2 = LP. All states advance even when the
    // corresponding section/global output is an exact bypass.
    double tick(double input, unsigned kind) noexcept;
    void clearHistory() noexcept;
  };

  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  static constexpr std::uint32_t kDirty = 4;
  std::array<PostAmpEQControls, 3> mMailbox{};
  std::atomic<std::uint32_t> mMiddle{1};
  std::uint32_t mWriter = 2, mReader = 0;
  PostAmpEQControls mControls{};
  bool adoptControls() noexcept;
  void configure(bool snap) noexcept;
  void clearHistory() noexcept;
  static double blend(double dry, double wet, double amount) noexcept;

  Section mHighPass{}, mLowPass{};
  std::array<Section, 3> mBands{};
  Ramp mEnabled{}, mHighPassEnabled{}, mLowPassEnabled{}, mTrim{};
  double mSampleRate = 48000.0;
  std::size_t mParameterRampSamples = 960, mEnableRampSamples = 480, mTrimRampSamples = 480;
  std::size_t mMaximumBlockSize = 0;
  bool mPrepared = false;
};
} // namespace holdsworth::dsp
