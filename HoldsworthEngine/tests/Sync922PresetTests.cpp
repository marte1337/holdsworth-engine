#include "../dsp/HoldsworthDelayPresets.h"
#include "../presets/YamahaBandStructureSourceValues.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <type_traits>
#include <vector>

static_assert(!std::is_convertible_v<holdsworth::presets::YamahaEffectBandNumber,
                                     holdsworth::dsp::DelayBandId>);
static_assert(!std::is_convertible_v<holdsworth::dsp::DelayBandId,
                                     holdsworth::presets::YamahaEffectBandNumber>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaSpeedControlValue,
                                     holdsworth::dsp::ModulationPhaseOffsetCycles>);
static_assert(!std::is_convertible_v<holdsworth::dsp::ModulationPhaseOffsetCycles,
                                     holdsworth::presets::YamahaSpeedControlValue>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaDepthControlValue,
                                     holdsworth::dsp::ModulationDepthMs>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaConnectControlValue,
                                     holdsworth::dsp::DelayBandId>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaGroupControlValue,
                                     holdsworth::dsp::DelayBandId>);

namespace holdsworth::test
{
namespace
{

using dsp::DelayBandId;
using dsp::HoldsworthDelayConfiguration;
using dsp::HoldsworthDelayEngine;
using dsp::ModulationClockSample;
using dsp::ModulationPhaseOffsetCycles;
using dsp::ModulationSyncApplyResult;
using presets::YamahaConnectControlState;
using presets::YamahaEffectBandNumber;
using presets::YamahaEffectBandSwitchState;
using presets::YamahaFilterControlState;
using presets::YamahaSyncControlState;

bool expectActiveSourceBand(const std::string_view testName,
                            const dsp::DocumentedYamahaBandValues& band,
                            const YamahaEffectBandNumber effectBand,
                            const YamahaSyncControlState syncState,
                            const YamahaEffectBandNumber displayedSyncBand,
                            const double speed,
                            const dsp::YamahaPanDirection panDirection)
{
  const bool valid =
    band.switchState == YamahaEffectBandSwitchState::on
    && band.connectControlValue.has_value()
    && band.connectControlValue->state() == YamahaConnectControlState::input
    && !band.connectControlValue->sourceBand().has_value()
    && band.groupControlValue.has_value()
    && band.groupControlValue->firstBand() == effectBand
    && band.groupControlValue->lastBand() == effectBand
    && band.syncControlValue.has_value()
    && band.syncControlValue->state() == syncState
    && band.syncControlValue->displayedBand() == displayedSyncBand
    && band.waveformControlValue == presets::YamahaWaveformControlValue::sine
    && band.delaySignalPhaseControlValue
         == presets::YamahaDelaySignalPhaseControlValue::normal
    && band.delayTimeMs.has_value() && nearlyEqual(band.delayTimeMs->value, 10.0)
    && band.lowCutControlValue.has_value()
    && band.lowCutControlValue->state == YamahaFilterControlState::off
    && band.highCutControlValue.has_value()
    && band.highCutControlValue->state == YamahaFilterControlState::off
    && band.feedbackControlValue.has_value()
    && nearlyEqual(band.feedbackControlValue->value, 0.0)
    && band.tapPercentValue.has_value()
    && nearlyEqual(band.tapPercentValue->value, 100.0)
    && band.speedControlValue.has_value()
    && nearlyEqual(band.speedControlValue->value, speed)
    && band.depthControlValue.has_value()
    && nearlyEqual(band.depthControlValue->value, 6.1)
    && band.panControlValue.has_value()
    && band.panControlValue->direction == panDirection
    && nearlyEqual(band.panControlValue->magnitude, 10.0)
    && band.levelControlValue.has_value()
    && nearlyEqual(band.levelControlValue->value, 10.0);

  if (!valid)
    std::cerr << testName << ": active Yamaha source band mismatch\n";
  return valid;
}

bool expectDisabledSourceBandHasOnlySwitch(
  const std::string_view testName,
  const dsp::DocumentedYamahaBandValues& band)
{
  const bool valid =
    band.switchState == YamahaEffectBandSwitchState::off
    && !band.connectControlValue.has_value()
    && !band.groupControlValue.has_value()
    && !band.feedbackControlValue.has_value()
    && !band.speedControlValue.has_value()
    && !band.depthControlValue.has_value()
    && !band.delayTimeMs.has_value()
    && !band.panControlValue.has_value()
    && !band.levelControlValue.has_value()
    && !band.lowCutControlValue.has_value()
    && !band.highCutControlValue.has_value()
    && !band.tapPercentValue.has_value()
    && !band.waveformControlValue.has_value()
    && !band.delaySignalPhaseControlValue.has_value()
    && !band.syncControlValue.has_value();

  if (!valid)
    std::cerr << testName << ": disabled Yamaha source row invented dashed fields\n";
  return valid;
}

bool expectExactDspBand(const std::string_view testName,
                        const dsp::DelayBandConfiguration& band,
                        const double rate,
                        const double pan)
{
  const bool valid =
    band.enabled
    && nearlyEqual(band.delayTimeMs, 10.0)
    && nearlyEqual(band.feedback.value, 0.0)
    && nearlyEqual(band.modulationRate.value, rate)
    && nearlyEqual(band.modulationDepth.value, 1.5)
    && nearlyEqual(band.modulationPhase.value, 0.0)
    && nearlyEqual(band.tapFraction.value, 1.0)
    && nearlyEqual(band.pan, pan)
    && nearlyEqual(band.outputLevel, 1.0)
    && band.modulationWaveform == dsp::ModulationWaveform::sine
    && !band.loopFilter.lowCut.has_value()
    && !band.loopFilter.highCut.has_value()
    && band.delaySignalPolarity == dsp::DelaySignalPolarity::normal;

  if (!valid)
    std::cerr << testName << ": DSP band mismatch\n";
  return valid;
}

bool expectExactPresetConfiguration(const std::string_view testName,
                                    const dsp::HoldsworthDelayPresetDefinition& preset,
                                    const std::optional<double> expectedPhaseOffset)
{
  if (!expectExactDspBand(testName, preset.dspConfiguration.bands[0], 0.27, -1.0)
      || !expectExactDspBand(testName, preset.dspConfiguration.bands[1], 0.0, 1.0)
      || !nearlyEqual(preset.dspConfiguration.globalWetOutputLevel, 1.0)
      || !nearlyEqual(preset.requiredMaximumDelayTimeMs, 11.5))
    return false;

  for (std::size_t bandIndex = 2; bandIndex < preset.dspConfiguration.bands.size(); ++bandIndex)
  {
    if (preset.dspConfiguration.bands[bandIndex].enabled)
    {
      std::cerr << testName << ": DSP Band " << (bandIndex + 1) << " was enabled\n";
      return false;
    }
  }

  for (std::size_t bandIndex = 0;
       bandIndex < preset.dspConfiguration.modulationSync.relationships.size();
       ++bandIndex)
  {
    const auto& relationship =
      preset.dspConfiguration.modulationSync.relationships[bandIndex];
    if (bandIndex == 1)
    {
      if (relationship.has_value() != expectedPhaseOffset.has_value()
          || (relationship.has_value()
              && (relationship->masterBand != DelayBandId::band1
                  || !nearlyEqual(relationship->phaseOffset.value,
                                  *expectedPhaseOffset))))
      {
        std::cerr << testName << ": Band 2 synchronization mismatch\n";
        return false;
      }
    }
    else if (relationship.has_value())
    {
      std::cerr << testName << ": unexpected synchronization at Band "
                << (bandIndex + 1) << '\n';
      return false;
    }
  }

  return true;
}

bool expectNoYamahaSourceMetadata(const std::string_view testName,
                                  const dsp::HoldsworthDelayPresetDefinition& preset)
{
  if (preset.documentedYamahaPresetIdentity.has_value()
      || preset.documentedYamahaGlobalValues.effectLevel.has_value()
      || preset.documentedYamahaGlobalValues.directLevel.has_value()
      || preset.documentedYamahaGlobalValues.directPan.has_value()
      || preset.documentedYamahaSyncAuditionReference.has_value())
  {
    std::cerr << testName << ": diagnostic invented Yamaha preset metadata\n";
    return false;
  }

  for (const auto& band : preset.documentedYamahaValues)
  {
    if (band.switchState.has_value() || band.connectControlValue.has_value()
        || band.groupControlValue.has_value() || band.feedbackControlValue.has_value()
        || band.speedControlValue.has_value() || band.depthControlValue.has_value()
        || band.delayTimeMs.has_value() || band.panControlValue.has_value()
        || band.levelControlValue.has_value() || band.lowCutControlValue.has_value()
        || band.highCutControlValue.has_value() || band.tapPercentValue.has_value()
        || band.waveformControlValue.has_value()
        || band.delaySignalPhaseControlValue.has_value()
        || band.syncControlValue.has_value())
    {
      std::cerr << testName << ": diagnostic invented Yamaha band metadata\n";
      return false;
    }
  }
  return true;
}

bool expectExactFactorySource(const std::string_view testName,
                              const dsp::HoldsworthDelayPresetDefinition& preset)
{
  if (!preset.documentedYamahaPresetIdentity.has_value()
      || preset.documentedYamahaPresetIdentity->presetNumber != "922"
      || preset.documentedYamahaPresetIdentity->presetName != "Sync Parameter Sample"
      || !preset.documentedYamahaPresetIdentity->author.empty()
      || !preset.documentedYamahaGlobalValues.effectLevel.has_value()
      || !nearlyEqual(preset.documentedYamahaGlobalValues.effectLevel->value, 10.0)
      || !preset.documentedYamahaGlobalValues.directLevel.has_value()
      || !nearlyEqual(preset.documentedYamahaGlobalValues.directLevel->value, 10.0)
      || !preset.documentedYamahaGlobalValues.directPan.has_value()
      || preset.documentedYamahaGlobalValues.directPan->direction
           != dsp::YamahaPanDirection::center
      || !nearlyEqual(preset.documentedYamahaGlobalValues.directPan->magnitude, 0.0))
  {
    std::cerr << testName << ": Yamaha identity/global metadata mismatch\n";
    return false;
  }

  if (!expectActiveSourceBand(testName,
                              preset.documentedYamahaValues[0],
                              YamahaEffectBandNumber::band1,
                              YamahaSyncControlState::independentSelf,
                              YamahaEffectBandNumber::band1,
                              3.0,
                              dsp::YamahaPanDirection::left)
      || !expectActiveSourceBand(testName,
                                 preset.documentedYamahaValues[1],
                                 YamahaEffectBandNumber::band2,
                                 YamahaSyncControlState::synchronizedToBand,
                                 YamahaEffectBandNumber::band1,
                                 0.0,
                                 dsp::YamahaPanDirection::right))
    return false;

  for (std::size_t bandIndex = 2; bandIndex < preset.documentedYamahaValues.size(); ++bandIndex)
  {
    if (!expectDisabledSourceBandHasOnlySwitch(testName,
                                               preset.documentedYamahaValues[bandIndex]))
      return false;
  }

  return true;
}

bool testFactorySourceAndDspVariantsAreExactAndSeparate()
{
  const auto& baseline = dsp::presets::sync922BaselineProvisionalV1();
  const auto& diagnostic = dsp::presets::sync922HalfCycleDiagnosticV1();
  const auto& independent = dsp::presets::sync922IndependentDiagnosticV1();

  if (baseline.id != "sync922-baseline-provisional-v1"
      || diagnostic.id != "sync922-half-cycle-diagnostic-v1"
      || independent.id != "sync922-independent-diagnostic-v1"
      || baseline.displayName
           != "Yamaha 922 Sync Parameter Sample (Baseline, Provisional v1)"
      || diagnostic.displayName
           != "Yamaha 922 Sync Parameter Sample (180° Diagnostic v1)"
      || independent.displayName != "Yamaha 922 Sync OFF Diagnostic"
      || !expectExactFactorySource("922 baseline source", baseline)
      || !expectExactFactorySource("922 diagnostic source", diagnostic)
      || !expectNoYamahaSourceMetadata("922 independent source", independent)
      || !expectExactPresetConfiguration("922 baseline DSP", baseline, 0.0)
      || !expectExactPresetConfiguration("922 half-cycle DSP", diagnostic, 0.5)
      || !expectExactPresetConfiguration("922 independent DSP", independent, std::nullopt)
      || baseline.documentedYamahaSyncAuditionReference.has_value()
      || !diagnostic.documentedYamahaSyncAuditionReference.has_value())
    return false;

  const auto& reference = *diagnostic.documentedYamahaSyncAuditionReference;
  return reference.synchronizedBand == YamahaEffectBandNumber::band2
         && nearlyEqual(reference.synchronizedSpeedControlValue.value, 5.0)
         && nearlyEqual(reference.documentedPhaseDifference.value, 180.0)
         && !reference.isFactoryPresetValue
         && diagnostic.documentedYamahaValues[1].speedControlValue.has_value()
         && nearlyEqual(diagnostic.documentedYamahaValues[1].speedControlValue->value, 0.0)
         && baseline.modulationCalibration.has_value()
         && baseline.modulationCalibration->phaseRelationship
              == dsp::ModulationPhaseRelationshipStatus::provisional
         && diagnostic.modulationCalibration.has_value()
         && diagnostic.modulationCalibration->phaseRelationship
              == dsp::ModulationPhaseRelationshipStatus::documentedReference;
}

bool testConfigurationsApplyTransactionallyAndRoundTrip()
{
  HoldsworthDelayEngine engine(11.5);
  engine.prepare(48000.0, 64);

  for (const auto* preset :
       std::array{&dsp::presets::sync922BaselineProvisionalV1(),
                  &dsp::presets::sync922HalfCycleDiagnosticV1(),
                  &dsp::presets::sync922IndependentDiagnosticV1()})
  {
    if (engine.applyConfiguration(preset->dspConfiguration)
          != ModulationSyncApplyResult::applied)
      return false;

    const auto actual = engine.configuration();
    const auto& actualRelationship = actual.modulationSync.relationships[1];
    const auto& expectedRelationship =
      preset->dspConfiguration.modulationSync.relationships[1];
    const bool relationshipsMatch =
      actualRelationship.has_value() == expectedRelationship.has_value()
      && (!actualRelationship.has_value()
          || (actualRelationship->masterBand == expectedRelationship->masterBand
              && nearlyEqual(actualRelationship->phaseOffset.value,
                             expectedRelationship->phaseOffset.value)));
    if (!expectExactDspBand("922 round-trip Band 1", actual.bands[0], 0.27, -1.0)
        || !expectExactDspBand("922 round-trip Band 2", actual.bands[1], 0.0, 1.0)
        || !relationshipsMatch)
      return false;
  }

  return true;
}

bool testBaselineLocksRootAndSlaveRatesAcrossLongRender()
{
  constexpr std::size_t sampleCount =
    24 * dsp::DelayModulator::kCacheResynchronizationInterval + 137;
  HoldsworthDelayEngine engine(11.5);
  engine.prepare(1000.0, sampleCount);
  if (engine.applyConfiguration(
        dsp::presets::sync922BaselineProvisionalV1().dspConfiguration)
      != ModulationSyncApplyResult::applied)
    return false;

  std::vector<double> input(sampleCount);
  for (std::size_t sample = 0; sample < input.size(); ++sample)
    input[sample] = std::sin(0.013 * static_cast<double>(sample))
                    + 0.25 * std::cos(0.037 * static_cast<double>(sample));
  std::vector<double> left(sampleCount);
  std::vector<double> right(sampleCount);
  engine.processBlock(input, left, right);
  return expectSamplesBitExact("922 baseline synchronized L/R", left, right);
}

bool testHalfCycleUsesExactOppositeEqualDepthSineOffsets()
{
  constexpr std::size_t sampleCount =
    4 * dsp::DelayModulator::kCacheResynchronizationInterval + 37;
  const auto& configuration =
    dsp::presets::sync922HalfCycleDiagnosticV1().dspConfiguration;
  dsp::DelayModulator root;
  dsp::DelayModulator slave;
  root.setRate(configuration.bands[0].modulationRate);
  root.setDepth(configuration.bands[0].modulationDepth);
  root.setPhase(configuration.bands[0].modulationPhase);
  root.setWaveform(configuration.bands[0].modulationWaveform);
  slave.setRate(configuration.bands[1].modulationRate);
  slave.setDepth(configuration.bands[1].modulationDepth);
  slave.setPhase(configuration.bands[1].modulationPhase);
  slave.setWaveform(configuration.bands[1].modulationWaveform);
  root.prepare(48000.0);
  slave.prepare(48000.0);
  const double dormantSlavePhase = slave.currentPhase().value;

  for (std::size_t sample = 0; sample < sampleCount; ++sample)
  {
    ModulationClockSample clock;
    const std::array rootOffset{root.nextOffsetMs(clock)};
    clock.phase.value += 0.5;
    if (clock.phase.value >= 1.0)
      clock.phase.value -= 1.0;
    clock.sine = -clock.sine;
    clock.cosine = -clock.cosine;
    const std::array slaveOffset{slave.offsetMsAtClockSample(clock)};
    const std::array expected{-rootOffset.front()};
    if (!expectSamplesBitExact("922 exact half-cycle offset", slaveOffset, expected)
        || !nearlyEqual(slave.currentPhase().value, dormantSlavePhase, 0.0))
      return false;
  }

  return true;
}

bool testRemovingSyncRestoresDormantZeroRateAndDisabledBandsStaySilent()
{
  constexpr std::size_t blockSize = 64;
  HoldsworthDelayEngine engine(11.5);
  engine.prepare(1000.0, blockSize);
  if (engine.applyConfiguration(
        dsp::presets::sync922BaselineProvisionalV1().dspConfiguration)
        != ModulationSyncApplyResult::applied)
    return false;

  std::array<double, blockSize> silence{};
  std::array<double, blockSize> left{};
  std::array<double, blockSize> right{};
  engine.processBlock(silence, left, right);
  if (engine.applyModulationSyncConfiguration({}) != ModulationSyncApplyResult::applied)
    return false;

  std::array<double, blockSize> impulse{1.0};
  engine.processBlock(impulse, left, right);
  std::array<double, blockSize> expectedRight{};
  expectedRight[10] = 1.0;
  return expectSamplesBitExact("922 dormant zero-rate Band 2", right, expectedRight);
}

bool testIndependentDiagnosticUsesDormantZeroRateWithoutSync()
{
  constexpr std::size_t blockSize = 32;
  HoldsworthDelayEngine engine(11.5);
  engine.prepare(1000.0, blockSize);
  const auto& configuration =
    dsp::presets::sync922IndependentDiagnosticV1().dspConfiguration;
  if (engine.applyConfiguration(configuration) != ModulationSyncApplyResult::applied)
    return false;

  for (const auto& relationship : engine.configuration().modulationSync.relationships)
  {
    if (relationship.has_value())
      return false;
  }

  std::array<double, blockSize> impulse{};
  impulse[0] = 1.0;
  std::array<double, blockSize> left{};
  std::array<double, blockSize> right{};
  engine.processBlock(impulse, left, right);
  std::array<double, blockSize> expected{};
  expected[10] = 1.0;
  // Band 1 remains the moving 0.27 Hz reference on the left. Band 2's own
  // independent rate is exactly zero, so only the hard-right output is the
  // fixed 10-sample delay.
  return expectSamplesBitExact("922 independent fixed right", right, expected)
         && left != expected;
}

void renderPartitioned(HoldsworthDelayEngine& engine,
                       const std::span<const double> input,
                       const std::span<double> left,
                       const std::span<double> right)
{
  constexpr std::array<std::size_t, 7> blockSizes{1, 31, 3, 17, 2, 29, 11};
  std::size_t offset = 0;
  std::size_t blockIndex = 0;
  while (offset < input.size())
  {
    const std::size_t blockSize =
      std::min(blockSizes[blockIndex % blockSizes.size()], input.size() - offset);
    engine.processBlock(input.subspan(offset, blockSize),
                        left.subspan(offset, blockSize),
                        right.subspan(offset, blockSize));
    offset += blockSize;
    ++blockIndex;
  }
}

bool testDiagnosticIsPartitionInvariantAndResetDeterministic()
{
  constexpr std::size_t sampleCount =
    2 * dsp::DelayModulator::kCacheResynchronizationInterval + 137;
  const auto& configuration =
    dsp::presets::sync922HalfCycleDiagnosticV1().dspConfiguration;
  HoldsworthDelayEngine whole(11.5);
  HoldsworthDelayEngine partitioned(11.5);
  whole.prepare(1000.0, sampleCount);
  partitioned.prepare(1000.0, 31);
  if (whole.applyConfiguration(configuration) != ModulationSyncApplyResult::applied
      || partitioned.applyConfiguration(configuration) != ModulationSyncApplyResult::applied)
    return false;

  std::vector<double> input(sampleCount);
  for (std::size_t sample = 0; sample < input.size(); ++sample)
    input[sample] = std::sin(0.019 * static_cast<double>(sample));
  std::vector<double> wholeLeft(sampleCount);
  std::vector<double> wholeRight(sampleCount);
  std::vector<double> partitionedLeft(sampleCount);
  std::vector<double> partitionedRight(sampleCount);
  whole.processBlock(input, wholeLeft, wholeRight);
  renderPartitioned(partitioned, input, partitionedLeft, partitionedRight);
  if (!expectSamplesBitExact("922 partition left", partitionedLeft, wholeLeft)
      || !expectSamplesBitExact("922 partition right", partitionedRight, wholeRight))
    return false;

  whole.reset();
  std::fill(partitionedLeft.begin(), partitionedLeft.end(), 0.0);
  std::fill(partitionedRight.begin(), partitionedRight.end(), 0.0);
  whole.processBlock(input, partitionedLeft, partitionedRight);
  return expectSamplesBitExact("922 reset left", partitionedLeft, wholeLeft)
         && expectSamplesBitExact("922 reset right", partitionedRight, wholeRight);
}

bool testConfigurationsAndProcessingDoNotAllocate()
{
  constexpr std::size_t blockSize = 256;
  HoldsworthDelayEngine engine(11.5);
  engine.prepare(48000.0, blockSize);
  std::array<double, blockSize> input{};
  std::array<double, blockSize> left{};
  std::array<double, blockSize> right{};

  beginAllocationTracking();
  const auto baselineResult = engine.applyConfiguration(
    dsp::presets::sync922BaselineProvisionalV1().dspConfiguration);
  engine.processBlock(input, left, right);
  const auto independentResult = engine.applyConfiguration(
    dsp::presets::sync922IndependentDiagnosticV1().dspConfiguration);
  engine.processBlock(input, left, right);
  const auto diagnosticResult = engine.applyConfiguration(
    dsp::presets::sync922HalfCycleDiagnosticV1().dspConfiguration);
  engine.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
    std::cerr << "922 configuration/processing allocated " << allocations << " time(s)\n";
  return baselineResult == ModulationSyncApplyResult::applied
         && independentResult == ModulationSyncApplyResult::applied
         && diagnosticResult == ModulationSyncApplyResult::applied
         && allocations == 0;
}

constexpr std::array kTests{
  TestCase{"Yamaha 922: factory source and DSP variants remain exact and separate",
           testFactorySourceAndDspVariantsAreExactAndSeparate},
  TestCase{"Yamaha 922: configurations apply transactionally and round-trip",
           testConfigurationsApplyTransactionallyAndRoundTrip},
  TestCase{"Yamaha 922: baseline locks root/slave rates across long render",
           testBaselineLocksRootAndSlaveRatesAcrossLongRender},
  TestCase{"Yamaha 922: half-cycle diagnostic has exact opposite Sine offsets",
           testHalfCycleUsesExactOppositeEqualDepthSineOffsets},
  TestCase{"Yamaha 922: removing SYNC restores dormant zero-rate slave",
           testRemovingSyncRestoresDormantZeroRateAndDisabledBandsStaySilent},
  TestCase{"Yamaha 922: independent diagnostic has dormant zero-rate modulation",
           testIndependentDiagnosticUsesDormantZeroRateWithoutSync},
  TestCase{"Yamaha 922: diagnostic is partition invariant and reset deterministic",
           testDiagnosticIsPartitionInvariantAndResetDeterministic},
  TestCase{"Yamaha 922: configuration and processing perform no allocations",
           testConfigurationsAndProcessingDoNotAllocate},
};

} // namespace

TestSuite sync922PresetTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
