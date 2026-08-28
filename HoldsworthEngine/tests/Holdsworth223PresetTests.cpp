#include "../dsp/HoldsworthDelayPresets.h"
#include "TestHarness.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <type_traits>

namespace holdsworth::test
{
namespace
{

using BandNumber = presets::YamahaEffectBandNumber;
using PhaseValue = presets::YamahaDelaySignalPhaseControlValue;

static_assert(!std::is_same_v<dsp::YamahaFeedbackControlValue, dsp::NormalizedFeedbackCoefficient>);
static_assert(!std::is_convertible_v<dsp::YamahaDelayTimeMs, double>);
static_assert(!std::is_convertible_v<presets::YamahaSpeedControlValue, dsp::ModulationRateHz>);
static_assert(!std::is_convertible_v<presets::YamahaDepthControlValue, dsp::ModulationDepthMs>);
static_assert(!std::is_convertible_v<presets::YamahaTapPercentValue, dsp::TapFraction>);
static_assert(!std::is_same_v<PhaseValue, dsp::DelaySignalPolarity>);
static_assert(!std::is_same_v<BandNumber, dsp::DelayBandId>);

struct ExpectedSourceBand final
{
  BandNumber number;
  double delayTimeMs;
  PhaseValue phase;
  double feedback;
  dsp::YamahaPanDirection panDirection;
  double level;
};

bool expectDisabledSourceBand(const std::size_t index, const dsp::DocumentedYamahaBandValues& band)
{
  const bool exact = band.switchState == presets::YamahaEffectBandSwitchState::off
                     && !band.connectControlValue.has_value() && !band.groupControlValue.has_value()
                     && !band.feedbackControlValue.has_value() && !band.speedControlValue.has_value()
                     && !band.depthControlValue.has_value() && !band.delayTimeMs.has_value()
                     && !band.panControlValue.has_value() && !band.levelControlValue.has_value()
                     && !band.lowCutControlValue.has_value() && !band.highCutControlValue.has_value()
                     && !band.tapPercentValue.has_value() && !band.waveformControlValue.has_value()
                     && !band.delaySignalPhaseControlValue.has_value() && !band.syncControlValue.has_value();
  if (!exact)
    std::cerr << "223 disabled Band " << (index + 1) << " contains undocumented Yamaha values\n";
  return exact;
}

bool expectActiveSourceBand(const dsp::DocumentedYamahaBandValues& band, const ExpectedSourceBand& expected)
{
  const bool exact =
    band.switchState == presets::YamahaEffectBandSwitchState::on && band.connectControlValue.has_value()
    && band.connectControlValue->state() == presets::YamahaConnectControlState::input
    && !band.connectControlValue->sourceBand().has_value() && band.groupControlValue.has_value()
    && band.groupControlValue->firstBand() == expected.number && band.groupControlValue->lastBand() == expected.number
    && band.feedbackControlValue.has_value() && band.feedbackControlValue->value == expected.feedback
    && band.speedControlValue.has_value() && band.speedControlValue->value == 0.0 && band.depthControlValue.has_value()
    && band.depthControlValue->value == 0.0 && band.delayTimeMs.has_value()
    && band.delayTimeMs->value == expected.delayTimeMs && band.panControlValue.has_value()
    && band.panControlValue->direction == expected.panDirection && band.panControlValue->magnitude == 10.0
    && band.levelControlValue.has_value() && band.levelControlValue->value == expected.level
    && band.lowCutControlValue.has_value() && band.lowCutControlValue->state == presets::YamahaFilterControlState::off
    && band.highCutControlValue.has_value() && band.highCutControlValue->state == presets::YamahaFilterControlState::off
    && band.tapPercentValue.has_value() && band.tapPercentValue->value == 100.0
    && band.waveformControlValue == presets::YamahaWaveformControlValue::sine
    && band.delaySignalPhaseControlValue == expected.phase && band.syncControlValue.has_value()
    && band.syncControlValue->state() == presets::YamahaSyncControlState::independentSelf
    && band.syncControlValue->displayedBand() == expected.number;
  if (!exact)
    std::cerr << "223 active Yamaha source Band " << static_cast<unsigned int>(expected.number) << " mismatch\n";
  return exact;
}

bool equalBandConfiguration(const dsp::DelayBandConfiguration& actual,
                            const dsp::DelayBandConfiguration& expected) noexcept
{
  return actual.delayTimeMs == expected.delayTimeMs && actual.feedback.value == expected.feedback.value
         && actual.outputLevel == expected.outputLevel && actual.pan == expected.pan
         && actual.enabled == expected.enabled && actual.modulationRate.value == expected.modulationRate.value
         && actual.modulationDepth.value == expected.modulationDepth.value
         && actual.modulationPhase.value == expected.modulationPhase.value
         && actual.loopFilter.lowCut.has_value() == expected.loopFilter.lowCut.has_value()
         && actual.loopFilter.highCut.has_value() == expected.loopFilter.highCut.has_value()
         && (!actual.loopFilter.lowCut.has_value()
             || actual.loopFilter.lowCut->value == expected.loopFilter.lowCut->value)
         && (!actual.loopFilter.highCut.has_value()
             || actual.loopFilter.highCut->value == expected.loopFilter.highCut->value)
         && actual.tapFraction.value == expected.tapFraction.value
         && actual.modulationWaveform == expected.modulationWaveform
         && actual.delaySignalPolarity == expected.delaySignalPolarity;
}

bool equalConfiguration(const dsp::HoldsworthDelayConfiguration& actual,
                        const dsp::HoldsworthDelayConfiguration& expected) noexcept
{
  if (actual.globalWetOutputLevel != expected.globalWetOutputLevel)
    return false;

  for (std::size_t index = 0; index < dsp::kHoldsworthDelayBandCount; ++index)
  {
    if (!equalBandConfiguration(actual.bands[index], expected.bands[index]))
      return false;

    const auto& actualSync = actual.modulationSync.relationships[index];
    const auto& expectedSync = expected.modulationSync.relationships[index];
    if (actualSync.has_value() != expectedSync.has_value()
        || (actualSync.has_value()
            && (actualSync->masterBand != expectedSync->masterBand
                || actualSync->phaseOffset.value != expectedSync->phaseOffset.value)))
      return false;

    const auto& actualInput = actual.audioRouting.inputs[index];
    const auto& expectedInput = expected.audioRouting.inputs[index];
    if (actualInput.has_value() != expectedInput.has_value()
        || (actualInput.has_value() && actualInput->sourceBand != expectedInput->sourceBand))
      return false;

    const auto& actualGroup = actual.delayGrouping.groupsByHead[index];
    const auto& expectedGroup = expected.delayGrouping.groupsByHead[index];
    if (actualGroup.has_value() != expectedGroup.has_value()
        || (actualGroup.has_value() && actualGroup->endBand != expectedGroup->endBand))
      return false;
  }
  return true;
}

bool expectPhysicalBand(const std::size_t index, const double delayTimeMs, const double feedback, const double pan,
                        const double level, const dsp::DelaySignalPolarity polarity)
{
  const auto& band = dsp::presets::holdsworth223ProvisionalV1().dspConfiguration.bands[index];
  const bool exact = band.enabled && band.delayTimeMs == delayTimeMs && band.feedback.value == feedback
                     && band.outputLevel == level && band.pan == pan && band.modulationRate.value == 0.0
                     && band.modulationDepth.value == 0.0 && band.modulationPhase.value == 0.0
                     && !band.loopFilter.lowCut.has_value() && !band.loopFilter.highCut.has_value()
                     && band.tapFraction.value == 1.0 && band.modulationWaveform == dsp::ModulationWaveform::sine
                     && band.delaySignalPolarity == polarity;
  if (!exact)
    std::cerr << "223 physical Band " << (index + 1) << " mismatch\n";
  return exact;
}

bool testExactIdentityAttributionAndYamahaSource()
{
  const auto& preset = dsp::presets::holdsworth223ProvisionalV1();
  if (preset.id != "holdsworth223-provisional-v1" || preset.displayName != "Holdsworth 223 (Provisional v1)"
      || !preset.documentedYamahaPresetIdentity.has_value()
      || preset.documentedYamahaPresetIdentity->presetNumber != "223"
      || preset.documentedYamahaPresetIdentity->presetName != "Single Source Point Stereo Microphone + Echos"
      || preset.documentedYamahaPresetIdentity->author != "Allan Holdsworth"
      || preset.documentedYamahaManualExerciseReference.has_value()
      || preset.documentedYamahaSyncAuditionReference.has_value())
    return false;

  constexpr std::array expected{
    ExpectedSourceBand{BandNumber::band1, 5.25, PhaseValue::normal, 0.0, dsp::YamahaPanDirection::left, 7.0},
    ExpectedSourceBand{BandNumber::band3, 5.48, PhaseValue::reverse, 0.0, dsp::YamahaPanDirection::right, 10.0},
    ExpectedSourceBand{BandNumber::band5, 250.0, PhaseValue::normal, 4.0, dsp::YamahaPanDirection::left, 3.7},
    ExpectedSourceBand{BandNumber::band6, 341.0, PhaseValue::normal, 3.5, dsp::YamahaPanDirection::right, 3.7},
    ExpectedSourceBand{BandNumber::band7, 300.0, PhaseValue::normal, 4.0, dsp::YamahaPanDirection::left, 3.7},
    ExpectedSourceBand{BandNumber::band8, 400.0, PhaseValue::normal, 3.5, dsp::YamahaPanDirection::right, 3.7}};
  constexpr std::array activeIndices{0U, 2U, 4U, 5U, 6U, 7U};
  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    if (!expectActiveSourceBand(preset.documentedYamahaValues[activeIndices[index]], expected[index]))
      return false;
  }

