#include "../dsp/HoldsworthDelayEngine.h"
#include "../dsp/HoldsworthDelayPresets.h"
#include "../presets/YamahaModulationSourceValues.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

static_assert(holdsworth::dsp::kHoldsworthDelayBandCount == 8);
static_assert(holdsworth::dsp::HoldsworthDelayEngine::kBandCount == 8);
static_assert(!std::is_copy_constructible_v<holdsworth::dsp::HoldsworthDelayEngine>);
static_assert(!std::is_copy_assignable_v<holdsworth::dsp::HoldsworthDelayEngine>);
static_assert(!std::is_move_constructible_v<holdsworth::dsp::HoldsworthDelayEngine>);
static_assert(!std::is_move_assignable_v<holdsworth::dsp::HoldsworthDelayEngine>);
static_assert(!std::is_convertible_v<double, holdsworth::dsp::NormalizedFeedbackCoefficient>);
static_assert(!std::is_convertible_v<double, holdsworth::dsp::YamahaFeedbackControlValue>);
static_assert(!std::is_convertible_v<holdsworth::dsp::YamahaFeedbackControlValue,
                                     holdsworth::dsp::NormalizedFeedbackCoefficient>);
static_assert(!std::is_convertible_v<holdsworth::dsp::NormalizedFeedbackCoefficient,
                                     holdsworth::dsp::YamahaFeedbackControlValue>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaSpeedControlValue,
                                     holdsworth::dsp::ModulationRateHz>);
static_assert(!std::is_convertible_v<holdsworth::dsp::ModulationRateHz,
                                     holdsworth::presets::YamahaSpeedControlValue>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaDepthControlValue,
                                     holdsworth::dsp::ModulationDepthMs>);
static_assert(!std::is_convertible_v<holdsworth::dsp::ModulationDepthMs,
                                     holdsworth::presets::YamahaDepthControlValue>);

