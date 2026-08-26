#include "../dsp/HoldsworthDelayEngine.h"
#include "../presets/YamahaBandStructureSourceValues.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

static_assert(!std::is_convertible_v<holdsworth::presets::YamahaEffectBandNumber, holdsworth::dsp::DelayBandId>);
static_assert(!std::is_convertible_v<holdsworth::dsp::DelayBandId, holdsworth::presets::YamahaEffectBandNumber>);
static_assert(
  !std::is_convertible_v<holdsworth::presets::YamahaConnectControlValue, holdsworth::dsp::ConnectedBandAudioInput>);
static_assert(
  !std::is_convertible_v<holdsworth::dsp::ConnectedBandAudioInput, holdsworth::presets::YamahaConnectControlValue>);

namespace holdsworth::test
{
namespace
{

using dsp::AudioRoutingApplyResult;
using dsp::AudioRoutingConfiguration;
using dsp::ConnectedBandAudioInput;
using dsp::DelayBandId;
using dsp::ModulationPhaseOffsetCycles;
using dsp::ModulationSyncApplyResult;
using dsp::SynchronizedModulationRelationship;

void configureBand(dsp::DelayBandConfiguration& band, const double delayTimeMs, const double feedback,
                   const double outputLevel, const double pan, const bool enabled = true) noexcept
{
  band.delayTimeMs = delayTimeMs;
  band.feedback = dsp::NormalizedFeedbackCoefficient{feedback};
  band.outputLevel = outputLevel;
  band.pan = pan;
  band.enabled = enabled;
}

void configureDelayBand(dsp::DelayBand& band, const dsp::DelayBandConfiguration& configuration) noexcept
{
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

void connect(AudioRoutingConfiguration& routing, const std::size_t destinationIndex,
             const DelayBandId sourceBand) noexcept
{
  routing.inputs[destinationIndex] = ConnectedBandAudioInput{sourceBand};
}

void synchronize(dsp::ModulationSyncConfiguration& synchronization, const std::size_t slaveIndex,
                 const DelayBandId masterBand, const double phaseOffsetCycles) noexcept
{
  synchronization.relationships[slaveIndex] =
    SynchronizedModulationRelationship{masterBand, ModulationPhaseOffsetCycles{phaseOffsetCycles}};
}

[[nodiscard]] double deterministicInput(const std::size_t sampleIndex) noexcept
{
  const int centered = static_cast<int>((sampleIndex * 29 + 17) % 97) - 48;
  return static_cast<double>(centered) / 51.0;
}

bool applyWholeConfiguration(dsp::HoldsworthDelayEngine& engine, const dsp::HoldsworthDelayConfiguration& configuration,
                             const std::string_view testName)
{
  const auto result = engine.applyConfiguration(configuration);
  if (result.wasApplied())
    return true;

  std::cerr << testName << ": whole configuration was rejected\n";
  return false;
}

bool equalAudioRouting(const AudioRoutingConfiguration& actual, const AudioRoutingConfiguration& expected,
                       const std::string_view testName)
{
  for (std::size_t band = 0; band < actual.inputs.size(); ++band)
  {
    if (actual.inputs[band].has_value() != expected.inputs[band].has_value())
    {
      std::cerr << testName << ": routing presence differs at Band " << (band + 1) << '\n';
      return false;
    }
    if (actual.inputs[band].has_value() && actual.inputs[band]->sourceBand != expected.inputs[band]->sourceBand)
    {
      std::cerr << testName << ": routing source differs at Band " << (band + 1) << '\n';
      return false;
    }
  }
  return true;
}

bool equalSynchronization(const dsp::ModulationSyncConfiguration& actual,
                          const dsp::ModulationSyncConfiguration& expected, const std::string_view testName)
{
  for (std::size_t band = 0; band < actual.relationships.size(); ++band)
  {
    const auto& a = actual.relationships[band];
    const auto& e = expected.relationships[band];
    if (a.has_value() != e.has_value())
    {
      std::cerr << testName << ": SYNC presence differs at Band " << (band + 1) << '\n';
      return false;
    }
    if (a.has_value()
        && (a->masterBand != e->masterBand || !nearlyEqual(a->phaseOffset.value, e->phaseOffset.value, 0.0)))
    {
      std::cerr << testName << ": SYNC relationship differs at Band " << (band + 1) << '\n';
      return false;
    }
  }
  return true;
}

template <typename Cutoff>
bool equalOptionalCutoff(const std::optional<Cutoff>& actual, const std::optional<Cutoff>& expected) noexcept
{
  return actual.has_value() == expected.has_value()
         && (!actual.has_value() || nearlyEqual(actual->value, expected->value, 0.0));
}

bool equalConfiguration(const dsp::HoldsworthDelayConfiguration& actual,
                        const dsp::HoldsworthDelayConfiguration& expected, const std::string_view testName)
{
  if (!nearlyEqual(actual.globalWetOutputLevel, expected.globalWetOutputLevel, 0.0)
      || !equalSynchronization(actual.modulationSync, expected.modulationSync, testName)
      || !equalAudioRouting(actual.audioRouting, expected.audioRouting, testName))
    return false;

  for (std::size_t band = 0; band < actual.bands.size(); ++band)
  {
    const auto& a = actual.bands[band];
    const auto& e = expected.bands[band];
    if (!nearlyEqual(a.delayTimeMs, e.delayTimeMs, 0.0) || !nearlyEqual(a.feedback.value, e.feedback.value, 0.0)
        || !nearlyEqual(a.outputLevel, e.outputLevel, 0.0) || !nearlyEqual(a.pan, e.pan, 0.0) || a.enabled != e.enabled
        || !nearlyEqual(a.modulationRate.value, e.modulationRate.value, 0.0)
        || !nearlyEqual(a.modulationDepth.value, e.modulationDepth.value, 0.0)
        || !nearlyEqual(a.modulationPhase.value, e.modulationPhase.value, 0.0)
        || a.modulationWaveform != e.modulationWaveform
        || !equalOptionalCutoff(a.loopFilter.lowCut, e.loopFilter.lowCut)
        || !equalOptionalCutoff(a.loopFilter.highCut, e.loopFilter.highCut)
        || !nearlyEqual(a.tapFraction.value, e.tapFraction.value, 0.0)
        || a.delaySignalPolarity != e.delaySignalPolarity)
    {
      std::cerr << testName << ": band configuration differs at Band " << (band + 1) << '\n';
      return false;
    }
  }
  return true;
}

template <std::size_t SampleCount>
bool render(dsp::HoldsworthDelayEngine& engine, const std::array<double, SampleCount>& input,
            std::array<double, SampleCount>& left, std::array<double, SampleCount>& right) noexcept
{
  engine.processBlock(input, left, right);
  return true;
}

bool testYamahaConnectMetadataIsStronglySeparated()
{
  using presets::YamahaConnectControlState;
  using presets::YamahaConnectControlValue;
  using presets::YamahaEffectBandNumber;

  const auto input = YamahaConnectControlValue::input();
  const auto band8 = YamahaConnectControlValue::fromEffectBand(YamahaEffectBandNumber::band8);
  return input.state() == YamahaConnectControlState::input && !input.sourceBand().has_value()
         && band8.state() == YamahaConnectControlState::effectBand
         && band8.sourceBand() == YamahaEffectBandNumber::band8;
}

bool testIndependentParallelPathMatchesStandaloneBandsBitExactly()
{
  constexpr std::size_t sampleCount = 12;
  constexpr std::array<double, sampleCount> input{1.0, -0.5, 0.25, 0.0, 0.75, -0.25, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 1.0, 0.25, 0.75, -1.0);
  configureBand(configuration.bands[4], 2.0, 0.0, 0.6, 1.0);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(engine, configuration, "parallel reference")
      || engine.applyAudioRoutingConfiguration(AudioRoutingConfiguration{}) != AudioRoutingApplyResult::applied)
    return false;

  std::array<double, sampleCount> actualLeft{};
  std::array<double, sampleCount> actualRight{};
  render(engine, input, actualLeft, actualRight);

  dsp::DelayBand leftBand(20.0);
  dsp::DelayBand rightBand(20.0);
  leftBand.prepare(1000.0, sampleCount);
  rightBand.prepare(1000.0, sampleCount);
  configureDelayBand(leftBand, configuration.bands[0]);
  configureDelayBand(rightBand, configuration.bands[4]);
  std::array<double, sampleCount> expectedLeft{};
  std::array<double, sampleCount> ignoredLeftRight{};
  std::array<double, sampleCount> ignoredRightLeft{};
  std::array<double, sampleCount> expectedRight{};
  leftBand.processBlock(input, expectedLeft, ignoredLeftRight);
  rightBand.processBlock(input, ignoredRightLeft, expectedRight);

  return expectSamplesBitExact("parallel standalone left", actualLeft, expectedLeft)
         && expectSamplesBitExact("parallel standalone right", actualRight, expectedRight);
}

bool testSerialBandReceivesOriginalPlusUpstreamDelayAndEngineStaysWetOnly()
{
  constexpr std::size_t sampleCount = 8;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedLeft{0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedRight{0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 2.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 3.0, 0.0, 1.0, 1.0);
  connect(configuration.audioRouting, 1, DelayBandId::band1);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(engine, configuration, "simple serial routing"))
    return false;

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);
  return expectSamples("serial original plus delay left", left, expectedLeft, 0.0)
         && expectSamples("serial original plus delay right", right, expectedRight, 0.0) && left[0] == 0.0
         && right[0] == 0.0;
}

bool testFanOutDoesNotDuplicateUpstreamWetOrDry()
{
  constexpr std::size_t sampleCount = 6;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedLeft{0.0, 1.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedRight{0.0, 0.0, 1.0, 2.0, 1.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 1.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 2.0, 0.0, 1.0, 1.0);
  configureBand(configuration.bands[2], 3.0, 0.0, 1.0, 1.0);
  connect(configuration.audioRouting, 1, DelayBandId::band1);
  connect(configuration.audioRouting, 2, DelayBandId::band1);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(engine, configuration, "fan-out routing"))
    return false;

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);
  return expectSamples("fan-out source wet once", left, expectedLeft, 0.0)
         && expectSamples("fan-out destinations", right, expectedRight, 0.0) && left[0] == 0.0 && right[0] == 0.0;
}

