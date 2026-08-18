#include "../dsp/DelayBand.h"
#include "../dsp/DelayLoopFilter.h"
#include "../dsp/DelayModulator.h"
#include "../dsp/FractionalDelayLine.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <span>

namespace holdsworth::test
{
namespace
{

dsp::DelayLoopFilterConfiguration tapFilterConfiguration()
{
  dsp::DelayLoopFilterConfiguration configuration;
  configuration.lowCut = dsp::LowCutFrequencyHz{35.0};
  configuration.highCut = dsp::HighCutFrequencyHz{280.0};
  return configuration;
}

void configureTappedBand(dsp::DelayBand& band, const std::size_t maximumBlockSize)
{
  band.setDelayTimeMs(7.25);
  band.setFeedbackCoefficient(0.61);
  band.setOutputLevel(0.78);
  band.setPan(-0.2);
  band.setModulationRate(dsp::ModulationRateHz{19.0});
  band.setModulationDepth(dsp::ModulationDepthMs{1.1});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.17});
  band.setLoopFilterConfiguration(tapFilterConfiguration());
  band.setTapFraction(dsp::TapFraction{0.43});
  band.prepare(1000.0, maximumBlockSize);
}

bool testFullTapIsBitExactWithLegacyPath()
{
  constexpr std::size_t numSamples = 103;
  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 17) % 31) - 15) * 0.03125;

  auto configure = [](dsp::DelayBand& band) {
    band.setDelayTimeMs(8.4);
    band.setFeedbackCoefficient(0.67);
    band.setOutputLevel(0.73);
    band.setPan(0.31);
    band.setModulationRate(dsp::ModulationRateHz{23.0});
    band.setModulationDepth(dsp::ModulationDepthMs{1.25});
    band.setModulationPhase(dsp::ModulationPhaseCycles{0.19});
    band.setLoopFilterConfiguration(tapFilterConfiguration());
    band.prepare(1000.0, numSamples);
  };

  dsp::DelayBand legacyDefault(20.0);
  dsp::DelayBand explicitFullTap(20.0);
  configure(legacyDefault);
  configure(explicitFullTap);
  explicitFullTap.setTapFraction(dsp::TapFraction{1.0});

  std::array<double, numSamples> legacyLeft{};
  std::array<double, numSamples> legacyRight{};
  std::array<double, numSamples> tappedLeft{};
  std::array<double, numSamples> tappedRight{};
  legacyDefault.processBlock(input, legacyLeft, legacyRight);
  explicitFullTap.processBlock(input, tappedLeft, tappedRight);

  return expectSamplesBitExact("TAP 100 legacy left", tappedLeft, legacyLeft)
         && expectSamplesBitExact("TAP 100 legacy right", tappedRight, legacyRight);
}

bool testDocumentedFractionalTapTimingAndFeedbackSpacing()
{
  constexpr std::size_t numSamples = 1000;
  constexpr double tapFraction = 0.666;
  constexpr double feedback = 0.5;

  dsp::DelayBand band(400.0);
  band.setDelayTimeMs(360.0);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
  band.setTapFraction(dsp::TapFraction{tapFraction});
  band.prepare(1000.0, numSamples);

  const std::array<double, numSamples> input{1.0};
  std::array<double, numSamples> left{};
  std::array<double, numSamples> right{};
  band.processBlock(input, left, right);

  // 360 * 0.666 = 239.76 samples. Linear interpolation therefore places 24%
  // of each impulse at the newer neighbor and 76% at the older neighbor.
  const bool firstTapIsExact = expectNear("66.6% TAP first newer neighbor", left[239], 0.24)
                               && expectNear("66.6% TAP first older neighbor", left[240], 0.76);
  const bool repeatIsFullLoopLater =
    expectNear("66.6% TAP repeat newer neighbor", left[599], 0.12)
    && expectNear("66.6% TAP repeat older neighbor", left[600], 0.38);

  double unexpectedMagnitude = 0.0;
  for (std::size_t frame = 0; frame < left.size(); ++frame)
  {
    if (frame != 239 && frame != 240 && frame != 599 && frame != 600 && frame != 959
        && frame != 960)
      unexpectedMagnitude += std::abs(left[frame]);
  }

  return firstTapIsExact && repeatIsFullLoopLater
         && expectNear("66.6% TAP repeat spacing", 599.0 - 239.0, 360.0)
         && expectNear("66.6% TAP has no 240 ms feedback repeat", left[479], 0.0)
         && expectNear("66.6% TAP unexpected output", unexpectedMagnitude, 0.0)
         && expectSamples("66.6% TAP hard-left right", right,
                          std::array<double, numSamples>{}, 0.0);
}

