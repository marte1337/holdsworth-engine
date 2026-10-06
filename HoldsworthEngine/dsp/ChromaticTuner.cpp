#include "ChromaticTuner.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace holdsworth::dsp
{
namespace
{
// Only used to design the fixed FIR in prepare(), never in a sample loop.
double besselI0(double x) noexcept
{
  const double quarterSquare = 0.25 * x * x;
  double sum = 1.0;
  double term = 1.0;
  for (unsigned int k = 1; k <= 64; ++k)
  {
    const double index = static_cast<double>(k);
    term *= quarterSquare / (index * index);
    sum += term;
    if (term <= sum * 1.0e-16)
      break;
  }
  return sum;
}
} // namespace

bool ChromaticTuner::prepare(double sampleRate) noexcept
{
  mPrepared = false;
  mDecimationFactor = 0;
  mAnalysisSampleRate = 0.0;
  mFirTaps = 0;
  reset();
  if (!std::isfinite(sampleRate))
    return false;
  if (sampleRate == 44100.0 || sampleRate == 48000.0)
    mDecimationFactor = 2;
  else if (sampleRate == 88200.0 || sampleRate == 96000.0)
    mDecimationFactor = 4;
  else if (sampleRate == 176400.0 || sampleRate == 192000.0)
    mDecimationFactor = 8;
  else
    return false;

  mAnalysisSampleRate = sampleRate / static_cast<double>(mDecimationFactor);
  mFirTaps = 64 * mDecimationFactor + 1;
  mMaximumLag = static_cast<std::size_t>(std::ceil(mAnalysisSampleRate / kMinimumFrequencyHz));
  mIntegrationSize = kWindowSize - mMaximumLag - 1;

  // Kaiser-windowed low-pass. Limit upper harmonics to improve raw-trough
  // parabolic accuracy while retaining the full fundamental search range and
  // useful guitar harmonics. Normalize for exact DC unity gain.
  constexpr double beta = 8.6;
  const double inverseBessel = 1.0 / besselI0(beta);
  const double cutoff = 0.20 / static_cast<double>(mDecimationFactor);
  const double middle = 0.5 * static_cast<double>(mFirTaps - 1);
  double coefficientSum = 0.0;
  for (std::size_t i = 0; i < mFirTaps; ++i)
  {
    const double offset = static_cast<double>(i) - middle;
    const double position = offset / middle;
    const double window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - position * position))) * inverseBessel;
    const double ideal = offset == 0.0 ? 2.0 * cutoff
      : std::sin(2.0 * std::numbers::pi * cutoff * offset) / (std::numbers::pi * offset);
    mFirCoefficients[i] = ideal * window;
    coefficientSum += mFirCoefficients[i];
  }
  for (std::size_t i = 0; i < mFirTaps; ++i)
    mFirCoefficients[i] /= coefficientSum;
  mPrepared = true;
  return true;
}

void ChromaticTuner::reset() noexcept
{
  // Counts guard all reads: complete FIR and analysis histories must be freshly
  // overwritten before they can be used, so clearing the arrays is unnecessary.
  mFirWrite = 0;
  mFirSamples = 0;
  mDecimationPhase = 0;
  mWindowWrite = 0;
  mWindowSamples = 0;
}

void ChromaticTuner::pushSamples(std::span<const float> input) noexcept
{
  if (!mPrepared)
    return;
  for (const float sample : input)
  {
    if (!std::isfinite(sample))
    {
      reset();
      continue;
    }
    const double value = static_cast<double>(sample);
    mFirHistory[mFirWrite] = value;
    mFirHistory[mFirWrite + mFirTaps] = value;
    const std::size_t newest = mFirWrite + mFirTaps;
    if (++mFirWrite == mFirTaps)
      mFirWrite = 0;
    mFirSamples = std::min(mFirSamples + 1, mFirTaps);
    if (++mDecimationPhase != mDecimationFactor)
      continue;
    mDecimationPhase = 0;
    if (mFirSamples != mFirTaps)
      continue;

    double filtered = 0.0;
    for (std::size_t tap = 0; tap < mFirTaps; ++tap)
      filtered += mFirCoefficients[tap] * mFirHistory[newest - tap];
    mWindow[mWindowWrite] = filtered;
    if (++mWindowWrite == kWindowSize)
      mWindowWrite = 0;
    mWindowSamples = std::min(mWindowSamples + 1, kWindowSize);
  }
}