bool testHigherToLowerChainAndMixedParallelTopology()
{
  constexpr std::size_t sampleCount = 10;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedLeft{0.0, 1.0, 1.0, 2.0, 1.0, 2.0, 1.0, 1.0, 1.0, 0.0};
  constexpr std::array<double, sampleCount> expectedRight{};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[7], 1.0, 0.0, 1.0, -1.0); // Band 8 root.
  configureBand(configuration.bands[4], 2.0, 0.0, 1.0, -1.0); // 8 -> 5.
  configureBand(configuration.bands[1], 4.0, 0.0, 1.0, -1.0); // 5 -> 2.
  configureBand(configuration.bands[2], 3.0, 0.0, 1.0, -1.0); // Band 3 root.
  configureBand(configuration.bands[5], 5.0, 0.0, 1.0, -1.0); // 3 -> 6.
  connect(configuration.audioRouting, 4, DelayBandId::band8);
  connect(configuration.audioRouting, 1, DelayBandId::band5);
  connect(configuration.audioRouting, 5, DelayBandId::band3);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(engine, configuration, "mixed higher-to-lower routing"))
    return false;

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);
  return expectSamples("mixed chain/parallel left", left, expectedLeft, 0.0)
         && expectSamples("mixed chain/parallel right", right, expectedRight, 0.0);
}

