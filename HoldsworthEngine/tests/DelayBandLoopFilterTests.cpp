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
#include <span>

namespace holdsworth::test
{
namespace
{

dsp::DelayLoopFilterConfiguration highCutConfiguration(const double cutoffHz)
{
  dsp::DelayLoopFilterConfiguration configuration;
  configuration.highCut = dsp::HighCutFrequencyHz{cutoffHz};
  return configuration;
}

dsp::DelayLoopFilterConfiguration bothFiltersConfiguration(const double lowCutHz,
                                                            const double highCutHz)
{
  dsp::DelayLoopFilterConfiguration configuration;
  configuration.lowCut = dsp::LowCutFrequencyHz{lowCutHz};
  configuration.highCut = dsp::HighCutFrequencyHz{highCutHz};
  return configuration;
}

void configureFilteredBand(dsp::DelayBand& band) noexcept
{
  band.setDelayTimeMs(4.25);
  band.setFeedbackCoefficient(0.63);
  band.setOutputLevel(0.72);
  band.setPan(-1.0);
  band.setModulationRate(dsp::ModulationRateHz{0.41});
  band.setModulationDepth(dsp::ModulationDepthMs{0.6});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.17});
  band.setLoopFilterConfiguration(bothFiltersConfiguration(65.0, 310.0));
}

bool testBothFiltersOffStaticPathIsBitExactWithLegacyRecurrence()
{
  constexpr double sampleRate = 1000.0;
  constexpr double delayTimeMs = 2.25;
  constexpr double feedback = 0.6;
  constexpr double outputLevel = 0.75;
  constexpr std::size_t numSamples = 37;

  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 11) % 23) - 11) * 0.125;

  dsp::FractionalDelayLine legacyDelay(20.0);
  legacyDelay.prepare(sampleRate, numSamples);
  legacyDelay.setDelayTimeMs(delayTimeMs);
  std::array<double, numSamples> expectedLeft{};
  std::array<double, numSamples> expectedRight{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    const double delayed = legacyDelay.readDelayedSample();
    legacyDelay.pushSample(input[frame] + feedback * delayed);
    expectedLeft[frame] = delayed * outputLevel;
    expectedRight[frame] = delayed * 0.0;
  }

  dsp::DelayBand band(20.0);
  band.setDelayTimeMs(delayTimeMs);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(outputLevel);
  band.setPan(-1.0);
  band.prepare(sampleRate, numSamples);
  std::array<double, numSamples> actualLeft{};
  std::array<double, numSamples> actualRight{};
  band.processBlock(input, actualLeft, actualRight);

  return expectSamplesBitExact("filter-OFF legacy static left", actualLeft, expectedLeft)
         && expectSamplesBitExact("filter-OFF legacy static right", actualRight, expectedRight);
}

bool testBothFiltersOffMovingPathIsBitExactWithLegacyRecurrence()
{
  constexpr double sampleRate = 1000.0;
  constexpr double baseDelayTimeMs = 4.0;
  constexpr double feedback = 0.55;
  constexpr double outputLevel = 0.625;
  constexpr double modulationRateHz = 0.73;
  constexpr double modulationDepthMs = 0.5;
  constexpr double modulationPhase = 0.21;
  constexpr std::size_t numSamples = 61;

  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 5) % 17) - 8) * 0.0625;

  dsp::FractionalDelayLine legacyDelay(20.0);
  legacyDelay.prepare(sampleRate, numSamples);
  legacyDelay.setDelayTimeMs(baseDelayTimeMs);
  dsp::DelayModulator legacyModulator;
  legacyModulator.setRate(dsp::ModulationRateHz{modulationRateHz});
  legacyModulator.setDepth(dsp::ModulationDepthMs{modulationDepthMs});
  legacyModulator.setPhase(dsp::ModulationPhaseCycles{modulationPhase});
  legacyModulator.prepare(sampleRate);

  std::array<double, numSamples> expectedLeft{};
  std::array<double, numSamples> expectedRight{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    const double movingDelayTimeMs = std::clamp(
      baseDelayTimeMs + legacyModulator.nextOffsetMs(), 1.0, 20.0);
    const double delayed = legacyDelay.readDelayedSampleAtDelayTimeMs(movingDelayTimeMs);
    legacyDelay.pushSample(input[frame] + feedback * delayed);
    expectedLeft[frame] = delayed * outputLevel;
    expectedRight[frame] = delayed * 0.0;
  }

  dsp::DelayBand band(20.0);
  band.setDelayTimeMs(baseDelayTimeMs);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(outputLevel);
  band.setPan(-1.0);
  band.setModulationRate(dsp::ModulationRateHz{modulationRateHz});
  band.setModulationDepth(dsp::ModulationDepthMs{modulationDepthMs});
  band.setModulationPhase(dsp::ModulationPhaseCycles{modulationPhase});
  band.prepare(sampleRate, numSamples);
  std::array<double, numSamples> actualLeft{};
  std::array<double, numSamples> actualRight{};
  band.processBlock(input, actualLeft, actualRight);

  return expectSamplesBitExact("filter-OFF legacy moving left", actualLeft, expectedLeft)
         && expectSamplesBitExact("filter-OFF legacy moving right", actualRight, expectedRight);
}

