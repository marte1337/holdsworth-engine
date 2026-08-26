#include "../dsp/HoldsworthDelayPresets.h"
#include "../presets/YamahaBandStructureSourceValues.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
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
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaConnectControlValue,
                                     holdsworth::dsp::ConnectedBandAudioInput>);
static_assert(!std::is_convertible_v<holdsworth::dsp::ConnectedBandAudioInput,
                                     holdsworth::presets::YamahaConnectControlValue>);
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

[[nodiscard]] std::uint64_t bitExactFingerprint(
  const std::span<const double> samples) noexcept
{
  constexpr std::uint64_t offsetBasis = UINT64_C(14695981039346656037);
  constexpr std::uint64_t prime = UINT64_C(1099511628211);
  std::uint64_t fingerprint = offsetBasis;
  for (const double sample : samples)
  {
    const std::uint64_t sampleBits = std::bit_cast<std::uint64_t>(sample);
    for (unsigned int shift = 0; shift < 64; shift += 8)
    {
      fingerprint ^= (sampleBits >> shift) & UINT64_C(0xff);
      fingerprint *= prime;
    }
  }
  return fingerprint;
}

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
                        const double pan,
                        const dsp::DelaySignalPolarity expectedPolarity =
                          dsp::DelaySignalPolarity::normal)
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
    && band.delaySignalPolarity == expectedPolarity;

  if (!valid)
    std::cerr << testName << ": DSP band mismatch\n";
  return valid;
}

bool expectConfigurationExact(const std::string_view testName,
                              const dsp::HoldsworthDelayConfiguration& actual,
                              const dsp::HoldsworthDelayConfiguration& expected)
{
  if (actual.globalWetOutputLevel != expected.globalWetOutputLevel)
  {
    std::cerr << testName << ": global wet level differs\n";
    return false;
  }

  for (std::size_t bandIndex = 0; bandIndex < dsp::kHoldsworthDelayBandCount; ++bandIndex)
  {
    const auto& actualBand = actual.bands[bandIndex];
    const auto& expectedBand = expected.bands[bandIndex];
    const bool matches =
      actualBand.delayTimeMs == expectedBand.delayTimeMs
      && actualBand.feedback.value == expectedBand.feedback.value
      && actualBand.outputLevel == expectedBand.outputLevel
      && actualBand.pan == expectedBand.pan
      && actualBand.enabled == expectedBand.enabled
      && actualBand.modulationRate.value == expectedBand.modulationRate.value
      && actualBand.modulationDepth.value == expectedBand.modulationDepth.value
      && actualBand.modulationPhase.value == expectedBand.modulationPhase.value
      && actualBand.tapFraction.value == expectedBand.tapFraction.value
      && actualBand.modulationWaveform == expectedBand.modulationWaveform
      && actualBand.delaySignalPolarity == expectedBand.delaySignalPolarity
      && actualBand.loopFilter.lowCut.has_value()
           == expectedBand.loopFilter.lowCut.has_value()
      && actualBand.loopFilter.highCut.has_value()
           == expectedBand.loopFilter.highCut.has_value()
      && (!actualBand.loopFilter.lowCut.has_value()
          || actualBand.loopFilter.lowCut->value == expectedBand.loopFilter.lowCut->value)
      && (!actualBand.loopFilter.highCut.has_value()
          || actualBand.loopFilter.highCut->value == expectedBand.loopFilter.highCut->value);
    if (!matches)
    {
      std::cerr << testName << ": band " << (bandIndex + 1) << " differs\n";
      return false;
    }
  }

  for (std::size_t bandIndex = 0; bandIndex < dsp::kHoldsworthDelayBandCount; ++bandIndex)
  {
    const auto& actualRelationship = actual.modulationSync.relationships[bandIndex];
    const auto& expectedRelationship = expected.modulationSync.relationships[bandIndex];
    const bool matches =
      actualRelationship.has_value() == expectedRelationship.has_value()
      && (!actualRelationship.has_value()
          || (actualRelationship->masterBand == expectedRelationship->masterBand
              && actualRelationship->phaseOffset.value
                   == expectedRelationship->phaseOffset.value));
    if (!matches)
    {
      std::cerr << testName << ": synchronization relationship " << (bandIndex + 1)
                << " differs\n";
      return false;
    }
  }

  for (std::size_t bandIndex = 0;
       bandIndex < dsp::kHoldsworthDelayBandCount;
       ++bandIndex)
  {
    const auto& actualInput = actual.audioRouting.inputs[bandIndex];
    const auto& expectedInput = expected.audioRouting.inputs[bandIndex];
    if (actualInput.has_value() != expectedInput.has_value()
        || (actualInput.has_value()
            && actualInput->sourceBand != expectedInput->sourceBand))
    {
      std::cerr << testName << ": audio-routing input " << (bandIndex + 1)
                << " differs\n";
      return false;
    }
  }

  return true;
}

