#include "HoldsworthDelayEngine.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{
namespace
{

[[nodiscard]] bool delayBandIndex(const DelayBandId bandId,
                                  std::size_t& index) noexcept
{
  const auto oneBased = static_cast<std::uint8_t>(bandId);
  if (oneBased < 1 || oneBased > kHoldsworthDelayBandCount)
    return false;

  index = static_cast<std::size_t>(oneBased - 1);
  return true;
}

[[nodiscard]] double wrapPhaseOffset(const double phaseOffsetCycles) noexcept
{
  const double wrapped = phaseOffsetCycles - std::floor(phaseOffsetCycles);
  if (wrapped == 0.0 || wrapped >= 1.0)
    return 0.0;
  return wrapped;
}

} // namespace

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
  // The swaps below cannot allocate or throw.
  std::vector<Sample> preparedInputScratch(maximumBlockSize, 0.0);
  std::vector<Sample> preparedBandWetLeft(maximumBlockSize, 0.0);
  std::vector<Sample> preparedBandWetRight(maximumBlockSize, 0.0);
  std::vector<Sample> preparedSynchronizedModulationOffsets(
    kBandCount * maximumBlockSize,
    0.0);
  std::vector<Sample> preparedBandRoutingOutputs(
    kBandCount * maximumBlockSize,
    0.0);
  std::vector<Sample> preparedConnectedBandWetLeft(
    kBandCount * maximumBlockSize,
    0.0);
  std::vector<Sample> preparedConnectedBandWetRight(
    kBandCount * maximumBlockSize,
    0.0);

  for (DelayBand& band : mBands)
    band.prepare(sampleRate, maximumBlockSize);

  mInputScratch.swap(preparedInputScratch);
  mBandWetLeft.swap(preparedBandWetLeft);
  mBandWetRight.swap(preparedBandWetRight);
  mSynchronizedModulationOffsets.swap(preparedSynchronizedModulationOffsets);
  mBandRoutingOutputs.swap(preparedBandRoutingOutputs);
  mConnectedBandWetLeft.swap(preparedConnectedBandWetLeft);
  mConnectedBandWetRight.swap(preparedConnectedBandWetRight);
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
  std::fill(mSynchronizedModulationOffsets.begin(),
            mSynchronizedModulationOffsets.end(),
            0.0);
  std::fill(mBandRoutingOutputs.begin(), mBandRoutingOutputs.end(), 0.0);
  std::fill(mConnectedBandWetLeft.begin(), mConnectedBandWetLeft.end(), 0.0);
  std::fill(mConnectedBandWetRight.begin(), mConnectedBandWetRight.end(), 0.0);
}

HoldsworthDelayConfigurationApplyResult HoldsworthDelayEngine::applyConfiguration(
  const HoldsworthDelayConfiguration& configuration) noexcept
{
  ModulationSyncConfiguration canonicalSyncConfiguration;
  ResolvedSynchronizationPlan resolvedSyncPlan;
  const ModulationSyncApplyResult syncResult =
    resolveModulationSyncConfiguration(configuration.modulationSync,
                                       canonicalSyncConfiguration,
                                       resolvedSyncPlan);

  AudioRoutingConfiguration canonicalAudioRoutingConfiguration;
  ResolvedAudioRoutingPlan resolvedAudioRoutingPlan;
  const AudioRoutingApplyResult audioRoutingResult =
    resolveAudioRoutingConfiguration(configuration.audioRouting,
                                     canonicalAudioRoutingConfiguration,
                                     resolvedAudioRoutingPlan);

  const HoldsworthDelayConfigurationApplyResult result{syncResult,
                                                        audioRoutingResult};
  if (!result.wasApplied())
    return result;

  // Graph configuration is externally changed only at an audio-block
  // boundary. Once both validations succeed, every operation below is
  // noexcept and cannot partially fail.
  for (std::size_t index = 0; index < kBandCount; ++index)
    setBandConfiguration(index, configuration.bands[index]);

  setGlobalWetOutputLevel(configuration.globalWetOutputLevel);
  commitModulationSyncConfiguration(canonicalSyncConfiguration,
                                    resolvedSyncPlan,
                                    false);
  commitAudioRoutingConfiguration(canonicalAudioRoutingConfiguration,
                                  resolvedAudioRoutingPlan);
  return result;
}

