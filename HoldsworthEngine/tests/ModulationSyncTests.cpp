#include "../dsp/HoldsworthDelayEngine.h"
#include "../dsp/HoldsworthDelayPresets.h"
#include "../presets/YamahaBandStructureSourceValues.h"
#include "../presets/YamahaSyncSourceValues.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

static_assert(!std::is_convertible_v<double,
                                     holdsworth::dsp::ModulationPhaseOffsetCycles>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaEffectBandNumber,
                                     holdsworth::dsp::DelayBandId>);
static_assert(!std::is_convertible_v<holdsworth::dsp::DelayBandId,
                                     holdsworth::presets::YamahaEffectBandNumber>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaSyncControlValue,
                                     holdsworth::dsp::SynchronizedModulationRelationship>);
static_assert(!std::is_convertible_v<holdsworth::dsp::SynchronizedModulationRelationship,
                                     holdsworth::presets::YamahaSyncControlValue>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaSpeedControlValue,
                                     holdsworth::dsp::ModulationPhaseOffsetCycles>);
static_assert(!std::is_convertible_v<holdsworth::dsp::ModulationPhaseOffsetCycles,
                                     holdsworth::presets::YamahaSpeedControlValue>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaConnectControlValue,
                                     holdsworth::dsp::ConnectedBandAudioInput>);
static_assert(!std::is_convertible_v<holdsworth::dsp::ConnectedBandAudioInput,
                                     holdsworth::presets::YamahaConnectControlValue>);