namespace holdsworth::test
{
namespace
{

void configureBand(dsp::DelayBandConfiguration& band,
                   const double delayTimeMs,
                   const double feedback,
                   const double outputLevel,
                   const double pan,
                   const bool enabled = true,
                   const double modulationRateHz = 0.0,
                   const double modulationDepthMs = 0.0,
                   const double modulationPhaseCycles = 0.0) noexcept
{
  band.delayTimeMs = delayTimeMs;
  band.feedback = dsp::NormalizedFeedbackCoefficient{feedback};
  band.outputLevel = outputLevel;
  band.pan = pan;
  band.enabled = enabled;
  band.modulationRate = dsp::ModulationRateHz{modulationRateHz};
  band.modulationDepth = dsp::ModulationDepthMs{modulationDepthMs};
  band.modulationPhase = dsp::ModulationPhaseCycles{modulationPhaseCycles};
}

dsp::HoldsworthDelayConfiguration makeExerciseConfiguration() noexcept
{
  dsp::HoldsworthDelayConfiguration configuration;
  configuration.globalWetOutputLevel = 0.67;

  for (std::size_t i = 0; i < configuration.bands.size(); ++i)
  {
    configureBand(configuration.bands[i],
                  1.25 + 0.75 * static_cast<double>(i),
                  0.10 + 0.07 * static_cast<double>(i),
                  0.20 + 0.08 * static_cast<double>(i),
                  -1.0 + 2.0 * static_cast<double>(i) / 7.0,
                  i != 3);
  }

  return configuration;
}

dsp::HoldsworthDelayConfiguration makeModulatedExerciseConfiguration() noexcept
{
  dsp::HoldsworthDelayConfiguration configuration;
  configuration.globalWetOutputLevel = 0.67;

  for (std::size_t i = 0; i < configuration.bands.size(); ++i)
  {
    configureBand(configuration.bands[i],
                  3.25 + 1.37 * static_cast<double>(i),
                  0.05 + 0.07 * static_cast<double>(i),
                  0.25 + 0.08 * static_cast<double>(i),
                  -1.0 + 2.0 * static_cast<double>(i) / 7.0,
                  true,
                  0.37 + 0.23 * static_cast<double>(i),
                  0.20 + 0.07 * static_cast<double>(i),
                  0.03125 + 0.101 * static_cast<double>(i));
  }

  return configuration;
}

bool expectConfiguration(const std::string_view testName,
                         const dsp::HoldsworthDelayConfiguration& actual,
                         const dsp::HoldsworthDelayConfiguration& expected)
{
  if (!nearlyEqual(actual.globalWetOutputLevel, expected.globalWetOutputLevel))
  {
    std::cerr << testName << ": global wet level was " << actual.globalWetOutputLevel
              << ", expected " << expected.globalWetOutputLevel << '\n';
    return false;
  }

  for (std::size_t i = 0; i < actual.bands.size(); ++i)
  {
    const auto& actualBand = actual.bands[i];
    const auto& expectedBand = expected.bands[i];
    if (!nearlyEqual(actualBand.delayTimeMs, expectedBand.delayTimeMs)
        || !nearlyEqual(actualBand.feedback.value, expectedBand.feedback.value)
        || !nearlyEqual(actualBand.outputLevel, expectedBand.outputLevel)
        || !nearlyEqual(actualBand.pan, expectedBand.pan)
        || !nearlyEqual(actualBand.modulationRate.value,
                        expectedBand.modulationRate.value)
        || !nearlyEqual(actualBand.modulationDepth.value,
                        expectedBand.modulationDepth.value)
        || !nearlyEqual(actualBand.modulationPhase.value,
                        expectedBand.modulationPhase.value)
        || actualBand.enabled != expectedBand.enabled)
    {
      std::cerr << testName << ": configuration mismatch at band " << (i + 1) << '\n';
      return false;
    }
  }

  return true;
}

bool testConfigurationReturnsRequestedModulationValues()
{
  dsp::HoldsworthDelayEngine engine(20.0);
  auto requested = makeModulatedExerciseConfiguration();
  requested.bands[0].delayTimeMs = 0.25; // Below one sample at 1 kHz.
  requested.bands[0].modulationDepth = dsp::ModulationDepthMs{4.0};
  // Above the technical effective-rate limit at 1 kHz. Configuration must
  // still report the sanitized requested value rather than 500 Hz.
  requested.bands[1].modulationRate = dsp::ModulationRateHz{777.0};
  engine.applyConfiguration(requested);
  engine.prepare(1000.0, 8);

  if (!expectConfiguration("pre-prepare configuration survived prepare",
                           engine.configuration(),
                           requested))
    return false;

  const std::array<double, 7> input{1.0, -0.5, 0.25, 0.0, 0.75, -0.25, 0.0};
  std::array<double, input.size()> left{};
  std::array<double, input.size()> right{};
  engine.processBlock(input, left, right);
  if (!expectConfiguration("configuration reports reset phases after processing",
                           engine.configuration(),
                           requested))
    return false;

  engine.prepare(48000.0, 8);
  if (!expectConfiguration("configuration after sample-rate change", engine.configuration(), requested))
    return false;

  auto replacement = requested.bands[4];
  replacement.delayTimeMs = 0.005; // Also below one sample at 48 kHz.
  replacement.feedback = dsp::NormalizedFeedbackCoefficient{0.82};
  replacement.modulationRate = dsp::ModulationRateHz{3.25};
  replacement.modulationDepth = dsp::ModulationDepthMs{7.5};
  replacement.modulationPhase = dsp::ModulationPhaseCycles{0.9375};
  engine.setBandConfiguration(4, replacement);
  requested.bands[4] = replacement;

  return expectConfiguration("configuration after band update", engine.configuration(), requested);
}

bool testSingleModulatedBandMatchesDelayBand()
{
  constexpr std::size_t numSamples = 12;
  constexpr std::array<double, numSamples> input{1.0, -0.5, 0.25, 0.0, 0.0, 0.0,
                                                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[2], 1.5, 0.5, 0.6, 0.2, true, 37.5, 0.4, 0.137);

  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, numSamples);
  engine.applyConfiguration(configuration);
  std::array<double, numSamples> engineLeft{};
  std::array<double, numSamples> engineRight{};
  engine.processBlock(input, engineLeft, engineRight);

  dsp::DelayBand referenceBand(20.0);
  referenceBand.prepare(1000.0, numSamples);
  referenceBand.setDelayTimeMs(1.5);
  referenceBand.setFeedbackCoefficient(0.5);
  referenceBand.setOutputLevel(0.6);
  referenceBand.setPan(0.2);
  referenceBand.setModulationRate(dsp::ModulationRateHz{37.5});
  referenceBand.setModulationDepth(dsp::ModulationDepthMs{0.4});
  referenceBand.setModulationPhase(dsp::ModulationPhaseCycles{0.137});
  referenceBand.setEnabled(true);
  std::array<double, numSamples> referenceLeft{};
  std::array<double, numSamples> referenceRight{};
  referenceBand.processBlock(input, referenceLeft, referenceRight);

