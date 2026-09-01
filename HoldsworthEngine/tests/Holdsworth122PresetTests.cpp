#include "../dsp/HoldsworthDelayPresets.h"
#include "TestHarness.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <span>
#include <type_traits>

namespace holdsworth::test
{
namespace
{

using BandNumber = presets::YamahaEffectBandNumber;

static_assert(!std::is_same_v<dsp::YamahaFeedbackControlValue,
                              dsp::NormalizedFeedbackCoefficient>);
static_assert(!std::is_convertible_v<dsp::YamahaDelayTimeMs, double>);
static_assert(!std::is_convertible_v<presets::YamahaSpeedControlValue,
                                     dsp::ModulationRateHz>);
static_assert(!std::is_convertible_v<presets::YamahaDepthControlValue,
                                     dsp::ModulationDepthMs>);
static_assert(!std::is_convertible_v<presets::YamahaTapPercentValue,
                                     dsp::TapFraction>);
static_assert(!std::is_same_v<BandNumber, dsp::DelayBandId>);

struct ExpectedSourceBand final
{
  BandNumber number;
  double delayTimeMs;
  double feedback;
  dsp::YamahaPanDirection panDirection;
  double panMagnitude;
  double level;
};

bool expectActiveSourceBand(const dsp::DocumentedYamahaBandValues& band,
                            const ExpectedSourceBand& expected)
{
  const bool exact =
    band.switchState == presets::YamahaEffectBandSwitchState::on
    && band.connectControlValue.has_value()
    && band.connectControlValue->state() == presets::YamahaConnectControlState::input
    && !band.connectControlValue->sourceBand().has_value()
    && band.groupControlValue.has_value()
    && band.groupControlValue->firstBand() == expected.number
    && band.groupControlValue->lastBand() == expected.number
    && band.feedbackControlValue.has_value()
    && band.feedbackControlValue->value == expected.feedback
    && band.speedControlValue.has_value()
    && band.speedControlValue->value == 0.0
    && band.depthControlValue.has_value()
    && band.depthControlValue->value == 0.0
    && band.delayTimeMs.has_value()
    && band.delayTimeMs->value == expected.delayTimeMs
    && band.panControlValue.has_value()
    && band.panControlValue->direction == expected.panDirection
    && band.panControlValue->magnitude == expected.panMagnitude
    && band.levelControlValue.has_value()
    && band.levelControlValue->value == expected.level
    && band.lowCutControlValue.has_value()
    && band.lowCutControlValue->state == presets::YamahaFilterControlState::off
    && band.highCutControlValue.has_value()
    && band.highCutControlValue->state == presets::YamahaFilterControlState::off
    && band.tapPercentValue.has_value()
    && band.tapPercentValue->value == 100.0
    && band.waveformControlValue == presets::YamahaWaveformControlValue::sine
    && band.delaySignalPhaseControlValue
         == presets::YamahaDelaySignalPhaseControlValue::normal
    && band.syncControlValue.has_value()
    && band.syncControlValue->state() == presets::YamahaSyncControlState::independentSelf
    && band.syncControlValue->displayedBand() == expected.number;
  if (!exact)
    std::cerr << "122 active Yamaha source Band "
              << static_cast<unsigned int>(expected.number) << " mismatch\n";
  return exact;
}

bool equalBandConfiguration(const dsp::DelayBandConfiguration& actual,
                            const dsp::DelayBandConfiguration& expected) noexcept
{
  return actual.delayTimeMs == expected.delayTimeMs
         && actual.feedback.value == expected.feedback.value
         && actual.outputLevel == expected.outputLevel
         && actual.pan == expected.pan
         && actual.enabled == expected.enabled
         && actual.modulationRate.value == expected.modulationRate.value
         && actual.modulationDepth.value == expected.modulationDepth.value
         && actual.modulationPhase.value == expected.modulationPhase.value
         && actual.loopFilter.lowCut.has_value() == expected.loopFilter.lowCut.has_value()
         && actual.loopFilter.highCut.has_value() == expected.loopFilter.highCut.has_value()
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
    if (!equalBandConfiguration(actual.bands[index], expected.bands[index])
        || actual.modulationSync.relationships[index].has_value()
             != expected.modulationSync.relationships[index].has_value()
        || actual.audioRouting.inputs[index].has_value()
             != expected.audioRouting.inputs[index].has_value()
        || actual.delayGrouping.groupsByHead[index].has_value()
             != expected.delayGrouping.groupsByHead[index].has_value())
      return false;
  }
  return true;
}

bool expectPhysicalBand(const std::size_t index,
                        const double delayTimeMs,
                        const double feedback,
                        const double pan,
                        const double level)
{
  const auto& band =
    dsp::presets::holdsworth122ProvisionalV1().dspConfiguration.bands[index];
  const bool exact = band.enabled
                     && band.delayTimeMs == delayTimeMs
                     && band.feedback.value == feedback
                     && band.outputLevel == level
                     && band.pan == pan
                     && band.modulationRate.value == 0.0
                     && band.modulationDepth.value == 0.0
                     && band.modulationPhase.value == 0.0
                     && !band.loopFilter.lowCut.has_value()
                     && !band.loopFilter.highCut.has_value()
                     && band.tapFraction.value == 1.0
                     && band.modulationWaveform == dsp::ModulationWaveform::sine
                     && band.delaySignalPolarity == dsp::DelaySignalPolarity::normal;
  if (!exact)
    std::cerr << "122 physical Band " << (index + 1) << " mismatch\n";
  return exact;
}

bool testExactIdentityAttributionAndYamahaSource()
{
  const auto& preset = dsp::presets::holdsworth122ProvisionalV1();
  if (preset.id != "holdsworth122-provisional-v1"
      || preset.displayName
           != "Holdsworth 122 / Stereo Enhanced Lead Solo Patch 2 (Provisional v1)"
      || !preset.documentedYamahaPresetIdentity.has_value()
      || preset.documentedYamahaPresetIdentity->presetNumber != "122"
      || preset.documentedYamahaPresetIdentity->presetName
           != "Stereo Enhanced Lead Solo Patch 2"
      || preset.documentedYamahaPresetIdentity->author != "Allan Holdsworth")
    return false;

  constexpr std::array expected{
    ExpectedSourceBand{BandNumber::band1, 25.0, 0.0,
                       dsp::YamahaPanDirection::left, 10.0, 10.0},
    ExpectedSourceBand{BandNumber::band2, 36.8, 0.0,
                       dsp::YamahaPanDirection::right, 10.0, 10.0},
    ExpectedSourceBand{BandNumber::band3, 96.0, 0.0,
                       dsp::YamahaPanDirection::left, 7.5, 7.0},
    ExpectedSourceBand{BandNumber::band4, 110.0, 0.0,
                       dsp::YamahaPanDirection::right, 7.5, 7.0},
    ExpectedSourceBand{BandNumber::band5, 276.0, 4.5,
                       dsp::YamahaPanDirection::left, 10.0, 5.0},
    ExpectedSourceBand{BandNumber::band6, 370.0, 4.0,
                       dsp::YamahaPanDirection::right, 10.0, 5.0},
    ExpectedSourceBand{BandNumber::band7, 355.0, 3.5,
                       dsp::YamahaPanDirection::left, 10.0, 5.0},
    ExpectedSourceBand{BandNumber::band8, 461.0, 3.0,
                       dsp::YamahaPanDirection::right, 10.0, 5.0}};
  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    if (!expectActiveSourceBand(preset.documentedYamahaValues[index], expected[index]))
      return false;
  }