bool testFirstEchoAndFeedbackUseFilteredDelayedSample()
{
  constexpr double sampleRate = 1000.0;
  constexpr double feedback = 0.5;
  constexpr std::size_t numSamples = 12;
  const std::array<double, numSamples> input{1.0};

  dsp::DelayBand band(20.0);
  band.setDelayTimeMs(1.0);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
  band.setLoopFilterConfiguration(highCutConfiguration(100.0));
  band.prepare(sampleRate, numSamples);
  std::array<double, numSamples> actualLeft{};
  std::array<double, numSamples> actualRight{};
  band.processBlock(input, actualLeft, actualRight);

  dsp::FractionalDelayLine referenceDelay(20.0);
  referenceDelay.prepare(sampleRate, numSamples);
  referenceDelay.setDelayTimeMs(1.0);
  dsp::DelayLoopFilter referenceFilter;
  referenceFilter.setConfiguration(highCutConfiguration(100.0));
  referenceFilter.prepare(sampleRate);
  std::array<double, numSamples> expected{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    const double rawDelayed = referenceDelay.readDelayedSample();
    const double filteredDelayed = referenceFilter.processSample(rawDelayed);
    referenceDelay.pushSample(input[frame] + feedback * filteredDelayed);
    expected[frame] = filteredDelayed;
  }

  if (actualLeft[1] == 1.0)
  {
    std::cerr << "first delayed signal was emitted without filtering\n";
    return false;
  }

  // A raw-feedback implementation would write feedback * 1.0 at the first
  // repeat instead of feedback * filteredDelayed. The third output sample
  // therefore provides a direct topology discriminator.
  dsp::FractionalDelayLine wrongDelay(20.0);
  wrongDelay.prepare(sampleRate, numSamples);
  wrongDelay.setDelayTimeMs(1.0);
  dsp::DelayLoopFilter wrongFilter;
  wrongFilter.setConfiguration(highCutConfiguration(100.0));
  wrongFilter.prepare(sampleRate);
  std::array<double, numSamples> wrongRawFeedback{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    const double rawDelayed = wrongDelay.readDelayedSample();
    wrongRawFeedback[frame] = wrongFilter.processSample(rawDelayed);
    wrongDelay.pushSample(input[frame] + feedback * rawDelayed);
  }

  return expectSamples("filtered feedback recurrence", actualLeft, expected, 0.0)
         && expectSamples("hard-left filtered right", actualRight, std::array<double, numSamples>{}, 0.0)
         && actualLeft[2] != wrongRawFeedback[2];
}

bool testFeedbackRepeatsAccumulateFiltering()
{
  constexpr double sampleRate = 8000.0;
  constexpr std::size_t delaySamples = 64;
  constexpr std::size_t burstSize = 16;
  constexpr std::size_t numSamples = 4 * delaySamples;
  constexpr double feedback = 0.8;

  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < burstSize; ++frame)
    input[frame] = frame % 2 == 0 ? 1.0 : -1.0;

  dsp::DelayBand band(100.0);
  band.setDelayTimeMs(1000.0 * static_cast<double>(delaySamples) / sampleRate);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
  band.setLoopFilterConfiguration(highCutConfiguration(500.0));
  band.prepare(sampleRate, numSamples);
  std::array<double, numSamples> left{};
  std::array<double, numSamples> right{};
  band.processBlock(input, left, right);

  auto repeatEnergy = [&](const std::size_t repeatIndex) {
    double energy = 0.0;
    const std::size_t start = repeatIndex * delaySamples;
    for (std::size_t frame = 0; frame < burstSize; ++frame)
      energy += left[start + frame] * left[start + frame];
    const double feedbackEnergy = std::pow(feedback, 2.0 * static_cast<double>(repeatIndex - 1));
    return energy / feedbackEnergy;
  };

  const double firstPassEnergy = repeatEnergy(1);
  const double secondPassEnergy = repeatEnergy(2);
  const double thirdPassEnergy = repeatEnergy(3);
  if (!(firstPassEnergy > secondPassEnergy && secondPassEnergy > thirdPassEnergy))
  {
    std::cerr << "normalized repeat energy did not decrease across successive filter passes\n";
    return false;
  }
  return true;
}