  return expectSamples("single modulated-band engine left", engineLeft, referenceLeft, 0.0)
         && expectSamples("single modulated-band engine right", engineRight, referenceRight, 0.0);
}

bool testEightIndependentBandsAppearAtExpectedPositions()
{
  dsp::HoldsworthDelayEngine engine(10.0);
  engine.prepare(1000.0, 9);

  dsp::HoldsworthDelayConfiguration configuration;
  for (std::size_t i = 0; i < configuration.bands.size(); ++i)
  {
    configureBand(configuration.bands[i],
                  static_cast<double>(i + 1),
                  0.0,
                  1.0,
                  (i % 2 == 0) ? -1.0 : 1.0);
  }
  engine.applyConfiguration(configuration);

  const std::array<double, 9> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  const std::array<double, 9> expectedLeft{0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0};
  const std::array<double, 9> expectedRight{0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
  std::array<double, 9> left{};
  std::array<double, 9> right{};
  engine.processBlock(input, left, right);

  return expectSamples("eight independent bands left", left, expectedLeft)
         && expectSamples("eight independent bands right", right, expectedRight);
}

bool testEightBandsSumWithoutNormalization()
{
  dsp::HoldsworthDelayEngine engine(10.0);
  engine.prepare(1000.0, 3);

  dsp::HoldsworthDelayConfiguration configuration;
  for (auto& band : configuration.bands)
    configureBand(band, 1.0, 0.0, 1.0, -1.0);
  engine.applyConfiguration(configuration);

  const std::array<double, 3> input{1.0, 0.0, 0.0};
  const std::array<double, 3> expectedLeft{0.0, 8.0, 0.0};
  const std::array<double, 3> expectedRight{};
  std::array<double, 3> left{};
  std::array<double, 3> right{};
  engine.processBlock(input, left, right);

  return expectSamples("unnormalized eight-band sum left", left, expectedLeft)
         && expectSamples("unnormalized eight-band sum right", right, expectedRight);
}

bool testGlobalWetLevelIsAfterSumAndOutsideFeedback()
{
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 1.0, 0.5, 1.0, -1.0);
  configuration.globalWetOutputLevel = 0.25;

  dsp::HoldsworthDelayEngine scaledEngine(10.0);
  scaledEngine.prepare(1000.0, 4);
  scaledEngine.applyConfiguration(configuration);
  const std::array<double, 4> impulse{1.0, 0.0, 0.0, 0.0};
  const std::array<double, 4> expectedScaled{0.0, 0.25, 0.125, 0.0625};
  std::array<double, 4> scaledLeft{};
  std::array<double, 4> scaledRight{};
  scaledEngine.processBlock(impulse, scaledLeft, scaledRight);
  if (!expectSamples("post-feedback global wet scaling", scaledLeft, expectedScaled))
    return false;

  configuration.globalWetOutputLevel = 0.0;
  dsp::HoldsworthDelayEngine mutedEngine(10.0);
  mutedEngine.prepare(1000.0, 3);
  mutedEngine.applyConfiguration(configuration);
  const std::array<double, 1> firstInput{1.0};
  std::array<double, 1> mutedLeft{1.0};
  std::array<double, 1> mutedRight{1.0};
  mutedEngine.processBlock(firstInput, mutedLeft, mutedRight);
  if (!expectNear("zero global wet left", mutedLeft[0], 0.0)
      || !expectNear("zero global wet right", mutedRight[0], 0.0))
    return false;

  mutedEngine.setGlobalWetOutputLevel(1.0);
  const std::array<double, 3> silence{};
  const std::array<double, 3> expectedTail{1.0, 0.5, 0.25};
  std::array<double, 3> tailLeft{};
  std::array<double, 3> tailRight{};
  mutedEngine.processBlock(silence, tailLeft, tailRight);

  return expectSamples("global mute preserves feedback history", tailLeft, expectedTail);
}

bool testWetOnlyOutputsOverwriteExistingData()
{
  dsp::HoldsworthDelayEngine engine(10.0);
  engine.prepare(1000.0, 4);
  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[0], 2.0, 0.0, 1.0, 1.0);
  engine.applyConfiguration(configuration);

  const std::array<double, 4> input{3.0, 0.0, 0.0, 0.0};
  const std::array<double, 4> expectedLeft{};
  const std::array<double, 4> expectedRight{0.0, 0.0, 3.0, 0.0};
  std::array<double, 4> left{99.0, 99.0, 99.0, 99.0};
  std::array<double, 4> right{99.0, 99.0, 99.0, 99.0};
  engine.processBlock(input, left, right);