  const auto& globals = preset.documentedYamahaGlobalValues;
  return globals.effectLevel.has_value()
         && globals.effectLevel->value == 8.5
         && globals.directLevel.has_value()
         && globals.directLevel->value == 8.5
         && globals.directPan.has_value()
         && globals.directPan->direction == dsp::YamahaPanDirection::center
         && globals.directPan->magnitude == 0.0;
}

bool testPhysicalConfigurationCapacityAndRelationships()
{
  const auto& preset = dsp::presets::holdsworth122ProvisionalV1();
  if (!expectPhysicalBand(0, 25.0, 0.0, -1.0, 1.0)
      || !expectPhysicalBand(1, 36.8, 0.0, 1.0, 1.0)
      || !expectPhysicalBand(2, 96.0, 0.0, -0.75, 0.7)
      || !expectPhysicalBand(3, 110.0, 0.0, 0.75, 0.7)
      || !expectPhysicalBand(4, 276.0, 0.45, -1.0, 0.5)
      || !expectPhysicalBand(5, 370.0, 0.40, 1.0, 0.5)
      || !expectPhysicalBand(6, 355.0, 0.35, -1.0, 0.5)
      || !expectPhysicalBand(7, 461.0, 0.30, 1.0, 0.5)
      || preset.dspConfiguration.globalWetOutputLevel != 1.0
      || preset.requiredMaximumDelayTimeMs != 461.0
      || preset.requiredGroupedDelayPhysicalCapacity.has_value()
      || preset.feedbackCalibration
           != dsp::FeedbackCalibrationStatus::provisionalUnmeasured
      || !preset.modulationCalibration.has_value()
      || preset.modulationCalibration->speedMapping
           != dsp::YamahaModulationMappingStatus::unmeasured
      || preset.modulationCalibration->depthMapping
           != dsp::YamahaModulationMappingStatus::unmeasured)
    return false;

  for (std::size_t index = 0; index < dsp::kHoldsworthDelayBandCount; ++index)
  {
    if (preset.dspConfiguration.audioRouting.inputs[index].has_value()
        || preset.dspConfiguration.modulationSync.relationships[index].has_value()
        || preset.dspConfiguration.delayGrouping.groupsByHead[index].has_value())
      return false;
  }
  return true;
}

bool testConfigurationRoundTripAndApplicationAreAllocationFree()
{
  const auto& expected = dsp::presets::holdsworth122ProvisionalV1().dspConfiguration;
  dsp::HoldsworthDelayEngine engine(461.0);
  engine.prepare(48000.0, 64);
  beginAllocationTracking();
  const auto result = engine.applyConfiguration(expected);
  const auto roundTrip = engine.configuration();
  const std::size_t allocations = endAllocationTracking();
  return result.wasApplied() && allocations == 0
         && engine.maximumDelayTimeMs() == 461.0
         && equalConfiguration(roundTrip, expected);
}

