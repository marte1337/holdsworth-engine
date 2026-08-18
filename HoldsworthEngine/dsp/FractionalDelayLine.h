#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace holdsworth::dsp
{

// A mono fractional delay line whose output contains only the delayed signal.
//
// prepare() performs all storage allocation. Once prepared, reset(), the
// parameter setter, and processBlock() do not allocate. Calls to
// setDelayTimeMs() and processBlock() must be externally synchronized; the
// intended use is to apply parameter changes at an audio-block boundary.
class FractionalDelayLine final
{
public:
  using Sample = double;

  explicit FractionalDelayLine(double maximumDelayTimeMs);

  FractionalDelayLine(const FractionalDelayLine&) = delete;
  FractionalDelayLine& operator=(const FractionalDelayLine&) = delete;
  FractionalDelayLine(FractionalDelayLine&&) noexcept = delete;
  FractionalDelayLine& operator=(FractionalDelayLine&&) noexcept = delete;

  // Allocates enough history for maximumDelayTimeMs at sampleRate. Calling
  // prepare() again discards existing history.
  void prepare(double sampleRate, std::size_t maximumBlockSize);

  // Clears delay history while retaining all allocated storage.
  void reset() noexcept;

  // Values outside [0, maximumDelayTimeMs] are clamped. Non-finite values are
  // treated as zero.
  void setDelayTimeMs(double delayTimeMs) noexcept;

  [[nodiscard]] double delayTimeMs() const noexcept { return mDelayTimeMs; }
  [[nodiscard]] double maximumDelayTimeMs() const noexcept { return mMaximumDelayTimeMs; }
  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }

  // Writes only the delayed signal to output. input and output must have equal
  // lengths no greater than the maximum block size passed to prepare(). Exact
  // in-place processing is supported.
  //
  // Delay semantics:
  //   0 samples: current input sample
  //   1 sample:  previous input sample
  //   fractional values: linear interpolation between adjacent samples
  void processBlock(std::span<const Sample> input, std::span<Sample> output) noexcept;

  // Reads from existing history without advancing the delay line. This split
  // history API is intended for external feedback loops and requires a
  // prepared delay of at least one sample. Call pushSample() exactly once
  // after each read to advance the line.
  [[nodiscard]] Sample readDelayedSample() const noexcept;

  // Reads from existing history at an explicit delay without changing the
  // delay configured by setDelayTimeMs(). This split-history read is clamped
  // to [one sample, maximumDelayTimeMs], so it never enters the zero-delay
  // feed-forward region used by processBlock(). Non-finite values are treated
  // as the one-sample minimum.
  //
  // The line must be prepared with capacity for at least one sample. Call
  // pushSample() exactly once after each read to advance the line.
  [[nodiscard]] Sample readDelayedSampleAtDelayTimeMs(double delayTimeMs) const noexcept;

  // Reads from the same pre-write history state while also making the pending
  // current sample available to 0/sub-one-sample positions. This is intended
  // for an audible tap that must be evaluated before the sample is pushed:
  //   0 samples: currentSample
  //   1 sample:  previous pushed sample
  //   fractional values: linear interpolation between those positions
  // The explicit delay is clamped to [0, maximumDelayTimeMs]. Non-finite
  // values are treated as zero. Neither configured delay nor history changes.
  [[nodiscard]] Sample readDelayedSampleAtDelayTimeMs(double delayTimeMs,
                                                      Sample currentSample) const noexcept;

  // Writes one sample and advances the delay line. This operation is valid at
  // every configured delay, including zero.
  void pushSample(Sample sample) noexcept;

private:
  [[nodiscard]] std::size_t indexBehindWriteHead(std::size_t sampleOffset) const noexcept;
  [[nodiscard]] Sample readHistoryAtDelayInSamples(double delayInSamples) const noexcept;
  [[nodiscard]] Sample interpolateHistory(std::size_t wholeSampleDelay,
                                          double fractionalDelay) const noexcept;
  void updateDelayInSamples() noexcept;

  const double mMaximumDelayTimeMs;
  double mSampleRate = 0.0;
  double mDelayTimeMs = 0.0;
  double mDelayInSamples = 0.0;
  std::size_t mMaximumBlockSize = 0;
  std::size_t mWriteIndex = 0;
  std::vector<Sample> mBuffer;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
