#include "DelayBand.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{
namespace
{

struct StereoGains
{
  double left;
  double right;
};

[[nodiscard]] StereoGains equalPowerPanGains(const double pan) noexcept
{
  if (pan <= -1.0)
    return {1.0, 0.0};
  if (pan >= 1.0)
    return {0.0, 1.0};

  const double angle = (pan + 1.0) * std::numbers::pi_v<double> * 0.25;
  return {std::cos(angle), std::sin(angle)};
}

// Provisional interpretation pending Magicstomp measurement: the audible tap
// follows the instantaneous modulated loop length proportionally.
[[nodiscard]] double proportionalTapDelayTimeMs(const TapFraction tapFraction,
                                                const double loopDelayTimeMs) noexcept
{
  return tapFraction.value * loopDelayTimeMs;
}

} // namespace

DelayBand::DelayBand(const double maximumDelayTimeMs)
: mDelayLine(maximumDelayTimeMs)
{
  updatePanGains();
}

void DelayBand::prepare(const double sampleRate, const std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0)
    throw std::invalid_argument("sampleRate must be finite and greater than zero");
  if (maximumBlockSize == 0)
    throw std::invalid_argument("maximumBlockSize must be greater than zero");

  double minimumDelayTimeMs = 1000.0 / sampleRate;
  if (minimumDelayTimeMs * sampleRate * 0.001 < 1.0)
    minimumDelayTimeMs = std::nextafter(minimumDelayTimeMs, std::numeric_limits<double>::infinity());
  if (maximumDelayTimeMs() < minimumDelayTimeMs)
    throw std::invalid_argument("maximumDelayTimeMs must accommodate at least one sample");

  mDelayLine.prepare(sampleRate, maximumBlockSize);
  mDelayModulator.prepare(sampleRate);
  mLoopFilter.prepare(sampleRate);
  mTapOutputFilter.prepare(sampleRate);
  mMinimumDelayTimeMs = minimumDelayTimeMs;
  mMaximumBlockSize = maximumBlockSize;
  mPrepared = true;
  applyEffectiveDelayTime();
}

void DelayBand::reset() noexcept
{
  mDelayLine.reset();
  mDelayModulator.reset();
  mLoopFilter.reset();
  mTapOutputFilter.reset();
  mCurrentModulatedDelayTimeMs = delayTimeMs();
}

void DelayBand::setDelayTimeMs(const Sample delayTimeMs) noexcept
{
  mRequestedDelayTimeMs = std::isfinite(delayTimeMs)
                            ? std::clamp(delayTimeMs, 0.0, maximumDelayTimeMs())
                            : 0.0;
  applyEffectiveDelayTime();
}

void DelayBand::setFeedbackCoefficient(const Sample coefficient) noexcept
{
  mFeedbackCoefficient = std::isfinite(coefficient)
                           ? std::clamp(coefficient, 0.0, kMaximumFeedbackCoefficient)
                           : 0.0;
}

void DelayBand::setOutputLevel(const Sample level) noexcept
{
  mOutputLevel = std::isfinite(level) ? std::clamp(level, 0.0, 1.0) : 0.0;
}

void DelayBand::setPan(const Sample pan) noexcept
{
  mPan = std::isfinite(pan) ? std::clamp(pan, -1.0, 1.0) : 0.0;
  updatePanGains();
}

void DelayBand::setModulationRate(const ModulationRateHz rate) noexcept
{
  mDelayModulator.setRate(rate);
}

void DelayBand::setModulationDepth(const ModulationDepthMs depth) noexcept
{
  mRequestedModulationDepth.value =
    std::isfinite(depth.value) && depth.value >= 0.0 ? depth.value : 0.0;
  applyEffectiveModulationDepth();
}

void DelayBand::setModulationPhase(const ModulationPhaseCycles phase) noexcept
{
  mDelayModulator.setPhase(phase);
  mCurrentModulatedDelayTimeMs = delayTimeMs();
}

void DelayBand::setModulationWaveform(const ModulationWaveform waveform) noexcept
{
  mDelayModulator.setWaveform(waveform);
}

void DelayBand::setLoopFilterConfiguration(
  const DelayLoopFilterConfiguration& configuration) noexcept
{
  mLoopFilter.setConfiguration(configuration);
  mTapOutputFilter.setConfiguration(configuration);
}