  return expectSamples("engine wet-only left overwrite", left, expectedLeft)
         && expectSamples("engine wet-only right overwrite", right, expectedRight);
}

bool testResetClearsAllHistoriesAndPreservesConfiguration()
{
  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, 12);
  auto configuration = makeModulatedExerciseConfiguration();
  engine.applyConfiguration(configuration);

  const std::array<double, 3> input{1.0, -0.5, 0.25};
  std::array<double, 3> discardedLeft{};
  std::array<double, 3> discardedRight{};
  engine.processBlock(input, discardedLeft, discardedRight);
  engine.reset();

  const std::array<double, 12> silence{};
  std::array<double, 12> left{};
  std::array<double, 12> right{};
  engine.processBlock(silence, left, right);

  return expectSamples("engine reset left", left, silence)
         && expectSamples("engine reset right", right, silence)
         && expectConfiguration("engine reset configuration", engine.configuration(), configuration);
}

bool testResetRestoresDeterministicModulationPhases()
{
  constexpr std::size_t numSamples = 173;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<double>(static_cast<int>((i * 17) % 31) - 15) * 0.03125;

  const auto configuration = makeModulatedExerciseConfiguration();
  dsp::HoldsworthDelayEngine engine(20.0);
  engine.prepare(1000.0, numSamples);
  engine.applyConfiguration(configuration);

  std::array<double, numSamples> firstLeft{};
  std::array<double, numSamples> firstRight{};
  std::array<double, numSamples> secondLeft{};
  std::array<double, numSamples> secondRight{};
  engine.processBlock(input, firstLeft, firstRight);

  if (!expectConfiguration("processing preserves configured reset phases",
                           engine.configuration(),
                           configuration))
    return false;

  engine.reset();
  if (!expectConfiguration("reset preserves modulated configuration",
                           engine.configuration(),
                           configuration))
    return false;

  engine.processBlock(input, secondLeft, secondRight);
  return expectSamples("engine modulation reset left", secondLeft, firstLeft, 0.0)
         && expectSamples("engine modulation reset right", secondRight, firstRight, 0.0);
}

bool testBlockPartitioningDoesNotChangeOutput()
{
  // Cross DelayModulator's periodic cache synchronization while using a
  // different block layout on the comparison engine.
  constexpr std::size_t numSamples = dsp::DelayModulator::kCacheResynchronizationInterval + 83;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = (static_cast<double>((i * 11) % 19) - 9.0) / 9.0;

  const auto configuration = makeModulatedExerciseConfiguration();

  dsp::HoldsworthDelayEngine wholeEngine(20.0);
  wholeEngine.prepare(1000.0, numSamples);
  wholeEngine.applyConfiguration(configuration);
  std::array<double, numSamples> wholeLeft{};
  std::array<double, numSamples> wholeRight{};
  wholeEngine.processBlock(input, wholeLeft, wholeRight);

  dsp::HoldsworthDelayEngine partitionedEngine(20.0);
  constexpr std::array<std::size_t, 9> blockSizes{1, 17, 3, 11, 2, 16, 5, 9, 13};
  partitionedEngine.prepare(1000.0, 17);
  partitionedEngine.applyConfiguration(configuration);
  std::array<double, numSamples> partitionedLeft{};
  std::array<double, numSamples> partitionedRight{};
  std::size_t offset = 0;
  std::size_t blockIndex = 0;
  while (offset < numSamples)
  {
    const std::size_t blockSize =
      std::min(blockSizes[blockIndex % blockSizes.size()], numSamples - offset);
    partitionedEngine.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                                   std::span<double>(partitionedLeft).subspan(offset, blockSize),
                                   std::span<double>(partitionedRight).subspan(offset, blockSize));
    offset += blockSize;
    ++blockIndex;
  }

  return expectSamples("eight-band modulated left block partitioning",
                       partitionedLeft,
                       wholeLeft,
                       0.0)
         && expectSamples("eight-band modulated right block partitioning",
                          partitionedRight,
                          wholeRight,
                          0.0)
         && expectConfiguration("whole-block configuration remains requested values",
                                wholeEngine.configuration(),
                                configuration)
         && expectConfiguration("partitioned configuration remains requested values",
                                partitionedEngine.configuration(),
                                configuration);
}