namespace holdsworth::test
{
namespace
{

using dsp::DelayBandId;
using dsp::ModulationPhaseOffsetCycles;
using dsp::ModulationSyncApplyResult;
using dsp::ModulationSyncConfiguration;
using dsp::SynchronizedModulationRelationship;

void configureBand(dsp::DelayBandConfiguration& band,
                   const double delayTimeMs,
                   const double feedback,
                   const double outputLevel,
                   const double pan,
                   const bool enabled,
                   const double rateHz,
                   const double depthMs,
                   const double resetPhaseCycles,
                   const dsp::ModulationWaveform waveform = dsp::ModulationWaveform::sine) noexcept
{
  band.delayTimeMs = delayTimeMs;
  band.feedback = dsp::NormalizedFeedbackCoefficient{feedback};
  band.outputLevel = outputLevel;
  band.pan = pan;
  band.enabled = enabled;
  band.modulationRate = dsp::ModulationRateHz{rateHz};
  band.modulationDepth = dsp::ModulationDepthMs{depthMs};
  band.modulationPhase = dsp::ModulationPhaseCycles{resetPhaseCycles};
  band.modulationWaveform = waveform;
}

void synchronize(ModulationSyncConfiguration& configuration,
                 const std::size_t slaveIndex,
                 const DelayBandId masterBand,
                 const double phaseOffsetCycles) noexcept
{
  configuration.relationships[slaveIndex] =
    SynchronizedModulationRelationship{masterBand,
                                       ModulationPhaseOffsetCycles{phaseOffsetCycles}};
}

bool equalSyncConfiguration(const ModulationSyncConfiguration& actual,
                            const ModulationSyncConfiguration& expected,
                            const std::string_view testName)
{
  for (std::size_t band = 0; band < actual.relationships.size(); ++band)
  {
    const auto& actualRelationship = actual.relationships[band];
    const auto& expectedRelationship = expected.relationships[band];
    if (actualRelationship.has_value() != expectedRelationship.has_value())
    {
      std::cerr << testName << ": relationship presence differs at Band "
                << (band + 1) << '\n';
      return false;
    }
    if (!actualRelationship.has_value())
      continue;

    if (actualRelationship->masterBand != expectedRelationship->masterBand
        || !nearlyEqual(actualRelationship->phaseOffset.value,
                        expectedRelationship->phaseOffset.value,
                        0.0))
    {
      std::cerr << testName << ": relationship differs at Band " << (band + 1)
                << '\n';
      return false;
    }
  }
  return true;
}

bool equalAudioRoutingConfiguration(const dsp::AudioRoutingConfiguration& actual,
                                    const dsp::AudioRoutingConfiguration& expected,
                                    const std::string_view testName)
{
  for (std::size_t band = 0; band < actual.inputs.size(); ++band)
  {
    const auto& actualInput = actual.inputs[band];
    const auto& expectedInput = expected.inputs[band];
    if (actualInput.has_value() != expectedInput.has_value()
        || (actualInput.has_value()
            && actualInput->sourceBand != expectedInput->sourceBand))
    {
      std::cerr << testName << ": audio-routing input differs at Band "
                << (band + 1) << '\n';
      return false;
    }
  }
  return true;
}

template <typename Cutoff>
bool equalOptionalCutoff(const std::optional<Cutoff>& actual,
                         const std::optional<Cutoff>& expected) noexcept
{
  return actual.has_value() == expected.has_value()
         && (!actual.has_value()
             || nearlyEqual(actual->value, expected->value, 0.0));
}

bool equalEngineConfiguration(const dsp::HoldsworthDelayConfiguration& actual,
                              const dsp::HoldsworthDelayConfiguration& expected,
                              const std::string_view testName)
{
  if (!nearlyEqual(actual.globalWetOutputLevel,
                   expected.globalWetOutputLevel,
                   0.0)
      || !equalSyncConfiguration(actual.modulationSync,
                                 expected.modulationSync,
                                 testName)
      || !equalAudioRoutingConfiguration(actual.audioRouting,
                                         expected.audioRouting,
                                         testName))
    return false;

  for (std::size_t bandIndex = 0; bandIndex < actual.bands.size(); ++bandIndex)
  {
    const auto& a = actual.bands[bandIndex];
    const auto& e = expected.bands[bandIndex];
    if (!nearlyEqual(a.delayTimeMs, e.delayTimeMs, 0.0)
        || !nearlyEqual(a.feedback.value, e.feedback.value, 0.0)
        || !nearlyEqual(a.outputLevel, e.outputLevel, 0.0)
        || !nearlyEqual(a.pan, e.pan, 0.0)
        || a.enabled != e.enabled
        || !nearlyEqual(a.modulationRate.value, e.modulationRate.value, 0.0)
        || !nearlyEqual(a.modulationDepth.value, e.modulationDepth.value, 0.0)
        || !nearlyEqual(a.modulationPhase.value, e.modulationPhase.value, 0.0)
        || a.modulationWaveform != e.modulationWaveform
        || !equalOptionalCutoff(a.loopFilter.lowCut, e.loopFilter.lowCut)
        || !equalOptionalCutoff(a.loopFilter.highCut, e.loopFilter.highCut)
        || !nearlyEqual(a.tapFraction.value, e.tapFraction.value, 0.0)
        || a.delaySignalPolarity != e.delaySignalPolarity)
    {
      std::cerr << testName << ": band configuration differs at Band "
                << (bandIndex + 1) << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] double deterministicInput(const std::size_t sampleIndex) noexcept
{
  const int centered = static_cast<int>((sampleIndex * 37 + 11) % 101) - 50;
  return static_cast<double>(centered) / 53.0;
}

bool testYamahaSyncSourceMetadataIsStrongAndExact()
{
  using presets::YamahaEffectBandNumber;
  using presets::YamahaSyncControlState;
  using presets::YamahaSyncControlValue;

  constexpr std::array expectedBandNumbers{YamahaEffectBandNumber::band1,
                                           YamahaEffectBandNumber::band2,
                                           YamahaEffectBandNumber::band3,
                                           YamahaEffectBandNumber::band4,
                                           YamahaEffectBandNumber::band5,
                                           YamahaEffectBandNumber::band6,
                                           YamahaEffectBandNumber::band7,
                                           YamahaEffectBandNumber::band8};
  const std::array presetDefinitions{
    &dsp::presets::lead121UnmodulatedProvisional(),
    &dsp::presets::chorus011ProvisionalV1(),
    &dsp::presets::chorus031ProvisionalV1()};

  for (const dsp::HoldsworthDelayPresetDefinition* preset : presetDefinitions)
  {
    for (std::size_t band = 0; band < expectedBandNumbers.size(); ++band)
    {
      const auto& syncSource =
        preset->documentedYamahaValues[band].syncControlValue;
      if (!syncSource.has_value()
          || syncSource->state() != YamahaSyncControlState::independentSelf
          || syncSource->displayedBand() != expectedBandNumbers[band]
          || preset->dspConfiguration.modulationSync.relationships[band].has_value()
          || preset->dspConfiguration.audioRouting.inputs[band].has_value())
      {
        std::cerr << preset->id << ": Yamaha SYNC metadata/DSP graphs mismatch at Band "
                  << (band + 1) << '\n';
        return false;
      }
    }
  }

  const auto synchronizedSource =
    YamahaSyncControlValue::synchronizedTo(YamahaEffectBandNumber::band3);
  const dsp::DocumentedYamahaBandValues unknownSource;
  return synchronizedSource.state() == YamahaSyncControlState::synchronizedToBand
         && synchronizedSource.displayedBand() == YamahaEffectBandNumber::band3
         && !unknownSource.syncControlValue.has_value();
}

bool testCanonicalOffsetsAndTransactionalGraphValidation()
{
  dsp::HoldsworthDelayEngine engine(50.0);
  ModulationSyncConfiguration requested;
  synchronize(requested, 0, DelayBandId::band8, 0.0);
  synchronize(requested, 1, DelayBandId::band8, 0.25);
  synchronize(requested, 2, DelayBandId::band8, 1.0);
  synchronize(requested, 3, DelayBandId::band8, 1.25);
  synchronize(requested, 4, DelayBandId::band8, -0.25);

  if (engine.applyModulationSyncConfiguration(requested)
      != ModulationSyncApplyResult::applied)
    return false;

  ModulationSyncConfiguration expected = requested;
  expected.relationships[0]->phaseOffset.value = 0.0;
  expected.relationships[1]->phaseOffset.value = 0.25;
  expected.relationships[2]->phaseOffset.value = 0.0;
  expected.relationships[3]->phaseOffset.value = 0.25;
  expected.relationships[4]->phaseOffset.value = 0.75;
  const auto acceptedConfiguration = engine.configuration();
  if (!equalSyncConfiguration(acceptedConfiguration.modulationSync,
                              expected,
                              "canonical phase offsets"))
    return false;

  const auto expectRejectedWithoutMutation =
    [&engine, &expected](const ModulationSyncConfiguration& invalid,
                        const ModulationSyncApplyResult expectedResult,
                        const std::string_view name) {
      if (engine.applyModulationSyncConfiguration(invalid) != expectedResult)
      {
        std::cerr << name << ": unexpected validation result\n";
        return false;
      }
      return equalSyncConfiguration(engine.configuration().modulationSync,
                                    expected,
                                    name);
    };

  for (const double invalidOffset :
       std::array{std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity()})
  {
    auto invalid = expected;
    invalid.relationships[0]->phaseOffset.value = invalidOffset;
    if (!expectRejectedWithoutMutation(invalid,
                                       ModulationSyncApplyResult::invalidPhaseOffset,
                                       "non-finite phase offset"))
      return false;
  }

  auto invalidMaster = expected;
  invalidMaster.relationships[0]->masterBand = static_cast<DelayBandId>(0);
  if (!expectRejectedWithoutMutation(invalidMaster,
                                     ModulationSyncApplyResult::invalidMasterReference,
                                     "invalid master"))
    return false;

  ModulationSyncConfiguration selfReference;
  synchronize(selfReference, 0, DelayBandId::band1, 0.0);
  if (!expectRejectedWithoutMutation(selfReference,
                                     ModulationSyncApplyResult::selfReference,
                                     "self reference"))
    return false;

  ModulationSyncConfiguration cycle;
  synchronize(cycle, 0, DelayBandId::band2, 0.0);
  synchronize(cycle, 1, DelayBandId::band1, 0.0);
  if (!expectRejectedWithoutMutation(cycle,
                                     ModulationSyncApplyResult::cycleDetected,
                                     "cycle"))
    return false;

  ModulationSyncConfiguration threeNodeCycle;
  synchronize(threeNodeCycle, 0, DelayBandId::band2, 0.0);
  synchronize(threeNodeCycle, 1, DelayBandId::band3, 0.0);
  synchronize(threeNodeCycle, 2, DelayBandId::band1, 0.0);
  if (!expectRejectedWithoutMutation(threeNodeCycle,
                                     ModulationSyncApplyResult::cycleDetected,
                                     "three-node cycle"))
    return false;

  ModulationSyncConfiguration chain;
  synchronize(chain, 0, DelayBandId::band2, 0.0);
  synchronize(chain, 1, DelayBandId::band8, 0.0);
  return expectRejectedWithoutMutation(chain,
                                       ModulationSyncApplyResult::unsupportedChain,
                                       "unsupported chain");
}

bool testRejectedWholeConfigurationPreservesAllStateAndContinuation()
{
  constexpr std::size_t blockSize = 128;
  dsp::HoldsworthDelayConfiguration accepted;
  accepted.globalWetOutputLevel = 0.73;
  configureBand(accepted.bands[0], 7.25, 0.31, 0.8, -1.0, true,
                19.0, 0.7, 0.125);
  configureBand(accepted.bands[2], 11.5, 0.43, 0.6, 1.0, true,
                3.75, 1.1, 0.25);
  synchronize(accepted.modulationSync, 0, DelayBandId::band3, 0.25);

  dsp::HoldsworthDelayEngine actual(40.0);
  dsp::HoldsworthDelayEngine reference(40.0);
  actual.prepare(1000.0, blockSize);
  reference.prepare(1000.0, blockSize);
  if (!actual.applyConfiguration(accepted).wasApplied()
      || !reference.applyConfiguration(accepted).wasApplied())
    return false;

  std::array<double, blockSize> input{};
  std::array<double, blockSize> actualLeft{};
  std::array<double, blockSize> actualRight{};
  std::array<double, blockSize> referenceLeft{};
  std::array<double, blockSize> referenceRight{};
  for (std::size_t sample = 0; sample < blockSize; ++sample)
    input[sample] = deterministicInput(sample);
  actual.processBlock(input, actualLeft, actualRight);
  reference.processBlock(input, referenceLeft, referenceRight);
  if (!expectSamplesBitExact("rollback prefix left", actualLeft, referenceLeft)
      || !expectSamplesBitExact("rollback prefix right", actualRight, referenceRight))
    return false;

  auto rejected = accepted;
  rejected.globalWetOutputLevel = 0.11;
  rejected.bands[2].delayTimeMs = 29.0;
  rejected.bands[2].modulationPhase = dsp::ModulationPhaseCycles{0.91};
  rejected.modulationSync.relationships[0]->phaseOffset.value =
    std::numeric_limits<double>::quiet_NaN();
  const auto rejectedResult = actual.applyConfiguration(rejected);
  if (rejectedResult.modulationSync
        != ModulationSyncApplyResult::invalidPhaseOffset
      || rejectedResult.audioRouting != dsp::AudioRoutingApplyResult::applied
      || !equalEngineConfiguration(actual.configuration(),
                                   reference.configuration(),
                                   "rejected whole configuration"))
    return false;

  for (std::size_t sample = 0; sample < blockSize; ++sample)
    input[sample] = deterministicInput(blockSize + sample);
  actual.processBlock(input, actualLeft, actualRight);
  reference.processBlock(input, referenceLeft, referenceRight);
  return expectSamplesBitExact("rollback continuation left", actualLeft, referenceLeft)
         && expectSamplesBitExact("rollback continuation right", actualRight, referenceRight);
}

bool testRejectedGraphOnlyConfigurationPreservesClockHistoryAndContinuation()
{
  constexpr std::size_t blockSize = 128;
  dsp::HoldsworthDelayConfiguration accepted;
  configureBand(accepted.bands[0], 13.25, 0.37, 0.74, -0.35, true,
                29.0, 0.9, 0.41, dsp::ModulationWaveform::triangle);
  configureBand(accepted.bands[5], 19.5, 0.43, 0.66, 0.55, true,
                4.75, 1.3, 0.17, dsp::ModulationWaveform::sine);
  synchronize(accepted.modulationSync, 0, DelayBandId::band6, 0.25);

  dsp::HoldsworthDelayEngine actual(50.0);
  dsp::HoldsworthDelayEngine reference(50.0);
  actual.prepare(1000.0, blockSize);
  reference.prepare(1000.0, blockSize);
  if (!actual.applyConfiguration(accepted).wasApplied()
      || !reference.applyConfiguration(accepted).wasApplied())
    return false;

  std::array<double, blockSize> input{};
  std::array<double, blockSize> actualLeft{};
  std::array<double, blockSize> actualRight{};
  std::array<double, blockSize> referenceLeft{};
  std::array<double, blockSize> referenceRight{};
  for (std::size_t sample = 0; sample < input.size(); ++sample)
    input[sample] = deterministicInput(sample + 700);
  actual.processBlock(input, actualLeft, actualRight);
  reference.processBlock(input, referenceLeft, referenceRight);
  if (!expectSamplesBitExact("graph rollback prefix left", actualLeft, referenceLeft)
      || !expectSamplesBitExact("graph rollback prefix right", actualRight, referenceRight))
    return false;

  auto rejectedGraph = accepted.modulationSync;
  rejectedGraph.relationships[0]->phaseOffset.value =
    std::numeric_limits<double>::infinity();
  if (actual.applyModulationSyncConfiguration(rejectedGraph)
        != ModulationSyncApplyResult::invalidPhaseOffset
      || !equalEngineConfiguration(actual.configuration(),
                                   reference.configuration(),
                                   "rejected graph-only configuration"))
    return false;

  for (std::size_t sample = 0; sample < input.size(); ++sample)
    input[sample] = deterministicInput(sample + 900);
  actual.processBlock(input, actualLeft, actualRight);
  reference.processBlock(input, referenceLeft, referenceRight);
  return expectSamplesBitExact("graph rollback continuation left", actualLeft, referenceLeft)
         && expectSamplesBitExact("graph rollback continuation right", actualRight, referenceRight);
}

dsp::HoldsworthDelayConfiguration makeRootEquivalenceConfiguration(
  const bool synchronized) noexcept
{
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 9.0, 0.2, 0.7, -1.0, false,
                91.0, 0.8, 0.63, dsp::ModulationWaveform::sawDown);
  configureBand(configuration.bands[3], 11.375, 0.43, 0.81, 0.17, true,
                7.123456789, 1.125, 0.137, dsp::ModulationWaveform::sine);
  configuration.bands[3].tapFraction = dsp::TapFraction{0.63};
  configuration.bands[3].loopFilter.lowCut = dsp::LowCutFrequencyHz{83.0};
  configuration.bands[3].loopFilter.highCut = dsp::HighCutFrequencyHz{9700.0};
  configuration.bands[3].delaySignalPolarity = dsp::DelaySignalPolarity::reverse;
  if (synchronized)
    synchronize(configuration.modulationSync, 0, DelayBandId::band4, 0.375);
  return configuration;
}

bool compareStreaming(dsp::HoldsworthDelayEngine& actual,
                      dsp::HoldsworthDelayEngine& expected,
                      const std::size_t sampleCount,
                      const std::size_t inputOffset,
                      const std::string_view name)
{
  constexpr std::size_t maximumBlockSize = 251;
  constexpr std::array<std::size_t, 9> blockPattern{1, 251, 17, 3, 128, 5, 64, 7, 193};
  std::array<double, maximumBlockSize> input{};
  std::array<double, maximumBlockSize> actualLeft{};
  std::array<double, maximumBlockSize> actualRight{};
  std::array<double, maximumBlockSize> expectedLeft{};
  std::array<double, maximumBlockSize> expectedRight{};

  std::size_t processed = 0;
  std::size_t blockIndex = 0;
  while (processed < sampleCount)
  {
    const std::size_t blockSize =
      std::min(blockPattern[blockIndex % blockPattern.size()], sampleCount - processed);
    for (std::size_t frame = 0; frame < blockSize; ++frame)
      input[frame] = deterministicInput(inputOffset + processed + frame);

    actual.processBlock(std::span<const double>{input.data(), blockSize},
                        std::span<double>{actualLeft.data(), blockSize},
                        std::span<double>{actualRight.data(), blockSize});
    expected.processBlock(std::span<const double>{input.data(), blockSize},
                          std::span<double>{expectedLeft.data(), blockSize},
                          std::span<double>{expectedRight.data(), blockSize});
    if (!expectSamplesBitExact(name,
                               std::span<const double>{actualLeft.data(), blockSize},
                               std::span<const double>{expectedLeft.data(), blockSize})
        || !expectSamplesBitExact(name,
                                  std::span<const double>{actualRight.data(), blockSize},
                                  std::span<const double>{expectedRight.data(), blockSize}))
    {
      std::cerr << name << ": block starting at sample " << processed << '\n';
      return false;
    }
    processed += blockSize;
    ++blockIndex;
  }
  return true;
}

bool testRootAudioAdvancesExactlyOnceAcrossLongRender()
{
  constexpr std::size_t maximumBlockSize = 251;
  constexpr std::size_t longRenderSamples =
    24 * dsp::DelayModulator::kCacheResynchronizationInterval + 138;

  dsp::HoldsworthDelayEngine synchronizedRoot(50.0);
  dsp::HoldsworthDelayEngine independentRoot(50.0);
  synchronizedRoot.prepare(48000.0, maximumBlockSize);
  independentRoot.prepare(48000.0, maximumBlockSize);
  if (!synchronizedRoot.applyConfiguration(makeRootEquivalenceConfiguration(true))
         .wasApplied()
      || !independentRoot.applyConfiguration(makeRootEquivalenceConfiguration(false))
            .wasApplied())
    return false;

  if (!compareStreaming(synchronizedRoot,
                        independentRoot,
                        longRenderSamples,
                        0,
                        "root once long render"))
    return false;

  // Removing only the graph must leave the root at the same authoritative
  // phase/cache position as the continuously independent reference.
  if (synchronizedRoot.applyModulationSyncConfiguration({})
      != ModulationSyncApplyResult::applied)
    return false;

  return compareStreaming(synchronizedRoot,
                          independentRoot,
                          2 * dsp::DelayModulator::kCacheResynchronizationInterval + 37,
                          longRenderSamples,
                          "root once post-SYNC continuation");
}

bool testZeroDepthAndDisabledRootsStillClockSlave()
{
  constexpr std::size_t sampleCount = 2048;
  dsp::HoldsworthDelayConfiguration synchronizedConfiguration;
  configureBand(synchronizedConfiguration.bands[1], 17.25, 0.23, 0.8, 1.0, true,
                99.0, 1.25, 0.71, dsp::ModulationWaveform::triangle);
  configureBand(synchronizedConfiguration.bands[6], 9.5, 0.31, 1.0, -1.0, true,
                3.75, 0.0, 0.125, dsp::ModulationWaveform::sawUp);
  synchronize(synchronizedConfiguration.modulationSync,
              1,
              DelayBandId::band7,
              0.25);

  auto disabledRootConfiguration = synchronizedConfiguration;
  disabledRootConfiguration.bands[6].enabled = false;
  auto independentRootConfiguration = synchronizedConfiguration;
  independentRootConfiguration.bands[1].enabled = false;
  independentRootConfiguration.modulationSync = {};
  auto staticSlaveConfiguration = disabledRootConfiguration;
  staticSlaveConfiguration.bands[1].modulationDepth = dsp::ModulationDepthMs{0.0};

  dsp::HoldsworthDelayEngine enabledRoot(40.0);
  dsp::HoldsworthDelayEngine disabledRoot(40.0);
  dsp::HoldsworthDelayEngine independentRoot(40.0);
  dsp::HoldsworthDelayEngine staticSlave(40.0);
  for (dsp::HoldsworthDelayEngine* engine :
       std::array{&enabledRoot, &disabledRoot, &independentRoot, &staticSlave})
    engine->prepare(1000.0, sampleCount);

  if (!enabledRoot.applyConfiguration(synchronizedConfiguration).wasApplied()
      || !disabledRoot.applyConfiguration(disabledRootConfiguration).wasApplied()
      || !independentRoot.applyConfiguration(independentRootConfiguration).wasApplied()
      || !staticSlave.applyConfiguration(staticSlaveConfiguration).wasApplied())
    return false;

  std::vector<double> input(sampleCount);
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
    input[sample] = deterministicInput(sample);
  std::vector<double> enabledLeft(sampleCount);
  std::vector<double> enabledRight(sampleCount);
  std::vector<double> disabledLeft(sampleCount);
  std::vector<double> disabledRight(sampleCount);
  std::vector<double> referenceLeft(sampleCount);
  std::vector<double> referenceRight(sampleCount);
  std::vector<double> staticLeft(sampleCount);
  std::vector<double> staticRight(sampleCount);

  enabledRoot.processBlock(input, enabledLeft, enabledRight);
  disabledRoot.processBlock(input, disabledLeft, disabledRight);
  independentRoot.processBlock(input, referenceLeft, referenceRight);
  staticSlave.processBlock(input, staticLeft, staticRight);

  if (!expectSamplesBitExact("zero-depth root audio", enabledLeft, referenceLeft)
      || !expectSamplesBitExact("disabled root still clocks slave",
                                disabledRight,
                                enabledRight))
    return false;

  for (std::size_t sample = 0; sample < sampleCount; ++sample)
  {
    if (!nearlyEqual(disabledRight[sample], staticRight[sample], 1.0e-12))
      return true;
  }

  std::cerr << "positive-depth synchronized slave matched static slave\n";
  return false;
}

bool testEqualDepthSineHalfCycleIsExactOpposite()
{
  // Low-level clock regression: equal-depth Sine voices at half a cycle must
  // produce exactly opposite modulation offsets across cache resynchronization
  // boundaries. The slave evaluation must not advance its dormant clock.
  constexpr std::size_t longRenderSamples =
    3 * dsp::DelayModulator::kCacheResynchronizationInterval + 29;
  dsp::DelayModulator root;
  dsp::DelayModulator slave;
  for (dsp::DelayModulator* modulator : std::array{&root, &slave})
  {
    modulator->setRate(dsp::ModulationRateHz{7.123456789});
    modulator->setDepth(dsp::ModulationDepthMs{1.125});
    modulator->setPhase(dsp::ModulationPhaseCycles{0.137});
    modulator->setWaveform(dsp::ModulationWaveform::sine);
    modulator->prepare(48000.0);
  }

  const double dormantSlavePhase = slave.currentPhase().value;
  for (std::size_t sample = 0; sample < longRenderSamples; ++sample)
  {
    dsp::ModulationClockSample rootClock;
    const std::array rootOffset{root.nextOffsetMs(rootClock)};
    dsp::ModulationClockSample halfCycleClock;
    halfCycleClock.phase.value = rootClock.phase.value + 0.5;
    if (halfCycleClock.phase.value >= 1.0)
      halfCycleClock.phase.value -= 1.0;
    halfCycleClock.sine = -rootClock.sine;
    halfCycleClock.cosine = -rootClock.cosine;
    const std::array slaveOffset{slave.offsetMsAtClockSample(halfCycleClock)};
    const std::array expectedSlaveOffset{-rootOffset.front()};
    if (!expectSamplesBitExact("equal-depth Sine half-cycle offset",
                               slaveOffset,
                               expectedSlaveOffset)
        || !nearlyEqual(slave.currentPhase().value, dormantSlavePhase, 0.0))
    {
      std::cerr << "equal-depth half-cycle offset mismatch at sample " << sample << '\n';
      return false;
    }
  }

  // Engine-level regression explicitly takes the exact half-rotation branch
  // with lower Band 1 as master and higher Band 2 as slave. At a fixed
  // quarter-cycle phase, equal 2 ms depths produce +2/-2 ms excursions.
  constexpr std::size_t sampleCount = 16;
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 10.0, 0.0, 1.0, -1.0, true,
                0.0, 2.0, 0.25, dsp::ModulationWaveform::sine);
  configureBand(configuration.bands[1], 10.0, 0.0, 1.0, 1.0, true,
                91.0, 2.0, 0.71, dsp::ModulationWaveform::sine);
  synchronize(configuration.modulationSync, 1, DelayBandId::band1, 0.5);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!engine.applyConfiguration(configuration).wasApplied())
    return false;