void DelayBand::setTapFraction(const TapFraction tapFraction) noexcept
{
  const Sample sanitizedValue = std::isfinite(tapFraction.value)
                                  ? std::clamp(tapFraction.value, 0.0, 1.0)
                                  : 1.0;
  const bool isActivatingIndependentTap = mTapFraction.value == 1.0 && sanitizedValue < 1.0;
  mTapFraction.value = sanitizedValue;

  // The independent output-filter state is meaningful only while TAP is below
  // 100%. Do not revive state left dormant during the exact legacy path.
  if (isActivatingIndependentTap)
    mTapOutputFilter.reset();
}

void DelayBand::setDelaySignalPolarity(const DelaySignalPolarity polarity) noexcept
{
  switch (polarity)
  {
    case DelaySignalPolarity::normal:
    case DelaySignalPolarity::reverse:
      mDelaySignalPolarity = polarity;
      return;
  }

  mDelaySignalPolarity = DelaySignalPolarity::normal;
}

DelayBand::Sample DelayBand::advanceModulationClock(
  ModulationClockSample& clockSample) noexcept
{
  return mDelayModulator.nextOffsetMs(clockSample);
}

DelayBand::Sample DelayBand::modulationOffsetAtClockSample(
  const ModulationClockSample& clockSample) const noexcept
{
  return mDelayModulator.offsetMsAtClockSample(clockSample);
}

DelayBand::Sample DelayBand::advanceGroupModulationClock(
  const ModulationDepthMs effectiveGroupDepth,
  ModulationClockSample& clockSample) noexcept
{
  // Advance the established authoritative oscillator exactly once, then
  // re-evaluate its captured pre-advance sample with GROUP's physical depth.
  static_cast<void>(mDelayModulator.nextOffsetMs(clockSample));
  return mDelayModulator.offsetMsAtClockSample(clockSample, effectiveGroupDepth);
}

DelayBand::Sample DelayBand::groupModulationOffsetAtClockSample(
  const ModulationClockSample& clockSample,
  const ModulationDepthMs effectiveGroupDepth) const noexcept
{
  return mDelayModulator.offsetMsAtClockSample(clockSample, effectiveGroupDepth);
}

void DelayBand::resetModulationClock() noexcept
{
  mDelayModulator.reset();
  mCurrentModulatedDelayTimeMs = delayTimeMs();
}

void DelayBand::resetDelayAndFilterHistoryPreservingModulationClock() noexcept
{
  mDelayLine.reset();
  mLoopFilter.reset();
  mTapOutputFilter.reset();
  mCurrentModulatedDelayTimeMs = delayTimeMs();
}

