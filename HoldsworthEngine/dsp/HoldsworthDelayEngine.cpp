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
: HoldsworthDelayEngine(maximumDelayTimeMs, GroupedDelayPhysicalCapacityMs{maximumDelayTimeMs})
{
}

HoldsworthDelayEngine::HoldsworthDelayEngine(const double maximumDelayTimeMs,
                                             const GroupedDelayPhysicalCapacityMs groupedPhysicalCapacity)
: mBands{DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs},
         DelayBand{maximumDelayTimeMs}}
, mGroupedPhysicalCapacity(groupedPhysicalCapacity)
, mGroupedCircuits{GroupedDelayCircuit{groupedPhysicalCapacity}, GroupedDelayCircuit{groupedPhysicalCapacity},
                   GroupedDelayCircuit{groupedPhysicalCapacity}, GroupedDelayCircuit{groupedPhysicalCapacity},
                   GroupedDelayCircuit{groupedPhysicalCapacity}, GroupedDelayCircuit{groupedPhysicalCapacity},
                   GroupedDelayCircuit{groupedPhysicalCapacity}, GroupedDelayCircuit{groupedPhysicalCapacity}}
{
  // An unconfigured engine is wet-silent. A configuration or preset explicitly
  // enables the bands it uses.
  for (DelayBand& band : mBands)
    band.setEnabled(false);

  for (std::size_t index = 0; index < kBandCount; ++index)
  {
    mResolvedDelayGroupingPlan.ownerHeadIndices[index] = index;
    mResolvedDelayGroupingPlan.endIndicesByHead[index] = index;
    mResolvedGroupAudioProcessingPlan.processingOrder[index] = index;
  }
  mResolvedGroupAudioProcessingPlan.nodeCount = kBandCount;
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
  for (GroupedDelayCircuit& circuit : mGroupedCircuits)
    circuit.prepare(sampleRate, maximumBlockSize);

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
  for (GroupedDelayCircuit& circuit : mGroupedCircuits)
    circuit.reset();

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

  DelayGroupingConfiguration canonicalDelayGroupingConfiguration;
  ResolvedDelayGroupingPlan resolvedDelayGroupingPlan;
  const DelayGroupingApplyResult delayGroupingResult = resolveDelayGroupingConfiguration(
    configuration.delayGrouping, configuration.bands, canonicalDelayGroupingConfiguration, resolvedDelayGroupingPlan);

  ResolvedGroupAudioProcessingPlan resolvedGroupAudioProcessingPlan;
  GroupAudioCompositionApplyResult groupAudioCompositionResult = GroupAudioCompositionApplyResult::applied;
  if (audioRoutingResult == AudioRoutingApplyResult::applied
      && delayGroupingResult == DelayGroupingApplyResult::applied)
  {
    groupAudioCompositionResult =
      resolveGroupAudioComposition(canonicalAudioRoutingConfiguration, resolvedAudioRoutingPlan,
                                   resolvedDelayGroupingPlan, resolvedGroupAudioProcessingPlan);
  }

  const HoldsworthDelayConfigurationApplyResult result{
    syncResult, audioRoutingResult, delayGroupingResult, groupAudioCompositionResult};
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
  commitDelayGroupingConfiguration(canonicalDelayGroupingConfiguration,
                                   resolvedDelayGroupingPlan,
                                   configuration.bands);
  commitGroupAudioProcessingPlan(resolvedGroupAudioProcessingPlan);
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

  ResolvedGroupAudioProcessingPlan resolvedGroupAudioProcessingPlan;
  const GroupAudioCompositionApplyResult compositionResult = resolveGroupAudioComposition(
    canonicalConfiguration, resolvedPlan, mResolvedDelayGroupingPlan, resolvedGroupAudioProcessingPlan);
  if (compositionResult == GroupAudioCompositionApplyResult::nonHeadConnectedDestination)
    return AudioRoutingApplyResult::nonHeadGroupedDestination;
  if (compositionResult == GroupAudioCompositionApplyResult::collapsedCycleDetected)
    return AudioRoutingApplyResult::groupCollapsedCycleDetected;

  commitAudioRoutingConfiguration(canonicalConfiguration, resolvedPlan);
  commitGroupAudioProcessingPlan(resolvedGroupAudioProcessingPlan);
  return AudioRoutingApplyResult::applied;
}