bool testExactInputOutputAliasing()
{
  constexpr std::size_t numSamples = 17;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<double>((i * 3) % 7) - 3.0;

  const auto configuration = makeExerciseConfiguration();

  dsp::HoldsworthDelayEngine referenceEngine(20.0);
  referenceEngine.prepare(1000.0, numSamples);
  referenceEngine.applyConfiguration(configuration);
  std::array<double, numSamples> referenceLeft{};
  std::array<double, numSamples> referenceRight{};
  referenceEngine.processBlock(input, referenceLeft, referenceRight);

  dsp::HoldsworthDelayEngine leftAliasEngine(20.0);
  leftAliasEngine.prepare(1000.0, numSamples);
  leftAliasEngine.applyConfiguration(configuration);
  std::array<double, numSamples> inputAndLeft = input;
  std::array<double, numSamples> leftAliasRight{};
  leftAliasEngine.processBlock(std::span<const double>(inputAndLeft),
                               std::span<double>(inputAndLeft),
                               leftAliasRight);

  dsp::HoldsworthDelayEngine rightAliasEngine(20.0);
  rightAliasEngine.prepare(1000.0, numSamples);
  rightAliasEngine.applyConfiguration(configuration);
  std::array<double, numSamples> inputAndRight = input;
  std::array<double, numSamples> rightAliasLeft{};
  rightAliasEngine.processBlock(std::span<const double>(inputAndRight),
                                rightAliasLeft,
                                std::span<double>(inputAndRight));

  return expectSamples("engine input/left exact alias", inputAndLeft, referenceLeft)
         && expectSamples("engine input/left separate right", leftAliasRight, referenceRight)
         && expectSamples("engine input/right separate left", rightAliasLeft, referenceLeft)
         && expectSamples("engine input/right exact alias", inputAndRight, referenceRight);
}

bool testGlobalWetLevelClampsAndSanitizes()
{
  dsp::HoldsworthDelayEngine engine(10.0);

  engine.setGlobalWetOutputLevel(-1.0);
  if (!expectNear("negative global wet clamp", engine.globalWetOutputLevel(), 0.0))
    return false;
  engine.setGlobalWetOutputLevel(2.0);
  if (!expectNear("excessive global wet clamp", engine.globalWetOutputLevel(), 1.0))
    return false;
  engine.setGlobalWetOutputLevel(std::numeric_limits<double>::quiet_NaN());
  return expectNear("non-finite global wet", engine.globalWetOutputLevel(), 0.0);
}

bool testProcessingDoesNotAllocate()
{
  constexpr std::size_t blockSize = 256;
  dsp::HoldsworthDelayEngine engine(700.0);
  engine.prepare(48000.0, blockSize);

  engine.applyConfiguration(dsp::presets::chorus011ProvisionalV1().dspConfiguration);

  std::array<double, blockSize> input{};
  std::array<double, blockSize> left{};
  std::array<double, blockSize> right{};

  beginAllocationTracking();
  engine.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: HoldsworthDelayEngine::processBlock made " << allocations
              << " allocation(s)\n";
    return false;
  }
  return true;
}