template <std::size_t SampleCount>
bool renderTwoBandSerialReference(const dsp::DelayBandConfiguration& upstreamConfiguration,
                                  const dsp::DelayBandConfiguration& downstreamConfiguration,
                                  const std::array<double, SampleCount>& input,
                                  std::array<double, SampleCount>& expectedLeft,
                                  std::array<double, SampleCount>& expectedRight)
{
  if (upstreamConfiguration.pan != -1.0)
  {
    std::cerr << "serial reference requires an exact hard-left upstream band\n";
    return false;
  }

  dsp::DelayBand upstream(80.0);
  dsp::DelayBand downstream(80.0);
  upstream.prepare(1000.0, SampleCount);
  downstream.prepare(1000.0, SampleCount);
  configureDelayBand(upstream, upstreamConfiguration);
  configureDelayBand(downstream, downstreamConfiguration);

  std::array<double, SampleCount> upstreamLeft{};
  std::array<double, SampleCount> upstreamRight{};
  upstream.processBlock(input, upstreamLeft, upstreamRight);

  std::array<double, SampleCount> routedInput{};
  for (std::size_t sample = 0; sample < SampleCount; ++sample)
    routedInput[sample] = input[sample] + upstreamLeft[sample];

  std::array<double, SampleCount> downstreamLeft{};
  std::array<double, SampleCount> downstreamRight{};
  downstream.processBlock(routedInput, downstreamLeft, downstreamRight);

  for (std::size_t sample = 0; sample < SampleCount; ++sample)
  {
    expectedLeft[sample] = upstreamLeft[sample] + downstreamLeft[sample];
    expectedRight[sample] = upstreamRight[sample] + downstreamRight[sample];
  }
  return true;
}

template <std::size_t SampleCount>
bool renderConnectedAndReference(const dsp::DelayBandConfiguration& upstreamConfiguration,
                                 const dsp::DelayBandConfiguration& downstreamConfiguration,
                                 const std::array<double, SampleCount>& input, const std::string_view testName,
                                 const double tolerance = 0.0)
{
  dsp::HoldsworthDelayConfiguration configuration;
  configuration.bands[0] = upstreamConfiguration;
  configuration.bands[1] = downstreamConfiguration;
  connect(configuration.audioRouting, 1, DelayBandId::band1);

  dsp::HoldsworthDelayEngine engine(80.0);
  engine.prepare(1000.0, SampleCount);
  if (!applyWholeConfiguration(engine, configuration, testName))
    return false;

  std::array<double, SampleCount> actualLeft{};
  std::array<double, SampleCount> actualRight{};
  engine.processBlock(input, actualLeft, actualRight);

  std::array<double, SampleCount> expectedLeft{};
  std::array<double, SampleCount> expectedRight{};
  if (!renderTwoBandSerialReference(upstreamConfiguration, downstreamConfiguration, input, expectedLeft, expectedRight))
    return false;

  if (tolerance == 0.0)
  {
    return expectSamplesBitExact(testName, actualLeft, expectedLeft)
           && expectSamplesBitExact(testName, actualRight, expectedRight);
  }
  return expectSamples(testName, actualLeft, expectedLeft, tolerance)
         && expectSamples(testName, actualRight, expectedRight, tolerance);
}

bool testDisabledIntermediateBandBypassesAndKeepsItsLocalTailSemantics()
{
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 1.0, 0.0, 1.0, -1.0, false);
  configureBand(configuration.bands[1], 2.0, 0.5, 1.0, -1.0);
  configureBand(configuration.bands[2], 1.0, 0.0, 1.0, 1.0);
  connect(configuration.audioRouting, 1, DelayBandId::band1);
  connect(configuration.audioRouting, 2, DelayBandId::band2);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, 4);
  if (!applyWholeConfiguration(engine, configuration, "disabled intermediate"))
    return false;

  const std::array<double, 1> seed{1.0};
  std::array<double, 1> seedLeft{};
  std::array<double, 1> seedRight{};
  engine.processBlock(seed, seedLeft, seedRight);

  auto disabledBand2 = configuration.bands[1];
  disabledBand2.enabled = false;
  engine.setBandConfiguration(1, disabledBand2);
  const std::array<double, 2> duringDisabled{0.7, -0.3};
  std::array<double, 2> disabledLeft{};
  std::array<double, 2> disabledRight{};
  engine.processBlock(duringDisabled, disabledLeft, disabledRight);

  const std::array<double, 2> expectedMutedWet{0.0, 0.0};
  const std::array<double, 2> expectedDownstream{1.0, 0.7};
  if (!expectSamples("disabled intermediate contributes no wet", disabledLeft, expectedMutedWet, 0.0)
      || !expectSamples("disabled intermediate passes source downstream", disabledRight, expectedDownstream, 0.0))
    return false;

  engine.setBandConfiguration(1, configuration.bands[1]);
  const std::array<double, 2> silence{};
  std::array<double, 2> resumedLeft{};
  std::array<double, 2> resumedRight{};
  engine.processBlock(silence, resumedLeft, resumedRight);
  const std::array<double, 2> expectedAdvancedTail{0.0, 0.5};
  return expectSamples("disabled recurrence rejects input and advances tail", resumedLeft, expectedAdvancedTail, 0.0);
}

