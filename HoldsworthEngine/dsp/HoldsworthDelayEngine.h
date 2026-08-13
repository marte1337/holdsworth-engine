#pragma once

#include "DelayBand.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace holdsworth::dsp
{

inline constexpr std::size_t kHoldsworthDelayBandCount = 8;

// A normalized feedback value for the DSP recurrence. This type deliberately
// has no relationship or conversion to Yamaha's documented control values.
struct NormalizedFeedbackCoefficient final
{
  explicit constexpr NormalizedFeedbackCoefficient(const double coefficient = 0.0)
  : value(coefficient)
  {
  }

  double value = 0.0;
};

struct DelayBandConfiguration final
{
  double delayTimeMs = 0.0;
  NormalizedFeedbackCoefficient feedback{};
  double outputLevel = 1.0;
  double pan = 0.0;
  bool enabled = false;
};

struct HoldsworthDelayConfiguration final
{
  std::array<DelayBandConfiguration, kHoldsworthDelayBandCount> bands{};
  double globalWetOutputLevel = 1.0;
};

// An eight-band mono-input, wet-only stereo delay processor.
//
// prepare() performs all scratch and delay-history allocation. Once prepared,
// reset(), configuration setters, and processBlock() do not allocate.
// Configuration changes and processing must be externally synchronized at an
// audio-block boundary.
class HoldsworthDelayEngine final
{
public:
  using Sample = double;

  static constexpr std::size_t kBandCount = kHoldsworthDelayBandCount;

  // maximumDelayTimeMs is the runtime capacity of each individual DelayBand.
  explicit HoldsworthDelayEngine(double maximumDelayTimeMs);

  HoldsworthDelayEngine(const HoldsworthDelayEngine&) = delete;
  HoldsworthDelayEngine& operator=(const HoldsworthDelayEngine&) = delete;
  HoldsworthDelayEngine(HoldsworthDelayEngine&&) noexcept = delete;
  HoldsworthDelayEngine& operator=(HoldsworthDelayEngine&&) noexcept = delete;

  // Allocates all eight delay histories and three maximum-block-sized scratch
  // buffers. Calling prepare() again discards existing delay history while
  // preserving parameter values.
  void prepare(double sampleRate, std::size_t maximumBlockSize);

  // Clears all eight delay histories without changing configuration.
  void reset() noexcept;

  // Applies all band parameters and the global wet level without resetting
  // history. Values are sanitized by the same rules as DelayBand's setters.
  void applyConfiguration(const HoldsworthDelayConfiguration& configuration) noexcept;

  // bandIndex must be in [0, kBandCount). Invalid indices assert in Debug and
  // are ignored without modifying any band when assertions are disabled.
  void setBandConfiguration(std::size_t bandIndex,
                            const DelayBandConfiguration& configuration) noexcept;

  // Returns sanitized requested parameter values. In particular,
  // delayTimeMs is independent of sample rate and is not the one-sample-clamped
  // effective delay reported by DelayBand::delayTimeMs().
  [[nodiscard]] HoldsworthDelayConfiguration configuration() const noexcept;

  // Linear gain in [0, 1], applied after summing all eight wet band outputs.
  // Non-finite values are treated as zero.
  void setGlobalWetOutputLevel(Sample level) noexcept;

  [[nodiscard]] Sample globalWetOutputLevel() const noexcept { return mGlobalWetOutputLevel; }
  [[nodiscard]] Sample maximumDelayTimeMs() const noexcept;
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }

  // Overwrites both output spans with the direct sum of all eight wet stereo
  // band signals, followed by the global wet gain. No dry signal, limiting,
  // clipping, or automatic normalization is applied.
  //
  // All spans must have equal lengths no greater than maximumBlockSize. Exact
  // aliasing between monoInput and either output is supported because input is
  // copied to prepared scratch before output is written. wetLeft and wetRight
  // must be distinct and non-overlapping. Partial overlap is not supported.
  void processBlock(std::span<const Sample> monoInput,
                    std::span<Sample> wetLeft,
                    std::span<Sample> wetRight) noexcept;

private:
  std::array<DelayBand, kBandCount> mBands;
  std::vector<Sample> mInputScratch;
  std::vector<Sample> mBandWetLeft;
  std::vector<Sample> mBandWetRight;
  Sample mGlobalWetOutputLevel = 1.0;
  std::size_t mMaximumBlockSize = 0;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
