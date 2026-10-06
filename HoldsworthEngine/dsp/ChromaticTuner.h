#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace holdsworth::dsp
{
enum class TunerPitchStatus
{
  collecting,
  noSignal,
  unstable,
  valid,
  unsupportedRate
};

// Periodicity is 1 minus the YIN normalized trough, not a probability.
// A valid estimate may have highConfidence == false: the display must require
// high confidence for acquisition and may use the weaker estimate for sustain.
struct TunerPitchEstimate
{
  TunerPitchStatus status = TunerPitchStatus::collecting;
  double frequencyHz = 0.0;
  double periodicity = 0.0;
  double rmsDbFS = -160.0;
  int midiNote = -1;
  int octave = 0;
  double cents = 0.0;
  bool highConfidence = false;
};

// Consumer-thread-only analysis. This class does not capture host audio, manage
// threads, publish results or smooth a display. Feed contiguous clean mono input
// and explicitly call analyze(); prepare/reset/push/analyze must share one owner.
// All state is fixed storage; none of these operations allocate or lock.
class ChromaticTuner final
{
public:
  static constexpr std::size_t kWindowSize = 2048;
  static constexpr double kMinimumFrequencyHz = 35.0;
  static constexpr double kMaximumFrequencyHz = 1400.0;
  static constexpr double kAcquisitionDifferenceThreshold = 0.10;
  static constexpr double kSustainDifferenceThreshold = 0.15;
  static constexpr double kAcquisitionRmsDbFS = -65.0;
  static constexpr double kSustainRmsDbFS = -70.0;

  // Exactly the six supported host rates. Unsupported rates invalidate state.
  [[nodiscard]] bool prepare(double sampleRate) noexcept;
  // Forget all history while preserving the prepared rate and coefficients.
  void reset() noexcept;
  // FIR startup samples are discarded. A nonfinite sample clears history so an
  // estimate never crosses invalid data or uses stale samples after a reset.
  void pushSamples(std::span<const float> input) noexcept;
  [[nodiscard]] TunerPitchEstimate analyze() noexcept;

  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] double analysisSampleRate() const noexcept { return mAnalysisSampleRate; }
  [[nodiscard]] std::size_t decimationFactor() const noexcept { return mDecimationFactor; }
  [[nodiscard]] std::size_t samplesAvailable() const noexcept { return mWindowSamples; }

  // A4 = 440 Hz, twelve equal-tempered notes; conversion does not impose the
  // detector's search range. Invalid frequencies return an unstable estimate.
  [[nodiscard]] static TunerPitchEstimate frequencyToPitch(double frequencyHz) noexcept;
  [[nodiscard]] static const char* noteName(int midiNote) noexcept;

private:
  static constexpr std::size_t kMaximumFirTaps = 513;
  // ceil(24000 / 35) plus one interpolation neighbor, including index zero.
  static constexpr std::size_t kDifferenceSize = 688;

  std::array<double, kMaximumFirTaps> mFirCoefficients{};
  // Duplicated history allows each decimated dot product a contiguous read.
  std::array<double, 2 * kMaximumFirTaps> mFirHistory{};
  std::array<double, kWindowSize> mWindow{};
  std::array<double, kWindowSize> mFrame{};
  std::array<double, kDifferenceSize> mDifference{};
  std::array<double, kDifferenceSize> mNormalized{};
  double mAnalysisSampleRate = 0.0;
  std::size_t mDecimationFactor = 0;
  std::size_t mFirTaps = 0;
  std::size_t mFirWrite = 0;
  std::size_t mFirSamples = 0;
  std::size_t mDecimationPhase = 0;
  std::size_t mWindowWrite = 0;
  std::size_t mWindowSamples = 0;
  std::size_t mMaximumLag = 0;
  std::size_t mIntegrationSize = 0;
  bool mPrepared = false;
};
} // namespace holdsworth::dsp
