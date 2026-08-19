#include "../dsp/HoldsworthDelayPresets.h"
#include "../integration/DevelopmentDelayPresetSelector.h"
#include "../integration/MonoDryStereoWetMixer.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

namespace holdsworth::test
{
namespace
{

using Mixer = integration::MonoDryStereoWetMixer;
using DevelopmentPreset = integration::DevelopmentDelayPreset;

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
      && actualBand.tapFraction.value == expectedBand.tapFraction.value;
    if (!matches)
    {
      std::cerr << testName << ": band " << (bandIndex + 1) << " differs\n";
      return false;
    }
  }

  return true;
}

dsp::HoldsworthDelayConfiguration makeTailTestConfiguration() noexcept
{
  dsp::HoldsworthDelayConfiguration configuration;
  auto& band = configuration.bands[0];
  band.delayTimeMs = 1.0;
  band.feedback = dsp::NormalizedFeedbackCoefficient{0.5};
  band.outputLevel = 1.0;
  band.pan = -1.0;
  band.enabled = true;
  configuration.globalWetOutputLevel = 1.0;
  return configuration;
}

bool testStereoMixIsDirectAndUnclipped()
{
  const std::array<double, 3> dry{2.0, -3.0, 4.0};
  const std::array<double, 3> wetLeft{3.0, 10.0, -2.0};
  const std::array<double, 3> wetRight{-1.0, 6.0, 8.0};
  const std::array<double, 3> expectedLeft{3.5, 2.0, 3.0};
  const std::array<double, 3> expectedRight{1.5, 0.0, 8.0};
  std::array<double, 3> outputLeft{};
  std::array<double, 3> outputRight{};

  Mixer::mixStereo(dry, wetLeft, wetRight, 0.5, outputLeft, outputRight);

  return expectSamples("direct stereo dry plus wet left", outputLeft, expectedLeft)
         && expectSamples("direct stereo dry plus wet right", outputRight, expectedRight);
}

bool testMonoMixUsesHalfStereoWetFoldDown()
{
  const std::array<double, 3> dry{1.0, -2.0, 0.0};
  const std::array<double, 3> wetLeft{4.0, 10.0, -4.0};
  const std::array<double, 3> wetRight{2.0, -6.0, 8.0};
  const std::array<double, 3> expected{4.0, 0.0, 2.0};
  std::array<double, 3> output{};

  Mixer::mixMono(dry, wetLeft, wetRight, 1.0, output);
  return expectSamples("explicit 0.5 mono wet fold-down", output, expected);
}

bool testWetMixMultiplierIsSeparateFromEngineGlobalWetLevel()
{
  constexpr std::size_t blockSize = 48;
  dsp::HoldsworthDelayEngine engine(700.0);
  engine.prepare(1000.0, blockSize);

  auto configuration = dsp::presets::lead121UnmodulatedProvisional().dspConfiguration;
  configuration.globalWetOutputLevel = 0.5;
  engine.applyConfiguration(configuration);

  std::array<double, blockSize> input{};
  input[0] = 1.0;
  const std::array<double, blockSize> silence{};
  std::array<double, blockSize> wetLeft{};
  std::array<double, blockSize> wetRight{};
  std::array<double, blockSize> outputLeft{};
  std::array<double, blockSize> outputRight{};

  Mixer::advanceDelay(engine, true, input, silence, wetLeft, wetRight);
  Mixer::mixStereo(input, wetLeft, wetRight, 0.25, outputLeft, outputRight);

  // Lead 121 band 2 is a unity-level, hard-right, 40 ms tap. At 1 kHz,
  // engine global wet 0.5 followed by integration wet mix 0.25 yields 0.125.
  return expectNear("engine global wet remains configured", engine.globalWetOutputLevel(), 0.5)
         && expectNear("Lead 121 wet before integration mix", wetRight[40], 0.5)
         && expectNear("separate integration wet multiplier", outputRight[40], 0.125)
         && expectNear("integration mixer does not alter dry", outputLeft[0], 1.0)
         && expectNear(
           "integration mixer does not mutate engine configuration", engine.configuration().globalWetOutputLevel, 0.5);
}