bool testSubOneSampleTapReadsPendingInputBeforeSingleWrite()
{
  dsp::DelayBand band(10.0);
  band.setDelayTimeMs(2.0);
  band.setFeedbackCoefficient(0.5);
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
  band.setTapFraction(dsp::TapFraction{0.25}); // 0.5 sample at 1 kHz.
  band.prepare(1000.0, 4);

  const std::array<double, 4> input{1.0, 0.0, 0.0, 0.0};
  const std::array<double, 4> expectedLeft{0.5, 0.5, 0.25, 0.25};
  std::array<double, 4> left{};
  std::array<double, 4> right{};
  band.processBlock(input, left, right);

  // A push-before-TAP-read implementation would produce 1.0 at frame zero:
  // the expected 0.5 proves the tap interpolated the pending lineInput against
  // pre-write history, then the line advanced exactly once.
  return expectSamples("sub-one TAP pre-write ordering", left, expectedLeft, 0.0);
}

bool testTapDoesNotAlterFeedbackLoopRecurrence()
{
  constexpr std::size_t excitationSize = 31;
  constexpr std::size_t tailSize = 79;
  std::array<double, excitationSize> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 7) % 19) - 9) * 0.0625;

  auto configure = [](dsp::DelayBand& band, const double tapFraction) {
    band.setDelayTimeMs(6.25);
    band.setFeedbackCoefficient(0.72);
    band.setOutputLevel(1.0);
    band.setPan(-1.0);
    band.setLoopFilterConfiguration(tapFilterConfiguration());
    band.setTapFraction(dsp::TapFraction{tapFraction});
    band.prepare(1000.0, tailSize);
  };

  dsp::DelayBand fullTap(20.0);
  dsp::DelayBand earlyTap(20.0);
  configure(fullTap, 1.0);
  configure(earlyTap, 0.37);

  std::array<double, excitationSize> discardedLeft{};
  std::array<double, excitationSize> discardedRight{};
  fullTap.processBlock(input, discardedLeft, discardedRight);
  earlyTap.processBlock(input, discardedLeft, discardedRight);

  // The audible observations differed, but the full-loop delay, loop filter,
  // and feedback history must now be identical.
  earlyTap.setTapFraction(dsp::TapFraction{1.0});
  const std::array<double, tailSize> silence{};
  std::array<double, tailSize> fullLeft{};
  std::array<double, tailSize> fullRight{};
  std::array<double, tailSize> earlyLeft{};
  std::array<double, tailSize> earlyRight{};
  fullTap.processBlock(silence, fullLeft, fullRight);
  earlyTap.processBlock(silence, earlyLeft, earlyRight);

  return expectSamplesBitExact("TAP-independent feedback history left", earlyLeft, fullLeft)
         && expectSamplesBitExact("TAP-independent feedback history right", earlyRight, fullRight);
}

bool testTapOutputUsesIndependentIdenticallyConfiguredFilter()
{
  constexpr std::size_t numSamples = 47;
  constexpr double delayTimeMs = 4.25;
  constexpr double tapFraction = 0.5;
  constexpr double feedback = 0.58;
  const auto filterConfiguration = tapFilterConfiguration();

  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 5) % 13) - 6) * 0.125;

  dsp::FractionalDelayLine delay(20.0);
  delay.setDelayTimeMs(delayTimeMs);
  delay.prepare(1000.0, numSamples);
  dsp::DelayLoopFilter loopFilter;
  dsp::DelayLoopFilter tapOutputFilter;
  loopFilter.setConfiguration(filterConfiguration);
  tapOutputFilter.setConfiguration(filterConfiguration);
  loopFilter.prepare(1000.0);
  tapOutputFilter.prepare(1000.0);

  std::array<double, numSamples> expected{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    const double rawLoopDelayed = delay.readDelayedSample();
    const double filteredLoopDelayed = loopFilter.processSample(rawLoopDelayed);
    const double lineInput = input[frame] + feedback * filteredLoopDelayed;
    const double rawTapDelayed =
      delay.readDelayedSampleAtDelayTimeMs(tapFraction * delayTimeMs, lineInput);
    expected[frame] = tapOutputFilter.processSample(rawTapDelayed);
    delay.pushSample(lineInput);
  }

  dsp::DelayBand band(20.0);
  band.setDelayTimeMs(delayTimeMs);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
  band.setLoopFilterConfiguration(filterConfiguration);
  band.setTapFraction(dsp::TapFraction{tapFraction});
  band.prepare(1000.0, numSamples);
  std::array<double, numSamples> actualLeft{};
  std::array<double, numSamples> actualRight{};
  band.processBlock(input, actualLeft, actualRight);

  return expectSamplesBitExact("filtered TAP reference left", actualLeft, expected)
         && expectSamples("filtered TAP hard-left right", actualRight,
                          std::array<double, numSamples>{}, 0.0);
}