bool testLead121PresetDefinition()
{
  const auto& preset = dsp::presets::lead121UnmodulatedProvisional();
  constexpr std::array<double, 8> expectedDelayTimes{29.7, 40.0, 96.0, 110.0,
                                                      300.0, 400.0, 355.0, 461.0};
  constexpr std::array<double, 8> expectedYamahaFeedback{0.0, 0.0, 0.0, 0.0,
                                                         4.5, 4.0, 3.5, 3.0};
  constexpr std::array<double, 8> expectedDspFeedback{0.0, 0.0, 0.0, 0.0,
                                                      0.45, 0.40, 0.35, 0.30};
  constexpr std::array<double, 8> expectedPan{-1.0, 1.0, -0.5, 0.5,
                                              -1.0, 1.0, -1.0, 1.0};
  constexpr std::array<double, 8> expectedLevel{1.0, 1.0, 0.4, 0.4,
                                                0.5, 0.5, 0.5, 0.5};

  if (preset.id != "lead121-unmodulated-provisional-v1"
      || preset.displayName.empty()
      || preset.feedbackCalibration != dsp::FeedbackCalibrationStatus::provisionalUnmeasured
      || preset.modulationCalibration.has_value()
      || preset.documentedYamahaGlobalValues.effectLevel.has_value()
      || preset.documentedYamahaGlobalValues.directLevel.has_value()
      || preset.documentedYamahaGlobalValues.directPan.has_value()
      || !expectNear("Lead 121 required maximum delay", preset.requiredMaximumDelayTimeMs, 461.0)
      || !expectNear("Lead 121 global wet level", preset.dspConfiguration.globalWetOutputLevel, 1.0))
  {
    std::cerr << "Lead 121 preset metadata mismatch\n";
    return false;
  }

  for (std::size_t i = 0; i < expectedDelayTimes.size(); ++i)
  {
    const auto& band = preset.dspConfiguration.bands[i];
    const auto& documented = preset.documentedYamahaValues[i];
    const auto& source = documented.feedbackControlValue;
    if (!source.has_value()
        || documented.speedControlValue.has_value()
        || documented.depthControlValue.has_value()
        || documented.delayTimeMs.has_value()
        || documented.panControlValue.has_value()
        || documented.levelControlValue.has_value()
        || !nearlyEqual(source->value, expectedYamahaFeedback[i])
        || !nearlyEqual(band.delayTimeMs, expectedDelayTimes[i])
        || !nearlyEqual(band.feedback.value, expectedDspFeedback[i])
        || !nearlyEqual(band.pan, expectedPan[i])
        || !nearlyEqual(band.outputLevel, expectedLevel[i])
        || !nearlyEqual(band.modulationRate.value, 0.0)
        || !nearlyEqual(band.modulationDepth.value, 0.0)
        || !nearlyEqual(band.modulationPhase.value, 0.0)
        || !band.enabled)
    {
      std::cerr << "Lead 121 preset mismatch at band " << (i + 1) << '\n';
      return false;
    }
  }

  dsp::HoldsworthDelayEngine developmentEngine(700.0);
  return expectNear("runtime capacity is independent of preset metadata",
                    developmentEngine.maximumDelayTimeMs(),
                    700.0);
}

bool testLead121OutputIsBitExactWithLegacyStaticTopology()
{
  constexpr std::size_t numSamples = 1000;
  constexpr double sampleRate = 1000.0;
  constexpr double maximumDelayTimeMs = 700.0;

  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<double>(static_cast<int>((i * 29) % 47) - 23) * 0.015625;

  const auto& preset = dsp::presets::lead121UnmodulatedProvisional();
  dsp::HoldsworthDelayEngine engine(maximumDelayTimeMs);
  engine.prepare(sampleRate, numSamples);
  engine.applyConfiguration(preset.dspConfiguration);
  std::array<double, numSamples> engineLeft{};
  std::array<double, numSamples> engineRight{};
  engine.processBlock(input, engineLeft, engineRight);

  // This reference intentionally uses only DelayBand's pre-modulation
  // controls. It proves that forwarding explicit zero-valued modulation from
  // the expanded engine configuration cannot perturb Lead 121's established
  // static-delay recurrence or eight-band summation order.
  std::array<dsp::DelayBand, dsp::HoldsworthDelayEngine::kBandCount> referenceBands{
    dsp::DelayBand{maximumDelayTimeMs},
    dsp::DelayBand{maximumDelayTimeMs},
    dsp::DelayBand{maximumDelayTimeMs},
    dsp::DelayBand{maximumDelayTimeMs},
    dsp::DelayBand{maximumDelayTimeMs},
    dsp::DelayBand{maximumDelayTimeMs},
    dsp::DelayBand{maximumDelayTimeMs},
    dsp::DelayBand{maximumDelayTimeMs}};

  std::array<double, numSamples> referenceLeft{};
  std::array<double, numSamples> referenceRight{};
  std::array<double, numSamples> bandLeft{};
  std::array<double, numSamples> bandRight{};
  for (std::size_t bandIndex = 0; bandIndex < referenceBands.size(); ++bandIndex)
  {
    auto& band = referenceBands[bandIndex];
    const auto& configuration = preset.dspConfiguration.bands[bandIndex];
    band.prepare(sampleRate, numSamples);
    band.setDelayTimeMs(configuration.delayTimeMs);
    band.setFeedbackCoefficient(configuration.feedback.value);
    band.setOutputLevel(configuration.outputLevel);
    band.setPan(configuration.pan);
    band.setEnabled(configuration.enabled);
    band.processBlock(input, bandLeft, bandRight);

    for (std::size_t frame = 0; frame < numSamples; ++frame)
    {
      referenceLeft[frame] += bandLeft[frame];
      referenceRight[frame] += bandRight[frame];
    }
  }

  for (std::size_t frame = 0; frame < numSamples; ++frame)
  {
    referenceLeft[frame] *= preset.dspConfiguration.globalWetOutputLevel;
    referenceRight[frame] *= preset.dspConfiguration.globalWetOutputLevel;
  }

  return expectSamples("Lead 121 legacy-static left", engineLeft, referenceLeft, 0.0)
         && expectSamples("Lead 121 legacy-static right", engineRight, referenceRight, 0.0);
}