TunerPitchEstimate ChromaticTuner::analyze() noexcept
{
  TunerPitchEstimate result;
  if (!mPrepared)
  {
    result.status = TunerPitchStatus::unsupportedRate;
    return result;
  }
  if (mWindowSamples != kWindowSize)
    return result;

  const std::size_t firstPart = kWindowSize - mWindowWrite;
  std::copy_n(mWindow.begin() + static_cast<std::ptrdiff_t>(mWindowWrite), firstPart, mFrame.begin());
  std::copy_n(mWindow.begin(), mWindowWrite, mFrame.begin() + static_cast<std::ptrdiff_t>(firstPart));
  double mean = 0.0;
  for (const double sample : mFrame)
    mean += sample;
  mean /= static_cast<double>(kWindowSize);
  double energy = 0.0;
  for (double& sample : mFrame)
  {
    sample -= mean;
    energy += sample * sample;
  }
  result.rmsDbFS = energy > 0.0
    ? std::max(-160.0, 10.0 * std::log10(energy / static_cast<double>(kWindowSize))) : -160.0;
  if (result.rmsDbFS < kSustainRmsDbFS)
  {
    result.status = TunerPitchStatus::noSignal;
    return result;
  }

  // Use one fixed integration width for every lag. The captured support also
  // includes the longest lag and one extra sample for parabolic interpolation.
  mDifference[0] = 0.0;
  mNormalized[0] = 1.0;
  double cumulative = 0.0;
  for (std::size_t lag = 1; lag <= mMaximumLag + 1; ++lag)
  {
    double difference = 0.0;
    for (std::size_t j = 0; j < mIntegrationSize; ++j)
    {
      const double delta = mFrame[j] - mFrame[j + lag];
      difference += delta * delta;
    }
    mDifference[lag] = difference;
    cumulative += difference;
    mNormalized[lag] = cumulative > 0.0
      ? difference * static_cast<double>(lag) / cumulative : 1.0;
  }

  std::size_t candidate = 0;
  double bestDifference = 1.0;
  // Examine shorter periods too: if the first credible trough is above the
  // supported frequency range, reject it rather than accepting a later octave.
  for (std::size_t lag = 2; lag <= mMaximumLag; ++lag)
  {
    bestDifference = std::min(bestDifference, mNormalized[lag]);
    if (mNormalized[lag] <= kSustainDifferenceThreshold)
    {
      candidate = lag;
      while (candidate < mMaximumLag && mNormalized[candidate + 1] < mNormalized[candidate])
        ++candidate;
      break;
    }
  }
  result.status = TunerPitchStatus::unstable;
  result.periodicity = std::clamp(1.0 - bestDifference, 0.0, 1.0);
  if (candidate == 0)
    return result;

  const double normalizedCurvature = mNormalized[candidate - 1] - 2.0 * mNormalized[candidate]
    + mNormalized[candidate + 1];
  double normalizedMinimum = mNormalized[candidate];
  if (normalizedCurvature > 0.0)
  {
    const double slope = mNormalized[candidate + 1] - mNormalized[candidate - 1];
    normalizedMinimum -= slope * slope / (8.0 * normalizedCurvature);
  }
  normalizedMinimum = std::clamp(normalizedMinimum, 0.0, 1.0);
  result.periodicity = 1.0 - normalizedMinimum;

  // CMNDF selects the period; its asymmetric normalization introduces cents
  // bias when interpolated. Refine the neighboring RAW-difference minimum.
  std::size_t rawMinimum = candidate;
  if (rawMinimum > 1 && mDifference[rawMinimum - 1] < mDifference[rawMinimum])
    --rawMinimum;
  if (rawMinimum < mMaximumLag && mDifference[rawMinimum + 1] < mDifference[rawMinimum])
    ++rawMinimum;
  const double curvature = mDifference[rawMinimum - 1] - 2.0 * mDifference[rawMinimum]
    + mDifference[rawMinimum + 1];
  if (!(curvature > 0.0))
    return result;
  const double shift = 0.5 * (mDifference[rawMinimum - 1] - mDifference[rawMinimum + 1]) / curvature;
  if (!std::isfinite(shift) || std::abs(shift) > 0.5)
    return result;
  const double frequency = mAnalysisSampleRate / (static_cast<double>(rawMinimum) + shift);
  if (!std::isfinite(frequency) || frequency < kMinimumFrequencyHz || frequency > kMaximumFrequencyHz)
    return result;

  const auto pitch = frequencyToPitch(frequency);
  result.status = pitch.status;
  result.frequencyHz = pitch.frequencyHz;
  result.midiNote = pitch.midiNote;
  result.octave = pitch.octave;
  result.cents = pitch.cents;
  result.highConfidence = normalizedMinimum <= kAcquisitionDifferenceThreshold
    && result.rmsDbFS >= kAcquisitionRmsDbFS;
  return result;
}

TunerPitchEstimate ChromaticTuner::frequencyToPitch(double frequencyHz) noexcept
{
  TunerPitchEstimate result;
  result.status = TunerPitchStatus::unstable;
  if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0)
    return result;
  const double midi = 69.0 + 12.0 * (std::log2(frequencyHz) - std::log2(440.0));
  const int note = static_cast<int>(std::round(midi));
  const int pitchClass = (note % 12 + 12) % 12;
  result.status = TunerPitchStatus::valid;
  result.frequencyHz = frequencyHz;
  result.midiNote = note;
  result.octave = (note - pitchClass) / 12 - 1;
  result.cents = 100.0 * (midi - static_cast<double>(note));
  return result;
}

const char* ChromaticTuner::noteName(int midiNote) noexcept
{
  constexpr std::array<const char*, 12> names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return names[static_cast<std::size_t>((midiNote % 12 + 12) % 12)];
}
} // namespace holdsworth::dsp
