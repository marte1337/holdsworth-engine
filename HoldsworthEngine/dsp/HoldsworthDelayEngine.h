#pragma once

#include "AudioRouting.h"
#include "DelayBand.h"
#include "DelayGrouping.h"
#include "GroupedDelayCircuit.h"
#include "ModulationSync.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace holdsworth::dsp
{

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
  ModulationRateHz modulationRate{};
  ModulationDepthMs modulationDepth{};
  ModulationPhaseCycles modulationPhase{};
  DelayLoopFilterConfiguration loopFilter{};
  TapFraction tapFraction{};
  ModulationWaveform modulationWaveform = ModulationWaveform::sine;
  DelaySignalPolarity delaySignalPolarity = DelaySignalPolarity::normal;
};

struct HoldsworthDelayConfiguration final
{
  std::array<DelayBandConfiguration, kHoldsworthDelayBandCount> bands{};
  double globalWetOutputLevel = 1.0;
  ModulationSyncConfiguration modulationSync{};
  AudioRoutingConfiguration audioRouting{};
  DelayGroupingConfiguration delayGrouping{};
};

// Full-engine configuration validates SYNC, CONNECT, GROUP, and their composed
// processing schedule before mutating any live state. Every domain result is
// reported, and any rejection preserves all live DSP and graph state.
struct HoldsworthDelayConfigurationApplyResult final
{
  ModulationSyncApplyResult modulationSync = ModulationSyncApplyResult::applied;
  AudioRoutingApplyResult audioRouting = AudioRoutingApplyResult::applied;
  DelayGroupingApplyResult delayGrouping = DelayGroupingApplyResult::applied;
  GroupAudioCompositionApplyResult groupAudioComposition =
    GroupAudioCompositionApplyResult::applied;

  [[nodiscard]] constexpr bool wasApplied() const noexcept
  {
    return modulationSync == ModulationSyncApplyResult::applied
           && audioRouting == AudioRoutingApplyResult::applied
           && delayGrouping == DelayGroupingApplyResult::applied
           && groupAudioComposition == GroupAudioCompositionApplyResult::applied;
  }
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

  // groupedPhysicalCapacity is allocated separately for every potential GROUP
  // head. The strongly typed value cannot be confused with an individual-band
  // capacity or a Yamaha documented maximum.
  HoldsworthDelayEngine(double maximumDelayTimeMs,
                        GroupedDelayPhysicalCapacityMs groupedPhysicalCapacity);

  HoldsworthDelayEngine(const HoldsworthDelayEngine&) = delete;
  HoldsworthDelayEngine& operator=(const HoldsworthDelayEngine&) = delete;
  HoldsworthDelayEngine(HoldsworthDelayEngine&&) noexcept = delete;
  HoldsworthDelayEngine& operator=(HoldsworthDelayEngine&&) noexcept = delete;

  // Allocates all eight independent delay histories, all potential GROUP
  // histories, the established audio scratch buffers, fixed eight-band
  // synchronized-modulation scratch, and routing/per-band wet scratch. Calling
  // prepare() again discards existing delay history while preserving parameter
  // values.
  void prepare(double sampleRate, std::size_t maximumBlockSize);

  // Clears all independent and prepared GROUP histories without changing
  // configuration.
  void reset() noexcept;

  // Transactionally validates synchronization, audio routing, delay grouping,
  // and their composed schedule before applying any band parameters, the
  // global wet level, or resolved plan. On failure, no engine state is
  // changed. Values are sanitized by the same rules as DelayBand's setters.
  HoldsworthDelayConfigurationApplyResult applyConfiguration(
    const HoldsworthDelayConfiguration& configuration) noexcept;

  // Applies only a complete synchronization relationship snapshot. Existing
  // audio/configuration state and root clock phases are retained. A private
  // slave oscillator that resumes independent operation is restored to its
  // configured reset phase without clearing delay/filter history.
  ModulationSyncApplyResult applyModulationSyncConfiguration(
    const ModulationSyncConfiguration& configuration) noexcept;

  // Applies only a complete CONNECT relationship snapshot. Existing band,
  // SYNC, oscillator, delay, and filter state is retained. Invalid requests do
  // not alter the previously accepted routing plan.
  AudioRoutingApplyResult applyAudioRoutingConfiguration(
    const AudioRoutingConfiguration& configuration) noexcept;

  // Applies only a complete GROUP topology snapshot. Active CONNECT is
  // revalidated against the candidate topology before any histories or plans
  // change. Topology changes use the documented deterministic history policy.
  DelayGroupingApplyResult applyDelayGroupingConfiguration(
    const DelayGroupingConfiguration& configuration) noexcept;

  // bandIndex must be in [0, kBandCount). Invalid indices assert in Debug and
  // are ignored without modifying any band when assertions are disabled.
  void setBandConfiguration(std::size_t bandIndex,
                            const DelayBandConfiguration& configuration) noexcept;

  // Returns sanitized requested parameter values. In particular, delay time,
  // modulation rate, modulation depth, loop-filter cutoffs, TAP fraction,
  // waveform, and signal polarity are requested configuration values rather
  // than sample-rate- or delay-boundary-dependent transient state.
  // modulationPhase is the configured reset phase rather than the phase
  // advancing during processing.
  [[nodiscard]] HoldsworthDelayConfiguration configuration() const noexcept;

  // Linear gain in [0, 1], applied after summing all eight wet band outputs.
  // Non-finite values are treated as zero.
  void setGlobalWetOutputLevel(Sample level) noexcept;