bool testChorus011PresetDefinition()
{
  const auto& preset = dsp::presets::chorus011ProvisionalV1();
  constexpr std::array<double, 8> expectedDelayTimes{23.6, 30.0, 38.1, 47.6,
                                                      300.0, 400.0, 341.0, 450.0};
  constexpr std::array<double, 8> expectedYamahaFeedback{0.0, 0.0, 0.0, 0.0,
                                                         4.5, 3.5, 4.3, 3.4};
  constexpr std::array<double, 8> expectedYamahaSpeed{3.5, 4.0, 4.2, 3.7,
                                                      3.5, 3.8, 4.7, 3.3};
  constexpr std::array<double, 8> expectedYamahaDepth{2.5, 2.5, 2.5, 2.5,
                                                      2.5, 2.5, 2.5, 2.5};
  constexpr std::array<dsp::YamahaPanDirection, 8> expectedYamahaPan{
    dsp::YamahaPanDirection::left,
    dsp::YamahaPanDirection::right,
    dsp::YamahaPanDirection::right,
    dsp::YamahaPanDirection::left,
    dsp::YamahaPanDirection::left,
    dsp::YamahaPanDirection::right,
    dsp::YamahaPanDirection::left,
    dsp::YamahaPanDirection::right};
  constexpr std::array<double, 8> expectedYamahaLevel{10.0, 10.0, 10.0, 10.0,
                                                      6.5, 6.5, 6.5, 6.5};

  constexpr std::array<double, 8> expectedDspFeedback{0.0, 0.0, 0.0, 0.0,
                                                      0.36, 0.28, 0.34, 0.26};
  constexpr std::array<double, 8> expectedDspRate{0.38, 0.52, 0.58, 0.43,
                                                  0.38, 0.46, 0.72, 0.33};
  constexpr std::array<double, 8> expectedDspDepth{0.75, 0.75, 0.75, 0.75,
                                                   0.75, 0.75, 0.75, 0.75};
  constexpr std::array<double, 8> expectedDspPhase{0.0, 0.5, 0.25, 0.75,
                                                   0.125, 0.625, 0.375, 0.875};
  constexpr std::array<double, 8> expectedDspPan{-1.0, 1.0, 1.0, -1.0,
                                                  -1.0, 1.0, -1.0, 1.0};
  constexpr std::array<double, 8> expectedDspLevel{1.0, 1.0, 1.0, 1.0,
                                                    0.65, 0.65, 0.65, 0.65};

  const auto& globals = preset.documentedYamahaGlobalValues;
  if (preset.id != "chorus011-provisional-v1"
      || preset.displayName.empty()
      || preset.feedbackCalibration != dsp::FeedbackCalibrationStatus::provisionalUnmeasured
      || !preset.modulationCalibration.has_value()
      || preset.modulationCalibration->speedMapping
           != dsp::YamahaModulationMappingStatus::unmeasured
      || preset.modulationCalibration->depthMapping
           != dsp::YamahaModulationMappingStatus::unmeasured
      || preset.modulationCalibration->phaseRelationship
           != dsp::ModulationPhaseRelationshipStatus::provisional
      || !expectNear("Chorus 011 required maximum delay",
                     preset.requiredMaximumDelayTimeMs,
                     450.75)
      || !expectNear("Chorus 011 global wet level",
                     preset.dspConfiguration.globalWetOutputLevel,
                     1.0)
      || !globals.effectLevel.has_value()
      || !expectNear("Chorus 011 Yamaha effect level", globals.effectLevel->value, 8.5)
      || !globals.directLevel.has_value()
      || !expectNear("Chorus 011 Yamaha direct level", globals.directLevel->value, 5.0)
      || !globals.directPan.has_value()
      || globals.directPan->direction != dsp::YamahaPanDirection::center
      || !expectNear("Chorus 011 Yamaha direct pan", globals.directPan->magnitude, 0.0))
  {
    std::cerr << "Chorus 011 preset global metadata mismatch\n";
    return false;
  }

  double requiredPhysicalCapacityMs = 0.0;
  for (std::size_t i = 0; i < expectedDelayTimes.size(); ++i)
  {
    const auto& source = preset.documentedYamahaValues[i];
    const auto& band = preset.dspConfiguration.bands[i];
    if (!source.feedbackControlValue.has_value()
        || !source.speedControlValue.has_value()
        || !source.depthControlValue.has_value()
        || !source.delayTimeMs.has_value()
        || !source.panControlValue.has_value()
        || !source.levelControlValue.has_value()
        || !nearlyEqual(source.feedbackControlValue->value, expectedYamahaFeedback[i])
        || !nearlyEqual(source.speedControlValue->value, expectedYamahaSpeed[i])
        || !nearlyEqual(source.depthControlValue->value, expectedYamahaDepth[i])
        || !nearlyEqual(source.delayTimeMs->value, expectedDelayTimes[i])
        || source.panControlValue->direction != expectedYamahaPan[i]
        || !nearlyEqual(source.panControlValue->magnitude, 10.0)
        || !nearlyEqual(source.levelControlValue->value, expectedYamahaLevel[i])
        || !nearlyEqual(band.delayTimeMs, expectedDelayTimes[i])
        || !nearlyEqual(band.feedback.value, expectedDspFeedback[i])
        || !nearlyEqual(band.modulationRate.value, expectedDspRate[i])
        || !nearlyEqual(band.modulationDepth.value, expectedDspDepth[i])
        || !nearlyEqual(band.modulationPhase.value, expectedDspPhase[i])
        || !nearlyEqual(band.pan, expectedDspPan[i])
        || !nearlyEqual(band.outputLevel, expectedDspLevel[i])
        || !band.enabled)
    {
      std::cerr << "Chorus 011 source/DSP mismatch at band " << (i + 1) << '\n';
      return false;
    }

    requiredPhysicalCapacityMs =
      std::max(requiredPhysicalCapacityMs, band.delayTimeMs + band.modulationDepth.value);
  }

  if (!expectNear("Chorus 011 capacity includes positive modulation excursion",
                  preset.requiredMaximumDelayTimeMs,
                  requiredPhysicalCapacityMs))
    return false;

  dsp::HoldsworthDelayEngine engine(preset.requiredMaximumDelayTimeMs);
  engine.applyConfiguration(preset.dspConfiguration);
  engine.prepare(48000.0, 8);
  return expectConfiguration("Chorus 011 physical values round-trip independently",
                             engine.configuration(),
                             preset.dspConfiguration);
}

