#include "../dsp/HoldsworthDelayEngine.h"
#include "../presets/YamahaBandStructureSourceValues.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

static_assert(!std::is_convertible_v<holdsworth::presets::YamahaGroupControlValue, holdsworth::dsp::GroupedDelayRange>);
static_assert(!std::is_convertible_v<holdsworth::dsp::GroupedDelayRange, holdsworth::presets::YamahaGroupControlValue>);
static_assert(!std::is_convertible_v<holdsworth::dsp::GroupedDelayPhysicalCapacityMs,
                                     holdsworth::dsp::DocumentedYamahaGroupMaximumDelayMs>);
static_assert(!std::is_convertible_v<holdsworth::dsp::DocumentedYamahaGroupMaximumDelayMs,
                                     holdsworth::dsp::GroupedDelayPhysicalCapacityMs>);

namespace holdsworth::test
{
namespace
{

using dsp::AudioRoutingApplyResult;
using dsp::ConnectedBandAudioInput;
using dsp::DelayBandId;
using dsp::DelayGroupingApplyResult;
using dsp::GroupedDelayPhysicalCapacityMs;
using dsp::GroupedDelayRange;

constexpr std::array<DelayBandId, dsp::kHoldsworthDelayBandCount> kBandIds{
  DelayBandId::band1, DelayBandId::band2, DelayBandId::band3, DelayBandId::band4,
  DelayBandId::band5, DelayBandId::band6, DelayBandId::band7, DelayBandId::band8};

void configureBand(dsp::DelayBandConfiguration& band, const double delayTimeMs, const double feedback,
                   const double outputLevel, const double pan, const bool enabled = true) noexcept
{
  band.delayTimeMs = delayTimeMs;
  band.feedback = dsp::NormalizedFeedbackCoefficient{feedback};
  band.outputLevel = outputLevel;
  band.pan = pan;
  band.enabled = enabled;
}

void group(dsp::DelayGroupingConfiguration& grouping, const std::size_t headIndex, const std::size_t endIndex) noexcept
{
  grouping.groupsByHead[headIndex] = GroupedDelayRange{kBandIds[endIndex]};
}

void connect(dsp::AudioRoutingConfiguration& routing, const std::size_t destinationIndex,
             const std::size_t sourceIndex) noexcept
{
  routing.inputs[destinationIndex] = ConnectedBandAudioInput{kBandIds[sourceIndex]};
}

[[nodiscard]] bool applied(dsp::HoldsworthDelayEngine& engine, const dsp::HoldsworthDelayConfiguration& configuration,
                           const std::string_view name)
{
  const auto result = engine.applyConfiguration(configuration);
  if (result.wasApplied())
    return true;
  std::cerr << name << ": GROUP configuration was rejected\n";
  return false;
}

template <std::size_t SampleCount>
void render(dsp::HoldsworthDelayEngine& engine, const std::array<double, SampleCount>& input,
            std::array<double, SampleCount>& left, std::array<double, SampleCount>& right) noexcept
{
  engine.processBlock(input, left, right);
}

[[nodiscard]] bool equalGrouping(const dsp::DelayGroupingConfiguration& actual,
                                 const dsp::DelayGroupingConfiguration& expected) noexcept
{
  for (std::size_t index = 0; index < actual.groupsByHead.size(); ++index)
  {
    if (actual.groupsByHead[index].has_value() != expected.groupsByHead[index].has_value())
      return false;
    if (actual.groupsByHead[index].has_value()
        && actual.groupsByHead[index]->endBand != expected.groupsByHead[index]->endBand)
      return false;
  }
  return true;
}

bool testSourceAndDspTypesAndDocumentedCapacityFactsStaySeparate()
{
  const auto one = dsp::documentedYamahaGroupMaximumDelayMs(1);
  const auto two = dsp::documentedYamahaGroupMaximumDelayMs(2);
  const auto three = dsp::documentedYamahaGroupMaximumDelayMs(3);
  const auto seven = dsp::documentedYamahaGroupMaximumDelayMs(7);
  const auto eight = dsp::documentedYamahaGroupMaximumDelayMs(8);

  return one.has_value() && one->value == 696.0 && two.has_value() && two->value == 1430.0 && !three.has_value()
         && !seven.has_value() && eight.has_value() && eight->value == 5890.0
         && dsp::groupedDelayMaximumPermittedTimeMs(3, GroupedDelayPhysicalCapacityMs{3210.0}) == 3210.0;
}

bool testValidSizesHeadsAndMultipleDisjointGroupsRoundTrip()
{
  for (std::size_t memberCount = 2; memberCount <= 8; ++memberCount)
  {
    dsp::HoldsworthDelayConfiguration configuration;
    configureBand(configuration.bands[0], 20.0, 0.0, 1.0, -1.0);
    group(configuration.delayGrouping, 0, memberCount - 1);

    dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{100.0});
    engine.prepare(1000.0, 1);
    if (!applied(engine, configuration, "valid GROUP size"))
      return false;
  }

