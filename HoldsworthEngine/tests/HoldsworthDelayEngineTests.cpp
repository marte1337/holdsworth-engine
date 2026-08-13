#include "../dsp/HoldsworthDelayEngine.h"
#include "../dsp/HoldsworthDelayPresets.h"
#include "TestHarness.h"

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

namespace holdsworth::test
{
namespace
{

void configureBand(dsp::DelayBandConfiguration& band,
                   const double delayTimeMs,
                   const double feedback,
                   const double outputLevel,
                   const double pan,
                   const bool enabled = true) noexcept
{
  band.delayTimeMs = delayTimeMs;
  band.feedback = dsp::NormalizedFeedbackCoefficient{feedback};
  band.outputLevel = outputLevel;
  band.pan = pan;
  band.enabled = enabled;
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
        || actualBand.enabled != expectedBand.enabled)
    {
      std::cerr << testName << ": configuration mismatch at band " << (i + 1) << '\n';
      return false;
    }
  }

  return true;
}

bool testConfigurationReturnsRequestedDelayTimes()
{
  dsp::HoldsworthDelayEngine engine(20.0);
  auto requested = makeExerciseConfiguration();
  requested.bands[0].delayTimeMs = 0.25; // Below one sample at 1 kHz.
  engine.applyConfiguration(requested);
  engine.prepare(1000.0, 8);

  if (!expectConfiguration("pre-prepare configuration survived prepare",
                           engine.configuration(),
                           requested))
    return false;

  engine.prepare(48000.0, 8);
  if (!expectConfiguration("configuration after sample-rate change", engine.configuration(), requested))
    return false;

  auto replacement = requested.bands[4];
  replacement.delayTimeMs = 0.005; // Also below one sample at 48 kHz.
  replacement.feedback = dsp::NormalizedFeedbackCoefficient{0.82};
  engine.setBandConfiguration(4, replacement);
  requested.bands[4] = replacement;

  return expectConfiguration("configuration after band update", engine.configuration(), requested);
}

bool testSingleBandMatchesDelayBand()
{
  constexpr std::size_t numSamples = 12;
  constexpr std::array<double, numSamples> input{1.0, -0.5, 0.25, 0.0, 0.0, 0.0,
                                                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  dsp::HoldsworthDelayConfiguration configuration;
  configureBand(configuration.bands[2], 1.5, 0.5, 0.6, 0.2);

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
  referenceBand.setEnabled(true);
  std::array<double, numSamples> referenceLeft{};
  std::array<double, numSamples> referenceRight{};
  referenceBand.processBlock(input, referenceLeft, referenceRight);

  return expectSamples("single-band engine left", engineLeft, referenceLeft)
         && expectSamples("single-band engine right", engineRight, referenceRight);
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
  auto configuration = makeExerciseConfiguration();
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

bool testBlockPartitioningDoesNotChangeOutput()
{
  constexpr std::size_t numSamples = 57;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = (static_cast<double>((i * 11) % 19) - 9.0) / 9.0;

  const auto configuration = makeExerciseConfiguration();

  dsp::HoldsworthDelayEngine wholeEngine(20.0);
  wholeEngine.prepare(1000.0, numSamples);
  wholeEngine.applyConfiguration(configuration);
  std::array<double, numSamples> wholeLeft{};
  std::array<double, numSamples> wholeRight{};
  wholeEngine.processBlock(input, wholeLeft, wholeRight);

  dsp::HoldsworthDelayEngine partitionedEngine(20.0);
  partitionedEngine.prepare(1000.0, numSamples);
  partitionedEngine.applyConfiguration(configuration);
  std::array<double, numSamples> partitionedLeft{};
  std::array<double, numSamples> partitionedRight{};
  constexpr std::array<std::size_t, 9> blockSizes{1, 4, 2, 7, 3, 11, 5, 9, 15};
  std::size_t offset = 0;
  for (const std::size_t blockSize : blockSizes)
  {
    partitionedEngine.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                                   std::span<double>(partitionedLeft).subspan(offset, blockSize),
                                   std::span<double>(partitionedRight).subspan(offset, blockSize));
    offset += blockSize;
  }

  return expectSamples("engine left block partitioning", partitionedLeft, wholeLeft)
         && expectSamples("engine right block partitioning", partitionedRight, wholeRight);
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

  auto configuration = makeExerciseConfiguration();
  for (std::size_t i = 0; i < configuration.bands.size(); ++i)
    configuration.bands[i].delayTimeMs = 29.7 + 67.125 * static_cast<double>(i);
  engine.applyConfiguration(configuration);

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
      || !expectNear("Lead 121 required maximum delay", preset.requiredMaximumDelayTimeMs, 461.0)
      || !expectNear("Lead 121 global wet level", preset.dspConfiguration.globalWetOutputLevel, 1.0))
  {
    std::cerr << "Lead 121 preset metadata mismatch\n";
    return false;
  }

  for (std::size_t i = 0; i < expectedDelayTimes.size(); ++i)
  {
    const auto& band = preset.dspConfiguration.bands[i];
    const auto& source = preset.documentedYamahaValues[i].feedbackControlValue;
    if (!source.has_value()
        || !nearlyEqual(source->value, expectedYamahaFeedback[i])
        || !nearlyEqual(band.delayTimeMs, expectedDelayTimes[i])
        || !nearlyEqual(band.feedback.value, expectedDspFeedback[i])
        || !nearlyEqual(band.pan, expectedPan[i])
        || !nearlyEqual(band.outputLevel, expectedLevel[i])
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

constexpr std::array kTests{
  TestCase{"HoldsworthDelayEngine: configuration preserves requested delay times",
           testConfigurationReturnsRequestedDelayTimes},
  TestCase{"HoldsworthDelayEngine: one active band matches DelayBand", testSingleBandMatchesDelayBand},
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
  TestCase{"HoldsworthDelayEngine: block partitioning is invariant",
           testBlockPartitioningDoesNotChangeOutput},
  TestCase{"HoldsworthDelayEngine: exact input/output aliasing", testExactInputOutputAliasing},
  TestCase{"HoldsworthDelayEngine: global wet clamps and sanitizes non-finite values",
           testGlobalWetLevelClampsAndSanitizes},
  TestCase{"HoldsworthDelayEngine: processing performs no allocations", testProcessingDoesNotAllocate},
  TestCase{"HoldsworthDelayEngine: Lead 121 source and DSP values remain separate",
           testLead121PresetDefinition},
};

} // namespace

TestSuite holdsworthDelayEngineTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