ModulationSyncApplyResult HoldsworthDelayEngine::applyModulationSyncConfiguration(
  const ModulationSyncConfiguration& configuration) noexcept
{
  ModulationSyncConfiguration canonicalConfiguration;
  ResolvedSynchronizationPlan resolvedPlan;
  const ModulationSyncApplyResult result =
    resolveModulationSyncConfiguration(configuration,
                                       canonicalConfiguration,
                                       resolvedPlan);
  if (result != ModulationSyncApplyResult::applied)
    return result;

  commitModulationSyncConfiguration(canonicalConfiguration,
                                    resolvedPlan,
                                    true);
  return ModulationSyncApplyResult::applied;
}

AudioRoutingApplyResult HoldsworthDelayEngine::applyAudioRoutingConfiguration(
  const AudioRoutingConfiguration& configuration) noexcept
{
  AudioRoutingConfiguration canonicalConfiguration;
  ResolvedAudioRoutingPlan resolvedPlan;
  const AudioRoutingApplyResult result =
    resolveAudioRoutingConfiguration(configuration, canonicalConfiguration, resolvedPlan);
  if (result != AudioRoutingApplyResult::applied)
    return result;

  commitAudioRoutingConfiguration(canonicalConfiguration, resolvedPlan);
  return AudioRoutingApplyResult::applied;
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
  band.setModulationRate(configuration.modulationRate);
  band.setModulationDepth(configuration.modulationDepth);
  band.setModulationPhase(configuration.modulationPhase);
  band.setModulationWaveform(configuration.modulationWaveform);
  band.setLoopFilterConfiguration(configuration.loopFilter);
  band.setTapFraction(configuration.tapFraction);
  band.setDelaySignalPolarity(configuration.delaySignalPolarity);
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
    bandConfiguration.modulationRate = band.requestedModulationRate();
    bandConfiguration.modulationDepth = band.requestedModulationDepth();
    bandConfiguration.modulationPhase = band.modulationResetPhase();
    bandConfiguration.modulationWaveform = band.modulationWaveform();
    bandConfiguration.loopFilter = band.requestedLoopFilterConfiguration();
    bandConfiguration.tapFraction = band.tapFraction();
    bandConfiguration.delaySignalPolarity = band.delaySignalPolarity();
  }
  result.globalWetOutputLevel = mGlobalWetOutputLevel;
  result.modulationSync = mModulationSyncConfiguration;
  result.audioRouting = mAudioRoutingConfiguration;
  return result;
}

ModulationSyncApplyResult HoldsworthDelayEngine::resolveModulationSyncConfiguration(
  const ModulationSyncConfiguration& requested,
  ModulationSyncConfiguration& canonical,
  ResolvedSynchronizationPlan& resolved) noexcept
{
  canonical = requested;
  resolved = ResolvedSynchronizationPlan{};
  for (std::size_t index = 0; index < kBandCount; ++index)
    resolved.bands[index].masterIndex = index;

  // Validate and canonicalize every direct request before resolving topology.
  // This entire pass operates on fixed local storage and cannot mutate the
  // active engine graph.
  for (std::size_t slaveIndex = 0; slaveIndex < kBandCount; ++slaveIndex)
  {
    auto& relationship = canonical.relationships[slaveIndex];
    if (!relationship.has_value())
      continue;

    std::size_t masterIndex = 0;
    if (!delayBandIndex(relationship->masterBand, masterIndex))
      return ModulationSyncApplyResult::invalidMasterReference;
    if (!std::isfinite(relationship->phaseOffset.value))
      return ModulationSyncApplyResult::invalidPhaseOffset;
    if (masterIndex == slaveIndex)
      return ModulationSyncApplyResult::selfReference;

    relationship->phaseOffset.value =
      wrapPhaseOffset(relationship->phaseOffset.value);
  }

  // Report circular relationships distinctly from unsupported acyclic chains.
  // Each band has at most one outgoing master edge, so a fixed eight-step walk
  // is sufficient and requires no allocation or recursion.
  for (std::size_t startIndex = 0; startIndex < kBandCount; ++startIndex)
  {
    std::array<bool, kBandCount> visited{};
    std::size_t currentIndex = startIndex;
    while (canonical.relationships[currentIndex].has_value())
    {
      if (visited[currentIndex])
        return ModulationSyncApplyResult::cycleDetected;
      visited[currentIndex] = true;

      std::size_t masterIndex = 0;
      static_cast<void>(delayBandIndex(
        canonical.relationships[currentIndex]->masterBand,
        masterIndex));
      currentIndex = masterIndex;
    }
  }

  for (std::size_t slaveIndex = 0; slaveIndex < kBandCount; ++slaveIndex)
  {
    const auto& relationship = canonical.relationships[slaveIndex];
    if (!relationship.has_value())
      continue;

    std::size_t masterIndex = 0;
    static_cast<void>(delayBandIndex(relationship->masterBand, masterIndex));
    if (canonical.relationships[masterIndex].has_value())
      return ModulationSyncApplyResult::unsupportedChain;

    ResolvedSynchronization& slave = resolved.bands[slaveIndex];
    slave.isSynchronizedSlave = true;
    slave.masterIndex = masterIndex;
    slave.phaseOffsetCycles = relationship->phaseOffset.value;

    if (slave.phaseOffsetCycles == 0.0)
    {
      slave.rotationKind = PhaseRotationKind::zero;
    }
    else if (slave.phaseOffsetCycles == 0.25)
    {
      slave.rotationKind = PhaseRotationKind::quarter;
    }
    else if (slave.phaseOffsetCycles == 0.5)
    {
      slave.rotationKind = PhaseRotationKind::half;
    }
    else if (slave.phaseOffsetCycles == 0.75)
    {
      slave.rotationKind = PhaseRotationKind::threeQuarter;
    }
    else
    {
      slave.rotationKind = PhaseRotationKind::arbitrary;
      const Sample phaseAngle =
        2.0 * std::numbers::pi_v<Sample> * slave.phaseOffsetCycles;
      slave.sineOffset = std::sin(phaseAngle);
      slave.cosineOffset = std::cos(phaseAngle);
    }

    ResolvedSynchronization& root = resolved.bands[masterIndex];
    root.isSynchronizationRoot = true;
    root.slaveIndices[root.slaveCount] = slaveIndex;
    ++root.slaveCount;
    resolved.hasSynchronization = true;
  }

  return ModulationSyncApplyResult::applied;
}