  dsp::HoldsworthDelayConfiguration multiple;
  configureBand(multiple.bands[1], 12.0, 0.0, 1.0, -1.0);
  configureBand(multiple.bands[5], 14.0, 0.0, 1.0, 1.0);
  group(multiple.delayGrouping, 1, 3);
  group(multiple.delayGrouping, 5, 7);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{100.0});
  engine.prepare(1000.0, 1);
  return applied(engine, multiple, "multiple GROUPs")
         && equalGrouping(engine.configuration().delayGrouping, multiple.delayGrouping)
         && engine.configuration().bands[1].delayTimeMs == 12.0 && engine.configuration().bands[5].delayTimeMs == 14.0;
}

bool testInvalidRangesAndMembershipAreRejected()
{
  dsp::HoldsworthDelayEngine engine(20.0, GroupedDelayPhysicalCapacityMs{100.0});
  engine.prepare(1000.0, 1);

  dsp::DelayGroupingConfiguration singleton;
  group(singleton, 2, 2);
  dsp::DelayGroupingConfiguration descending;
  descending.groupsByHead[4] = GroupedDelayRange{DelayBandId::band2};
  dsp::DelayGroupingConfiguration invalid;
  invalid.groupsByHead[0] = GroupedDelayRange{static_cast<DelayBandId>(0)};
  dsp::DelayGroupingConfiguration overlap;
  group(overlap, 0, 3);
  group(overlap, 2, 5);
  dsp::DelayGroupingConfiguration nested;
  group(nested, 0, 5);
  group(nested, 1, 2);
  dsp::DelayGroupingConfiguration duplicateMember;
  group(duplicateMember, 0, 1);
  group(duplicateMember, 1, 2);

  return engine.applyDelayGroupingConfiguration(singleton) == DelayGroupingApplyResult::singletonRange
         && engine.applyDelayGroupingConfiguration(descending) == DelayGroupingApplyResult::descendingRange
         && engine.applyDelayGroupingConfiguration(invalid) == DelayGroupingApplyResult::invalidBandReference
         && engine.applyDelayGroupingConfiguration(overlap) == DelayGroupingApplyResult::overlappingMembership
         && engine.applyDelayGroupingConfiguration(nested) == DelayGroupingApplyResult::overlappingMembership
         && engine.applyDelayGroupingConfiguration(duplicateMember) == DelayGroupingApplyResult::overlappingMembership;
}

bool testDocumentedAndPhysicalCapacityPoliciesAreValidated()
{
  dsp::HoldsworthDelayEngine engine(20.0, GroupedDelayPhysicalCapacityMs{2000.0});
  engine.prepare(1000.0, 1);

  dsp::HoldsworthDelayConfiguration documentedTwo;
  configureBand(documentedTwo.bands[0], 1430.0, 0.0, 1.0, -1.0);
  group(documentedTwo.delayGrouping, 0, 1);
  if (!engine.applyConfiguration(documentedTwo).wasApplied())
    return false;
  documentedTwo.bands[0].delayTimeMs = 1430.001;
  if (engine.applyConfiguration(documentedTwo).delayGrouping != DelayGroupingApplyResult::delayTimeExceedsCapacity)
    return false;

  dsp::HoldsworthDelayConfiguration undocumentedThree;
  configureBand(undocumentedThree.bands[2], 1999.0, 0.0, 1.0, -1.0);
  group(undocumentedThree.delayGrouping, 2, 4);
  if (!engine.applyConfiguration(undocumentedThree).wasApplied())
    return false;
  undocumentedThree.bands[2].delayTimeMs = 2000.001;
  if (engine.applyConfiguration(undocumentedThree).delayGrouping != DelayGroupingApplyResult::delayTimeExceedsCapacity)
    return false;

  dsp::HoldsworthDelayEngine eightEngine(20.0, GroupedDelayPhysicalCapacityMs{6000.0});
  eightEngine.prepare(1000.0, 1);
  dsp::HoldsworthDelayConfiguration documentedEight;
  configureBand(documentedEight.bands[0], 5890.001, 0.0, 1.0, -1.0);
  group(documentedEight.delayGrouping, 0, 7);
  return eightEngine.applyConfiguration(documentedEight).delayGrouping
         == DelayGroupingApplyResult::delayTimeExceedsCapacity;
}

