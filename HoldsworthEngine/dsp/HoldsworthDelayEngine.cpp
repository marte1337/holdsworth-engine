#include "HoldsworthDelayEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace holdsworth::dsp
{

HoldsworthDelayEngine::HoldsworthDelayEngine(const double maximumDelayTimeMs)
: mBands{DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs}}
{
  // An unconfigured engine is wet-silent. A configuration or preset explicitly
  // enables the bands it uses.
  for (DelayBand& band : mBands)
    band.setEnabled(false);
}

void HoldsworthDelayEngine::prepare(const double sampleRate,
                                    const std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::invalid_argument("sampleRate must be finite and greater than zero");
  if (maximumBlockSize == 0)
    throw std::invalid_argument("maximumBlockSize must be greater than zero");

  mPrepared = false;

  // Allocate replacement scratch before changing the currently owned buffers.
  // The three swaps below cannot allocate or throw.
  std::vector<Sample> preparedInputScratch(maximumBlockSize, 0.0);
  std::vector<Sample> preparedBandWetLeft(maximumBlockSize, 0.0);
  std::vector<Sample> preparedBandWetRight(maximumBlockSize, 0.0);

  for (DelayBand& band : mBands)
    band.prepare(sampleRate, maximumBlockSize);

  mInputScratch.swap(preparedInputScratch);
  mBandWetLeft.swap(preparedBandWetLeft);
  mBandWetRight.swap(preparedBandWetRight);
  mMaximumBlockSize = maximumBlockSize;
  mPrepared = true;
}

void HoldsworthDelayEngine::reset() noexcept
{
  for (DelayBand& band : mBands)
    band.reset();

  std::fill(mInputScratch.begin(), mInputScratch.end(), 0.0);
  std::fill(mBandWetLeft.begin(), mBandWetLeft.end(), 0.0);
  std::fill(mBandWetRight.begin(), mBandWetRight.end(), 0.0);
}

void HoldsworthDelayEngine::applyConfiguration(
  const HoldsworthDelayConfiguration& configuration) noexcept
{
  for (std::size_t index = 0; index < kBandCount; ++index)
    setBandConfiguration(index, configuration.bands[index]);

  setGlobalWetOutputLevel(configuration.globalWetOutputLevel);
}

void HoldsworthDelayEngine::setBandConfiguration(
  const std::size_t bandIndex,
  const DelayBandConfiguration& configuration) noexcept
{
  const bool validIndex = bandIndex < kBandCount;
  assert(validIndex && "bandIndex must identify one of the eight DelayBands");
  if (!validIndex)
    return;

  DelayBand& band = mBands[bandIndex];
  band.setDelayTimeMs(configuration.delayTimeMs);
  band.setFeedbackCoefficient(configuration.feedback.value);
  band.setOutputLevel(configuration.outputLevel);
  band.setPan(configuration.pan);
  band.setEnabled(configuration.enabled);
}

HoldsworthDelayConfiguration HoldsworthDelayEngine::configuration() const noexcept
{
  HoldsworthDelayConfiguration result{};
  for (std::size_t index = 0; index < kBandCount; ++index)
  {
    const DelayBand& band = mBands[index];
    DelayBandConfiguration& bandConfiguration = result.bands[index];
    bandConfiguration.delayTimeMs = band.requestedDelayTimeMs();
    bandConfiguration.feedback = NormalizedFeedbackCoefficient{band.feedbackCoefficient()};
    bandConfiguration.outputLevel = band.outputLevel();
    bandConfiguration.pan = band.pan();
    bandConfiguration.enabled = band.isEnabled();
  }
  result.globalWetOutputLevel = mGlobalWetOutputLevel;
  return result;
}

void HoldsworthDelayEngine::setGlobalWetOutputLevel(const Sample level) noexcept
{
  mGlobalWetOutputLevel = std::isfinite(level) ? std::clamp(level, 0.0, 1.0) : 0.0;
}

HoldsworthDelayEngine::Sample HoldsworthDelayEngine::maximumDelayTimeMs() const noexcept
{
  return mBands.front().maximumDelayTimeMs();
}

void HoldsworthDelayEngine::processBlock(const std::span<const Sample> monoInput,
                                         const std::span<Sample> wetLeft,
                                         const std::span<Sample> wetRight) noexcept
{
  const bool outputsAreDistinct = monoInput.empty() || wetLeft.data() != wetRight.data();
  const bool validCall = mPrepared && monoInput.size() == wetLeft.size()
                         && monoInput.size() == wetRight.size()
                         && monoInput.size() <= mMaximumBlockSize && outputsAreDistinct;
  assert(validCall && "prepare() must precede processBlock(), and spans must satisfy the prepared contract");
  if (!validCall)
  {
    std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
    std::fill(wetRight.begin(), wetRight.end(), 0.0);
    return;
  }

  const std::size_t frameCount = monoInput.size();
  std::copy(monoInput.begin(), monoInput.end(), mInputScratch.begin());
  std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
  std::fill(wetRight.begin(), wetRight.end(), 0.0);

  const std::span<const Sample> inputScratch{mInputScratch.data(), frameCount};
  const std::span<Sample> bandWetLeft{mBandWetLeft.data(), frameCount};
  const std::span<Sample> bandWetRight{mBandWetRight.data(), frameCount};

  for (DelayBand& band : mBands)
  {
    band.processBlock(inputScratch, bandWetLeft, bandWetRight);
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
      wetLeft[frame] += bandWetLeft[frame];
      wetRight[frame] += bandWetRight[frame];
    }
  }

  for (std::size_t frame = 0; frame < frameCount; ++frame)
  {
    wetLeft[frame] *= mGlobalWetOutputLevel;
    wetRight[frame] *= mGlobalWetOutputLevel;
  }
}

} // namespace holdsworth::dsp