bool testZeroLevelPassesDirectInputButRemovesRoutedDelayContribution()
{
  constexpr std::size_t sampleCount = 6;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedLeft{};
  constexpr std::array<double, sampleCount> expectedRight{0.0, 0.0, 1.0, 0.0, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 1.0, 0.0, 0.0, -1.0);
  configureBand(configuration.bands[1], 2.0, 0.0, 1.0, 1.0);
  connect(configuration.audioRouting, 1, DelayBandId::band1);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(engine, configuration, "zero-level routing"))
    return false;

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  engine.processBlock(input, left, right);
  return expectSamples("zero-level source wet", left, expectedLeft, 0.0)
         && expectSamples("zero-level source direct route", right, expectedRight, 0.0);
}

bool testFeedbackRemainsLocalToEachBand()
{
  constexpr std::size_t sampleCount = 32;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  dsp::DelayBandConfiguration upstream;
  dsp::DelayBandConfiguration downstream;
  configureBand(upstream, 3.0, 0.5, 0.8, -1.0);
  configureBand(downstream, 5.0, 0.25, 0.7, 1.0);
  return renderConnectedAndReference(upstream, downstream, input, "local feedback serial reference");
}

bool testTapBelowFullLoopFeedsTheDownstreamBand()
{
  constexpr std::size_t sampleCount = 9;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedLeft{0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedRight{0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 4.0, 0.0, 1.0, -1.0);
  configuration.bands[0].tapFraction = dsp::TapFraction{0.5};
  configureBand(configuration.bands[1], 3.0, 0.0, 1.0, 1.0);
  connect(configuration.audioRouting, 1, DelayBandId::band1);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(engine, configuration, "TAP routing"))
    return false;

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  engine.processBlock(input, left, right);
  return expectSamples("TAP upstream audible position", left, expectedLeft, 0.0)
         && expectSamples("TAP enters downstream recurrence", right, expectedRight, 0.0);
}

bool testFilteredTapContributionIsNotFilteredTwiceBeforeRouting()
{
  constexpr std::size_t sampleCount = 48;
  std::array<double, sampleCount> input{};
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
    input[sample] = deterministicInput(sample);

  dsp::DelayBandConfiguration upstream;
  dsp::DelayBandConfiguration downstream;
  configureBand(upstream, 8.0, 0.35, 0.65, -1.0);
  upstream.tapFraction = dsp::TapFraction{0.375};
  upstream.loopFilter.lowCut = dsp::LowCutFrequencyHz{35.0};
  upstream.loopFilter.highCut = dsp::HighCutFrequencyHz{180.0};
  configureBand(downstream, 5.0, 0.2, 0.8, 1.0);
  return renderConnectedAndReference(upstream, downstream, input, "filtered TAP single routing pass", 1.0e-14);
}