bool testSharedHistoryProducesIndependentTapOutputsAboveBandCapacity()
{
  constexpr std::size_t sampleCount = 31;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> expectedLeft{};
  std::array<double, sampleCount> expectedRight{};
  expectedLeft[12] = 1.0;
  expectedRight[24] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 24.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, 1.0);
  configuration.bands[0].tapFraction = dsp::TapFraction{0.5};
  configuration.bands[1].tapFraction = dsp::TapFraction{1.0};
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{40.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "shared GROUP taps"))
    return false;

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);
  return expectSamples("GROUP early TAP", left, expectedLeft, 0.0)
         && expectSamples("GROUP full TAP", right, expectedRight, 0.0);
}

bool testChangingTotalDelayPreservesProportionalTapTiming()
{
  constexpr std::size_t sampleCount = 12;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 4.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 0.0, 1.0);
  configuration.bands[0].tapFraction = dsp::TapFraction{0.5};
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "proportional GROUP TAP"))
    return false;
  std::array<double, sampleCount> firstLeft{};
  std::array<double, sampleCount> firstRight{};
  render(engine, input, firstLeft, firstRight);

  engine.reset();
  configuration.bands[0].delayTimeMs = 8.0;
  if (!applied(engine, configuration, "changed proportional GROUP TAP"))
    return false;
  std::array<double, sampleCount> secondLeft{};
  std::array<double, sampleCount> secondRight{};
  render(engine, input, secondLeft, secondRight);

  return firstLeft[2] == 1.0 && secondLeft[4] == 1.0 && firstLeft[4] == 0.0 && secondLeft[2] == 0.0;
}

bool testOneFeedbackRecurrenceIsIndependentOfMemberOutputControls()
{
  constexpr std::size_t sampleCount = 24;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  auto makeConfiguration = [] {
    dsp::HoldsworthDelayConfiguration result;
    configureBand(result.bands[0], 4.0, 0.5, 1.0, -1.0);
    configureBand(result.bands[1], 1.0, 0.99, 1.0, 1.0);
    result.bands[1].tapFraction = dsp::TapFraction{0.5};
    group(result.delayGrouping, 0, 1);
    return result;
  };

  auto referenceConfiguration = makeConfiguration();
  auto alteredConfiguration = makeConfiguration();
  alteredConfiguration.bands[1].outputLevel = 0.0;
  alteredConfiguration.bands[1].pan = -1.0;
  alteredConfiguration.bands[1].delaySignalPolarity = dsp::DelaySignalPolarity::reverse;
  alteredConfiguration.bands[1].enabled = false;

  dsp::HoldsworthDelayEngine reference(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  dsp::HoldsworthDelayEngine altered(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  reference.prepare(1000.0, sampleCount);
  altered.prepare(1000.0, sampleCount);
  if (!applied(reference, referenceConfiguration, "feedback reference")
      || !applied(altered, alteredConfiguration, "feedback altered"))
    return false;

  std::array<double, sampleCount> referenceLeft{};
  std::array<double, sampleCount> referenceRight{};
  std::array<double, sampleCount> alteredLeft{};
  std::array<double, sampleCount> alteredRight{};
  render(reference, input, referenceLeft, referenceRight);
  render(altered, input, alteredLeft, alteredRight);
  return expectSamplesBitExact("GROUP feedback control independence", alteredLeft, referenceLeft)
         && alteredLeft[4] == 1.0 && alteredLeft[8] == 0.5 && alteredLeft[12] == 0.25;
}

bool testGroupLoopFilterFeedsOutputAndFeedbackOnce()
{
  constexpr std::size_t sampleCount = 10;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  dsp::HoldsworthDelayConfiguration filtered;
  configureBand(filtered.bands[0], 2.0, 0.5, 1.0, -1.0);
  configureBand(filtered.bands[1], 1.0, 0.0, 0.0, 1.0);
  filtered.bands[0].loopFilter.highCut = dsp::HighCutFrequencyHz{100.0};
  group(filtered.delayGrouping, 0, 1);

  dsp::HoldsworthDelayConfiguration bypassed = filtered;
  bypassed.bands[0].loopFilter = {};

  dsp::HoldsworthDelayEngine filteredEngine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  dsp::HoldsworthDelayEngine bypassedEngine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  filteredEngine.prepare(1000.0, sampleCount);
  bypassedEngine.prepare(1000.0, sampleCount);
  if (!applied(filteredEngine, filtered, "filtered GROUP") || !applied(bypassedEngine, bypassed, "bypassed GROUP"))
    return false;

  std::array<double, sampleCount> filteredLeft{};
  std::array<double, sampleCount> filteredRight{};
  std::array<double, sampleCount> bypassedLeft{};
  std::array<double, sampleCount> bypassedRight{};
  render(filteredEngine, input, filteredLeft, filteredRight);
  render(bypassedEngine, input, bypassedLeft, bypassedRight);
  return filteredLeft[2] > 0.0 && filteredLeft[2] < 1.0 && filteredLeft[4] != bypassedLeft[4] && bypassedLeft[2] == 1.0
         && bypassedLeft[4] == 0.5;
}

bool testLevelPanAndPhaseArePerOutputAndOutsideFeedback()
{
  constexpr std::size_t sampleCount = 8;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 3.0, 0.0, 0.5, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 0.25, 1.0);
  configuration.bands[1].delaySignalPolarity = dsp::DelaySignalPolarity::reverse;
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "GROUP output controls"))
    return false;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);
  return left[3] == 0.5 && right[3] == -0.25;
}

