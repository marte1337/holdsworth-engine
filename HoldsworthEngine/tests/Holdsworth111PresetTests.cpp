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
  double tapPercent;
  double speed;
  double depth;
  dsp::YamahaPanDirection panDirection;
  double level;
};

bool expectDisabledSourceBand(const std::size_t index,
                              const dsp::DocumentedYamahaBandValues& band)
{
  const bool exact =
    band.switchState == presets::YamahaEffectBandSwitchState::off
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
  if (!exact)
    std::cerr << "111 disabled Band " << (index + 1)
              << " contains undocumented Yamaha values\n";
  return exact;
}

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
    && band.speedControlValue->value == expected.speed
    && band.depthControlValue.has_value()
    && band.depthControlValue->value == expected.depth
    && band.delayTimeMs.has_value()
    && band.delayTimeMs->value == expected.delayTimeMs
    && band.panControlValue.has_value()
    && band.panControlValue->direction == expected.panDirection
    && band.panControlValue->magnitude == 10.0
    && band.levelControlValue.has_value()
    && band.levelControlValue->value == expected.level
    && band.lowCutControlValue.has_value()
    && band.lowCutControlValue->state == presets::YamahaFilterControlState::off
    && band.highCutControlValue.has_value()
    && band.highCutControlValue->state == presets::YamahaFilterControlState::off
    && band.tapPercentValue.has_value()
    && band.tapPercentValue->value == expected.tapPercent
    && band.waveformControlValue == presets::YamahaWaveformControlValue::sine
    && band.delaySignalPhaseControlValue
         == presets::YamahaDelaySignalPhaseControlValue::normal
    && band.syncControlValue.has_value()
    && band.syncControlValue->state() == presets::YamahaSyncControlState::independentSelf
    && band.syncControlValue->displayedBand() == expected.number;
  if (!exact)
    std::cerr << "111 active Yamaha source Band "
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
    if (!equalBandConfiguration(actual.bands[index], expected.bands[index]))
      return false;
    if (actual.modulationSync.relationships[index].has_value()
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
                        const double level,
                        const double rateHz,
                        const double depthMs,
                        const double phaseCycles,
                        const double tapFraction)
{
  const auto& band =
    dsp::presets::holdsworth111ProvisionalV1().dspConfiguration.bands[index];
  const bool exact = band.enabled
                     && band.delayTimeMs == delayTimeMs
                     && band.feedback.value == feedback
                     && band.outputLevel == level
                     && band.pan == pan
                     && band.modulationRate.value == rateHz
                     && band.modulationDepth.value == depthMs
                     && band.modulationPhase.value == phaseCycles
                     && !band.loopFilter.lowCut.has_value()
                     && !band.loopFilter.highCut.has_value()
                     && band.tapFraction.value == tapFraction
                     && band.modulationWaveform == dsp::ModulationWaveform::sine
                     && band.delaySignalPolarity == dsp::DelaySignalPolarity::normal;
  if (!exact)
    std::cerr << "111 physical Band " << (index + 1) << " mismatch\n";
  return exact;
}

bool testExactIdentityAttributionAndYamahaSource()
{
  const auto& preset = dsp::presets::holdsworth111ProvisionalV1();
  if (preset.id != "holdsworth111-provisional-v1"
      || preset.displayName != "Holdsworth 111 / Chorus 10 (Provisional v1)"
      || !preset.documentedYamahaPresetIdentity.has_value()
      || preset.documentedYamahaPresetIdentity->presetNumber != "111"
      || preset.documentedYamahaPresetIdentity->presetName != "Chorus 10"
      || preset.documentedYamahaPresetIdentity->author != "Allan Holdsworth")
    return false;

  constexpr std::array expected{
    ExpectedSourceBand{BandNumber::band1, 19.4, 0.0, 25.4, 4.5, 2.7,
                       dsp::YamahaPanDirection::left, 10.0},
    ExpectedSourceBand{BandNumber::band2, 15.0, 0.0, 25.4, 5.2, 2.7,
                       dsp::YamahaPanDirection::right, 10.0},
    ExpectedSourceBand{BandNumber::band5, 250.0, 5.0, 100.0, 3.8, 3.0,
                       dsp::YamahaPanDirection::left, 3.5},
    ExpectedSourceBand{BandNumber::band6, 351.0, 4.0, 100.0, 4.2, 3.0,
                       dsp::YamahaPanDirection::right, 3.5},
    ExpectedSourceBand{BandNumber::band7, 300.0, 5.0, 100.0, 3.5, 3.0,
                       dsp::YamahaPanDirection::left, 3.5},
    ExpectedSourceBand{BandNumber::band8, 400.0, 3.0, 100.0, 5.0, 3.0,
                       dsp::YamahaPanDirection::right, 3.5}};
  constexpr std::array activeIndices{0U, 1U, 4U, 5U, 6U, 7U};
  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    if (!expectActiveSourceBand(preset.documentedYamahaValues[activeIndices[index]],
                                expected[index]))
      return false;
  }

  const auto& globals = preset.documentedYamahaGlobalValues;
  return expectDisabledSourceBand(2, preset.documentedYamahaValues[2])
         && expectDisabledSourceBand(3, preset.documentedYamahaValues[3])
         && globals.effectLevel.has_value()
         && globals.effectLevel->value == 8.5
         && globals.directLevel.has_value()
         && globals.directLevel->value == 6.0
         && globals.directPan.has_value()
         && globals.directPan->direction == dsp::YamahaPanDirection::center
         && globals.directPan->magnitude == 0.0;
}