bool testNormalAndReversePolarityFeedTheExpectedSignedAudioDownstream()
{
  constexpr std::size_t sampleCount = 6;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedNormalLeft{0.0, 0.5, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedReverseLeft{0.0, -0.5, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedNormalRight{0.0, 0.0, 1.0, 0.5, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedReverseRight{0.0, 0.0, 1.0, -0.5, 0.0, 0.0};

  auto renderPolarity = [&input](const dsp::DelaySignalPolarity polarity, auto& left, auto& right) {
    dsp::HoldsworthDelayConfiguration configuration;
    configureBand(configuration.bands[0], 1.0, 0.0, 0.5, -1.0);
    configuration.bands[0].delaySignalPolarity = polarity;
    configureBand(configuration.bands[1], 2.0, 0.0, 1.0, 1.0);
    connect(configuration.audioRouting, 1, DelayBandId::band1);

    dsp::HoldsworthDelayEngine engine(20.0);
    engine.prepare(1000.0, input.size());
    if (!applyWholeConfiguration(engine, configuration, "polarity routing"))
      return false;
    engine.processBlock(input, left, right);
    return true;
  };

  std::array<double, sampleCount> normalLeft{};
  std::array<double, sampleCount> normalRight{};
  std::array<double, sampleCount> reverseLeft{};
  std::array<double, sampleCount> reverseRight{};
  return renderPolarity(dsp::DelaySignalPolarity::normal, normalLeft, normalRight)
         && renderPolarity(dsp::DelaySignalPolarity::reverse, reverseLeft, reverseRight)
         && expectSamples("NOR upstream", normalLeft, expectedNormalLeft, 0.0)
         && expectSamples("REV upstream", reverseLeft, expectedReverseLeft, 0.0)
         && expectSamples("NOR downstream", normalRight, expectedNormalRight, 0.0)
         && expectSamples("REV downstream", reverseRight, expectedReverseRight, 0.0);
}

bool testPanIsExcludedFromTheMonoRoutingSignal()
{
  constexpr std::size_t sampleCount = 6;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  auto destinationOutputForPan = [&input](const double pan, auto& left, auto& right) {
    dsp::HoldsworthDelayConfiguration configuration;
    configureBand(configuration.bands[0], 1.0, 0.0, 1.0, pan);
    configureBand(configuration.bands[1], 2.0, 0.0, 1.0, 1.0);
    connect(configuration.audioRouting, 1, DelayBandId::band1);
    dsp::HoldsworthDelayEngine engine(20.0);
    engine.prepare(1000.0, input.size());
    if (!applyWholeConfiguration(engine, configuration, "pan exclusion"))
      return false;
    engine.processBlock(input, left, right);
    return true;
  };

  std::array<double, sampleCount> hardLeftOutputLeft{};
  std::array<double, sampleCount> hardLeftOutputRight{};
  std::array<double, sampleCount> hardRightOutputLeft{};
  std::array<double, sampleCount> hardRightOutputRight{};
  return destinationOutputForPan(-1.0, hardLeftOutputLeft, hardLeftOutputRight)
         && destinationOutputForPan(1.0, hardRightOutputLeft, hardRightOutputRight) && hardLeftOutputRight[2] == 1.0
         && hardRightOutputRight[2] == 1.0 && hardLeftOutputRight[3] == 1.0 && hardRightOutputRight[3] == 1.0
         && hardLeftOutputLeft[1] == 1.0 && hardRightOutputRight[1] == 1.0;
}

bool testGlobalWetLevelIsPostSumAndExcludedFromRouting()
{
  constexpr std::size_t sampleCount = 6;
  constexpr std::array<double, sampleCount> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedLeft{0.0, 0.25, 0.0, 0.0, 0.0, 0.0};
  constexpr std::array<double, sampleCount> expectedRight{0.0, 0.0, 0.25, 0.25, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 1.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 2.0, 0.0, 1.0, 1.0);
  configuration.globalWetOutputLevel = 0.25;
  connect(configuration.audioRouting, 1, DelayBandId::band1);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(engine, configuration, "global wet routing"))
    return false;

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  engine.processBlock(input, left, right);
  return expectSamples("global wet post-sum left", left, expectedLeft, 0.0)
         && expectSamples("global wet post-sum right", right, expectedRight, 0.0);
}

bool testSyncPrepassIsIndependentOfReverseAudioProcessingOrder()
{
  constexpr std::size_t sampleCount = 1024;
  dsp::HoldsworthDelayConfiguration independentAudio;
  configureBand(independentAudio.bands[0], 12.0, 0.35, 0.8, -0.25);
  independentAudio.bands[0].modulationRate = dsp::ModulationRateHz{2.75};
  independentAudio.bands[0].modulationDepth = dsp::ModulationDepthMs{2.0};
  independentAudio.bands[0].modulationPhase = dsp::ModulationPhaseCycles{0.13};
  configureBand(independentAudio.bands[1], 9.0, 0.2, 0.0, 0.7);
  independentAudio.bands[1].modulationRate = dsp::ModulationRateHz{7.0};
  independentAudio.bands[1].modulationDepth = dsp::ModulationDepthMs{1.5};
  independentAudio.bands[1].modulationPhase = dsp::ModulationPhaseCycles{0.6};
  synchronize(independentAudio.modulationSync, 1, DelayBandId::band1, 0.25);

  auto reverseAudioOrder = independentAudio;
  connect(reverseAudioOrder.audioRouting, 0, DelayBandId::band2);

  dsp::HoldsworthDelayEngine legacyOrder(40.0);
  dsp::HoldsworthDelayEngine connectedOrder(40.0);
  legacyOrder.prepare(1000.0, sampleCount);
  connectedOrder.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(legacyOrder, independentAudio, "SYNC independent audio order")
      || !applyWholeConfiguration(connectedOrder, reverseAudioOrder, "SYNC reverse audio order"))
    return false;

  std::array<double, sampleCount> input{};
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
    input[sample] = deterministicInput(sample);
  std::array<double, sampleCount> legacyLeft{};
  std::array<double, sampleCount> legacyRight{};
  std::array<double, sampleCount> connectedLeft{};
  std::array<double, sampleCount> connectedRight{};
  legacyOrder.processBlock(input, legacyLeft, legacyRight);
  connectedOrder.processBlock(input, connectedLeft, connectedRight);
  return expectSamplesBitExact("SYNC/CONNECT left", connectedLeft, legacyLeft)
         && expectSamplesBitExact("SYNC/CONNECT right", connectedRight, legacyRight);
}

void makeDeterministicComplexConfiguration(dsp::HoldsworthDelayConfiguration& configuration) noexcept
{
  configureBand(configuration.bands[3], 11.0, 0.3, 0.75, -0.6);
  configuration.bands[3].modulationRate = dsp::ModulationRateHz{1.25};
  configuration.bands[3].modulationDepth = dsp::ModulationDepthMs{1.5};
  configuration.bands[3].modulationPhase = dsp::ModulationPhaseCycles{0.17};
  configuration.bands[3].loopFilter.highCut = dsp::HighCutFrequencyHz{190.0};

  configureBand(configuration.bands[0], 7.0, 0.2, 0.6, 0.25);
  configuration.bands[0].tapFraction = dsp::TapFraction{0.6};
  configuration.bands[0].modulationRate = dsp::ModulationRateHz{3.0};
  configuration.bands[0].modulationDepth = dsp::ModulationDepthMs{0.75};
  configuration.bands[0].modulationPhase = dsp::ModulationPhaseCycles{0.41};

  configureBand(configuration.bands[2], 5.0, 0.15, 0.55, 1.0);
  configuration.bands[2].delaySignalPolarity = dsp::DelaySignalPolarity::reverse;
  configureBand(configuration.bands[5], 13.0, 0.1, 0.45, -1.0);

  connect(configuration.audioRouting, 0, DelayBandId::band4);
  connect(configuration.audioRouting, 2, DelayBandId::band1);
  connect(configuration.audioRouting, 5, DelayBandId::band4);
  synchronize(configuration.modulationSync, 0, DelayBandId::band4, 0.5);
  configuration.globalWetOutputLevel = 0.83;
}

bool renderPartitioned(dsp::HoldsworthDelayEngine& engine, const std::span<const double> input,
                       const std::span<double> left, const std::span<double> right,
                       const std::size_t blockSize) noexcept
{
  std::size_t offset = 0;
  while (offset < input.size())
  {
    const std::size_t frames = std::min(blockSize, input.size() - offset);
    engine.processBlock(input.subspan(offset, frames), left.subspan(offset, frames), right.subspan(offset, frames));
    offset += frames;
  }
  return true;
}

bool testConnectedProcessingIsPartitionInvariantAndResetDeterministic()
{
  constexpr std::size_t sampleCount = 513;
  constexpr std::size_t partitionSize = 37;
  dsp::HoldsworthDelayConfiguration configuration;
  makeDeterministicComplexConfiguration(configuration);

  dsp::HoldsworthDelayEngine whole(40.0);
  dsp::HoldsworthDelayEngine partitioned(40.0);
  whole.prepare(1000.0, sampleCount);
  partitioned.prepare(1000.0, partitionSize);
  if (!applyWholeConfiguration(whole, configuration, "whole CONNECT render")
      || !applyWholeConfiguration(partitioned, configuration, "partitioned CONNECT render"))
    return false;

  std::vector<double> input(sampleCount);
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
    input[sample] = deterministicInput(sample);
  std::vector<double> wholeLeft(sampleCount);
  std::vector<double> wholeRight(sampleCount);
  std::vector<double> partitionedLeft(sampleCount);
  std::vector<double> partitionedRight(sampleCount);
  whole.processBlock(input, wholeLeft, wholeRight);
  renderPartitioned(partitioned, input, partitionedLeft, partitionedRight, partitionSize);
  if (!expectSamplesBitExact("CONNECT partition left", partitionedLeft, wholeLeft)
      || !expectSamplesBitExact("CONNECT partition right", partitionedRight, wholeRight))
    return false;

  partitioned.reset();
  std::fill(partitionedLeft.begin(), partitionedLeft.end(), 0.0);
  std::fill(partitionedRight.begin(), partitionedRight.end(), 0.0);
  renderPartitioned(partitioned, input, partitionedLeft, partitionedRight, partitionSize);
  return expectSamplesBitExact("CONNECT reset left", partitionedLeft, wholeLeft)
         && expectSamplesBitExact("CONNECT reset right", partitionedRight, wholeRight);
}

bool testGraphValidationRejectsInvalidSelfAndCyclesWithoutChangingRouting()
{
  dsp::HoldsworthDelayEngine engine(20.0);
  AudioRoutingConfiguration accepted;
  connect(accepted, 1, DelayBandId::band1);
  if (engine.applyAudioRoutingConfiguration(accepted) != AudioRoutingApplyResult::applied)
    return false;

  AudioRoutingConfiguration invalidReference = accepted;
  connect(invalidReference, 4, static_cast<DelayBandId>(9));
  AudioRoutingConfiguration selfReference = accepted;
  connect(selfReference, 0, DelayBandId::band1);
  AudioRoutingConfiguration cycle;
  connect(cycle, 0, DelayBandId::band2);
  connect(cycle, 1, DelayBandId::band1);

  return engine.applyAudioRoutingConfiguration(invalidReference) == AudioRoutingApplyResult::invalidSourceReference
         && engine.applyAudioRoutingConfiguration(selfReference) == AudioRoutingApplyResult::selfReference
         && engine.applyAudioRoutingConfiguration(cycle) == AudioRoutingApplyResult::cycleDetected
         && equalAudioRouting(engine.configuration().audioRouting, accepted, "routing validation rollback");
}

bool testRoutingOnlyRejectionPreservesHistoryAndContinuation()
{
  constexpr std::size_t blockSize = 24;
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 3.0, 0.45, 0.8, -1.0);
  configureBand(configuration.bands[1], 5.0, 0.25, 0.7, 1.0);
  connect(configuration.audioRouting, 1, DelayBandId::band1);

  dsp::HoldsworthDelayEngine actual(40.0);
  dsp::HoldsworthDelayEngine reference(40.0);
  actual.prepare(1000.0, blockSize);
  reference.prepare(1000.0, blockSize);
  if (!applyWholeConfiguration(actual, configuration, "routing rollback actual")
      || !applyWholeConfiguration(reference, configuration, "routing rollback reference"))
    return false;

  std::array<double, blockSize> prefix{};
  std::array<double, blockSize> suffix{};
  for (std::size_t sample = 0; sample < blockSize; ++sample)
  {
    prefix[sample] = deterministicInput(sample);
    suffix[sample] = deterministicInput(sample + blockSize);
  }
  std::array<double, blockSize> throwawayLeft{};
  std::array<double, blockSize> throwawayRight{};
  actual.processBlock(prefix, throwawayLeft, throwawayRight);
  reference.processBlock(prefix, throwawayLeft, throwawayRight);

  AudioRoutingConfiguration rejected;
  connect(rejected, 0, DelayBandId::band2);
  connect(rejected, 1, DelayBandId::band1);
  if (actual.applyAudioRoutingConfiguration(rejected) != AudioRoutingApplyResult::cycleDetected
      || !equalAudioRouting(
        actual.configuration().audioRouting, configuration.audioRouting, "routing-only continuation rollback"))
    return false;

  std::array<double, blockSize> actualLeft{};
  std::array<double, blockSize> actualRight{};
  std::array<double, blockSize> referenceLeft{};
  std::array<double, blockSize> referenceRight{};
  actual.processBlock(suffix, actualLeft, actualRight);
  reference.processBlock(suffix, referenceLeft, referenceRight);
  return expectSamplesBitExact("routing rollback continuation left", actualLeft, referenceLeft)
         && expectSamplesBitExact("routing rollback continuation right", actualRight, referenceRight);
}