  [[nodiscard]] Sample globalWetOutputLevel() const noexcept { return mGlobalWetOutputLevel; }
  [[nodiscard]] Sample maximumDelayTimeMs() const noexcept;
  [[nodiscard]] Sample groupedPhysicalCapacityMs() const noexcept
  {
    return mGroupedPhysicalCapacity.value;
  }
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
  enum class PhaseRotationKind
  {
    zero,
    quarter,
    half,
    threeQuarter,
    arbitrary
  };

  struct ResolvedSynchronization final
  {
    std::array<std::size_t, kBandCount> slaveIndices{};
    std::size_t slaveCount = 0;
    std::size_t masterIndex = 0;
    Sample phaseOffsetCycles = 0.0;
    Sample sineOffset = 0.0;
    Sample cosineOffset = 1.0;
    PhaseRotationKind rotationKind = PhaseRotationKind::zero;
    bool isSynchronizedSlave = false;
    bool isSynchronizationRoot = false;
  };

  struct ResolvedSynchronizationPlan final
  {
    std::array<ResolvedSynchronization, kBandCount> bands{};
    bool hasSynchronization = false;
  };

  struct ResolvedAudioRoutingPlan final
  {
    // kBandCount denotes direct engine input (Yamaha CONNECT IN).
    std::array<std::size_t, kBandCount> sourceIndices{};
    std::array<std::size_t, kBandCount> processingOrder{};
    bool hasConnections = false;
  };

  struct ResolvedDelayGroupingPlan final
  {
    // ownerHeadIndices maps every band identity to its logical processing
    // node. For an ungrouped band, the owner is the band itself.
    std::array<std::size_t, kBandCount> ownerHeadIndices{};
    std::array<std::size_t, kBandCount> endIndicesByHead{};
    std::array<bool, kBandCount> isGroupHead{};
    bool hasGroups = false;
  };

  struct ResolvedGroupAudioProcessingPlan final
  {
    // Entries are logical-node head indices. At most one entry represents a
    // complete GROUP range.
    std::array<std::size_t, kBandCount> processingOrder{};
    std::size_t nodeCount = 0;
  };

  static ModulationSyncApplyResult resolveModulationSyncConfiguration(
    const ModulationSyncConfiguration& requested,
    ModulationSyncConfiguration& canonical,
    ResolvedSynchronizationPlan& resolved) noexcept;

  static AudioRoutingApplyResult resolveAudioRoutingConfiguration(
    const AudioRoutingConfiguration& requested,
    AudioRoutingConfiguration& canonical,
    ResolvedAudioRoutingPlan& resolved) noexcept;

  DelayGroupingApplyResult resolveDelayGroupingConfiguration(
    const DelayGroupingConfiguration& requested,
    const std::array<DelayBandConfiguration, kBandCount>& bandConfigurations,
    DelayGroupingConfiguration& canonical,
    ResolvedDelayGroupingPlan& resolved) const noexcept;

  static GroupAudioCompositionApplyResult resolveGroupAudioComposition(
    const AudioRoutingConfiguration& audioRouting,
    const ResolvedAudioRoutingPlan& resolvedAudioRouting,
    const ResolvedDelayGroupingPlan& grouping,
    ResolvedGroupAudioProcessingPlan& resolved) noexcept;

  void commitModulationSyncConfiguration(
    const ModulationSyncConfiguration& canonical,
    const ResolvedSynchronizationPlan& resolved,
    bool resetNewlyIndependentSlaveClocks) noexcept;
  void commitAudioRoutingConfiguration(
    const AudioRoutingConfiguration& canonical,
    const ResolvedAudioRoutingPlan& resolved) noexcept;
  void commitDelayGroupingConfiguration(
    const DelayGroupingConfiguration& canonical,
    const ResolvedDelayGroupingPlan& resolved,
    const std::array<DelayBandConfiguration, kBandCount>& bandConfigurations) noexcept;
  void commitGroupAudioProcessingPlan(
    const ResolvedGroupAudioProcessingPlan& resolved) noexcept;
  void synchronizeGroupedCircuitParameters(
    std::size_t headIndex,
    Sample requestedBaseDelayTimeMs) noexcept;
  void generateSynchronizedModulationOffsets(std::size_t frameCount) noexcept;
  void generateGroupedModulationOffsets(std::size_t frameCount) noexcept;
  void processConnectedBlock(std::span<const Sample> monoInput,
                             std::span<Sample> wetLeft,
                             std::span<Sample> wetRight) noexcept;
  void processGroupedBlock(std::span<const Sample> monoInput,
                           std::span<Sample> wetLeft,
                           std::span<Sample> wetRight) noexcept;

  std::array<DelayBand, kBandCount> mBands;
  const GroupedDelayPhysicalCapacityMs mGroupedPhysicalCapacity;
  std::array<GroupedDelayCircuit, kBandCount> mGroupedCircuits;
  std::vector<Sample> mInputScratch;
  std::vector<Sample> mBandWetLeft;
  std::vector<Sample> mBandWetRight;
  std::vector<Sample> mSynchronizedModulationOffsets;
  std::vector<Sample> mBandRoutingOutputs;
  std::vector<Sample> mConnectedBandWetLeft;
  std::vector<Sample> mConnectedBandWetRight;
  ModulationSyncConfiguration mModulationSyncConfiguration{};
  ResolvedSynchronizationPlan mResolvedSynchronizationPlan{};
  AudioRoutingConfiguration mAudioRoutingConfiguration{};
  ResolvedAudioRoutingPlan mResolvedAudioRoutingPlan{};
  DelayGroupingConfiguration mDelayGroupingConfiguration{};
  ResolvedDelayGroupingPlan mResolvedDelayGroupingPlan{};
  ResolvedGroupAudioProcessingPlan mResolvedGroupAudioProcessingPlan{};
  Sample mGlobalWetOutputLevel = 1.0;
  std::size_t mMaximumBlockSize = 0;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