  const std::array<double, sampleCount> impulse{1.0};
  std::array<double, sampleCount> actualLeft{};
  std::array<double, sampleCount> actualRight{};
  std::array<double, sampleCount> expectedLeft{};
  std::array<double, sampleCount> expectedRight{};
  expectedRight[8] = 1.0;
  expectedLeft[12] = 1.0;
  engine.processBlock(impulse, actualLeft, actualRight);
  return expectSamplesBitExact("half-cycle lower-master render left",
                               actualLeft,
                               expectedLeft)
         && expectSamplesBitExact("half-cycle lower-master render right",
                                  actualRight,
                                  expectedRight);
}

bool testHalfCycleAndSlaveWaveformDepthRemainIndependent()
{
  constexpr double sampleRate = 8.0;
  constexpr std::size_t sampleCount = 64;
  constexpr std::array waveforms{dsp::ModulationWaveform::sine,
                                 dsp::ModulationWaveform::triangle,
                                 dsp::ModulationWaveform::sawUp,
                                 dsp::ModulationWaveform::sawDown};
  std::array<double, sampleCount> input{};
  for (std::size_t sample = 0; sample < input.size(); ++sample)
    input[sample] = deterministicInput(sample);

  for (const dsp::ModulationWaveform waveform : waveforms)
  {
    dsp::HoldsworthDelayConfiguration synchronizedConfiguration;
    configureBand(synchronizedConfiguration.bands[0], 500.0, 0.37, 0.63, 0.31, true,
                  7.0, 100.0, 0.37, waveform);
    synchronizedConfiguration.bands[0].tapFraction = dsp::TapFraction{0.625};
    synchronizedConfiguration.bands[0].loopFilter.lowCut =
      dsp::LowCutFrequencyHz{0.2};
    synchronizedConfiguration.bands[0].loopFilter.highCut =
      dsp::HighCutFrequencyHz{3.0};
    synchronizedConfiguration.bands[0].delaySignalPolarity =
      dsp::DelaySignalPolarity::reverse;
    configureBand(synchronizedConfiguration.bands[7], 500.0, 0.0, 0.0, -1.0, false,
                  1.0, 0.0, 0.0, dsp::ModulationWaveform::sawDown);
    synchronize(synchronizedConfiguration.modulationSync,
                0,
                DelayBandId::band8,
                0.5);

    auto independentReferenceConfiguration = synchronizedConfiguration;
    independentReferenceConfiguration.modulationSync = {};
    independentReferenceConfiguration.bands[0].modulationRate = dsp::ModulationRateHz{1.0};
    independentReferenceConfiguration.bands[0].modulationPhase =
      dsp::ModulationPhaseCycles{0.5};

    dsp::HoldsworthDelayEngine synchronizedEngine(1000.0);
    dsp::HoldsworthDelayEngine independentReference(1000.0);
    synchronizedEngine.prepare(sampleRate, sampleCount);
    independentReference.prepare(sampleRate, sampleCount);
    if (!synchronizedEngine.applyConfiguration(synchronizedConfiguration).wasApplied()
        || !independentReference.applyConfiguration(independentReferenceConfiguration)
              .wasApplied())
      return false;

    std::array<double, sampleCount> syncLeft{};
    std::array<double, sampleCount> syncRight{};
    std::array<double, sampleCount> referenceLeft{};
    std::array<double, sampleCount> referenceRight{};
    synchronizedEngine.processBlock(input, syncLeft, syncRight);
    independentReference.processBlock(input, referenceLeft, referenceRight);
    if (!expectSamples("half-cycle slave waveform/depth left",
                       syncLeft,
                       referenceLeft,
                       1.0e-10)
        || !expectSamples("half-cycle slave waveform/depth right",
                       syncRight,
                       referenceRight,
                       1.0e-10))
    {
      std::cerr << "half-cycle mismatch for waveform "
                << static_cast<int>(waveform) << '\n';
      return false;
    }
  }

  return true;
}

dsp::HoldsworthDelayConfiguration makeFanOutConfiguration() noexcept
{
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[7], 30.0, 0.22, 0.35, 0.0, true,
                1.37, 1.7, 0.13, dsp::ModulationWaveform::sine);
  configureBand(configuration.bands[0], 12.0, 0.11, 0.4, -1.0, true,
                41.0, 0.5, 0.2, dsp::ModulationWaveform::sine);
  configureBand(configuration.bands[2], 18.0, 0.17, 0.5, 1.0, true,
                42.0, 1.0, 0.3, dsp::ModulationWaveform::triangle);
  configureBand(configuration.bands[4], 24.0, 0.19, 0.6, -0.25, true,
                43.0, 1.2, 0.4, dsp::ModulationWaveform::sawUp);
  synchronize(configuration.modulationSync, 0, DelayBandId::band8, 0.0);
  synchronize(configuration.modulationSync, 2, DelayBandId::band8, 0.25);
  synchronize(configuration.modulationSync, 4, DelayBandId::band8, 0.75);
  return configuration;
}