AudioRoutingApplyResult HoldsworthDelayEngine::resolveAudioRoutingConfiguration(
  const AudioRoutingConfiguration& requested, AudioRoutingConfiguration& canonical,
  ResolvedAudioRoutingPlan& resolved) noexcept
{
  canonical = requested;
  resolved = ResolvedAudioRoutingPlan{};
  resolved.sourceIndices.fill(kBandCount);

  std::array<std::size_t, kBandCount> incomingEdgeCounts{};
  for (std::size_t destinationIndex = 0; destinationIndex < kBandCount; ++destinationIndex)
  {
    const auto& input = canonical.inputs[destinationIndex];
    if (!input.has_value())
      continue;

    std::size_t sourceIndex = 0;
    if (!delayBandIndex(input->sourceBand, sourceIndex))
      return AudioRoutingApplyResult::invalidSourceReference;
    if (sourceIndex == destinationIndex)
      return AudioRoutingApplyResult::selfReference;

    resolved.sourceIndices[destinationIndex] = sourceIndex;
    incomingEdgeCounts[destinationIndex] = 1;
    resolved.hasConnections = true;
  }

  // Resolve a deterministic topological order once at configuration time.
  // At each step the lowest-numbered ready band wins. The destination-indexed
  // representation makes fan-in unrepresentable while allowing chains,
  // arbitrary numerical direction, and source fan-out.
  std::array<bool, kBandCount> wasScheduled{};
  for (std::size_t orderIndex = 0; orderIndex < kBandCount; ++orderIndex)
  {
    std::size_t readyIndex = kBandCount;
    for (std::size_t candidateIndex = 0; candidateIndex < kBandCount; ++candidateIndex)
    {
      if (!wasScheduled[candidateIndex] && incomingEdgeCounts[candidateIndex] == 0)
      {
        readyIndex = candidateIndex;
        break;
      }
    }

    if (readyIndex == kBandCount)
      return AudioRoutingApplyResult::cycleDetected;

    resolved.processingOrder[orderIndex] = readyIndex;
    wasScheduled[readyIndex] = true;

    for (std::size_t destinationIndex = 0; destinationIndex < kBandCount; ++destinationIndex)
    {
      if (!wasScheduled[destinationIndex] && resolved.sourceIndices[destinationIndex] == readyIndex)
      {
        incomingEdgeCounts[destinationIndex] = 0;
      }
    }
  }

  return AudioRoutingApplyResult::applied;
}