void DelayBand::processBlock(const std::span<const Sample> monoInput,
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

  const Sample leftOutputGain = mOutputLevel * mLeftPanGain;
  const Sample rightOutputGain = mOutputLevel * mRightPanGain;
  const bool reversesAudibleDelay = mDelaySignalPolarity == DelaySignalPolarity::reverse;

  if (mTapFraction.value < 1.0)
  {
    const bool filtersAreBypassed = mLoopFilter.isBypassed();
    const Sample baseDelayTimeMs = delayTimeMs();
    const Sample maximumDelay = maximumDelayTimeMs();
    const bool hasModulation = mEffectiveModulationDepth.value != 0.0;

    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      // Both reads observe the same pre-write history. The full-loop read
      // determines feedback; the current-sample-aware TAP read observes the
      // pending lineInput at zero/sub-one-sample positions. Exactly one write
      // advances the shared history after both reads are complete.
      Sample instantaneousLoopDelayTimeMs = baseDelayTimeMs;
      if (hasModulation)
      {
        const Sample modulationOffsetMs = mDelayModulator.nextOffsetMs();
        instantaneousLoopDelayTimeMs =
          std::clamp(baseDelayTimeMs + modulationOffsetMs,
                     mMinimumDelayTimeMs,
                     maximumDelay);
      }

      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample rawLoopDelayed = hasModulation
                                      ? mDelayLine.readDelayedSampleAtDelayTimeMs(
                                          instantaneousLoopDelayTimeMs)
                                      : mDelayLine.readDelayedSample();
      const Sample filteredLoopDelayed = filtersAreBypassed
                                           ? rawLoopDelayed
                                           : mLoopFilter.processSample(rawLoopDelayed);
      const Sample lineInput =
        externalInput + mFeedbackCoefficient * filteredLoopDelayed;

      const Sample tapDelayTimeMs =
        proportionalTapDelayTimeMs(mTapFraction, instantaneousLoopDelayTimeMs);
      const Sample rawTapDelayed =
        mDelayLine.readDelayedSampleAtDelayTimeMs(tapDelayTimeMs, lineInput);
      const Sample filteredTapDelayed = filtersAreBypassed
                                          ? rawTapDelayed
                                          : mTapOutputFilter.processSample(rawTapDelayed);

      mDelayLine.pushSample(lineInput);
      if (!hasModulation)
        static_cast<void>(mDelayModulator.nextOffsetMs());
      mCurrentModulatedDelayTimeMs = instantaneousLoopDelayTimeMs;

      if (mEnabled)
      {
        if (!reversesAudibleDelay)
        {
          // Keep the established Normal output expressions verbatim.
          wetLeft[frame] = filteredTapDelayed * leftOutputGain;
          wetRight[frame] = filteredTapDelayed * rightOutputGain;
        }
        else
        {
          const Sample reversedTapDelayed = -filteredTapDelayed;
          wetLeft[frame] = reversedTapDelayed * leftOutputGain;
          wetRight[frame] = reversedTapDelayed * rightOutputGain;
        }
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    return;
  }

  // Both-filter-OFF is an explicit legacy path. Keep the established static
  // and moving recurrences operationally unchanged: no nominally-neutral
  // filter is called and no extra arithmetic can perturb Lead 121, Chorus 011,
  // or any other existing configuration.
  if (mLoopFilter.isBypassed())
  {
    // Keep the established static-delay recurrence in a dedicated path. The
    // modulator still advances once per sample, but no moving-read arithmetic
    // can perturb a zero-depth band's samples.
    if (mEffectiveModulationDepth.value == 0.0)
    {
      for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
      {
        // Capture input before writing either output so exact input/output
        // aliasing remains safe.
        const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
        const Sample delayed = mDelayLine.readDelayedSample();
        const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
        mDelayLine.pushSample(lineInput);
        static_cast<void>(mDelayModulator.nextOffsetMs());

        if (mEnabled)
        {
          if (!reversesAudibleDelay)
          {
            // Keep the established Normal output expressions verbatim.
            wetLeft[frame] = delayed * leftOutputGain;
            wetRight[frame] = delayed * rightOutputGain;
          }
          else
          {
            const Sample reversedDelayed = -delayed;
            wetLeft[frame] = reversedDelayed * leftOutputGain;
            wetRight[frame] = reversedDelayed * rightOutputGain;
          }
        }
        else
        {
          wetLeft[frame] = 0.0;
          wetRight[frame] = 0.0;
        }
      }

      mCurrentModulatedDelayTimeMs = delayTimeMs();
      return;
    }

    const Sample baseDelayTimeMs = delayTimeMs();
    const Sample maximumDelay = maximumDelayTimeMs();
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      // DelayModulator advances exactly once for every processed sample, across
      // both block and enabled-state boundaries.
      const Sample modulationOffsetMs = mDelayModulator.nextOffsetMs();
      const Sample movingDelayTimeMs =
        std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

      // The moving tap is read before the conventional external feedback write:
      // lineInput[n] = enabledInput[n] + feedback * delayed[n].
      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample delayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
      const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
      mDelayLine.pushSample(lineInput);
      mCurrentModulatedDelayTimeMs = movingDelayTimeMs;

      if (mEnabled)
      {
        if (!reversesAudibleDelay)
        {
          // Keep the established Normal output expressions verbatim.
          wetLeft[frame] = delayed * leftOutputGain;
          wetRight[frame] = delayed * rightOutputGain;
        }
        else
        {
          const Sample reversedDelayed = -delayed;
          wetLeft[frame] = reversedDelayed * leftOutputGain;
          wetRight[frame] = reversedDelayed * rightOutputGain;
        }
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    return;
  }

  // Active-filter static recurrence. The first delayed sample is filtered
  // before both output and feedback, so every repeat accumulates filtering.
  if (mEffectiveModulationDepth.value == 0.0)
  {
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample rawDelayed = mDelayLine.readDelayedSample();
      const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
      const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
      mDelayLine.pushSample(lineInput);
      static_cast<void>(mDelayModulator.nextOffsetMs());

      if (mEnabled)
      {
        if (!reversesAudibleDelay)
        {
          // Keep the established Normal output expressions verbatim.
          wetLeft[frame] = filteredDelayed * leftOutputGain;
          wetRight[frame] = filteredDelayed * rightOutputGain;
        }
        else
        {
          const Sample reversedFilteredDelayed = -filteredDelayed;
          wetLeft[frame] = reversedFilteredDelayed * leftOutputGain;
          wetRight[frame] = reversedFilteredDelayed * rightOutputGain;
        }
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    mCurrentModulatedDelayTimeMs = delayTimeMs();
    return;
  }

  // Active-filter moving recurrence. Preserve the established modulation/read
  // order and insert filtering only between the delay read and feedback write.
  const Sample baseDelayTimeMs = delayTimeMs();
  const Sample maximumDelay = maximumDelayTimeMs();
  for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
  {
    // DelayModulator advances exactly once for every processed sample, across
    // both block and enabled-state boundaries.
    const Sample modulationOffsetMs = mDelayModulator.nextOffsetMs();
    const Sample movingDelayTimeMs =
      std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

    // The moving tap is read before the conventional external feedback write:
    // lineInput[n] = enabledInput[n] + feedback * delayed[n].
    const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
    const Sample rawDelayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
    const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
    const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
    mDelayLine.pushSample(lineInput);
    mCurrentModulatedDelayTimeMs = movingDelayTimeMs;

    if (mEnabled)
    {
      if (!reversesAudibleDelay)
      {
        // Keep the established Normal output expressions verbatim.
        wetLeft[frame] = filteredDelayed * leftOutputGain;
        wetRight[frame] = filteredDelayed * rightOutputGain;
      }
      else
      {
        const Sample reversedFilteredDelayed = -filteredDelayed;
        wetLeft[frame] = reversedFilteredDelayed * leftOutputGain;
        wetRight[frame] = reversedFilteredDelayed * rightOutputGain;
      }
    }
    else
    {
      wetLeft[frame] = 0.0;
      wetRight[frame] = 0.0;
    }
  }
}

void DelayBand::processBlockUsingPrecomputedModulationOffsets(
  const std::span<const Sample> monoInput,
  const std::span<const Sample> modulationOffsetsMs,
  const std::span<Sample> wetLeft,
  const std::span<Sample> wetRight) noexcept
{
  const bool outputsAreDistinct = monoInput.empty() || wetLeft.data() != wetRight.data();
  const bool validCall = mPrepared && monoInput.size() == modulationOffsetsMs.size()
                         && monoInput.size() == wetLeft.size()
                         && monoInput.size() == wetRight.size()
                         && monoInput.size() <= mMaximumBlockSize && outputsAreDistinct;
  assert(validCall && "prepare() must precede synchronized processing, and spans must satisfy the prepared contract");
  if (!validCall)
  {
    std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
    std::fill(wetRight.begin(), wetRight.end(), 0.0);
    return;
  }

  const Sample leftOutputGain = mOutputLevel * mLeftPanGain;
  const Sample rightOutputGain = mOutputLevel * mRightPanGain;
  const bool reversesAudibleDelay = mDelaySignalPolarity == DelaySignalPolarity::reverse;

  if (mTapFraction.value < 1.0)
  {
    const bool filtersAreBypassed = mLoopFilter.isBypassed();
    const Sample baseDelayTimeMs = delayTimeMs();
    const Sample maximumDelay = maximumDelayTimeMs();
    const bool hasModulation = mEffectiveModulationDepth.value != 0.0;

    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      // The SYNC prepass has already advanced the authoritative root clock.
      // Both delay reads still observe the same pre-write history, exactly as
      // in the independent path.
      Sample instantaneousLoopDelayTimeMs = baseDelayTimeMs;
      if (hasModulation)
      {
        const Sample modulationOffsetMs = modulationOffsetsMs[frame];
        instantaneousLoopDelayTimeMs =
          std::clamp(baseDelayTimeMs + modulationOffsetMs,
                     mMinimumDelayTimeMs,
                     maximumDelay);
      }

      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample rawLoopDelayed = hasModulation
                                      ? mDelayLine.readDelayedSampleAtDelayTimeMs(
                                          instantaneousLoopDelayTimeMs)
                                      : mDelayLine.readDelayedSample();
      const Sample filteredLoopDelayed = filtersAreBypassed
                                           ? rawLoopDelayed
                                           : mLoopFilter.processSample(rawLoopDelayed);
      const Sample lineInput =
        externalInput + mFeedbackCoefficient * filteredLoopDelayed;

      const Sample tapDelayTimeMs =
        proportionalTapDelayTimeMs(mTapFraction, instantaneousLoopDelayTimeMs);
      const Sample rawTapDelayed =
        mDelayLine.readDelayedSampleAtDelayTimeMs(tapDelayTimeMs, lineInput);
      const Sample filteredTapDelayed = filtersAreBypassed
                                          ? rawTapDelayed
                                          : mTapOutputFilter.processSample(rawTapDelayed);

      mDelayLine.pushSample(lineInput);
      mCurrentModulatedDelayTimeMs = instantaneousLoopDelayTimeMs;

      if (mEnabled)
      {
        if (!reversesAudibleDelay)
        {
          // Keep the established Normal output expressions verbatim.
          wetLeft[frame] = filteredTapDelayed * leftOutputGain;
          wetRight[frame] = filteredTapDelayed * rightOutputGain;
        }
        else
        {
          const Sample reversedTapDelayed = -filteredTapDelayed;
          wetLeft[frame] = reversedTapDelayed * leftOutputGain;
          wetRight[frame] = reversedTapDelayed * rightOutputGain;
        }
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    return;
  }

  // Match the independent path's dedicated both-filter-OFF static and moving
  // recurrences. Only modulation-offset production has moved to the prepass.
  if (mLoopFilter.isBypassed())
  {
    if (mEffectiveModulationDepth.value == 0.0)
    {
      for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
      {
        const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
        const Sample delayed = mDelayLine.readDelayedSample();
        const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
        mDelayLine.pushSample(lineInput);

        if (mEnabled)
        {
          if (!reversesAudibleDelay)
          {
            // Keep the established Normal output expressions verbatim.
            wetLeft[frame] = delayed * leftOutputGain;
            wetRight[frame] = delayed * rightOutputGain;
          }
          else
          {
            const Sample reversedDelayed = -delayed;
            wetLeft[frame] = reversedDelayed * leftOutputGain;
            wetRight[frame] = reversedDelayed * rightOutputGain;
          }
        }
        else
        {
          wetLeft[frame] = 0.0;
          wetRight[frame] = 0.0;
        }
      }

      mCurrentModulatedDelayTimeMs = delayTimeMs();
      return;
    }

    const Sample baseDelayTimeMs = delayTimeMs();
    const Sample maximumDelay = maximumDelayTimeMs();
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      const Sample modulationOffsetMs = modulationOffsetsMs[frame];
      const Sample movingDelayTimeMs =
        std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample delayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
      const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
      mDelayLine.pushSample(lineInput);
      mCurrentModulatedDelayTimeMs = movingDelayTimeMs;

      if (mEnabled)
      {
        if (!reversesAudibleDelay)
        {
          // Keep the established Normal output expressions verbatim.
          wetLeft[frame] = delayed * leftOutputGain;
          wetRight[frame] = delayed * rightOutputGain;
        }
        else
        {
          const Sample reversedDelayed = -delayed;
          wetLeft[frame] = reversedDelayed * leftOutputGain;
          wetRight[frame] = reversedDelayed * rightOutputGain;
        }
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    return;
  }

  if (mEffectiveModulationDepth.value == 0.0)
  {
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
      const Sample rawDelayed = mDelayLine.readDelayedSample();
      const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
      const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
      mDelayLine.pushSample(lineInput);

      if (mEnabled)
      {
        if (!reversesAudibleDelay)
        {
          // Keep the established Normal output expressions verbatim.
          wetLeft[frame] = filteredDelayed * leftOutputGain;
          wetRight[frame] = filteredDelayed * rightOutputGain;
        }
        else
        {
          const Sample reversedFilteredDelayed = -filteredDelayed;
          wetLeft[frame] = reversedFilteredDelayed * leftOutputGain;
          wetRight[frame] = reversedFilteredDelayed * rightOutputGain;
        }
      }
      else
      {
        wetLeft[frame] = 0.0;
        wetRight[frame] = 0.0;
      }
    }

    mCurrentModulatedDelayTimeMs = delayTimeMs();
    return;
  }

  const Sample baseDelayTimeMs = delayTimeMs();
  const Sample maximumDelay = maximumDelayTimeMs();
  for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
  {
    const Sample modulationOffsetMs = modulationOffsetsMs[frame];
    const Sample movingDelayTimeMs =
      std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

    const Sample externalInput = mEnabled ? monoInput[frame] : 0.0;
    const Sample rawDelayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
    const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
    const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
    mDelayLine.pushSample(lineInput);
    mCurrentModulatedDelayTimeMs = movingDelayTimeMs;

    if (mEnabled)
    {
      if (!reversesAudibleDelay)
      {
        // Keep the established Normal output expressions verbatim.
        wetLeft[frame] = filteredDelayed * leftOutputGain;
        wetRight[frame] = filteredDelayed * rightOutputGain;
      }
      else
      {
        const Sample reversedFilteredDelayed = -filteredDelayed;
        wetLeft[frame] = reversedFilteredDelayed * leftOutputGain;
        wetRight[frame] = reversedFilteredDelayed * rightOutputGain;
      }
    }
    else
    {
      wetLeft[frame] = 0.0;
      wetRight[frame] = 0.0;
    }
  }
}

DelayBand::Sample DelayBand::connectedRoutingOutputForSample(const Sample bandInput,
                                                             const Sample audibleDelayedSignal) const noexcept
{
  // Provisional Yamaha CONNECT interpretation pending hardware measurement.
  // Keep this as the single seam for any future measured EFFECT LEVEL/send
  // placement. A disabled band bypasses its input for routing while retaining
  // the established muted-wet/reject-new-delay-input recurrence semantics.
  if (!mEnabled)
    return bandInput;

  const Sample signedAudibleDelay =
    mDelaySignalPolarity == DelaySignalPolarity::reverse ? -audibleDelayedSignal : audibleDelayedSignal;
  return bandInput + signedAudibleDelay * mOutputLevel;
}

void DelayBand::processBlockWithRoutingOutput(const std::span<const Sample> monoInput,
                                              const std::span<Sample> routedOutput, const std::span<Sample> wetLeft,
                                              const std::span<Sample> wetRight) noexcept
{
  processBlockWithRoutingOutputImpl<false>(monoInput, {}, routedOutput, wetLeft, wetRight);
}

void DelayBand::processBlockUsingPrecomputedModulationOffsetsWithRoutingOutput(
  const std::span<const Sample> monoInput, const std::span<const Sample> modulationOffsetsMs,
  const std::span<Sample> routedOutput, const std::span<Sample> wetLeft, const std::span<Sample> wetRight) noexcept
{
  processBlockWithRoutingOutputImpl<true>(monoInput, modulationOffsetsMs, routedOutput, wetLeft, wetRight);
}

template <bool UsesPrecomputedModulationOffsets>
void DelayBand::processBlockWithRoutingOutputImpl(const std::span<const Sample> monoInput,
                                                  const std::span<const Sample> modulationOffsetsMs,
                                                  const std::span<Sample> routedOutput, const std::span<Sample> wetLeft,
                                                  const std::span<Sample> wetRight) noexcept
{
  const bool outputsAreDistinct = monoInput.empty()
                                  || (routedOutput.data() != wetLeft.data() && routedOutput.data() != wetRight.data()
                                      && wetLeft.data() != wetRight.data());
  const bool offsetsAreValid = !UsesPrecomputedModulationOffsets || monoInput.size() == modulationOffsetsMs.size();
  const bool validCall = mPrepared && offsetsAreValid && monoInput.size() == routedOutput.size()
                         && monoInput.size() == wetLeft.size() && monoInput.size() == wetRight.size()
                         && monoInput.size() <= mMaximumBlockSize && outputsAreDistinct;
  assert(validCall && "prepare() must precede CONNECT processing, and spans must satisfy the prepared contract");
  if (!validCall)
  {
    std::fill(routedOutput.begin(), routedOutput.end(), 0.0);
    std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
    std::fill(wetRight.begin(), wetRight.end(), 0.0);
    return;
  }

  const Sample leftOutputGain = mOutputLevel * mLeftPanGain;
  const Sample rightOutputGain = mOutputLevel * mRightPanGain;
  const bool reversesAudibleDelay = mDelaySignalPolarity == DelaySignalPolarity::reverse;

  const auto writeOutputs = [this, routedOutput, wetLeft, wetRight, leftOutputGain, rightOutputGain,
                             reversesAudibleDelay](const std::size_t frame, const Sample bandInput,
                                                   const Sample audibleDelayedSignal) noexcept {
    routedOutput[frame] = connectedRoutingOutputForSample(bandInput, audibleDelayedSignal);

    if (!mEnabled)
    {
      wetLeft[frame] = 0.0;
      wetRight[frame] = 0.0;
      return;
    }

    if (!reversesAudibleDelay)
    {
      wetLeft[frame] = audibleDelayedSignal * leftOutputGain;
      wetRight[frame] = audibleDelayedSignal * rightOutputGain;
    }
    else
    {
      const Sample reversedAudibleDelay = -audibleDelayedSignal;
      wetLeft[frame] = reversedAudibleDelay * leftOutputGain;
      wetRight[frame] = reversedAudibleDelay * rightOutputGain;
    }
  };

  if (mTapFraction.value < 1.0)
  {
    const bool filtersAreBypassed = mLoopFilter.isBypassed();
    const Sample baseDelayTimeMs = delayTimeMs();
    const Sample maximumDelay = maximumDelayTimeMs();
    const bool hasModulation = mEffectiveModulationDepth.value != 0.0;

    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      Sample instantaneousLoopDelayTimeMs = baseDelayTimeMs;
      if (hasModulation)
      {
        Sample modulationOffsetMs = 0.0;
        if constexpr (UsesPrecomputedModulationOffsets)
          modulationOffsetMs = modulationOffsetsMs[frame];
        else
          modulationOffsetMs = mDelayModulator.nextOffsetMs();

        instantaneousLoopDelayTimeMs =
          std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);
      }

      const Sample bandInput = monoInput[frame];
      const Sample externalInput = mEnabled ? bandInput : 0.0;
      const Sample rawLoopDelayed = hasModulation
                                      ? mDelayLine.readDelayedSampleAtDelayTimeMs(instantaneousLoopDelayTimeMs)
                                      : mDelayLine.readDelayedSample();
      const Sample filteredLoopDelayed =
        filtersAreBypassed ? rawLoopDelayed : mLoopFilter.processSample(rawLoopDelayed);
      const Sample lineInput = externalInput + mFeedbackCoefficient * filteredLoopDelayed;

      const Sample tapDelayTimeMs = proportionalTapDelayTimeMs(mTapFraction, instantaneousLoopDelayTimeMs);
      const Sample rawTapDelayed = mDelayLine.readDelayedSampleAtDelayTimeMs(tapDelayTimeMs, lineInput);
      const Sample filteredTapDelayed =
        filtersAreBypassed ? rawTapDelayed : mTapOutputFilter.processSample(rawTapDelayed);

      mDelayLine.pushSample(lineInput);
      if constexpr (!UsesPrecomputedModulationOffsets)
      {
        if (!hasModulation)
          static_cast<void>(mDelayModulator.nextOffsetMs());
      }
      mCurrentModulatedDelayTimeMs = instantaneousLoopDelayTimeMs;
      writeOutputs(frame, bandInput, filteredTapDelayed);
    }

    return;
  }

  if (mLoopFilter.isBypassed())
  {
    if (mEffectiveModulationDepth.value == 0.0)
    {
      for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
      {
        const Sample bandInput = monoInput[frame];
        const Sample externalInput = mEnabled ? bandInput : 0.0;
        const Sample delayed = mDelayLine.readDelayedSample();
        const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
        mDelayLine.pushSample(lineInput);
        if constexpr (!UsesPrecomputedModulationOffsets)
          static_cast<void>(mDelayModulator.nextOffsetMs());
        writeOutputs(frame, bandInput, delayed);
      }

      mCurrentModulatedDelayTimeMs = delayTimeMs();
      return;
    }

    const Sample baseDelayTimeMs = delayTimeMs();
    const Sample maximumDelay = maximumDelayTimeMs();
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      Sample modulationOffsetMs = 0.0;
      if constexpr (UsesPrecomputedModulationOffsets)
        modulationOffsetMs = modulationOffsetsMs[frame];
      else
        modulationOffsetMs = mDelayModulator.nextOffsetMs();
      const Sample movingDelayTimeMs =
        std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

      const Sample bandInput = monoInput[frame];
      const Sample externalInput = mEnabled ? bandInput : 0.0;
      const Sample delayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
      const Sample lineInput = externalInput + mFeedbackCoefficient * delayed;
      mDelayLine.pushSample(lineInput);
      mCurrentModulatedDelayTimeMs = movingDelayTimeMs;
      writeOutputs(frame, bandInput, delayed);
    }

    return;
  }

  if (mEffectiveModulationDepth.value == 0.0)
  {
    for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
    {
      const Sample bandInput = monoInput[frame];
      const Sample externalInput = mEnabled ? bandInput : 0.0;
      const Sample rawDelayed = mDelayLine.readDelayedSample();
      const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
      const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
      mDelayLine.pushSample(lineInput);
      if constexpr (!UsesPrecomputedModulationOffsets)
        static_cast<void>(mDelayModulator.nextOffsetMs());
      writeOutputs(frame, bandInput, filteredDelayed);
    }

    mCurrentModulatedDelayTimeMs = delayTimeMs();
    return;
  }

  const Sample baseDelayTimeMs = delayTimeMs();
  const Sample maximumDelay = maximumDelayTimeMs();
  for (std::size_t frame = 0; frame < monoInput.size(); ++frame)
  {
    Sample modulationOffsetMs = 0.0;
    if constexpr (UsesPrecomputedModulationOffsets)
      modulationOffsetMs = modulationOffsetsMs[frame];
    else
      modulationOffsetMs = mDelayModulator.nextOffsetMs();
    const Sample movingDelayTimeMs =
      std::clamp(baseDelayTimeMs + modulationOffsetMs, mMinimumDelayTimeMs, maximumDelay);

    const Sample bandInput = monoInput[frame];
    const Sample externalInput = mEnabled ? bandInput : 0.0;
    const Sample rawDelayed = mDelayLine.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
    const Sample filteredDelayed = mLoopFilter.processSample(rawDelayed);
    const Sample lineInput = externalInput + mFeedbackCoefficient * filteredDelayed;
    mDelayLine.pushSample(lineInput);
    mCurrentModulatedDelayTimeMs = movingDelayTimeMs;
    writeOutputs(frame, bandInput, filteredDelayed);
  }
}

void DelayBand::applyEffectiveDelayTime() noexcept
{
  const Sample effectiveDelayTimeMs = mPrepared
                                        ? std::max(mRequestedDelayTimeMs, mMinimumDelayTimeMs)
                                        : mRequestedDelayTimeMs;
  mDelayLine.setDelayTimeMs(effectiveDelayTimeMs);
  mCurrentModulatedDelayTimeMs = effectiveDelayTimeMs;
  applyEffectiveModulationDepth();
}

void DelayBand::applyEffectiveModulationDepth() noexcept
{
  if (!mPrepared)
  {
    mEffectiveModulationDepth = ModulationDepthMs{};
    mDelayModulator.setDepth(mEffectiveModulationDepth);
    return;
  }

  // Limit depth symmetrically: the requested base remains the modulation
  // center rather than shifting near a boundary. This also guarantees every
  // moving feedback read remains in the one-sample-to-maximum history range.
  const Sample baseDelayTimeMs = delayTimeMs();
  const Sample capacityBelow = std::max(baseDelayTimeMs - mMinimumDelayTimeMs, 0.0);
  const Sample capacityAbove = std::max(maximumDelayTimeMs() - baseDelayTimeMs, 0.0);
  const Sample maximumSymmetricDepth = std::min(capacityBelow, capacityAbove);
  mEffectiveModulationDepth.value =
    std::min(mRequestedModulationDepth.value, maximumSymmetricDepth);
  mDelayModulator.setDepth(mEffectiveModulationDepth);
}

void DelayBand::updatePanGains() noexcept
{
  const StereoGains gains = equalPowerPanGains(mPan);
  mLeftPanGain = gains.left;
  mRightPanGain = gains.right;
}

} // namespace holdsworth::dsp