bool testDisabledNonHeadMutesDelayButPreservesConnectBypass()
{
  constexpr std::size_t sampleCount = 6;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> expectedLeft{};
  std::array<double, sampleCount> expectedRight{};
  expectedRight[1] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 3.0, 0.0, 0.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, -1.0, false);
  configureBand(configuration.bands[2], 1.0, 0.0, 1.0, 1.0);
  group(configuration.delayGrouping, 0, 1);
  connect(configuration.audioRouting, 2, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "disabled non-head bypass"))
    return false;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);
  return expectSamples("disabled GROUP non-head wet", left, expectedLeft, 0.0)
         && expectSamples("disabled GROUP non-head CONNECT bypass", right, expectedRight, 0.0);
}

bool testDisabledHeadGatesNewHistoryKeepsTailAndConnectBypass()
{
  constexpr std::array<double, 1> seed{1.0};
  constexpr std::array<double, 5> disabledInput{2.0, 0.0, 0.0, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 3.0, 0.0, 0.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[2], 1.0, 0.0, 1.0, 1.0);
  group(configuration.delayGrouping, 0, 1);
  connect(configuration.audioRouting, 2, 0);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, disabledInput.size());
  if (!applied(engine, configuration, "enabled GROUP head seed"))
    return false;

  std::array<double, 1> seedLeft{};
  std::array<double, 1> seedRight{};
  render(engine, seed, seedLeft, seedRight);

  configuration.bands[0].enabled = false;
  engine.setBandConfiguration(0, configuration.bands[0]);
  std::array<double, disabledInput.size()> left{};
  std::array<double, disabledInput.size()> right{};
  render(engine, disabledInput, left, right);

  return right[1] == 2.0 && left[2] == 1.0 && left[3] == 0.0;
}

bool testDisabledGroupedOutputsKeepAdvancingTheirModulationClocks()
{
  constexpr std::array<double, 1> initialSilence{};
  constexpr std::size_t sampleCount = 16;
  std::array<double, sampleCount> impulse{};
  impulse[0] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 10.0, 0.0, 0.0, -1.0, false);
  configureBand(configuration.bands[1], 1.0, 0.0, 0.0, 1.0, false);
  configureBand(configuration.bands[2], 10.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[3], 10.0, 0.0, 1.0, 1.0);
  for (std::size_t bandIndex = 0; bandIndex < 4; ++bandIndex)
  {
    configuration.bands[bandIndex].modulationRate = dsp::ModulationRateHz{250.0};
    configuration.bands[bandIndex].modulationDepth = dsp::ModulationDepthMs{2.0};
    configuration.bands[bandIndex].modulationPhase = dsp::ModulationPhaseCycles{0.0};
  }
  configuration.modulationSync.relationships[2] =
    dsp::SynchronizedModulationRelationship{DelayBandId::band1, dsp::ModulationPhaseOffsetCycles{0.0}};
  configuration.modulationSync.relationships[3] =
    dsp::SynchronizedModulationRelationship{DelayBandId::band2, dsp::ModulationPhaseOffsetCycles{0.0}};
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(20.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "disabled GROUP clocks"))
    return false;

  std::array<double, 1> ignoredLeft{};
  std::array<double, 1> ignoredRight{};
  render(engine, initialSilence, ignoredLeft, ignoredRight);

  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, impulse, left, right);
  return left[10] == 0.0 && right[10] == 0.0
         && left[12] == 1.0 && right[12] == 1.0;
}