bool testWholeConfigurationCrossDomainRejectionIsFullyTransactional()
{
  constexpr std::size_t blockSize = 32;
  dsp::HoldsworthDelayConfiguration accepted;
  configureBand(accepted.bands[0], 7.0, 0.3, 0.8, -0.5);
  accepted.bands[0].modulationRate = dsp::ModulationRateHz{2.0};
  accepted.bands[0].modulationDepth = dsp::ModulationDepthMs{1.0};
  configureBand(accepted.bands[1], 5.0, 0.2, 0.6, 0.75);
  accepted.bands[1].modulationDepth = dsp::ModulationDepthMs{0.5};
  connect(accepted.audioRouting, 1, DelayBandId::band1);
  synchronize(accepted.modulationSync, 1, DelayBandId::band1, 0.25);
  accepted.globalWetOutputLevel = 0.9;

  dsp::HoldsworthDelayEngine actual(40.0);
  dsp::HoldsworthDelayEngine reference(40.0);
  actual.prepare(1000.0, blockSize);
  reference.prepare(1000.0, blockSize);
  if (!applyWholeConfiguration(actual, accepted, "full rollback actual")
      || !applyWholeConfiguration(reference, accepted, "full rollback reference"))
    return false;

  std::array<double, blockSize> prefix{};
  std::array<double, blockSize> suffix{};
  for (std::size_t sample = 0; sample < blockSize; ++sample)
  {
    prefix[sample] = deterministicInput(sample);
    suffix[sample] = deterministicInput(sample + blockSize);
  }
  std::array<double, blockSize> throwawayLeft{};
  std::array<double, blockSize> throwawayRight{};
  actual.processBlock(prefix, throwawayLeft, throwawayRight);
  reference.processBlock(prefix, throwawayLeft, throwawayRight);

  auto rejected = accepted;
  rejected.bands[0].delayTimeMs = 19.0;
  rejected.bands[1].outputLevel = 0.1;
  rejected.globalWetOutputLevel = 0.2;
  rejected.modulationSync.relationships[1]->phaseOffset.value = std::numeric_limits<double>::quiet_NaN();
  connect(rejected.audioRouting, 0, DelayBandId::band2);
  const auto result = actual.applyConfiguration(rejected);
  if (result.modulationSync != ModulationSyncApplyResult::invalidPhaseOffset
      || result.audioRouting != AudioRoutingApplyResult::cycleDetected
      || !equalConfiguration(actual.configuration(), reference.configuration(), "whole cross-domain rollback"))
    return false;

  std::array<double, blockSize> actualLeft{};
  std::array<double, blockSize> actualRight{};
  std::array<double, blockSize> referenceLeft{};
  std::array<double, blockSize> referenceRight{};
  actual.processBlock(suffix, actualLeft, actualRight);
  reference.processBlock(suffix, referenceLeft, referenceRight);
  return expectSamplesBitExact("whole rollback continuation left", actualLeft, referenceLeft)
         && expectSamplesBitExact("whole rollback continuation right", actualRight, referenceRight);
}