bool testModulatedTapMatchesProportionalReference()
{
  constexpr std::size_t numSamples = 73;
  constexpr double baseDelayTimeMs = 5.0;
  constexpr double feedback = 0.47;
  constexpr double tapFraction = 0.4;

  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 11) % 23) - 11) * 0.03125;

  dsp::FractionalDelayLine delay(20.0);
  delay.setDelayTimeMs(baseDelayTimeMs);
  delay.prepare(1000.0, numSamples);
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{37.0});
  modulator.setDepth(dsp::ModulationDepthMs{1.25});
  modulator.setPhase(dsp::ModulationPhaseCycles{0.13});
  modulator.prepare(1000.0);

  std::array<double, numSamples> expected{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    const double loopDelayTimeMs =
      std::clamp(baseDelayTimeMs + modulator.nextOffsetMs(), 1.0, 20.0);
    const double rawLoopDelayed = delay.readDelayedSampleAtDelayTimeMs(loopDelayTimeMs);
    const double lineInput = input[frame] + feedback * rawLoopDelayed;
    expected[frame] = delay.readDelayedSampleAtDelayTimeMs(
      tapFraction * loopDelayTimeMs, lineInput);
    delay.pushSample(lineInput);
  }

  dsp::DelayBand band(20.0);
  band.setDelayTimeMs(baseDelayTimeMs);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
  band.setModulationRate(dsp::ModulationRateHz{37.0});
  band.setModulationDepth(dsp::ModulationDepthMs{1.25});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.13});
  band.setTapFraction(dsp::TapFraction{tapFraction});
  band.prepare(1000.0, numSamples);
  std::array<double, numSamples> actualLeft{};
  std::array<double, numSamples> actualRight{};
  band.processBlock(input, actualLeft, actualRight);

  return expectSamplesBitExact("proportional modulated TAP", actualLeft, expected);
}

bool testOutputLevelAndPanRemainOutsideTappedRecurrence()
{
  constexpr std::size_t excitationSize = 29;
  constexpr std::size_t tailSize = 61;
  const std::array<double, excitationSize> input{1.0, -0.25, 0.5};

  dsp::DelayBand reference(20.0);
  dsp::DelayBand changedOutput(20.0);
  configureTappedBand(reference, tailSize);
  configureTappedBand(changedOutput, tailSize);
  reference.setOutputLevel(1.0);
  reference.setPan(-1.0);
  changedOutput.setOutputLevel(0.2);
  changedOutput.setPan(1.0);

  std::array<double, excitationSize> referenceLeft{};
  std::array<double, excitationSize> referenceRight{};
  std::array<double, excitationSize> changedLeft{};
  std::array<double, excitationSize> changedRight{};
  reference.processBlock(input, referenceLeft, referenceRight);
  changedOutput.processBlock(input, changedLeft, changedRight);

  changedOutput.setOutputLevel(1.0);
  changedOutput.setPan(-1.0);
  const std::array<double, tailSize> silence{};
  std::array<double, tailSize> referenceTailLeft{};
  std::array<double, tailSize> referenceTailRight{};
  std::array<double, tailSize> changedTailLeft{};
  std::array<double, tailSize> changedTailRight{};
  reference.processBlock(silence, referenceTailLeft, referenceTailRight);
  changedOutput.processBlock(silence, changedTailLeft, changedTailRight);

  return expectSamplesBitExact("TAP level/pan-independent tail left",
                               changedTailLeft, referenceTailLeft)
         && expectSamplesBitExact("TAP level/pan-independent tail right",
                                  changedTailRight, referenceTailRight);
}