bool expectExactPresetConfiguration(const std::string_view testName,
                                    const dsp::HoldsworthDelayPresetDefinition& preset,
                                    const std::optional<double> expectedPhaseOffset,
                                    const dsp::DelaySignalPolarity band1Polarity =
                                      dsp::DelaySignalPolarity::normal)
{
  if (!expectExactDspBand(
        testName, preset.dspConfiguration.bands[0], 0.27, -1.0, band1Polarity)
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

  for (std::size_t bandIndex = 0;
       bandIndex < preset.dspConfiguration.audioRouting.inputs.size();
       ++bandIndex)
  {
    if (preset.dspConfiguration.audioRouting.inputs[bandIndex].has_value())
    {
      std::cerr << testName << ": unexpected DSP CONNECT routing at Band "
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
  const auto& band1Reverse = dsp::presets::sync922Band1ReverseDiagnosticV1();
  const auto& halfCycle = dsp::presets::sync922HalfCycleDiagnosticV1();
  const auto& independent = dsp::presets::sync922IndependentDiagnosticV1();

  if (baseline.id != "sync922-baseline-provisional-v1"
      || band1Reverse.id != "sync922-band1-reverse-diagnostic-v1"
      || halfCycle.id != "sync922-half-cycle-diagnostic-v1"
      || independent.id != "sync922-independent-diagnostic-v1"
      || baseline.displayName
           != "Yamaha 922 Sync Parameter Sample (Baseline, Provisional v1)"
      || band1Reverse.displayName
           != "Yamaha 922 Sync Parameter Sample (Band 1 Reverse Diagnostic v1)"
      || halfCycle.displayName
           != "Yamaha 922 Sync Parameter Sample (180° Diagnostic v1)"
      || independent.displayName != "Yamaha 922 Sync OFF Diagnostic"
      || !expectExactFactorySource("922 baseline source", baseline)
      || !expectExactFactorySource("922 Band 1 Reverse source", band1Reverse)
      || !expectExactFactorySource("922 half-cycle source", halfCycle)
      || !expectNoYamahaSourceMetadata("922 independent source", independent)
      || !expectExactPresetConfiguration("922 baseline DSP", baseline, 0.0)
      || !expectExactPresetConfiguration("922 Band 1 Reverse DSP",
                                         band1Reverse,
                                         0.0,
                                         dsp::DelaySignalPolarity::reverse)
      || !expectExactPresetConfiguration("922 half-cycle DSP", halfCycle, 0.5)
      || !expectExactPresetConfiguration("922 independent DSP", independent, std::nullopt)
      || baseline.documentedYamahaSyncAuditionReference.has_value()
      || band1Reverse.documentedYamahaSyncAuditionReference.has_value()
      || !halfCycle.documentedYamahaSyncAuditionReference.has_value())
    return false;

  const auto& reference = *halfCycle.documentedYamahaSyncAuditionReference;
  return reference.synchronizedBand == YamahaEffectBandNumber::band2
         && nearlyEqual(reference.synchronizedSpeedControlValue.value, 5.0)
         && nearlyEqual(reference.documentedPhaseDifference.value, 180.0)
         && !reference.isFactoryPresetValue
         && halfCycle.documentedYamahaValues[1].speedControlValue.has_value()
         && nearlyEqual(halfCycle.documentedYamahaValues[1].speedControlValue->value, 0.0)
         && baseline.modulationCalibration.has_value()
         && baseline.modulationCalibration->phaseRelationship
              == dsp::ModulationPhaseRelationshipStatus::provisional
         && band1Reverse.modulationCalibration.has_value()
         && band1Reverse.modulationCalibration->phaseRelationship
              == dsp::ModulationPhaseRelationshipStatus::provisional
         && halfCycle.modulationCalibration.has_value()
         && halfCycle.modulationCalibration->phaseRelationship
              == dsp::ModulationPhaseRelationshipStatus::documentedReference;
}

bool testBand1ReverseChangesOnlyDspPolarityAndKeepsFactoryNorNorSource()
{
  const auto& baseline = dsp::presets::sync922BaselineProvisionalV1();
  const auto& diagnostic = dsp::presets::sync922Band1ReverseDiagnosticV1();
  auto expectedDiagnostic = baseline.dspConfiguration;
  expectedDiagnostic.bands[0].delaySignalPolarity = dsp::DelaySignalPolarity::reverse;

  const auto& baselineBand1 = baseline.dspConfiguration.bands[0];
  const auto& baselineBand2 = baseline.dspConfiguration.bands[1];
  const auto& diagnosticBand1 = diagnostic.dspConfiguration.bands[0];
  const auto& diagnosticBand2 = diagnostic.dspConfiguration.bands[1];
  const auto& sourceBand1 = diagnostic.documentedYamahaValues[0];
  const auto& sourceBand2 = diagnostic.documentedYamahaValues[1];

  return baselineBand1.delaySignalPolarity == dsp::DelaySignalPolarity::normal
         && baselineBand2.delaySignalPolarity == dsp::DelaySignalPolarity::normal
         && diagnosticBand1.delaySignalPolarity == dsp::DelaySignalPolarity::reverse
         && diagnosticBand2.delaySignalPolarity == dsp::DelaySignalPolarity::normal
         && expectConfigurationExact("922 Band 1 Reverse single-field DSP delta",
                                     diagnostic.dspConfiguration,
                                     expectedDiagnostic)
         && expectExactFactorySource("922 Band 1 Reverse NOR/NOR source", diagnostic)
         && sourceBand1.delaySignalPhaseControlValue
              == presets::YamahaDelaySignalPhaseControlValue::normal
         && sourceBand2.delaySignalPhaseControlValue
              == presets::YamahaDelaySignalPhaseControlValue::normal
         && !diagnostic.documentedYamahaSyncAuditionReference.has_value();
}

bool testConfigurationsApplyTransactionallyAndRoundTrip()
{
  HoldsworthDelayEngine engine(11.5);
  engine.prepare(48000.0, 64);

  for (const auto* preset :
       std::array{&dsp::presets::sync922BaselineProvisionalV1(),
                  &dsp::presets::sync922Band1ReverseDiagnosticV1(),
                  &dsp::presets::sync922HalfCycleDiagnosticV1(),
                  &dsp::presets::sync922IndependentDiagnosticV1()})
  {
    if (!engine.applyConfiguration(preset->dspConfiguration).wasApplied())
      return false;

    if (!expectConfigurationExact(
          "922 configuration round-trip", engine.configuration(), preset->dspConfiguration))
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
  if (!engine.applyConfiguration(
         dsp::presets::sync922BaselineProvisionalV1().dspConfiguration)
         .wasApplied())
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
  if (!engine.applyConfiguration(
         dsp::presets::sync922BaselineProvisionalV1().dspConfiguration)
         .wasApplied())
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
  if (!engine.applyConfiguration(configuration).wasApplied())
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
  if (!whole.applyConfiguration(configuration).wasApplied()
      || !partitioned.applyConfiguration(configuration).wasApplied())
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

bool testAllExistingDiagnosticRendersMatchFrozenBitExactFingerprints()
{
  constexpr std::size_t sampleCount =
    2 * dsp::DelayModulator::kCacheResynchronizationInterval + 137;
  const std::array presets{
    &dsp::presets::sync922BaselineProvisionalV1(),
    &dsp::presets::sync922Band1ReverseDiagnosticV1(),
    &dsp::presets::sync922HalfCycleDiagnosticV1(),
    &dsp::presets::sync922IndependentDiagnosticV1()};
  constexpr std::array<std::uint64_t, 4> expectedLeft{
    UINT64_C(6041986933145211498),
    UINT64_C(9619149460176975850),
    UINT64_C(6041986933145211498),
    UINT64_C(6041986933145211498)};
  constexpr std::array<std::uint64_t, 4> expectedRight{
    UINT64_C(6041986933145211498),
    UINT64_C(6041986933145211498),
    UINT64_C(12852922705196279779),
    UINT64_C(9710487674041144302)};

  std::vector<double> input(sampleCount);
  for (std::size_t sample = 0; sample < input.size(); ++sample)
  {
    const int centered = static_cast<int>((sample * 43 + 17) % 109) - 54;
    input[sample] = static_cast<double>(centered) / 59.0;
  }
  std::vector<double> left(sampleCount);
  std::vector<double> right(sampleCount);

  bool allMatch = true;
  for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
  {
    HoldsworthDelayEngine engine(11.5);
    engine.prepare(1000.0, sampleCount);
    if (!engine.applyConfiguration(presets[presetIndex]->dspConfiguration)
           .wasApplied())
      return false;
    engine.processBlock(input, left, right);

    const std::uint64_t actualLeft = bitExactFingerprint(left);
    const std::uint64_t actualRight = bitExactFingerprint(right);
    if (actualLeft != expectedLeft[presetIndex]
        || actualRight != expectedRight[presetIndex])
    {
      std::cerr << presets[presetIndex]->id
                << ": frozen 922 render fingerprint mismatch; left="
                << actualLeft << ", right=" << actualRight << '\n';
      allMatch = false;
    }
  }

  return allMatch;
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
  const auto band1ReverseResult = engine.applyConfiguration(
    dsp::presets::sync922Band1ReverseDiagnosticV1().dspConfiguration);
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
  return baselineResult.wasApplied()
         && band1ReverseResult.wasApplied()
         && independentResult.wasApplied()
         && diagnosticResult.wasApplied()
         && allocations == 0;
}

constexpr std::array kTests{
  TestCase{"Yamaha 922: factory source and DSP variants remain exact and separate",
           testFactorySourceAndDspVariantsAreExactAndSeparate},
  TestCase{"Yamaha 922: Band 1 Reverse changes only DSP polarity and keeps NOR/NOR source",
           testBand1ReverseChangesOnlyDspPolarityAndKeepsFactoryNorNorSource},
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
  TestCase{"Yamaha 922: all existing diagnostic renders remain bit-exact",
           testAllExistingDiagnosticRendersMatchFrozenBitExactFingerprints},
  TestCase{"Yamaha 922: configuration and processing perform no allocations",
           testConfigurationsAndProcessingDoNotAllocate},
};

} // namespace

TestSuite sync922PresetTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