bool testDevelopmentPresetSelectionAppliesExactExistingConfigurations()
{
  dsp::HoldsworthDelayEngine engine(700.0);
  engine.prepare(48000.0, 64);

  const auto& selectedLead = integration::developmentDelayPresetDefinition(DevelopmentPreset::lead121);
  const auto& selectedChorus011 =
    integration::developmentDelayPresetDefinition(DevelopmentPreset::chorus011);
  const auto& selectedChorus031 =
    integration::developmentDelayPresetDefinition(DevelopmentPreset::chorus031);
  if (&selectedLead != &dsp::presets::lead121UnmodulatedProvisional()
      || &selectedChorus011 != &dsp::presets::chorus011ProvisionalV1()
      || &selectedChorus031 != &dsp::presets::chorus031ProvisionalV1())
  {
    std::cerr << "development preset selector did not return the existing preset definitions\n";
    return false;
  }

  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::lead121);
  if (!expectConfigurationExact(
        "Lead selection", engine.configuration(), selectedLead.dspConfiguration))
    return false;

  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus011);
  if (!expectConfigurationExact(
        "Chorus 011 selection", engine.configuration(), selectedChorus011.dspConfiguration))
    return false;

  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus031);
  return expectConfigurationExact(
           "Chorus 031 selection", engine.configuration(), selectedChorus031.dspConfiguration)
         && integration::developmentDelayPresetFromIndex(99U) == DevelopmentPreset::lead121
         && integration::developmentDelayPresetFromNormalizedControlValue(0.0)
              == DevelopmentPreset::lead121
         && integration::developmentDelayPresetFromNormalizedControlValue(0.5)
              == DevelopmentPreset::chorus011
         && integration::developmentDelayPresetFromNormalizedControlValue(1.0)
              == DevelopmentPreset::chorus031
         && integration::developmentDelayPresetNormalizedControlValue(DevelopmentPreset::lead121)
              == 0.0
         && integration::developmentDelayPresetNormalizedControlValue(DevelopmentPreset::chorus011)
              == 0.5
         && integration::developmentDelayPresetNormalizedControlValue(DevelopmentPreset::chorus031)
              == 1.0;
}

bool testDevelopmentPresetSwitchDoesNotAllocateOrResetHistory()
{
  constexpr std::size_t seedSize = 20;
  constexpr std::size_t tailSize = 20;
  dsp::HoldsworthDelayEngine engine(700.0);
  engine.prepare(1000.0, 64);
  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::lead121);

  std::array<double, seedSize> seedInput{};
  seedInput.fill(1.0);
  const std::array<double, seedSize> seedSilence{};
  std::array<double, seedSize> seedWetLeft{};
  std::array<double, seedSize> seedWetRight{};
  Mixer::advanceDelay(engine, true, seedInput, seedSilence, seedWetLeft, seedWetRight);

  beginAllocationTracking();
  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus011);
  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus031);
  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::lead121);
  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus011);
  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus031);
  const std::size_t switchAllocations = endAllocationTracking();
  if (switchAllocations != 0)
  {
    std::cerr << "development preset switch made " << switchAllocations << " allocation(s)\n";
    return false;
  }

  // No new input follows the switch. Chorus 031's early TAPs must
  // still encounter the ones stored while Lead was selected. A reset inside
  // preset application would make both output blocks completely silent.
  const std::array<double, tailSize> silence{};
  std::array<double, tailSize> wetLeft{};
  std::array<double, tailSize> wetRight{};
  Mixer::advanceDelay(engine, true, silence, silence, wetLeft, wetRight);
  const bool preservedHistory =
    std::any_of(wetLeft.begin(), wetLeft.end(), [](const double sample) { return sample != 0.0; })
    || std::any_of(wetRight.begin(), wetRight.end(), [](const double sample) { return sample != 0.0; });

  if (!preservedHistory)
    std::cerr << "development preset switch implicitly cleared existing delay history\n";
  return preservedHistory;
}