constexpr std::array kTests{
  TestCase{"HoldsworthDelayEngine: configuration preserves requested modulation values",
           testConfigurationReturnsRequestedModulationValues},
  TestCase{"HoldsworthDelayEngine: one modulated band matches DelayBand",
           testSingleModulatedBandMatchesDelayBand},
  TestCase{"HoldsworthDelayEngine: eight bands have independent tap positions",
           testEightIndependentBandsAppearAtExpectedPositions},
  TestCase{"HoldsworthDelayEngine: eight bands sum without hidden normalization",
           testEightBandsSumWithoutNormalization},
  TestCase{"HoldsworthDelayEngine: global wet is post-sum and outside feedback",
           testGlobalWetLevelIsAfterSumAndOutsideFeedback},
  TestCase{"HoldsworthDelayEngine: outputs are overwritten with wet-only signal",
           testWetOnlyOutputsOverwriteExistingData},
  TestCase{"HoldsworthDelayEngine: reset clears histories and preserves configuration",
           testResetClearsAllHistoriesAndPreservesConfiguration},
  TestCase{"HoldsworthDelayEngine: reset restores deterministic modulation phases",
           testResetRestoresDeterministicModulationPhases},
  TestCase{"HoldsworthDelayEngine: eight modulated bands are block-partition invariant",
           testBlockPartitioningDoesNotChangeOutput},
  TestCase{"HoldsworthDelayEngine: exact input/output aliasing", testExactInputOutputAliasing},
  TestCase{"HoldsworthDelayEngine: global wet clamps and sanitizes non-finite values",
           testGlobalWetLevelClampsAndSanitizes},
  TestCase{"HoldsworthDelayEngine: processing performs no allocations", testProcessingDoesNotAllocate},
  TestCase{"HoldsworthDelayEngine: Lead 121 source and DSP values remain separate",
           testLead121PresetDefinition},
  TestCase{"HoldsworthDelayEngine: Lead 121 output is bit-exact with legacy static topology",
           testLead121OutputIsBitExactWithLegacyStaticTopology},
  TestCase{"HoldsworthDelayEngine: Chorus 011 source and provisional DSP values remain separate",
           testChorus011PresetDefinition},
};

} // namespace

TestSuite holdsworthDelayEngineTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