void renderPartitioned(dsp::HoldsworthDelayEngine& engine,
                       const std::span<const double> input,
                       const std::span<double> left,
                       const std::span<double> right)
{
  constexpr std::array<std::size_t, 7> blocks{1, 31, 3, 17, 2, 29, 11};
  std::size_t offset = 0;
  std::size_t blockIndex = 0;
  while (offset < input.size())
  {
    const std::size_t blockSize =
      std::min(blocks[blockIndex % blocks.size()], input.size() - offset);
    engine.processBlock(input.subspan(offset, blockSize),
                        left.subspan(offset, blockSize),
                        right.subspan(offset, blockSize));
    offset += blockSize;
    ++blockIndex;
  }
}

bool testFanOutMasterAfterSlavesIsPartitionInvariantAndResetDeterministic()
{
  constexpr std::size_t sampleCount =
    2 * dsp::DelayModulator::kCacheResynchronizationInterval + 137;
  const auto configuration = makeFanOutConfiguration();
  dsp::HoldsworthDelayEngine whole(60.0);
  dsp::HoldsworthDelayEngine partitioned(60.0);
  whole.prepare(1000.0, sampleCount);
  partitioned.prepare(1000.0, 31);
  if (!whole.applyConfiguration(configuration).wasApplied()
      || !partitioned.applyConfiguration(configuration).wasApplied())
    return false;

  std::vector<double> input(sampleCount);
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
    input[sample] = deterministicInput(sample);
  std::vector<double> wholeLeft(sampleCount);
  std::vector<double> wholeRight(sampleCount);
  std::vector<double> partitionedLeft(sampleCount);
  std::vector<double> partitionedRight(sampleCount);
  std::vector<double> resetLeft(sampleCount);
  std::vector<double> resetRight(sampleCount);

  whole.processBlock(input, wholeLeft, wholeRight);
  renderPartitioned(partitioned, input, partitionedLeft, partitionedRight);
  if (!expectSamplesBitExact("SYNC fan-out partition left", partitionedLeft, wholeLeft)
      || !expectSamplesBitExact("SYNC fan-out partition right", partitionedRight, wholeRight))
    return false;

  whole.reset();
  partitioned.reset();
  whole.processBlock(input, resetLeft, resetRight);
  if (!expectSamplesBitExact("SYNC reset whole left", resetLeft, wholeLeft)
      || !expectSamplesBitExact("SYNC reset whole right", resetRight, wholeRight))
    return false;

  std::fill(resetLeft.begin(), resetLeft.end(), 0.0);
  std::fill(resetRight.begin(), resetRight.end(), 0.0);
  renderPartitioned(partitioned, input, resetLeft, resetRight);
  return expectSamplesBitExact("SYNC reset partitioned left", resetLeft, wholeLeft)
         && expectSamplesBitExact("SYNC reset partitioned right", resetRight, wholeRight);
}