bool testDevelopmentPresetSwitchLeavesWetMultiplierIndependent()
{
  constexpr double integrationWetMultiplier = 0.17;
  const std::array<double, 2> dry{1.0, -1.0};
  const std::array<double, 2> wetLeft{2.0, 4.0};
  const std::array<double, 2> wetRight{-2.0, 6.0};
  std::array<double, 2> leadOutputLeft{};
  std::array<double, 2> leadOutputRight{};
  std::array<double, 2> chorusOutputLeft{};
  std::array<double, 2> chorusOutputRight{};
  std::array<double, 2> chorus031OutputLeft{};
  std::array<double, 2> chorus031OutputRight{};

  dsp::HoldsworthDelayEngine engine(700.0);
  engine.prepare(48000.0, dry.size());
  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::lead121);
  Mixer::mixStereo(
    dry, wetLeft, wetRight, integrationWetMultiplier, leadOutputLeft, leadOutputRight);

  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus011);
  Mixer::mixStereo(
    dry, wetLeft, wetRight, integrationWetMultiplier, chorusOutputLeft, chorusOutputRight);

  integration::applyDevelopmentDelayPreset(engine, DevelopmentPreset::chorus031);
  Mixer::mixStereo(
    dry, wetLeft, wetRight, integrationWetMultiplier, chorus031OutputLeft, chorus031OutputRight);

  return expectSamples("preset switch preserves integration wet mix left", chorusOutputLeft, leadOutputLeft, 0.0)
         && expectSamples("preset switch preserves integration wet mix right", chorusOutputRight, leadOutputRight, 0.0)
         && expectSamples("Chorus 031 switch preserves integration wet mix left",
                          chorus031OutputLeft,
                          leadOutputLeft,
                          0.0)
         && expectSamples("Chorus 031 switch preserves integration wet mix right",
                          chorus031OutputRight,
                          leadOutputRight,
                          0.0)
         && expectNear("Chorus 031 preset retains its own DSP wet level",
                       engine.configuration().globalWetOutputLevel,
                       dsp::presets::chorus031ProvisionalV1().dspConfiguration.globalWetOutputLevel,
                       0.0);
}