bool testOutputLevelAndPanRemainOutsideFilteredRecurrence()
{
  constexpr std::size_t firstBlockSize = 9;
  constexpr std::size_t tailSize = 23;
  const std::array<double, firstBlockSize> impulse{1.0};
  const std::array<double, tailSize> silence{};

  auto configure = [](dsp::DelayBand& band) {
    band.setDelayTimeMs(2.0);
    band.setFeedbackCoefficient(0.72);
    band.setLoopFilterConfiguration(bothFiltersConfiguration(45.0, 260.0));
    band.prepare(1000.0, tailSize);
  };

  dsp::DelayBand reference(20.0);
  configure(reference);
  reference.setOutputLevel(1.0);
  reference.setPan(-1.0);

  dsp::DelayBand changedOutput(20.0);
  configure(changedOutput);
  changedOutput.setOutputLevel(0.25);
  changedOutput.setPan(1.0);

  std::array<double, firstBlockSize> referenceFirstLeft{};
  std::array<double, firstBlockSize> referenceFirstRight{};
  std::array<double, firstBlockSize> changedFirstLeft{};
  std::array<double, firstBlockSize> changedFirstRight{};
  reference.processBlock(impulse, referenceFirstLeft, referenceFirstRight);
  changedOutput.processBlock(impulse, changedFirstLeft, changedFirstRight);

  changedOutput.setOutputLevel(1.0);
  changedOutput.setPan(-1.0);
  std::array<double, tailSize> referenceTailLeft{};
  std::array<double, tailSize> referenceTailRight{};
  std::array<double, tailSize> changedTailLeft{};
  std::array<double, tailSize> changedTailRight{};
  reference.processBlock(silence, referenceTailLeft, referenceTailRight);
  changedOutput.processBlock(silence, changedTailLeft, changedTailRight);

  for (std::size_t frame = 0; frame < firstBlockSize; ++frame)
  {
    if (!nearlyEqual(changedFirstRight[frame], 0.25 * referenceFirstLeft[frame], 0.0)
        || changedFirstLeft[frame] != 0.0)
    {
      std::cerr << "temporary level/pan did not affect only the observed wet output\n";
      return false;
    }
  }

  return expectSamplesBitExact("level/pan-independent filtered tail left",
                               changedTailLeft,
                               referenceTailLeft)
         && expectSamplesBitExact("level/pan-independent filtered tail right",
                                  changedTailRight,
                                  referenceTailRight);
}

bool testResetAndBlockPartitioningAreDeterministic()
{
  constexpr std::size_t numSamples = 53;
  std::array<double, numSamples> input{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
    input[frame] = static_cast<double>(static_cast<int>((frame * 13) % 29) - 14) * 0.03125;

  dsp::DelayBand whole(20.0);
  configureFilteredBand(whole);
  whole.prepare(1000.0, numSamples);
  std::array<double, numSamples> wholeLeft{};
  std::array<double, numSamples> wholeRight{};
  whole.processBlock(input, wholeLeft, wholeRight);

  whole.reset();
  std::array<double, numSamples> resetLeft{};
  std::array<double, numSamples> resetRight{};
  whole.processBlock(input, resetLeft, resetRight);

  dsp::DelayBand partitioned(20.0);
  configureFilteredBand(partitioned);
  partitioned.prepare(1000.0, numSamples);
  std::array<double, numSamples> partitionedLeft{};
  std::array<double, numSamples> partitionedRight{};
  constexpr std::array<std::size_t, 8> blockSizes{1, 8, 3, 11, 2, 13, 7, 8};
  std::size_t offset = 0;
  for (const std::size_t blockSize : blockSizes)
  {
    partitioned.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                             std::span<double>(partitionedLeft).subspan(offset, blockSize),
                             std::span<double>(partitionedRight).subspan(offset, blockSize));
    offset += blockSize;
  }

  return expectSamplesBitExact("filtered reset deterministic left", resetLeft, wholeLeft)
         && expectSamplesBitExact("filtered reset deterministic right", resetRight, wholeRight)
         && expectSamplesBitExact("filtered partition left", partitionedLeft, wholeLeft)
         && expectSamplesBitExact("filtered partition right", partitionedRight, wholeRight);
}

bool testFilteredProcessingDoesNotAllocate()
{
  constexpr std::size_t blockSize = 256;
  dsp::DelayBand band(700.0);
  configureFilteredBand(band);
  band.prepare(48000.0, blockSize);
  std::array<double, blockSize> input{};
  std::array<double, blockSize> left{};
  std::array<double, blockSize> right{};

  beginAllocationTracking();
  band.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();
  if (allocations != 0)
  {
    std::cerr << "real-time allocation: filtered DelayBand made " << allocations << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"DelayBand loop filter: OFF static path is bit-exact legacy processing",
           testBothFiltersOffStaticPathIsBitExactWithLegacyRecurrence},
  TestCase{"DelayBand loop filter: OFF moving path is bit-exact legacy processing",
           testBothFiltersOffMovingPathIsBitExactWithLegacyRecurrence},
  TestCase{"DelayBand loop filter: first echo and feedback use filtered delay",
           testFirstEchoAndFeedbackUseFilteredDelayedSample},
  TestCase{"DelayBand loop filter: feedback repeats accumulate filtering",
           testFeedbackRepeatsAccumulateFiltering},
  TestCase{"DelayBand loop filter: output level and pan remain outside recurrence",
           testOutputLevelAndPanRemainOutsideFilteredRecurrence},
  TestCase{"DelayBand loop filter: reset and partitioning are deterministic",
           testResetAndBlockPartitioningAreDeterministic},
  TestCase{"DelayBand loop filter: processing performs no allocations",
           testFilteredProcessingDoesNotAllocate},
};

} // namespace

TestSuite delayBandLoopFilterTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