bool testConnectedProcessingSupportsExactInputOutputAliasing()
{
  constexpr std::size_t sampleCount = 64;
  dsp::HoldsworthDelayConfiguration configuration;
  makeDeterministicComplexConfiguration(configuration);

  dsp::HoldsworthDelayEngine reference(40.0);
  dsp::HoldsworthDelayEngine leftAlias(40.0);
  dsp::HoldsworthDelayEngine rightAlias(40.0);
  reference.prepare(1000.0, sampleCount);
  leftAlias.prepare(1000.0, sampleCount);
  rightAlias.prepare(1000.0, sampleCount);
  if (!applyWholeConfiguration(reference, configuration, "alias reference")
      || !applyWholeConfiguration(leftAlias, configuration, "left alias")
      || !applyWholeConfiguration(rightAlias, configuration, "right alias"))
    return false;

  std::array<double, sampleCount> input{};
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
    input[sample] = deterministicInput(sample);
  std::array<double, sampleCount> expectedLeft{};
  std::array<double, sampleCount> expectedRight{};
  reference.processBlock(input, expectedLeft, expectedRight);

  auto aliasedLeft = input;
  std::array<double, sampleCount> leftAliasRight{};
  leftAlias.processBlock(aliasedLeft, aliasedLeft, leftAliasRight);
  auto aliasedRight = input;
  std::array<double, sampleCount> rightAliasLeft{};
  rightAlias.processBlock(aliasedRight, rightAliasLeft, aliasedRight);
  return expectSamplesBitExact("CONNECT left alias left", aliasedLeft, expectedLeft)
         && expectSamplesBitExact("CONNECT left alias right", leftAliasRight, expectedRight)
         && expectSamplesBitExact("CONNECT right alias left", rightAliasLeft, expectedLeft)
         && expectSamplesBitExact("CONNECT right alias right", aliasedRight, expectedRight);
}