bool testGroupModulationUsesGroupDepthAndLeavesFeedbackAtBaseDelay()
{
  constexpr std::size_t sampleCount = 51;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 20.0, 0.5, 0.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, 1.0);
  configuration.bands[1].modulationRate = dsp::ModulationRateHz{0.0};
  configuration.bands[1].modulationDepth = dsp::ModulationDepthMs{5.0};
  configuration.bands[1].modulationPhase = dsp::ModulationPhaseCycles{0.25};
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{40.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "GROUP modulation"))
    return false;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);

  return right[25] == 1.0 && right[45] == 0.5 && right[30] == 0.0;
}

bool testGroupTimingHelperClampsThenAppliesTapProvisionally()
{
  return dsp::provisionalGroupedOutputTapDelayTimeMs(20.0, 5.0, 0.25, 1.0, 40.0) == 6.25
         && dsp::provisionalGroupedOutputTapDelayTimeMs(20.0, -30.0, 0.5, 1.0, 40.0) == 0.5
         && dsp::provisionalGroupedOutputTapDelayTimeMs(
              20.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1.0, 40.0)
              == 20.0;
}

bool testGroupSyncRetainsPerOutputDepthAndHalfCycleTiming()
{
  constexpr std::size_t sampleCount = 17;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 10.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, 1.0);
  for (std::size_t bandIndex = 0; bandIndex < 2; ++bandIndex)
  {
    configuration.bands[bandIndex].modulationRate = dsp::ModulationRateHz{0.0};
    configuration.bands[bandIndex].modulationDepth = dsp::ModulationDepthMs{2.0};
    configuration.bands[bandIndex].modulationPhase = dsp::ModulationPhaseCycles{0.25};
  }
  configuration.modulationSync.relationships[1] =
    dsp::SynchronizedModulationRelationship{DelayBandId::band1, dsp::ModulationPhaseOffsetCycles{0.5}};
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(5.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "GROUP SYNC"))
    return false;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);
  return left[12] == 1.0 && right[8] == 1.0;
}

bool testGroupedOutputCanFeedAnotherGroupAndFanOutWithoutWetDuplication()
{
  constexpr std::size_t sampleCount = 9;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 2.0, 0.0, 0.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[2], 3.0, 0.0, 0.0, 1.0);
  configureBand(configuration.bands[3], 1.0, 0.0, 1.0, 1.0);
  configureBand(configuration.bands[4], 1.0, 0.0, 0.0, 1.0);
  group(configuration.delayGrouping, 0, 1);
  group(configuration.delayGrouping, 2, 3);
  connect(configuration.audioRouting, 2, 1);
  connect(configuration.audioRouting, 4, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, sampleCount);
  if (!applied(engine, configuration, "GROUP to GROUP fan-out"))
    return false;
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};
  render(engine, input, left, right);

  return left[2] == 1.0 && right[3] == 1.0 && right[5] == 1.0;
}

bool testNonHeadDestinationAndPostCollapseCycleAreRejected()
{
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 3.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, 1.0);
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, 1);
  if (!applied(engine, configuration, "GROUP CONNECT validation seed"))
    return false;

  dsp::AudioRoutingConfiguration nonHead;
  connect(nonHead, 1, 2);
  dsp::AudioRoutingConfiguration collapsedCycle;
  connect(collapsedCycle, 0, 1);

  return engine.applyAudioRoutingConfiguration(nonHead) == AudioRoutingApplyResult::nonHeadGroupedDestination
         && engine.applyAudioRoutingConfiguration(collapsedCycle)
              == AudioRoutingApplyResult::groupCollapsedCycleDetected;
}

bool testStandaloneGroupApplicationValidatesActiveConnect()
{
  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(1000.0, 1);
  dsp::AudioRoutingConfiguration routing;
  connect(routing, 1, 2);
  if (engine.applyAudioRoutingConfiguration(routing) != AudioRoutingApplyResult::applied)
    return false;

  dsp::DelayGroupingConfiguration grouping;
  group(grouping, 0, 1);
  return engine.applyDelayGroupingConfiguration(grouping) == DelayGroupingApplyResult::nonHeadConnectedDestination;
}

