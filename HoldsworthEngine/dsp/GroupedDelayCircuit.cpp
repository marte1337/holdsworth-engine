#include "GroupedDelayCircuit.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holdsworth::dsp
{

double provisionalGroupedOutputTapDelayTimeMs(const double groupBaseDelayTimeMs, const double outputModulationOffsetMs,
                                              const double tapFraction, const double minimumDelayTimeMs,
                                              const double maximumDelayTimeMs) noexcept
{
  const double finiteOffset = std::isfinite(outputModulationOffsetMs) ? outputModulationOffsetMs : 0.0;
  const double instantaneousOutputTime =
    std::clamp(groupBaseDelayTimeMs + finiteOffset, minimumDelayTimeMs, maximumDelayTimeMs);
  const double finiteTap = std::isfinite(tapFraction) ? std::clamp(tapFraction, 0.0, 1.0) : 1.0;
  return finiteTap * instantaneousOutputTime;
}

GroupedDelayCircuit::GroupedDelayCircuit(const GroupedDelayPhysicalCapacityMs physicalCapacity)
: mPhysicalCapacity(physicalCapacity)
, mDelayLine(physicalCapacity.value)
{
  if (!std::isfinite(physicalCapacity.value) || physicalCapacity.value < 0.0)
    throw std::invalid_argument("grouped physical capacity must be finite and non-negative");
}

void GroupedDelayCircuit::prepare(const double sampleRate, const std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::invalid_argument("sampleRate must be finite and greater than zero");
  if (maximumBlockSize == 0)
    throw std::invalid_argument("maximumBlockSize must be greater than zero");

  double minimumDelayTimeMs = 1000.0 / sampleRate;
  if (minimumDelayTimeMs * sampleRate * 0.001 < 1.0)
  {
    minimumDelayTimeMs = std::nextafter(minimumDelayTimeMs, std::numeric_limits<double>::infinity());
  }
  if (mPhysicalCapacity.value < minimumDelayTimeMs)
  {
    throw std::invalid_argument("grouped physical capacity must accommodate at least one sample");
  }

  mDelayLine.prepare(sampleRate, maximumBlockSize);
  mLoopFilter.prepare(sampleRate);
  for (DelayLoopFilter& filter : mObservationFilters)
    filter.prepare(sampleRate);

  mMinimumDelayTimeMs = minimumDelayTimeMs;
  mMaximumBlockSize = maximumBlockSize;
  mPrepared = true;
  setMembership(mHeadIndex, mEndIndex);
  applyEffectiveBaseDelayTime();
}

void GroupedDelayCircuit::reset() noexcept
{
  mDelayLine.reset();
  mLoopFilter.reset();
  for (DelayLoopFilter& filter : mObservationFilters)
    filter.reset();
}

void GroupedDelayCircuit::setMembership(const std::size_t headIndex, const std::size_t endIndex) noexcept
{
  const bool valid =
    headIndex < kHoldsworthDelayBandCount && endIndex < kHoldsworthDelayBandCount && headIndex < endIndex;
  assert(valid && "GROUP membership must be an ascending non-singleton range");
  if (!valid)
    return;

  mHeadIndex = headIndex;
  mEndIndex = endIndex;
  const std::size_t memberCount = endIndex - headIndex + 1;
  mPermittedMaximumDelayTimeMs = groupedDelayMaximumPermittedTimeMs(memberCount, mPhysicalCapacity);
  setBaseDelayTimeMs(mRequestedBaseDelayTimeMs);
}

void GroupedDelayCircuit::setBaseDelayTimeMs(const Sample delayTimeMs) noexcept
{
  mRequestedBaseDelayTimeMs =
    std::isfinite(delayTimeMs) ? std::clamp(delayTimeMs, 0.0, mPermittedMaximumDelayTimeMs) : 0.0;
  applyEffectiveBaseDelayTime();
}

void GroupedDelayCircuit::setFeedbackCoefficient(const Sample coefficient) noexcept
{
  mFeedbackCoefficient =
    std::isfinite(coefficient) ? std::clamp(coefficient, 0.0, DelayBand::kMaximumFeedbackCoefficient) : 0.0;
}

void GroupedDelayCircuit::setLoopFilterConfiguration(const DelayLoopFilterConfiguration& configuration) noexcept
{
  mLoopFilter.setConfiguration(configuration);
  for (DelayLoopFilter& filter : mObservationFilters)
    filter.setConfiguration(configuration);
}

void GroupedDelayCircuit::setOutputTapFraction(const std::size_t bandIndex, const TapFraction tapFraction) noexcept
{
  const bool valid = bandIndex < kHoldsworthDelayBandCount;
  assert(valid && "bandIndex must identify one of the eight GROUP outputs");
  if (!valid)
    return;

  const Sample sanitized = std::isfinite(tapFraction.value) ? std::clamp(tapFraction.value, 0.0, 1.0) : 1.0;
  const bool activatesIndependentObservation = mTapFractions[bandIndex].value == 1.0 && sanitized < 1.0;
  mTapFractions[bandIndex].value = sanitized;
  if (activatesIndependentObservation)
    mObservationFilters[bandIndex].reset();
}

void GroupedDelayCircuit::setOutputObservationIsIndependent(const std::size_t bandIndex,
                                                            const bool isIndependent) noexcept
{
  const bool valid = bandIndex < kHoldsworthDelayBandCount;
  assert(valid && "bandIndex must identify one of the eight GROUP outputs");
  if (!valid)
    return;

  if (!mIndependentObservationStates[bandIndex] && isIndependent)
    mObservationFilters[bandIndex].reset();
  mIndependentObservationStates[bandIndex] = isIndependent;
}

ModulationDepthMs GroupedDelayCircuit::effectiveOutputModulationDepth(
  const ModulationDepthMs requestedDepth) const noexcept
{
  const Sample sanitizedRequested =
    std::isfinite(requestedDepth.value) && requestedDepth.value >= 0.0 ? requestedDepth.value : 0.0;
  if (!mPrepared)
    return ModulationDepthMs{};

  const Sample baseDelay = baseDelayTimeMs();
  const Sample capacityBelow = std::max(baseDelay - mMinimumDelayTimeMs, 0.0);
  const Sample capacityAbove = std::max(mPermittedMaximumDelayTimeMs - baseDelay, 0.0);
  return ModulationDepthMs{std::min(sanitizedRequested, std::min(capacityBelow, capacityAbove))};
}

void GroupedDelayCircuit::processBlock(const std::span<const Sample> groupInput,
                                       const std::array<OutputParameters, kHoldsworthDelayBandCount>& outputs,
                                       const std::array<const Sample*, kHoldsworthDelayBandCount>& modulationOffsets,
                                       Sample* const routedOutputs, Sample* const wetLeftOutputs,
                                       Sample* const wetRightOutputs, const std::size_t bandStride) noexcept
{
  const bool valid = mPrepared && groupInput.size() <= mMaximumBlockSize && routedOutputs != nullptr
                     && wetLeftOutputs != nullptr && wetRightOutputs != nullptr && bandStride >= groupInput.size();
  assert(valid && "prepare() must precede GROUP processing");
  if (!valid)
    return;

  const bool filtersAreBypassed = mLoopFilter.isBypassed();
  const Sample baseDelay = baseDelayTimeMs();
  const OutputParameters& headOutput = outputs[mHeadIndex];

  for (std::size_t frame = 0; frame < groupInput.size(); ++frame)
  {
    const Sample rawLoopDelayed = mDelayLine.readDelayedSample();
    const Sample filteredLoopDelayed = filtersAreBypassed ? rawLoopDelayed : mLoopFilter.processSample(rawLoopDelayed);
    const Sample externalInput = headOutput.enabled ? groupInput[frame] : 0.0;
    const Sample lineInput = externalInput + mFeedbackCoefficient * filteredLoopDelayed;

    for (std::size_t bandIndex = mHeadIndex; bandIndex <= mEndIndex; ++bandIndex)
    {
      const Sample modulationOffset =
        modulationOffsets[bandIndex] != nullptr ? modulationOffsets[bandIndex][frame] : 0.0;
      const Sample tapDelay = provisionalGroupedOutputTapDelayTimeMs(
        baseDelay, modulationOffset, mTapFractions[bandIndex].value, mMinimumDelayTimeMs, mPermittedMaximumDelayTimeMs);

      Sample audibleDelayed = 0.0;
      if (!mIndependentObservationStates[bandIndex])
      {
        audibleDelayed = filteredLoopDelayed;
      }
      else
      {
        const Sample rawTapDelayed = mDelayLine.readDelayedSampleAtDelayTimeMs(tapDelay, lineInput);
        audibleDelayed =
          filtersAreBypassed ? rawTapDelayed : mObservationFilters[bandIndex].processSample(rawTapDelayed);
      }

      const OutputParameters& output = outputs[bandIndex];
      const Sample signedDelayed = output.polarity == DelaySignalPolarity::reverse ? -audibleDelayed : audibleDelayed;
      Sample* const routed = routedOutputs + bandIndex * bandStride;
      Sample* const wetLeft = wetLeftOutputs + bandIndex * bandStride;
      Sample* const wetRight = wetRightOutputs + bandIndex * bandStride;

      if (output.enabled)
      {
        routed[frame] = groupInput[frame] + output.outputLevel * signedDelayed;
        wetLeft[frame] = signedDelayed * output.outputLevel * output.leftPanGain;
        wetRight[frame] = signedDelayed * output.outputLevel * output.rightPanGain;
      }
      else
      {
        routed[frame] = groupInput[frame];
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    // Every output observation above sees the same pre-write history.
    mDelayLine.pushSample(lineInput);
  }
}

void GroupedDelayCircuit::applyEffectiveBaseDelayTime() noexcept
{
  const Sample effective =
    mPrepared ? std::max(mRequestedBaseDelayTimeMs, mMinimumDelayTimeMs) : mRequestedBaseDelayTimeMs;
  mDelayLine.setDelayTimeMs(effective);
}

} // namespace holdsworth::dsp