bool testSynchronizedConfigurationAndProcessingDoNotAllocate()
{
  constexpr std::size_t blockSize = 256;
  const auto configuration = makeFanOutConfiguration();
  dsp::HoldsworthDelayEngine engine(60.0);
  engine.prepare(48000.0, blockSize);
  std::array<double, blockSize> input{};
  std::array<double, blockSize> left{};
  std::array<double, blockSize> right{};
  const ModulationSyncConfiguration independent;

  beginAllocationTracking();
  const auto firstApply = engine.applyConfiguration(configuration);
  engine.processBlock(input, left, right);
  const auto independentApply = engine.applyModulationSyncConfiguration(independent);
  const auto synchronizedApply =
    engine.applyModulationSyncConfiguration(configuration.modulationSync);
  engine.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: synchronized configuration/processing made "
              << allocations << " allocation(s)\n";
    return false;
  }
  return firstApply.wasApplied()
         && independentApply == ModulationSyncApplyResult::applied
         && synchronizedApply == ModulationSyncApplyResult::applied;
}

constexpr std::array kTests{
  TestCase{"Yamaha SYNC: source metadata is exact and strongly separated",
           testYamahaSyncSourceMetadataIsStrongAndExact},
  TestCase{"HoldsworthDelayEngine SYNC: offsets canonicalize and invalid graphs roll back",
           testCanonicalOffsetsAndTransactionalGraphValidation},
  TestCase{"HoldsworthDelayEngine SYNC: rejected whole configuration preserves continuation",
           testRejectedWholeConfigurationPreservesAllStateAndContinuation},
  TestCase{"HoldsworthDelayEngine SYNC: rejected graph-only configuration preserves continuation",
           testRejectedGraphOnlyConfigurationPreservesClockHistoryAndContinuation},
  TestCase{"HoldsworthDelayEngine SYNC: root audio advances exactly once across long render",
           testRootAudioAdvancesExactlyOnceAcrossLongRender},
  TestCase{"HoldsworthDelayEngine SYNC: zero-depth and disabled roots still clock slaves",
           testZeroDepthAndDisabledRootsStillClockSlave},
  TestCase{"HoldsworthDelayEngine SYNC: equal-depth Sine half-cycle is exact with lower master",
           testEqualDepthSineHalfCycleIsExactOpposite},
  TestCase{"HoldsworthDelayEngine SYNC: half-cycle slave retains waveform and depth",
           testHalfCycleAndSlaveWaveformDepthRemainIndependent},
  TestCase{"HoldsworthDelayEngine SYNC: fan-out is ordered, partition invariant, and reset deterministic",
           testFanOutMasterAfterSlavesIsPartitionInvariantAndResetDeterministic},
  TestCase{"HoldsworthDelayEngine SYNC: configuration and processing do not allocate",
           testSynchronizedConfigurationAndProcessingDoNotAllocate},
};

} // namespace

TestSuite modulationSyncTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