bool testMemberCountDoesNotAdvanceSharedHistoryMoreThanOnce()
{
  constexpr std::size_t sampleCount = 16;
  std::array<double, sampleCount> input{};
  input[0] = 1.0;

  auto makeConfiguration = [](const std::size_t endIndex) {
    dsp::HoldsworthDelayConfiguration result;
    configureBand(result.bands[0], 4.0, 0.5, 1.0, -1.0);
    for (std::size_t index = 1; index <= endIndex; ++index)
      configureBand(result.bands[index], 1.0, 0.0, 0.0, 1.0, false);
    group(result.delayGrouping, 0, endIndex);
    return result;
  };

  dsp::HoldsworthDelayEngine two(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  dsp::HoldsworthDelayEngine four(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  two.prepare(1000.0, sampleCount);
  four.prepare(1000.0, sampleCount);
  if (!applied(two, makeConfiguration(1), "two-member GROUP")
      || !applied(four, makeConfiguration(3), "four-member GROUP"))
    return false;

  std::array<double, sampleCount> twoLeft{};
  std::array<double, sampleCount> twoRight{};
  std::array<double, sampleCount> fourLeft{};
  std::array<double, sampleCount> fourRight{};
  render(two, input, twoLeft, twoRight);
  render(four, input, fourLeft, fourRight);
  return expectSamplesBitExact("GROUP single history advancement", fourLeft, twoLeft);
}

bool testBlockPartitionResetAndAliasingAreDeterministic()
{
  constexpr std::size_t sampleCount = 32;
  constexpr std::size_t partitionSize = 7;
  std::array<double, sampleCount> input{};
  for (std::size_t index = 0; index < sampleCount; ++index)
    input[index] = static_cast<double>(static_cast<int>(index % 9) - 4) / 5.0;

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[1], 7.5, 0.35, 0.8, -1.0);
  configureBand(configuration.bands[2], 1.0, 0.0, 0.6, 1.0);
  configuration.bands[1].tapFraction = dsp::TapFraction{0.4};
  configuration.bands[2].modulationRate = dsp::ModulationRateHz{2.0};
  configuration.bands[2].modulationDepth = dsp::ModulationDepthMs{1.0};
  group(configuration.delayGrouping, 1, 2);

  dsp::HoldsworthDelayEngine whole(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  dsp::HoldsworthDelayEngine partitioned(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  dsp::HoldsworthDelayEngine aliased(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  whole.prepare(1000.0, sampleCount);
  partitioned.prepare(1000.0, partitionSize);
  aliased.prepare(1000.0, sampleCount);
  if (!applied(whole, configuration, "whole GROUP") || !applied(partitioned, configuration, "partitioned GROUP")
      || !applied(aliased, configuration, "aliased GROUP"))
    return false;

  std::array<double, sampleCount> wholeLeft{};
  std::array<double, sampleCount> wholeRight{};
  render(whole, input, wholeLeft, wholeRight);

  std::array<double, sampleCount> partitionedLeft{};
  std::array<double, sampleCount> partitionedRight{};
  std::size_t offset = 0;
  while (offset < sampleCount)
  {
    const std::size_t frames = std::min(partitionSize, sampleCount - offset);
    partitioned.processBlock(std::span<const double>(input).subspan(offset, frames),
                             std::span<double>(partitionedLeft).subspan(offset, frames),
                             std::span<double>(partitionedRight).subspan(offset, frames));
    offset += frames;
  }

  std::array<double, sampleCount> aliasLeft = input;
  std::array<double, sampleCount> aliasRight{};
  aliased.processBlock(aliasLeft, aliasLeft, aliasRight);

  whole.reset();
  std::array<double, sampleCount> resetLeft{};
  std::array<double, sampleCount> resetRight{};
  render(whole, input, resetLeft, resetRight);

  return expectSamplesBitExact("GROUP block partition left", partitionedLeft, wholeLeft)
         && expectSamplesBitExact("GROUP block partition right", partitionedRight, wholeRight)
         && expectSamplesBitExact("GROUP aliased left", aliasLeft, wholeLeft)
         && expectSamplesBitExact("GROUP aliased right", aliasRight, wholeRight)
         && expectSamplesBitExact("GROUP reset left", resetLeft, wholeLeft)
         && expectSamplesBitExact("GROUP reset right", resetRight, wholeRight);
}

bool testUnchangedTopologyPreservesHistoryAndChangedTopologyClearsIt()
{
  constexpr std::array<double, 1> seed{1.0};
  constexpr std::array<double, 5> silence{};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 4.0, 0.0, 1.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 0.0, 1.0);
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine preserved(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  dsp::HoldsworthDelayEngine cleared(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  preserved.prepare(1000.0, silence.size());
  cleared.prepare(1000.0, silence.size());
  if (!applied(preserved, configuration, "preserved topology") || !applied(cleared, configuration, "changed topology"))
    return false;

  std::array<double, 1> ignoredLeft{};
  std::array<double, 1> ignoredRight{};
  render(preserved, seed, ignoredLeft, ignoredRight);
  render(cleared, seed, ignoredLeft, ignoredRight);

  if (preserved.applyDelayGroupingConfiguration(configuration.delayGrouping) != DelayGroupingApplyResult::applied)
    return false;
  dsp::DelayGroupingConfiguration expanded;
  group(expanded, 0, 2);
  if (cleared.applyDelayGroupingConfiguration(expanded) != DelayGroupingApplyResult::applied)
    return false;

  std::array<double, silence.size()> preservedLeft{};
  std::array<double, silence.size()> preservedRight{};
  std::array<double, silence.size()> clearedLeft{};
  std::array<double, silence.size()> clearedRight{};
  render(preserved, silence, preservedLeft, preservedRight);
  render(cleared, silence, clearedLeft, clearedRight);
  return preservedLeft[3] == 1.0 && clearedLeft[3] == 0.0;
}

bool testFullAndStandaloneRejectionsPreserveHistoryAndConfiguration()
{
  constexpr std::array<double, 1> seed{1.0};
  constexpr std::array<double, 6> silence{};

  dsp::HoldsworthDelayConfiguration accepted;
  configureBand(accepted.bands[0], 4.0, 0.5, 1.0, -1.0);
  configureBand(accepted.bands[1], 1.0, 0.0, 0.0, 1.0);
  group(accepted.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine actual(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  dsp::HoldsworthDelayEngine reference(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  actual.prepare(1000.0, silence.size());
  reference.prepare(1000.0, silence.size());
  if (!applied(actual, accepted, "transaction actual") || !applied(reference, accepted, "transaction reference"))
    return false;

  std::array<double, 1> ignoredLeft{};
  std::array<double, 1> ignoredRight{};
  render(actual, seed, ignoredLeft, ignoredRight);
  render(reference, seed, ignoredLeft, ignoredRight);

  dsp::AudioRoutingConfiguration invalidRouting;
  connect(invalidRouting, 1, 2);
  if (actual.applyAudioRoutingConfiguration(invalidRouting) != AudioRoutingApplyResult::nonHeadGroupedDestination)
    return false;

  auto rejectedFull = accepted;
  rejectedFull.bands[0].delayTimeMs = 25.0;
  rejectedFull.globalWetOutputLevel = 0.0;
  if (actual.applyConfiguration(rejectedFull).delayGrouping != DelayGroupingApplyResult::delayTimeExceedsCapacity)
    return false;

  std::array<double, silence.size()> actualLeft{};
  std::array<double, silence.size()> actualRight{};
  std::array<double, silence.size()> referenceLeft{};
  std::array<double, silence.size()> referenceRight{};
  render(actual, silence, actualLeft, actualRight);
  render(reference, silence, referenceLeft, referenceRight);
  return expectSamplesBitExact("GROUP transactional left", actualLeft, referenceLeft)
         && expectSamplesBitExact("GROUP transactional right", actualRight, referenceRight)
         && equalGrouping(actual.configuration().delayGrouping, accepted.delayGrouping)
         && actual.globalWetOutputLevel() == 1.0;
}

bool testGroupingConfigurationAndProcessingAllocateNothing()
{
  constexpr std::size_t sampleCount = 256;
  std::array<double, sampleCount> input{};
  std::array<double, sampleCount> left{};
  std::array<double, sampleCount> right{};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 12.0, 0.4, 1.0, -1.0);
  configureBand(configuration.bands[1], 1.0, 0.0, 1.0, 1.0);
  group(configuration.delayGrouping, 0, 1);

  dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{20.0});
  engine.prepare(48000.0, sampleCount);

  beginAllocationTracking();
  const auto fullResult = engine.applyConfiguration(configuration);
  const auto groupingResult = engine.applyDelayGroupingConfiguration(configuration.delayGrouping);
  const auto routingResult = engine.applyAudioRoutingConfiguration({});
  engine.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
    std::cerr << "GROUP realtime/configuration made " << allocations << " allocation(s)\n";
  return fullResult.wasApplied() && groupingResult == DelayGroupingApplyResult::applied
         && routingResult == AudioRoutingApplyResult::applied && allocations == 0;
}

constexpr std::array<TestCase, 25> kTests{
  {{"GROUP: source/DSP types and documented capacities stay separate",
    testSourceAndDspTypesAndDocumentedCapacityFactsStaySeparate},
   {"GROUP: sizes 2-8, non-Band-1 heads, and multiple ranges round-trip",
    testValidSizesHeadsAndMultipleDisjointGroupsRoundTrip},
   {"GROUP: invalid ranges, overlap, nesting, and duplicates are rejected", testInvalidRangesAndMembershipAreRejected},
   {"GROUP: documented and physical capacities validate independently",
    testDocumentedAndPhysicalCapacityPoliciesAreValidated},
   {"GROUP: shared history supports independent TAPs above band capacity",
    testSharedHistoryProducesIndependentTapOutputsAboveBandCapacity},
   {"GROUP: changing total delay preserves proportional TAP timing",
    testChangingTotalDelayPreservesProportionalTapTiming},
   {"GROUP: one feedback recurrence ignores member output controls",
    testOneFeedbackRecurrenceIsIndependentOfMemberOutputControls},
   {"GROUP: one authoritative loop filter feeds output and feedback", testGroupLoopFilterFeedsOutputAndFeedbackOnce},
   {"GROUP: LEVEL, PAN, and PHASE remain per-output", testLevelPanAndPhaseArePerOutputAndOutsideFeedback},
   {"GROUP: disabled non-head mutes delay and preserves CONNECT bypass",
    testDisabledNonHeadMutesDelayButPreservesConnectBypass},
   {"GROUP: disabled head gates input, keeps tail, and bypasses CONNECT",
    testDisabledHeadGatesNewHistoryKeepsTailAndConnectBypass},
   {"GROUP: disabled head and member modulation clocks keep advancing",
    testDisabledGroupedOutputsKeepAdvancingTheirModulationClocks},
   {"GROUP: modulation uses group depth and not feedback-loop timing",
    testGroupModulationUsesGroupDepthAndLeavesFeedbackAtBaseDelay},
   {"GROUP: provisional modulation/TAP helper clamps then applies TAP",
    testGroupTimingHelperClampsThenAppliesTapProvisionally},
   {"GROUP: SYNC retains per-output depth and half-cycle timing", testGroupSyncRetainsPerOutputDepthAndHalfCycleTiming},
   {"GROUP: grouped output feeds another group and fans out once",
    testGroupedOutputCanFeedAnotherGroupAndFanOutWithoutWetDuplication},
   {"GROUP: non-head destination and collapsed cycles are rejected",
    testNonHeadDestinationAndPostCollapseCycleAreRejected},
   {"GROUP: standalone topology validates active CONNECT", testStandaloneGroupApplicationValidatesActiveConnect},
   {"GROUP: member count cannot advance shared history more than once",
    testMemberCountDoesNotAdvanceSharedHistoryMoreThanOnce},
   {"GROUP: block partition, reset, and input aliasing are deterministic",
    testBlockPartitionResetAndAliasingAreDeterministic},
   {"GROUP: unchanged topology preserves history and changed topology clears",
    testUnchangedTopologyPreservesHistoryAndChangedTopologyClearsIt},
   {"GROUP: full and standalone rejection preserve state transactionally",
    testFullAndStandaloneRejectionsPreserveHistoryAndConfiguration},
   {"GROUP: configuration and processing allocate nothing", testGroupingConfigurationAndProcessingAllocateNothing},
   {"GROUP: physical capacity getter is distinct",
    [] {
      dsp::HoldsworthDelayEngine engine(10.0, GroupedDelayPhysicalCapacityMs{123.0});
      return engine.maximumDelayTimeMs() == 10.0 && engine.groupedPhysicalCapacityMs() == 123.0;
    }},
   {"GROUP: singleton source metadata remains non-DSP", [] {
      const auto source = presets::YamahaGroupControlValue::individual(presets::YamahaEffectBandNumber::band4);
      dsp::DelayGroupingConfiguration grouping;
      return source.firstBand() == presets::YamahaEffectBandNumber::band4
             && source.lastBand() == presets::YamahaEffectBandNumber::band4 && !grouping.groupsByHead[3].has_value();
    }}}};

} // namespace

TestSuite delayGroupingTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