void HoldsworthDelayEngine::commitModulationSyncConfiguration(
  const ModulationSyncConfiguration& canonical,
  const ResolvedSynchronizationPlan& resolved,
  const bool resetNewlyIndependentSlaveClocks) noexcept
{
  if (resetNewlyIndependentSlaveClocks)
  {
    for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
    {
      const bool wasSynchronized =
        mModulationSyncConfiguration.relationships[bandIndex].has_value();
      const bool willBeSynchronized =
        canonical.relationships[bandIndex].has_value();
      if (wasSynchronized && !willBeSynchronized)
        mBands[bandIndex].resetModulationClock();
    }
  }

  mModulationSyncConfiguration = canonical;
  mResolvedSynchronizationPlan = resolved;
}

void HoldsworthDelayEngine::commitAudioRoutingConfiguration(const AudioRoutingConfiguration& canonical,
                                                            const ResolvedAudioRoutingPlan& resolved) noexcept
{
  mAudioRoutingConfiguration = canonical;
  mResolvedAudioRoutingPlan = resolved;
}

void HoldsworthDelayEngine::generateSynchronizedModulationOffsets(
  const std::size_t frameCount) noexcept
{
  for (std::size_t rootIndex = 0; rootIndex < kBandCount; ++rootIndex)
  {
    const ResolvedSynchronization& root =
      mResolvedSynchronizationPlan.bands[rootIndex];
    if (!root.isSynchronizationRoot)
      continue;

    Sample* const rootOffsets =
      mSynchronizedModulationOffsets.data() + rootIndex * mMaximumBlockSize;
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
      ModulationClockSample rootClockSample;
      rootOffsets[frame] =
        mBands[rootIndex].advanceModulationClock(rootClockSample);

      for (std::size_t member = 0; member < root.slaveCount; ++member)
      {
        const std::size_t slaveIndex = root.slaveIndices[member];
        const ResolvedSynchronization& slave =
          mResolvedSynchronizationPlan.bands[slaveIndex];

        ModulationClockSample slaveClockSample;
        slaveClockSample.phase.value =
          rootClockSample.phase.value + slave.phaseOffsetCycles;
        if (slaveClockSample.phase.value >= 1.0)
          slaveClockSample.phase.value -= 1.0;

        switch (slave.rotationKind)
        {
          case PhaseRotationKind::zero:
            slaveClockSample.sine = rootClockSample.sine;
            slaveClockSample.cosine = rootClockSample.cosine;
            break;
          case PhaseRotationKind::quarter:
            slaveClockSample.sine = rootClockSample.cosine;
            slaveClockSample.cosine = -rootClockSample.sine;
            break;
          case PhaseRotationKind::half:
            slaveClockSample.sine = -rootClockSample.sine;
            slaveClockSample.cosine = -rootClockSample.cosine;
            break;
          case PhaseRotationKind::threeQuarter:
            slaveClockSample.sine = -rootClockSample.cosine;
            slaveClockSample.cosine = rootClockSample.sine;
            break;
          case PhaseRotationKind::arbitrary:
            slaveClockSample.sine =
              rootClockSample.sine * slave.cosineOffset
              + rootClockSample.cosine * slave.sineOffset;
            slaveClockSample.cosine =
              rootClockSample.cosine * slave.cosineOffset
              - rootClockSample.sine * slave.sineOffset;
            break;
        }

        Sample* const slaveOffsets =
          mSynchronizedModulationOffsets.data()
          + slaveIndex * mMaximumBlockSize;
        slaveOffsets[frame] =
          mBands[slaveIndex].modulationOffsetAtClockSample(slaveClockSample);
      }
    }
  }
}

void HoldsworthDelayEngine::setGlobalWetOutputLevel(const Sample level) noexcept
{
  mGlobalWetOutputLevel = std::isfinite(level) ? std::clamp(level, 0.0, 1.0) : 0.0;
}

HoldsworthDelayEngine::Sample HoldsworthDelayEngine::maximumDelayTimeMs() const noexcept
{
  return mBands.front().maximumDelayTimeMs();
}