bool testBypassAdvancesTailWithoutCapturingDisabledInput()
{
  dsp::HoldsworthDelayEngine engine(10.0);
  engine.prepare(1000.0, 4);
  const auto configuration = makeTailTestConfiguration();
  engine.applyConfiguration(configuration);

  const std::array<double, 1> silenceOne{};
  std::array<double, 1> wetLeft{};
  std::array<double, 1> wetRight{};

  // Seed the one-sample feedback loop while enabled.
  const std::array<double, 1> impulse{1.0};
  Mixer::advanceDelay(engine, true, impulse, silenceOne, wetLeft, wetRight);
  if (!expectNear("initial delayed output", wetLeft[0], 0.0))
    return false;

  // Model the stock disabled dry route outside the mixer. advanceDelay() must
  // feed silence, advance the existing tail, and discard its wet output.
  const std::array<double, 1> firstDisabledInput{9.0};
  Mixer::advanceDelay(engine, false, firstDisabledInput, silenceOne, wetLeft, wetRight);
  if (!expectNear("first bypassed wet left discarded", wetLeft[0], 0.0)
      || !expectNear("first bypassed wet right discarded", wetRight[0], 0.0)
      || !expectNear("first stock bypass dry sample", firstDisabledInput[0], 9.0))
    return false;

  const std::array<double, 1> secondDisabledInput{-7.0};
  Mixer::advanceDelay(engine, false, secondDisabledInput, silenceOne, wetLeft, wetRight);
  if (!expectNear("second bypassed wet left discarded", wetLeft[0], 0.0)
      || !expectNear("second bypassed wet right discarded", wetRight[0], 0.0)
      || !expectNear("second stock bypass dry sample", secondDisabledInput[0], -7.0))
    return false;

  // The tail advanced twice: 1.0 -> 0.5 -> 0.25. Neither 9.0 nor -7.0 was
  // admitted to the delay while bypassed.
  const std::array<double, 4> silenceFour{};
  std::array<double, 4> resumedWetLeft{};
  std::array<double, 4> resumedWetRight{};
  std::array<double, 4> resumedOutputLeft{};
  std::array<double, 4> resumedOutputRight{};
  const std::array<double, 4> expectedTail{0.25, 0.125, 0.0625, 0.03125};
  Mixer::advanceDelay(engine, true, silenceFour, silenceFour, resumedWetLeft, resumedWetRight);
  Mixer::mixStereo(silenceFour, resumedWetLeft, resumedWetRight, 1.0, resumedOutputLeft, resumedOutputRight);

  const auto actualConfiguration = engine.configuration();
  return expectSamples("tail advances while globally bypassed", resumedOutputLeft, expectedTail)
         && expectSamples("hard-left tail has no right output", resumedOutputRight, silenceFour)
         && actualConfiguration.bands[0].enabled
         && expectNear("bypass preserves band feedback", actualConfiguration.bands[0].feedback.value,
                       configuration.bands[0].feedback.value)
         && expectNear("bypass preserves engine global wet", actualConfiguration.globalWetOutputLevel,
                       configuration.globalWetOutputLevel);
}

bool testLiveIntegrationHelpersDoNotAllocate()
{
  constexpr std::size_t blockSize = 64;
  dsp::HoldsworthDelayEngine engine(700.0);
  engine.prepare(48000.0, blockSize);
  engine.applyConfiguration(dsp::presets::lead121UnmodulatedProvisional().dspConfiguration);

  std::array<double, blockSize> input{};
  input[0] = 1.0;
  const std::array<double, blockSize> silence{};
  std::array<double, blockSize> wetLeft{};
  std::array<double, blockSize> wetRight{};
  std::array<double, blockSize> outputLeft{};
  std::array<double, blockSize> outputRight{};
  std::array<double, blockSize> outputMono{};

  beginAllocationTracking();
  Mixer::advanceDelay(engine, true, input, silence, wetLeft, wetRight);
  Mixer::mixStereo(input, wetLeft, wetRight, 0.75, outputLeft, outputRight);
  Mixer::mixMono(input, wetLeft, wetRight, 0.75, outputMono);
  Mixer::advanceDelay(engine, false, input, silence, wetLeft, wetRight);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: live integration helpers made " << allocations << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"Live integration: stereo mix is direct and unclipped", testStereoMixIsDirectAndUnclipped},
  TestCase{"Live integration: mono output uses explicit 0.5 wet fold-down", testMonoMixUsesHalfStereoWetFoldDown},
  TestCase{"Live integration: wet mix is separate from engine global wet",
           testWetMixMultiplierIsSeparateFromEngineGlobalWetLevel},
  TestCase{"Live integration: development preset selection applies existing configurations exactly",
           testDevelopmentPresetSelectionAppliesExactExistingConfigurations},
  TestCase{"Live integration: preset switching allocates nothing and preserves history",
           testDevelopmentPresetSwitchDoesNotAllocateOrResetHistory},
  TestCase{"Live integration: preset switching leaves temporary wet mix independent",
           testDevelopmentPresetSwitchLeavesWetMultiplierIndependent},
  TestCase{"Live integration: bypass advances tails without capturing input",
           testBypassAdvancesTailWithoutCapturingDisabledInput},
  TestCase{"Live integration: processing helpers perform no allocations", testLiveIntegrationHelpersDoNotAllocate},
};

} // namespace

TestSuite holdsworthDelayLiveIntegrationTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