  const auto& globals = preset.documentedYamahaGlobalValues;
  return expectDisabledSourceBand(1, preset.documentedYamahaValues[1])
         && expectDisabledSourceBand(3, preset.documentedYamahaValues[3]) && globals.effectLevel.has_value()
         && globals.effectLevel->value == 10.0 && globals.directLevel.has_value() && globals.directLevel->value == 8.0
         && globals.directPan.has_value() && globals.directPan->direction == dsp::YamahaPanDirection::center
         && globals.directPan->magnitude == 0.0;
}

bool testPhysicalConfigurationCapacityAndRelationships()
{
  const auto& preset = dsp::presets::holdsworth223ProvisionalV1();
  if (!expectPhysicalBand(0, 5.25, 0.0, -1.0, 0.7, dsp::DelaySignalPolarity::normal)
      || !expectPhysicalBand(2, 5.48, 0.0, 1.0, 1.0, dsp::DelaySignalPolarity::reverse)
      || !expectPhysicalBand(4, 250.0, 0.40, -1.0, 0.37, dsp::DelaySignalPolarity::normal)
      || !expectPhysicalBand(5, 341.0, 0.35, 1.0, 0.37, dsp::DelaySignalPolarity::normal)
      || !expectPhysicalBand(6, 300.0, 0.40, -1.0, 0.37, dsp::DelaySignalPolarity::normal)
      || !expectPhysicalBand(7, 400.0, 0.35, 1.0, 0.37, dsp::DelaySignalPolarity::normal)
      || preset.dspConfiguration.bands[1].enabled || preset.dspConfiguration.bands[3].enabled
      || preset.dspConfiguration.globalWetOutputLevel != 1.0 || preset.requiredMaximumDelayTimeMs != 400.0
      || preset.requiredGroupedDelayPhysicalCapacity.has_value()
      || preset.feedbackCalibration != dsp::FeedbackCalibrationStatus::provisionalUnmeasured
      || !preset.modulationCalibration.has_value()
      || preset.modulationCalibration->speedMapping != dsp::YamahaModulationMappingStatus::unmeasured
      || preset.modulationCalibration->depthMapping != dsp::YamahaModulationMappingStatus::unmeasured)
    return false;

  for (std::size_t index = 0; index < dsp::kHoldsworthDelayBandCount; ++index)
  {
    if (preset.dspConfiguration.audioRouting.inputs[index].has_value()
        || preset.dspConfiguration.modulationSync.relationships[index].has_value()
        || preset.dspConfiguration.delayGrouping.groupsByHead[index].has_value())
    {
      std::cerr << "223 independent CONNECT/GROUP/SYNC relationship mismatch\n";
      return false;
    }
  }
  return true;
}

bool testConfigurationRoundTripAndApplicationAreAllocationFree()
{
  const auto& expected = dsp::presets::holdsworth223ProvisionalV1().dspConfiguration;
  dsp::HoldsworthDelayEngine engine(400.0);
  engine.prepare(48000.0, 64);

  beginAllocationTracking();
  const auto result = engine.applyConfiguration(expected);
  const auto roundTrip = engine.configuration();
  const std::size_t allocations = endAllocationTracking();
  if (allocations != 0)
    std::cerr << "223 application made " << allocations << " allocation(s)\n";
  return result.wasApplied() && allocations == 0 && engine.maximumDelayTimeMs() == 400.0
         && equalConfiguration(roundTrip, expected);
}

bool testImpulseRenderPreservesDocumentedTimingPolarityAndRecurrence()
{
  constexpr std::size_t sampleCount = 901;
  dsp::HoldsworthDelayEngine engine(400.0);
  engine.prepare(1000.0, sampleCount);
  if (!engine.applyConfiguration(dsp::presets::holdsworth223ProvisionalV1().dspConfiguration).wasApplied())
    return false;

  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  engine.processBlock(input, left, right);

  double shortLeftEnergy = 0.0;
  double shortRightEnergy = 0.0;
  for (std::size_t index = 1; index < 20; ++index)
  {
    shortLeftEnergy += left[index];
    shortRightEnergy += right[index];
  }

  return shortLeftEnergy > 0.0 && shortRightEnergy < 0.0 && expectNear("223 Band 5 first echo", left[250], 0.37)
         && expectNear("223 Band 7 first echo", left[300], 0.37)
         && expectNear("223 Band 6 first echo", right[341], 0.37)
         && expectNear("223 Band 8 first echo", right[400], 0.37)
         && expectNear("223 Band 5 feedback recurrence", left[500], 0.148)
         && expectNear("223 Band 7 feedback recurrence", left[600], 0.148)
         && expectNear("223 Band 6 feedback recurrence", right[682], 0.1295)
         && expectNear("223 Band 8 feedback recurrence", right[800], 0.1295);
}

bool testDocumentedReverseChangesOnlyBand3AudibleOutput()
{
  constexpr std::size_t sampleCount = 901;
  auto documented = dsp::presets::holdsworth223ProvisionalV1().dspConfiguration;
  auto normalBand3 = documented;
  normalBand3.bands[2].delaySignalPolarity = dsp::DelaySignalPolarity::normal;

  dsp::HoldsworthDelayEngine reverseEngine(400.0);
  dsp::HoldsworthDelayEngine normalEngine(400.0);
  reverseEngine.prepare(1000.0, sampleCount);
  normalEngine.prepare(1000.0, sampleCount);
  if (!reverseEngine.applyConfiguration(documented).wasApplied()
      || !normalEngine.applyConfiguration(normalBand3).wasApplied())
    return false;

  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> reverseLeft{};
  std::array<double, sampleCount> reverseRight{};
  std::array<double, sampleCount> normalLeft{};
  std::array<double, sampleCount> normalRight{};
  reverseEngine.processBlock(input, reverseLeft, reverseRight);
  normalEngine.processBlock(input, normalLeft, normalRight);

  if (!expectSamplesBitExact("223 REV leaves hard-left and unrelated bands unchanged", reverseLeft, normalLeft))
    return false;

  bool heardBand3Difference = false;
  for (std::size_t index = 0; index < 20; ++index)
  {
    heardBand3Difference =
      heardBand3Difference
      || std::bit_cast<std::uint64_t>(reverseRight[index]) != std::bit_cast<std::uint64_t>(normalRight[index]);
    if (!nearlyEqual(std::abs(reverseRight[index]), std::abs(normalRight[index]), 0.0))
    {
      std::cerr << "223 REV changed Band 3 delay timing or magnitude\n";
      return false;
    }
  }
  if (!heardBand3Difference
      || !expectSamplesBitExact("223 REV leaves longer right echoes and recurrence unchanged",
                                std::span<const double>{reverseRight}.subspan(20),
                                std::span<const double>{normalRight}.subspan(20)))
    return false;

  // Once both engines use NOR, their subsequent output is bit-exact. Thus REV
  // did not mutate delay history, feedback recurrence, modulation, or filters.
  if (!reverseEngine.applyConfiguration(normalBand3).wasApplied()
      || !normalEngine.applyConfiguration(normalBand3).wasApplied())
    return false;
  const std::array<double, sampleCount> silence{};
  std::array<double, sampleCount> reverseTailLeft{};
  std::array<double, sampleCount> reverseTailRight{};
  std::array<double, sampleCount> normalTailLeft{};
  std::array<double, sampleCount> normalTailRight{};
  reverseEngine.processBlock(silence, reverseTailLeft, reverseTailRight);
  normalEngine.processBlock(silence, normalTailLeft, normalTailRight);
  return expectSamplesBitExact("223 REV preserves subsequent left state", reverseTailLeft, normalTailLeft)
         && expectSamplesBitExact("223 REV preserves subsequent right state", reverseTailRight, normalTailRight)
         && equalConfiguration(reverseEngine.configuration(), normalEngine.configuration());
}

bool testSourceAndDspTypesRemainSeparated()
{
  return true;
}

constexpr std::array kTests{
  TestCase{"Holdsworth 223: exact identity, attribution, and Yamaha source metadata",
           testExactIdentityAttributionAndYamahaSource},
  TestCase{"Holdsworth 223: source and DSP types remain strongly separated", testSourceAndDspTypesRemainSeparated},
  TestCase{"Holdsworth 223: exact physical configuration, capacity, and relationships",
           testPhysicalConfigurationCapacityAndRelationships},
  TestCase{"Holdsworth 223: configuration round-trip and application are allocation-free",
           testConfigurationRoundTripAndApplicationAreAllocationFree},
  TestCase{"Holdsworth 223: impulse render preserves timing, polarity, and recurrence",
           testImpulseRenderPreservesDocumentedTimingPolarityAndRecurrence},
  TestCase{"Holdsworth 223: documented REV changes only Band 3 audible output",
           testDocumentedReverseChangesOnlyBand3AudibleOutput},
};

} // namespace

TestSuite holdsworth223PresetTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