void HoldsworthDelayEngine::processConnectedBlock(const std::span<const Sample> monoInput,
                                                  const std::span<Sample> wetLeft,
                                                  const std::span<Sample> wetRight) noexcept
{
  const std::size_t frameCount = monoInput.size();
  std::copy(monoInput.begin(), monoInput.end(), mInputScratch.begin());
  std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
  std::fill(wetRight.begin(), wetRight.end(), 0.0);

  if (mResolvedSynchronizationPlan.hasSynchronization)
    generateSynchronizedModulationOffsets(frameCount);

  for (const std::size_t bandIndex : mResolvedAudioRoutingPlan.processingOrder)
  {
    const std::size_t sourceIndex = mResolvedAudioRoutingPlan.sourceIndices[bandIndex];
    const Sample* const bandInputData =
      sourceIndex == kBandCount ? mInputScratch.data() : mBandRoutingOutputs.data() + sourceIndex * mMaximumBlockSize;
    const std::span<const Sample> bandInput{bandInputData, frameCount};
    const std::span<Sample> routingOutput{mBandRoutingOutputs.data() + bandIndex * mMaximumBlockSize, frameCount};
    const std::span<Sample> bandWetLeft{mConnectedBandWetLeft.data() + bandIndex * mMaximumBlockSize, frameCount};
    const std::span<Sample> bandWetRight{mConnectedBandWetRight.data() + bandIndex * mMaximumBlockSize, frameCount};

    const ResolvedSynchronization& synchronization = mResolvedSynchronizationPlan.bands[bandIndex];
    if (synchronization.isSynchronizedSlave || synchronization.isSynchronizationRoot)
    {
      const std::span<const Sample> modulationOffsets{
        mSynchronizedModulationOffsets.data() + bandIndex * mMaximumBlockSize, frameCount};
      mBands[bandIndex].processBlockUsingPrecomputedModulationOffsetsWithRoutingOutput(
        bandInput, modulationOffsets, routingOutput, bandWetLeft, bandWetRight);
    }
    else
    {
      mBands[bandIndex].processBlockWithRoutingOutput(bandInput, routingOutput, bandWetLeft, bandWetRight);
    }
  }

  // Routing fan-out never changes final summation multiplicity. Each band's
  // own wet contribution is added exactly once, in stable Band 1..8 order.
  for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
  {
    const Sample* const bandWetLeft = mConnectedBandWetLeft.data() + bandIndex * mMaximumBlockSize;
    const Sample* const bandWetRight = mConnectedBandWetRight.data() + bandIndex * mMaximumBlockSize;
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

  if (mResolvedAudioRoutingPlan.hasConnections)
  {
    processConnectedBlock(monoInput, wetLeft, wetRight);
    return;
  }

  const std::size_t frameCount = monoInput.size();
  std::copy(monoInput.begin(), monoInput.end(), mInputScratch.begin());
  std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
  std::fill(wetRight.begin(), wetRight.end(), 0.0);

  const std::span<const Sample> inputScratch{mInputScratch.data(), frameCount};
  const std::span<Sample> bandWetLeft{mBandWetLeft.data(), frameCount};
  const std::span<Sample> bandWetRight{mBandWetRight.data(), frameCount};

  if (!mResolvedSynchronizationPlan.hasSynchronization)
  {
    for (DelayBand& band : mBands)
    {
      band.processBlock(inputScratch, bandWetLeft, bandWetRight);
      for (std::size_t frame = 0; frame < frameCount; ++frame)
      {
        wetLeft[frame] += bandWetLeft[frame];
        wetRight[frame] += bandWetRight[frame];
      }
    }
  }
  else
  {
    generateSynchronizedModulationOffsets(frameCount);
    for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
    {
      const ResolvedSynchronization& synchronization =
        mResolvedSynchronizationPlan.bands[bandIndex];
      if (synchronization.isSynchronizedSlave
          || synchronization.isSynchronizationRoot)
      {
        const std::span<const Sample> modulationOffsets{
          mSynchronizedModulationOffsets.data()
            + bandIndex * mMaximumBlockSize,
          frameCount};
        mBands[bandIndex].processBlockUsingPrecomputedModulationOffsets(
          inputScratch,
          modulationOffsets,
          bandWetLeft,
          bandWetRight);
      }
      else
      {
        mBands[bandIndex].processBlock(inputScratch,
                                       bandWetLeft,
                                       bandWetRight);
      }

      for (std::size_t frame = 0; frame < frameCount; ++frame)
      {
        wetLeft[frame] += bandWetLeft[frame];
        wetRight[frame] += bandWetRight[frame];
      }
    }
  }

  for (std::size_t frame = 0; frame < frameCount; ++frame)
  {
    wetLeft[frame] *= mGlobalWetOutputLevel;
    wetRight[frame] *= mGlobalWetOutputLevel;
  }
}

} // namespace holdsworth::dsp