DelayGroupingApplyResult HoldsworthDelayEngine::applyDelayGroupingConfiguration(
  const DelayGroupingConfiguration& configuration) noexcept
{
  const auto currentBands = this->configuration().bands;
  DelayGroupingConfiguration canonicalConfiguration;
  ResolvedDelayGroupingPlan resolvedPlan;
  const DelayGroupingApplyResult result =
    resolveDelayGroupingConfiguration(configuration, currentBands, canonicalConfiguration, resolvedPlan);
  if (result != DelayGroupingApplyResult::applied)
    return result;

  ResolvedGroupAudioProcessingPlan resolvedGroupAudioProcessingPlan;
  const GroupAudioCompositionApplyResult compositionResult = resolveGroupAudioComposition(
    mAudioRoutingConfiguration, mResolvedAudioRoutingPlan, resolvedPlan, resolvedGroupAudioProcessingPlan);
  if (compositionResult == GroupAudioCompositionApplyResult::nonHeadConnectedDestination)
    return DelayGroupingApplyResult::nonHeadConnectedDestination;
  if (compositionResult == GroupAudioCompositionApplyResult::collapsedCycleDetected)
    return DelayGroupingApplyResult::collapsedCycleDetected;

  commitDelayGroupingConfiguration(canonicalConfiguration, resolvedPlan, currentBands);
  commitGroupAudioProcessingPlan(resolvedGroupAudioProcessingPlan);
  return DelayGroupingApplyResult::applied;
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

  if (mResolvedDelayGroupingPlan.isGroupHead[bandIndex])
  {
    synchronizeGroupedCircuitParameters(bandIndex, configuration.delayTimeMs);
  }
  else
  {
    const std::size_t ownerHead = mResolvedDelayGroupingPlan.ownerHeadIndices[bandIndex];
    if (ownerHead != bandIndex)
    {
      synchronizeGroupedCircuitParameters(ownerHead, mGroupedCircuits[ownerHead].requestedBaseDelayTimeMs());
    }
  }
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
  result.delayGrouping = mDelayGroupingConfiguration;
  for (std::size_t headIndex = 0; headIndex < kBandCount; ++headIndex)
  {
    if (mResolvedDelayGroupingPlan.isGroupHead[headIndex])
    {
      result.bands[headIndex].delayTimeMs = mGroupedCircuits[headIndex].requestedBaseDelayTimeMs();
    }
  }
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

DelayGroupingApplyResult HoldsworthDelayEngine::resolveDelayGroupingConfiguration(
  const DelayGroupingConfiguration& requested, const std::array<DelayBandConfiguration, kBandCount>& bandConfigurations,
  DelayGroupingConfiguration& canonical, ResolvedDelayGroupingPlan& resolved) const noexcept
{
  canonical = requested;
  resolved = ResolvedDelayGroupingPlan{};
  for (std::size_t index = 0; index < kBandCount; ++index)
  {
    resolved.ownerHeadIndices[index] = index;
    resolved.endIndicesByHead[index] = index;
  }

  std::array<bool, kBandCount> claimedMembership{};
  for (std::size_t headIndex = 0; headIndex < kBandCount; ++headIndex)
  {
    const auto& requestedRange = canonical.groupsByHead[headIndex];
    if (!requestedRange.has_value())
      continue;

    std::size_t endIndex = 0;
    if (!delayBandIndex(requestedRange->endBand, endIndex))
      return DelayGroupingApplyResult::invalidBandReference;
    if (endIndex == headIndex)
      return DelayGroupingApplyResult::singletonRange;
    if (endIndex < headIndex)
      return DelayGroupingApplyResult::descendingRange;

    for (std::size_t memberIndex = headIndex; memberIndex <= endIndex; ++memberIndex)
    {
      if (claimedMembership[memberIndex])
        return DelayGroupingApplyResult::overlappingMembership;
      claimedMembership[memberIndex] = true;
    }

    const std::size_t memberCount = endIndex - headIndex + 1;
    const Sample maximumPermittedDelay = groupedDelayMaximumPermittedTimeMs(memberCount, mGroupedPhysicalCapacity);
    const Sample requestedDelay = std::isfinite(bandConfigurations[headIndex].delayTimeMs)
                                    ? std::max(bandConfigurations[headIndex].delayTimeMs, 0.0)
                                    : 0.0;
    if (requestedDelay > maximumPermittedDelay)
      return DelayGroupingApplyResult::delayTimeExceedsCapacity;

    resolved.isGroupHead[headIndex] = true;
    resolved.endIndicesByHead[headIndex] = endIndex;
    for (std::size_t memberIndex = headIndex; memberIndex <= endIndex; ++memberIndex)
      resolved.ownerHeadIndices[memberIndex] = headIndex;
    resolved.hasGroups = true;
  }

  return DelayGroupingApplyResult::applied;
}

GroupAudioCompositionApplyResult HoldsworthDelayEngine::resolveGroupAudioComposition(
  const AudioRoutingConfiguration& audioRouting, const ResolvedAudioRoutingPlan& resolvedAudioRouting,
  const ResolvedDelayGroupingPlan& grouping, ResolvedGroupAudioProcessingPlan& resolved) noexcept
{
  resolved = ResolvedGroupAudioProcessingPlan{};

  for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
  {
    const std::size_t ownerHead = grouping.ownerHeadIndices[bandIndex];
    if (ownerHead != bandIndex && audioRouting.inputs[bandIndex].has_value())
    {
      return GroupAudioCompositionApplyResult::nonHeadConnectedDestination;
    }
  }

  std::array<bool, kBandCount> isNodeHead{};
  std::array<std::size_t, kBandCount> incomingEdgeCounts{};
  for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
  {
    const std::size_t headIndex = grouping.ownerHeadIndices[bandIndex];
    if (headIndex == bandIndex)
      isNodeHead[headIndex] = true;
  }

  for (std::size_t destinationHead = 0; destinationHead < kBandCount; ++destinationHead)
  {
    if (!isNodeHead[destinationHead])
      continue;

    const std::size_t sourceBand = resolvedAudioRouting.sourceIndices[destinationHead];
    if (sourceBand == kBandCount)
      continue;

    const std::size_t sourceHead = grouping.ownerHeadIndices[sourceBand];
    if (sourceHead == destinationHead)
      return GroupAudioCompositionApplyResult::collapsedCycleDetected;
    incomingEdgeCounts[destinationHead] = 1;
  }

  std::array<bool, kBandCount> wasScheduled{};
  std::size_t scheduledCount = 0;
  while (scheduledCount < kBandCount)
  {
    std::size_t readyHead = kBandCount;
    for (std::size_t candidateHead = 0; candidateHead < kBandCount; ++candidateHead)
    {
      if (isNodeHead[candidateHead] && !wasScheduled[candidateHead] && incomingEdgeCounts[candidateHead] == 0)
      {
        readyHead = candidateHead;
        break;
      }
    }

    if (readyHead == kBandCount)
      break;

    resolved.processingOrder[resolved.nodeCount] = readyHead;
    ++resolved.nodeCount;
    wasScheduled[readyHead] = true;
    ++scheduledCount;

    for (std::size_t destinationHead = 0; destinationHead < kBandCount; ++destinationHead)
    {
      if (!isNodeHead[destinationHead] || wasScheduled[destinationHead] || incomingEdgeCounts[destinationHead] == 0)
        continue;

      const std::size_t sourceBand = resolvedAudioRouting.sourceIndices[destinationHead];
      if (sourceBand != kBandCount && grouping.ownerHeadIndices[sourceBand] == readyHead)
        incomingEdgeCounts[destinationHead] = 0;
    }
  }

  std::size_t nodeCount = 0;
  for (const bool nodeHead : isNodeHead)
    nodeCount += nodeHead ? 1U : 0U;
  if (resolved.nodeCount != nodeCount)
    return GroupAudioCompositionApplyResult::collapsedCycleDetected;

  return GroupAudioCompositionApplyResult::applied;
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

void HoldsworthDelayEngine::commitDelayGroupingConfiguration(
  const DelayGroupingConfiguration& canonical, const ResolvedDelayGroupingPlan& resolved,
  const std::array<DelayBandConfiguration, kBandCount>& bandConfigurations) noexcept
{
  std::array<bool, kBandCount> affectedBands{};
  for (std::size_t headIndex = 0; headIndex < kBandCount; ++headIndex)
  {
    const bool oldIsGroup = mResolvedDelayGroupingPlan.isGroupHead[headIndex];
    const bool newIsGroup = resolved.isGroupHead[headIndex];
    const std::size_t oldEnd = mResolvedDelayGroupingPlan.endIndicesByHead[headIndex];
    const std::size_t newEnd = resolved.endIndicesByHead[headIndex];
    const bool topologyChanged = oldIsGroup != newIsGroup || (oldIsGroup && oldEnd != newEnd);
    if (!topologyChanged)
      continue;

    if (oldIsGroup)
    {
      for (std::size_t bandIndex = headIndex; bandIndex <= oldEnd; ++bandIndex)
        affectedBands[bandIndex] = true;
      mGroupedCircuits[headIndex].reset();
    }
    if (newIsGroup)
    {
      for (std::size_t bandIndex = headIndex; bandIndex <= newEnd; ++bandIndex)
        affectedBands[bandIndex] = true;
      mGroupedCircuits[headIndex].reset();
    }
  }

  for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
  {
    if (affectedBands[bandIndex])
      mBands[bandIndex].resetDelayAndFilterHistoryPreservingModulationClock();
  }

  mDelayGroupingConfiguration = canonical;
  mResolvedDelayGroupingPlan = resolved;

  for (std::size_t headIndex = 0; headIndex < kBandCount; ++headIndex)
  {
    if (!resolved.isGroupHead[headIndex])
      continue;

    GroupedDelayCircuit& circuit = mGroupedCircuits[headIndex];
    circuit.setMembership(headIndex, resolved.endIndicesByHead[headIndex]);
    synchronizeGroupedCircuitParameters(headIndex, bandConfigurations[headIndex].delayTimeMs);
  }
}

void HoldsworthDelayEngine::commitGroupAudioProcessingPlan(const ResolvedGroupAudioProcessingPlan& resolved) noexcept
{
  mResolvedGroupAudioProcessingPlan = resolved;
}

void HoldsworthDelayEngine::synchronizeGroupedCircuitParameters(const std::size_t headIndex,
                                                                const Sample requestedBaseDelayTimeMs) noexcept
{
  const bool valid = headIndex < kBandCount && mResolvedDelayGroupingPlan.isGroupHead[headIndex];
  assert(valid && "headIndex must identify an active GROUP head");
  if (!valid)
    return;

  GroupedDelayCircuit& circuit = mGroupedCircuits[headIndex];
  const DelayBand& headBand = mBands[headIndex];
  circuit.setBaseDelayTimeMs(requestedBaseDelayTimeMs);
  circuit.setFeedbackCoefficient(headBand.feedbackCoefficient());
  circuit.setLoopFilterConfiguration(headBand.requestedLoopFilterConfiguration());
  const std::size_t endIndex = mResolvedDelayGroupingPlan.endIndicesByHead[headIndex];
  for (std::size_t bandIndex = headIndex; bandIndex <= endIndex; ++bandIndex)
  {
    circuit.setOutputTapFraction(bandIndex, mBands[bandIndex].tapFraction());
    const bool hasIndependentObservation =
      mBands[bandIndex].tapFraction().value < 1.0
      || circuit.effectiveOutputModulationDepth(mBands[bandIndex].requestedModulationDepth()).value != 0.0;
    circuit.setOutputObservationIsIndependent(bandIndex, hasIndependentObservation);
  }
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

void HoldsworthDelayEngine::generateGroupedModulationOffsets(const std::size_t frameCount) noexcept
{
  std::array<ModulationDepthMs, kBandCount> effectiveDepths{
    ModulationDepthMs{},
    ModulationDepthMs{},
    ModulationDepthMs{},
    ModulationDepthMs{},
    ModulationDepthMs{},
    ModulationDepthMs{},
    ModulationDepthMs{},
    ModulationDepthMs{}};
  std::array<bool, kBandCount> groupedBands{};
  for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
  {
    const std::size_t ownerHead = mResolvedDelayGroupingPlan.ownerHeadIndices[bandIndex];
    if (mResolvedDelayGroupingPlan.isGroupHead[ownerHead])
    {
      groupedBands[bandIndex] = true;
      effectiveDepths[bandIndex] =
        mGroupedCircuits[ownerHead].effectiveOutputModulationDepth(mBands[bandIndex].requestedModulationDepth());
    }
    else
    {
      effectiveDepths[bandIndex] = mBands[bandIndex].effectiveModulationDepth();
    }
  }

  for (std::size_t rootIndex = 0; rootIndex < kBandCount; ++rootIndex)
  {
    const ResolvedSynchronization& root = mResolvedSynchronizationPlan.bands[rootIndex];
    if (!root.isSynchronizationRoot)
      continue;

    const ModulationDepthMs rootDepth = effectiveDepths[rootIndex];
    const bool rootIsGrouped = groupedBands[rootIndex];
    Sample* const rootOffsets = mSynchronizedModulationOffsets.data() + rootIndex * mMaximumBlockSize;

    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
      ModulationClockSample rootClockSample;
      rootOffsets[frame] = rootIsGrouped ? mBands[rootIndex].advanceGroupModulationClock(rootDepth, rootClockSample)
                                         : mBands[rootIndex].advanceModulationClock(rootClockSample);

      for (std::size_t member = 0; member < root.slaveCount; ++member)
      {
        const std::size_t slaveIndex = root.slaveIndices[member];
        const ResolvedSynchronization& slave = mResolvedSynchronizationPlan.bands[slaveIndex];

        ModulationClockSample slaveClockSample;
        slaveClockSample.phase.value = rootClockSample.phase.value + slave.phaseOffsetCycles;
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
              rootClockSample.sine * slave.cosineOffset + rootClockSample.cosine * slave.sineOffset;
            slaveClockSample.cosine =
              rootClockSample.cosine * slave.cosineOffset - rootClockSample.sine * slave.sineOffset;
            break;
        }

        const bool slaveIsGrouped = groupedBands[slaveIndex];
        Sample* const slaveOffsets = mSynchronizedModulationOffsets.data() + slaveIndex * mMaximumBlockSize;
        slaveOffsets[frame] = slaveIsGrouped ? mBands[slaveIndex].groupModulationOffsetAtClockSample(
                                                 slaveClockSample, effectiveDepths[slaveIndex])
                                             : mBands[slaveIndex].modulationOffsetAtClockSample(slaveClockSample);
      }
    }
  }

  // Unsynchronized GROUP members need offsets before their shared circuit is
  // traversed. Independent ungrouped bands retain their established in-band
  // modulation path when their logical node is processed below.
  for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
  {
    const ResolvedSynchronization& synchronization = mResolvedSynchronizationPlan.bands[bandIndex];
    if (synchronization.isSynchronizedSlave || synchronization.isSynchronizationRoot)
      continue;

    if (!groupedBands[bandIndex])
      continue;

    const ModulationDepthMs effectiveDepth = effectiveDepths[bandIndex];
    Sample* const offsets = mSynchronizedModulationOffsets.data() + bandIndex * mMaximumBlockSize;
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
      ModulationClockSample clockSample;
      offsets[frame] = mBands[bandIndex].advanceGroupModulationClock(effectiveDepth, clockSample);
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

void HoldsworthDelayEngine::processGroupedBlock(const std::span<const Sample> monoInput,
                                                const std::span<Sample> wetLeft,
                                                const std::span<Sample> wetRight) noexcept
{
  const std::size_t frameCount = monoInput.size();
  std::copy(monoInput.begin(), monoInput.end(), mInputScratch.begin());
  std::fill(wetLeft.begin(), wetLeft.end(), 0.0);
  std::fill(wetRight.begin(), wetRight.end(), 0.0);

  generateGroupedModulationOffsets(frameCount);

  std::array<const Sample*, kBandCount> modulationOffsets{};
  for (std::size_t bandIndex = 0; bandIndex < kBandCount; ++bandIndex)
  {
    modulationOffsets[bandIndex] = mSynchronizedModulationOffsets.data() + bandIndex * mMaximumBlockSize;
  }

  for (std::size_t nodeOrder = 0; nodeOrder < mResolvedGroupAudioProcessingPlan.nodeCount; ++nodeOrder)
  {
    const std::size_t headIndex = mResolvedGroupAudioProcessingPlan.processingOrder[nodeOrder];
    const std::size_t sourceIndex = mResolvedAudioRoutingPlan.sourceIndices[headIndex];
    const Sample* const nodeInputData =
      sourceIndex == kBandCount ? mInputScratch.data() : mBandRoutingOutputs.data() + sourceIndex * mMaximumBlockSize;
    const std::span<const Sample> nodeInput{nodeInputData, frameCount};

    if (!mResolvedDelayGroupingPlan.isGroupHead[headIndex])
    {
      const std::span<Sample> routingOutput{mBandRoutingOutputs.data() + headIndex * mMaximumBlockSize, frameCount};
      const std::span<Sample> bandWetLeft{mConnectedBandWetLeft.data() + headIndex * mMaximumBlockSize, frameCount};
      const std::span<Sample> bandWetRight{mConnectedBandWetRight.data() + headIndex * mMaximumBlockSize, frameCount};
      const ResolvedSynchronization& synchronization = mResolvedSynchronizationPlan.bands[headIndex];
      if (synchronization.isSynchronizedSlave || synchronization.isSynchronizationRoot)
      {
        const std::span<const Sample> offsets{modulationOffsets[headIndex], frameCount};
        mBands[headIndex].processBlockUsingPrecomputedModulationOffsetsWithRoutingOutput(
          nodeInput, offsets, routingOutput, bandWetLeft, bandWetRight);
      }
      else
      {
        mBands[headIndex].processBlockWithRoutingOutput(nodeInput, routingOutput, bandWetLeft, bandWetRight);
      }
      continue;
    }

    std::array<GroupedDelayCircuit::OutputParameters, kBandCount> outputParameters{};
    const std::size_t endIndex = mResolvedDelayGroupingPlan.endIndicesByHead[headIndex];
    for (std::size_t bandIndex = headIndex; bandIndex <= endIndex; ++bandIndex)
    {
      const DelayBand& band = mBands[bandIndex];
      outputParameters[bandIndex] = {
        band.outputLevel(), band.mLeftPanGain, band.mRightPanGain, band.delaySignalPolarity(), band.isEnabled()};
    }

    mGroupedCircuits[headIndex].processBlock(nodeInput, outputParameters, modulationOffsets, mBandRoutingOutputs.data(),
                                             mConnectedBandWetLeft.data(), mConnectedBandWetRight.data(),
                                             mMaximumBlockSize);
  }

  // GROUP and CONNECT never change the stable final wet multiplicity: every
  // Yamaha Effect Band output identity contributes exactly once.
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

  if (mResolvedDelayGroupingPlan.hasGroups)
  {
    processGroupedBlock(monoInput, wetLeft, wetRight);
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