bool testRoutingConfigurationAndProcessingDoNotAllocate()
{
  constexpr std::size_t blockSize = 128;
  dsp::HoldsworthDelayConfiguration configuration;
  makeDeterministicComplexConfiguration(configuration);
  dsp::HoldsworthDelayEngine engine(40.0);
  engine.prepare(1000.0, blockSize);

  AudioRoutingConfiguration alternateRouting;
  connect(alternateRouting, 0, DelayBandId::band4);
  connect(alternateRouting, 2, DelayBandId::band1);
  connect(alternateRouting, 6, DelayBandId::band4);
  AudioRoutingConfiguration rejectedRouting;
  connect(rejectedRouting, 0, DelayBandId::band2);
  connect(rejectedRouting, 1, DelayBandId::band1);
  std::array<double, blockSize> input{};
  std::array<double, blockSize> left{};
  std::array<double, blockSize> right{};
  for (std::size_t sample = 0; sample < blockSize; ++sample)
    input[sample] = deterministicInput(sample);

  beginAllocationTracking();
  const auto fullResult = engine.applyConfiguration(configuration);
  const auto routingResult = engine.applyAudioRoutingConfiguration(alternateRouting);
  const auto rejectionResult = engine.applyAudioRoutingConfiguration(rejectedRouting);
  engine.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
    std::cerr << "CONNECT configuration/processing made " << allocations << " allocation(s)\n";
  return fullResult.wasApplied() && routingResult == AudioRoutingApplyResult::applied
         && rejectionResult == AudioRoutingApplyResult::cycleDetected && allocations == 0;
}

constexpr std::array kTests{
  TestCase{"Yamaha CONNECT: source metadata remains strongly separated", testYamahaConnectMetadataIsStronglySeparated},
  TestCase{"CONNECT: independent path matches standalone bands bit-exactly",
           testIndependentParallelPathMatchesStandaloneBandsBitExactly},
  TestCase{"CONNECT: serial input is original plus upstream delay and output stays wet-only",
           testSerialBandReceivesOriginalPlusUpstreamDelayAndEngineStaysWetOnly},
  TestCase{
    "CONNECT: fan-out does not duplicate source wet or zero-latency dry", testFanOutDoesNotDuplicateUpstreamWetOrDry},
  TestCase{"CONNECT: higher-to-lower chains and mixed parallel roots are deterministic",
           testHigherToLowerChainAndMixedParallelTopology},
  TestCase{"CONNECT: a disabled intermediate band bypasses while its local tail advances",
           testDisabledIntermediateBandBypassesAndKeepsItsLocalTailSemantics},
  TestCase{"CONNECT: LEVEL zero removes routed delay but preserves direct input",
           testZeroLevelPassesDirectInputButRemovesRoutedDelayContribution},
  TestCase{"CONNECT: each band's feedback recurrence remains local", testFeedbackRemainsLocalToEachBand},
  TestCase{"CONNECT: an early TAP feeds the downstream band", testTapBelowFullLoopFeedsTheDownstreamBand},
  TestCase{
    "CONNECT: filtered TAP audio is not filtered twice", testFilteredTapContributionIsNotFilteredTwiceBeforeRouting},
  TestCase{"CONNECT: NOR and REV polarity feed signed audio downstream",
           testNormalAndReversePolarityFeedTheExpectedSignedAudioDownstream},
  TestCase{"CONNECT: PAN is excluded from mono routing", testPanIsExcludedFromTheMonoRoutingSignal},
  TestCase{
    "CONNECT: global wet remains post-sum and outside routing", testGlobalWetLevelIsPostSumAndExcludedFromRouting},
  TestCase{"CONNECT: SYNC prepass is independent of audio topological order",
           testSyncPrepassIsIndependentOfReverseAudioProcessingOrder},
  TestCase{"CONNECT: processing is partition invariant and reset deterministic",
           testConnectedProcessingIsPartitionInvariantAndResetDeterministic},
  TestCase{"CONNECT: graph validation rejects invalid, self, and cyclic inputs transactionally",
           testGraphValidationRejectsInvalidSelfAndCyclesWithoutChangingRouting},
  TestCase{"CONNECT: routing-only rejection preserves history and continuation",
           testRoutingOnlyRejectionPreservesHistoryAndContinuation},
  TestCase{"CONNECT: whole cross-domain rejection is fully transactional",
           testWholeConfigurationCrossDomainRejectionIsFullyTransactional},
  TestCase{
    "CONNECT: exact input/output aliasing is supported", testConnectedProcessingSupportsExactInputOutputAliasing},
  TestCase{
    "CONNECT: configuration and processing perform no allocations", testRoutingConfigurationAndProcessingDoNotAllocate},
};

} // namespace

TestSuite audioRoutingTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