template <std::size_t SampleCount>
double absoluteEnergy(const std::array<double, SampleCount>& samples,
                      const std::size_t begin,
                      const std::size_t end)
{
  double result = 0.0;
  for (std::size_t index = begin; index < end; ++index)
    result += std::abs(samples[index]);
  return result;
}

bool testRenderPreservesTimingStereoPlacementAndFeedbackRecurrence()
{
  constexpr std::size_t sampleCount = 1000;
  dsp::HoldsworthDelayEngine engine(461.0);
  engine.prepare(1000.0, sampleCount);
  if (!engine.applyConfiguration(
               dsp::presets::holdsworth122ProvisionalV1().dspConfiguration)
         .wasApplied())
    return false;

  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  engine.processBlock(input, left, right);

  const double band3Left = absoluteEnergy(left, 95, 98);
  const double band3Right = absoluteEnergy(right, 95, 98);
  const double band4Left = absoluteEnergy(left, 109, 112);
  const double band4Right = absoluteEnergy(right, 109, 112);
  return absoluteEnergy(left, 25, 26) > 0.0
         && absoluteEnergy(right, 25, 26) == 0.0
         && absoluteEnergy(right, 36, 39) > 0.0
         && absoluteEnergy(left, 36, 39) == 0.0
         && band3Left > band3Right
         && band3Right > 0.0
         && band4Right > band4Left
         && band4Left > 0.0
         && absoluteEnergy(left, 276, 277) > 0.0
         && absoluteEnergy(right, 276, 277) == 0.0
         && absoluteEnergy(left, 355, 356) > 0.0
         && absoluteEnergy(right, 355, 356) == 0.0
         && absoluteEnergy(right, 370, 371) > 0.0
         && absoluteEnergy(left, 370, 371) == 0.0
         && absoluteEnergy(right, 461, 462) > 0.0
         && absoluteEnergy(left, 461, 462) == 0.0
         && absoluteEnergy(left, 552, 553) > 0.0
         && absoluteEnergy(right, 740, 741) > 0.0
         && absoluteEnergy(right, 922, 923) > 0.0;
}

bool testRenderIsPartitionInvariantAndResetDeterministic()
{
  constexpr std::size_t sampleCount = 1000;
  constexpr std::size_t split = 173;
  const auto& configuration =
    dsp::presets::holdsworth122ProvisionalV1().dspConfiguration;
  dsp::HoldsworthDelayEngine whole(461.0);
  dsp::HoldsworthDelayEngine partitioned(461.0);
  whole.prepare(1000.0, sampleCount);
  partitioned.prepare(1000.0, sampleCount);
  if (!whole.applyConfiguration(configuration).wasApplied()
      || !partitioned.applyConfiguration(configuration).wasApplied())
    return false;

  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> wholeLeft{};
  std::array<double, sampleCount> wholeRight{};
  std::array<double, sampleCount> splitLeft{};
  std::array<double, sampleCount> splitRight{};
  whole.processBlock(input, wholeLeft, wholeRight);
  partitioned.processBlock(std::span<const double>{input}.first(split),
                           std::span<double>{splitLeft}.first(split),
                           std::span<double>{splitRight}.first(split));
  partitioned.processBlock(std::span<const double>{input}.subspan(split),
                           std::span<double>{splitLeft}.subspan(split),
                           std::span<double>{splitRight}.subspan(split));
  if (!expectSamplesBitExact("122 partitioned left", splitLeft, wholeLeft)
      || !expectSamplesBitExact("122 partitioned right", splitRight, wholeRight))
    return false;

  partitioned.reset();
  splitLeft.fill(0.0);
  splitRight.fill(0.0);
  partitioned.processBlock(input, splitLeft, splitRight);
  return expectSamplesBitExact("122 reset left", splitLeft, wholeLeft)
         && expectSamplesBitExact("122 reset right", splitRight, wholeRight);
}

bool testSourceAndDspTypesRemainSeparated()
{
  return true;
}

constexpr std::array kTests{
  TestCase{"Holdsworth 122: exact identity, attribution, and Yamaha source metadata",
           testExactIdentityAttributionAndYamahaSource},
  TestCase{"Holdsworth 122: source and DSP types remain strongly separated",
           testSourceAndDspTypesRemainSeparated},
  TestCase{"Holdsworth 122: exact physical configuration, capacity, and relationships",
           testPhysicalConfigurationCapacityAndRelationships},
  TestCase{"Holdsworth 122: configuration round-trip and application are allocation-free",
           testConfigurationRoundTripAndApplicationAreAllocationFree},
  TestCase{"Holdsworth 122: render preserves timing, stereo placement, and feedback recurrence",
           testRenderPreservesTimingStereoPlacementAndFeedbackRecurrence},
  TestCase{"Holdsworth 122: render is partition invariant and reset deterministic",
           testRenderIsPartitionInvariantAndResetDeterministic},
};

} // namespace

TestSuite holdsworth122PresetTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