bool testPhysicalConfigurationCapacityAndRelationships()
{
  const auto& preset = dsp::presets::holdsworth111ProvisionalV1();
  if (!expectPhysicalBand(0, 19.4, 0.0, -1.0, 1.0, 0.66, 0.81, 0.0, 0.254)
      || !expectPhysicalBand(1, 15.0, 0.0, 1.0, 1.0, 0.87, 0.81, 0.5, 0.254)
      || !expectPhysicalBand(4, 250.0, 0.40, -1.0, 0.35, 0.46, 0.90, 0.125, 1.0)
      || !expectPhysicalBand(5, 351.0, 0.32, 1.0, 0.35, 0.58, 0.90, 0.625, 1.0)
      || !expectPhysicalBand(6, 300.0, 0.40, -1.0, 0.35, 0.38, 0.90, 0.375, 1.0)
      || !expectPhysicalBand(7, 400.0, 0.24, 1.0, 0.35, 0.81, 0.90, 0.875, 1.0)
      || preset.dspConfiguration.bands[2].enabled
      || preset.dspConfiguration.bands[3].enabled
      || preset.dspConfiguration.globalWetOutputLevel != 1.0
      || preset.requiredMaximumDelayTimeMs != 400.9
      || preset.requiredGroupedDelayPhysicalCapacity.has_value())
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
  const auto& expected = dsp::presets::holdsworth111ProvisionalV1().dspConfiguration;
  dsp::HoldsworthDelayEngine engine(400.9);
  engine.prepare(48000.0, 64);
  beginAllocationTracking();
  const auto result = engine.applyConfiguration(expected);
  const auto roundTrip = engine.configuration();
  const std::size_t allocations = endAllocationTracking();
  return result.wasApplied() && allocations == 0
         && engine.maximumDelayTimeMs() == 400.9
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

bool testRenderHasEarlyTapsLongEchoesAndFeedbackTail()
{
  constexpr std::size_t sampleCount = 1001;
  dsp::HoldsworthDelayEngine engine(400.9);
  engine.prepare(1000.0, sampleCount);
  if (!engine.applyConfiguration(
               dsp::presets::holdsworth111ProvisionalV1().dspConfiguration)
         .wasApplied())
    return false;

  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  engine.processBlock(input, left, right);

  return absoluteEnergy(left, 3, 8) > 0.0
         && absoluteEnergy(right, 2, 7) > 0.0
         && absoluteEnergy(left, 245, 305) > 0.0
         && absoluteEnergy(right, 345, 405) > 0.0
         && absoluteEnergy(left, 450, sampleCount) > 0.0
         && absoluteEnergy(right, 450, sampleCount) > 0.0;
}

bool testRenderIsPartitionInvariantAndResetDeterministic()
{
  constexpr std::size_t sampleCount = 1024;
  constexpr std::size_t split = 137;
  const auto& configuration =
    dsp::presets::holdsworth111ProvisionalV1().dspConfiguration;
  dsp::HoldsworthDelayEngine whole(400.9);
  dsp::HoldsworthDelayEngine partitioned(400.9);
  whole.prepare(48000.0, sampleCount);
  partitioned.prepare(48000.0, sampleCount);
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
  if (!expectSamplesBitExact("111 partitioned left", splitLeft, wholeLeft)
      || !expectSamplesBitExact("111 partitioned right", splitRight, wholeRight))
    return false;

  partitioned.reset();
  splitLeft.fill(0.0);
  splitRight.fill(0.0);
  partitioned.processBlock(input, splitLeft, splitRight);
  return expectSamplesBitExact("111 reset left", splitLeft, wholeLeft)
         && expectSamplesBitExact("111 reset right", splitRight, wholeRight);
}

bool testSourceAndDspTypesRemainSeparated()
{
  return true;
}

constexpr std::array kTests{
  TestCase{"Holdsworth 111: exact identity, attribution, and Yamaha source metadata",
           testExactIdentityAttributionAndYamahaSource},
  TestCase{"Holdsworth 111: source and DSP types remain strongly separated",
           testSourceAndDspTypesRemainSeparated},
  TestCase{"Holdsworth 111: exact physical configuration, capacity, and relationships",
           testPhysicalConfigurationCapacityAndRelationships},
  TestCase{"Holdsworth 111: configuration round-trip and application are allocation-free",
           testConfigurationRoundTripAndApplicationAreAllocationFree},
  TestCase{"Holdsworth 111: render has early TAPs, long echoes, and feedback tail",
           testRenderHasEarlyTapsLongEchoesAndFeedbackTail},
  TestCase{"Holdsworth 111: render is partition invariant and reset deterministic",
           testRenderIsPartitionInvariantAndResetDeterministic},
};

} // namespace

TestSuite holdsworth111PresetTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