bool testTapResetAndBlockPartitioningAreDeterministic()
{
  constexpr std::size_t numSamples = 97;
  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 13) % 29) - 14) * 0.03125;

  dsp::DelayBand whole(20.0);
  configureTappedBand(whole, numSamples);
  std::array<double, numSamples> wholeLeft{};
  std::array<double, numSamples> wholeRight{};
  whole.processBlock(input, wholeLeft, wholeRight);

  whole.reset();
  std::array<double, numSamples> resetLeft{};
  std::array<double, numSamples> resetRight{};
  whole.processBlock(input, resetLeft, resetRight);

  dsp::DelayBand partitioned(20.0);
  configureTappedBand(partitioned, 23);
  std::array<double, numSamples> partitionedLeft{};
  std::array<double, numSamples> partitionedRight{};
  constexpr std::array<std::size_t, 9> blockSizes{1, 13, 2, 17, 5, 11, 16, 9, 23};
  std::size_t offset = 0;
  for (const std::size_t blockSize : blockSizes)
  {
    partitioned.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                             std::span<double>(partitionedLeft).subspan(offset, blockSize),
                             std::span<double>(partitionedRight).subspan(offset, blockSize));
    offset += blockSize;
  }

  return expectSamplesBitExact("TAP reset deterministic left", resetLeft, wholeLeft)
         && expectSamplesBitExact("TAP reset deterministic right", resetRight, wholeRight)
         && expectSamplesBitExact("TAP partition left", partitionedLeft, wholeLeft)
         && expectSamplesBitExact("TAP partition right", partitionedRight, wholeRight);
}

bool testTapSanitizationAndProcessingDoNotAllocate()
{
  dsp::DelayBand band(700.0);
  band.setTapFraction(dsp::TapFraction{-1.0});
  if (!expectNear("negative TAP clamp", band.tapFraction().value, 0.0))
    return false;
  band.setTapFraction(dsp::TapFraction{2.0});
  if (!expectNear("maximum TAP clamp", band.tapFraction().value, 1.0))
    return false;
  band.setTapFraction(dsp::TapFraction{std::numeric_limits<double>::quiet_NaN()});
  if (!expectNear("non-finite TAP legacy fallback", band.tapFraction().value, 1.0))
    return false;

  configureTappedBand(band, 256);
  std::array<double, 256> input{};
  std::array<double, 256> left{};
  std::array<double, 256> right{};
  beginAllocationTracking();
  band.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();
  if (allocations != 0)
  {
    std::cerr << "real-time allocation: tapped DelayBand made " << allocations
              << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"DelayBand TAP: 100 percent is bit-exact legacy processing",
           testFullTapIsBitExactWithLegacyPath},
  TestCase{"DelayBand TAP: 66.6 percent interpolates at 240 ms and repeats at 360 ms",
           testDocumentedFractionalTapTimingAndFeedbackSpacing},
  TestCase{"DelayBand TAP: sub-one read observes pending input before one write",
           testSubOneSampleTapReadsPendingInputBeforeSingleWrite},
  TestCase{"DelayBand TAP: audible position does not alter feedback recurrence",
           testTapDoesNotAlterFeedbackLoopRecurrence},
  TestCase{"DelayBand TAP: output uses an independent identically configured filter",
           testTapOutputUsesIndependentIdenticallyConfiguredFilter},
  TestCase{"DelayBand TAP: modulation follows the proportional moving-loop reference",
           testModulatedTapMatchesProportionalReference},
  TestCase{"DelayBand TAP: output level and pan remain outside recurrence",
           testOutputLevelAndPanRemainOutsideTappedRecurrence},
  TestCase{"DelayBand TAP: reset and block partitioning are deterministic",
           testTapResetAndBlockPartitioningAreDeterministic},
  TestCase{"DelayBand TAP: values sanitize safely and processing does not allocate",
           testTapSanitizationAndProcessingDoNotAllocate},
};

} // namespace

TestSuite delayBandTapTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
