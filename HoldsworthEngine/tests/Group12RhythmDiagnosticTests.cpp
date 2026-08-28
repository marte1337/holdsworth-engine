#include "../dsp/HoldsworthDelayPresets.h"
#include "TestHarness.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

static_assert(!std::is_convertible_v<holdsworth::dsp::GroupedDelayPhysicalCapacityMs,
                                     holdsworth::dsp::DocumentedYamahaGroupMaximumDelayMs>);
static_assert(!std::is_convertible_v<holdsworth::dsp::DocumentedYamahaGroupMaximumDelayMs,
                                     holdsworth::dsp::GroupedDelayPhysicalCapacityMs>);

namespace holdsworth::test
{
namespace
{

[[nodiscard]] bool hasNoDocumentedYamahaValues(
  const dsp::DocumentedYamahaBandValues& band) noexcept
{
  return !band.switchState.has_value()
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
}

[[nodiscard]] bool hasNoInventedYamahaMetadata(
  const dsp::HoldsworthDelayPresetDefinition& diagnostic) noexcept
{
  for (const auto& band : diagnostic.documentedYamahaValues)
  {
    if (!hasNoDocumentedYamahaValues(band))
      return false;
  }

  return !diagnostic.documentedYamahaGlobalValues.effectLevel.has_value()
         && !diagnostic.documentedYamahaGlobalValues.directLevel.has_value()
         && !diagnostic.documentedYamahaGlobalValues.directPan.has_value()
         && !diagnostic.documentedYamahaPresetIdentity.has_value()
         && !diagnostic.documentedYamahaSyncAuditionReference.has_value()
         && !diagnostic.documentedYamahaManualExerciseReference.has_value();
}

[[nodiscard]] bool isExactDiagnosticBand(
  const dsp::DelayBandConfiguration& band,
  const double delayTimeMs,
  const double tapFraction,
  const double pan) noexcept
{
  return band.enabled
         && band.delayTimeMs == delayTimeMs
         && band.feedback.value == 0.0
         && band.outputLevel == 1.0
         && band.pan == pan
         && band.modulationRate.value == 0.0
         && band.modulationDepth.value == 0.0
         && band.modulationPhase.value == 0.0
         && !band.loopFilter.lowCut.has_value()
         && !band.loopFilter.highCut.has_value()
         && band.tapFraction.value == tapFraction
         && band.delaySignalPolarity == dsp::DelaySignalPolarity::normal;
}

[[nodiscard]] bool hasNoConnectOrSync(
  const dsp::HoldsworthDelayConfiguration& configuration) noexcept
{
  for (std::size_t index = 0; index < dsp::kHoldsworthDelayBandCount; ++index)
  {
    if (configuration.audioRouting.inputs[index].has_value()
        || configuration.modulationSync.relationships[index].has_value())
      return false;
  }
  return true;
}

[[nodiscard]] bool hasOnlyGroup12(
  const dsp::DelayGroupingConfiguration& grouping) noexcept
{
  if (!grouping.groupsByHead[0].has_value()
      || grouping.groupsByHead[0]->endBand != dsp::DelayBandId::band2)
    return false;

  for (std::size_t headIndex = 1; headIndex < grouping.groupsByHead.size(); ++headIndex)
  {
    if (grouping.groupsByHead[headIndex].has_value())
      return false;
  }
  return true;
}

[[nodiscard]] bool isExactGroup12Diagnostic(
  const dsp::HoldsworthDelayPresetDefinition& diagnostic,
  const double groupBaseDelayTimeMs,
  const double requiredGroupedCapacityMs) noexcept
{
  const auto& configuration = diagnostic.dspConfiguration;
  if (!isExactDiagnosticBand(configuration.bands[0], groupBaseDelayTimeMs, 0.5, -1.0)
      || !isExactDiagnosticBand(configuration.bands[1], 0.0, 1.0, 1.0)
      || !hasOnlyGroup12(configuration.delayGrouping)
      || !hasNoConnectOrSync(configuration)
      || configuration.globalWetOutputLevel != 1.0
      || diagnostic.requiredMaximumDelayTimeMs != 696.0
      || !diagnostic.requiredGroupedDelayPhysicalCapacity.has_value()
      || diagnostic.requiredGroupedDelayPhysicalCapacity->value != requiredGroupedCapacityMs)
    return false;

  for (std::size_t bandIndex = 2; bandIndex < configuration.bands.size(); ++bandIndex)
  {
    if (configuration.bands[bandIndex].enabled)
      return false;
  }
  return true;
}

bool testDocumentedCapacityFactsRemainSeparateFromDiagnosticPhysicalValues()
{
  const auto individualMaximum = dsp::documentedYamahaGroupMaximumDelayMs(1);
  const auto twoBandMaximum = dsp::documentedYamahaGroupMaximumDelayMs(2);
  const auto& rhythm1200 = dsp::presets::group12Rhythm1200DiagnosticV1();
  const auto& rhythm900 = dsp::presets::group12Rhythm900DiagnosticV1();

  return individualMaximum.has_value()
         && individualMaximum->value == 696.0
         && twoBandMaximum.has_value()
         && twoBandMaximum->value == 1430.0
         && rhythm1200.dspConfiguration.bands[0].delayTimeMs == 1200.0
         && rhythm900.dspConfiguration.bands[0].delayTimeMs == 900.0
         && hasNoInventedYamahaMetadata(rhythm1200)
         && hasNoInventedYamahaMetadata(rhythm900);
}

bool testDiagnosticDefinitionsUseExactSharedGroupConfiguration()
{
  const auto& rhythm1200 = dsp::presets::group12Rhythm1200DiagnosticV1();
  const auto& rhythm900 = dsp::presets::group12Rhythm900DiagnosticV1();
  return rhythm1200.id == "group12-rhythm-1200-diagnostic-v1"
         && rhythm1200.displayName == "GROUP 1-2 Rhythm 1200 ms Diagnostic"
         && rhythm900.id == "group12-rhythm-900-diagnostic-v1"
         && rhythm900.displayName == "GROUP 1-2 Rhythm 900 ms Diagnostic"
         && isExactGroup12Diagnostic(rhythm1200, 1200.0, 1200.0)
         && isExactGroup12Diagnostic(rhythm900, 900.0, 900.0);
}

[[nodiscard]] bool expectOnlyImpulseAt(
  const std::string_view testName,
  const std::span<const double> samples,
  const std::size_t impulseIndex)
{
  for (std::size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
  {
    const double expected = sampleIndex == impulseIndex ? 1.0 : 0.0;
    if (samples[sampleIndex] != expected)
    {
      std::cerr << testName << ": sample " << sampleIndex << " was "
                << samples[sampleIndex] << ", expected " << expected << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool rendersOnlyExpectedGroupTaps(
  const dsp::HoldsworthDelayPresetDefinition& diagnostic,
  const std::size_t earlyTapSample,
  const std::size_t fullTapSample)
{
  const std::size_t sampleCount = fullTapSample * 2U + 1U;
  std::vector<double> input(sampleCount, 0.0);
  std::vector<double> wetLeft(sampleCount, 0.0);
  std::vector<double> wetRight(sampleCount, 0.0);
  input[0] = 1.0;

  dsp::HoldsworthDelayEngine engine(
    696.0, dsp::GroupedDelayPhysicalCapacityMs{1430.0});
  engine.prepare(1000.0, sampleCount);
  if (!engine.applyConfiguration(diagnostic.dspConfiguration).wasApplied())
  {
    std::cerr << diagnostic.displayName << ": configuration was rejected\n";
    return false;
  }

  engine.processBlock(input, wetLeft, wetRight);
  const auto applied = engine.configuration();
  return hasOnlyGroup12(applied.delayGrouping)
         && applied.bands[0].delayTimeMs == diagnostic.dspConfiguration.bands[0].delayTimeMs
         && applied.bands[1].delayTimeMs == 0.0
         && wetLeft[0] == 0.0
         && wetRight[0] == 0.0
         && expectOnlyImpulseAt("GROUP rhythm left", wetLeft, earlyTapSample)
         && expectOnlyImpulseAt("GROUP rhythm right", wetRight, fullTapSample);
}

bool testDiagnosticsRenderProportionalTapsWithoutDryOrRepeats()
{
  constexpr std::size_t rhythm1200EarlyTap = 600;
  constexpr std::size_t rhythm1200FullTap = 1200;
  constexpr std::size_t rhythm900EarlyTap = 450;
  constexpr std::size_t rhythm900FullTap = 900;

  return rhythm1200EarlyTap * 2U == rhythm1200FullTap
         && rhythm900EarlyTap * 2U == rhythm900FullTap
         && rendersOnlyExpectedGroupTaps(
           dsp::presets::group12Rhythm1200DiagnosticV1(),
           rhythm1200EarlyTap,
           rhythm1200FullTap)
         && rendersOnlyExpectedGroupTaps(
           dsp::presets::group12Rhythm900DiagnosticV1(),
           rhythm900EarlyTap,
           rhythm900FullTap);
}

bool testDiagnosticConfigurationApplicationIsAllocationFree()
{
  dsp::HoldsworthDelayEngine engine(
    696.0, dsp::GroupedDelayPhysicalCapacityMs{1430.0});
  engine.prepare(48000.0, 64);

  beginAllocationTracking();
  const auto rhythm1200Result = engine.applyConfiguration(
    dsp::presets::group12Rhythm1200DiagnosticV1().dspConfiguration);
  const auto rhythm900Result = engine.applyConfiguration(
    dsp::presets::group12Rhythm900DiagnosticV1().dspConfiguration);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
    std::cerr << "GROUP rhythm diagnostic application made " << allocations
              << " allocation(s)\n";
  return allocations == 0
         && rhythm1200Result.wasApplied()
         && rhythm900Result.wasApplied()
         && hasOnlyGroup12(engine.configuration().delayGrouping)
         && engine.configuration().bands[0].delayTimeMs == 900.0;
}

constexpr std::array kTests{
  TestCase{"GROUP rhythm diagnostics: documented capacities and physical values remain separate",
           testDocumentedCapacityFactsRemainSeparateFromDiagnosticPhysicalValues},
  TestCase{"GROUP rhythm diagnostics: exact shared GROUP configuration",
           testDiagnosticDefinitionsUseExactSharedGroupConfiguration},
  TestCase{"GROUP rhythm diagnostics: proportional taps have no dry leak or repeats",
           testDiagnosticsRenderProportionalTapsWithoutDryOrRepeats},
  TestCase{"GROUP rhythm diagnostics: configuration application is allocation-free",
           testDiagnosticConfigurationApplicationIsAllocationFree}};

} // namespace

TestSuite group12RhythmDiagnosticTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
