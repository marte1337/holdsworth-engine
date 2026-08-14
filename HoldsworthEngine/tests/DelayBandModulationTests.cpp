#include "../dsp/DelayBand.h"
#include "../dsp/FractionalDelayLine.h"
#include "TestHarness.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <span>

namespace holdsworth::test
{
namespace
{

void configureHardLeft(dsp::DelayBand& band) noexcept
{
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
}

bool testZeroDepthIsBitExactWithLegacyFeedbackAndPartitioning()
{
  constexpr double sampleRate = 1000.0;
  constexpr double delayTimeMs = 2.25;
  constexpr double feedback = 0.6;
  constexpr double outputLevel = 0.73;
  constexpr std::size_t numSamples = 31;

  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<double>(static_cast<int>((i * 7) % 19) - 9) * 0.125;

  // Recreate DelayBand's pre-modulation feedback topology directly from the
  // unchanged delay primitive. This makes the comparison independent of the
  // new moving-read implementation.
  dsp::FractionalDelayLine legacyDelayLine(20.0);
  legacyDelayLine.prepare(sampleRate, numSamples);
  legacyDelayLine.setDelayTimeMs(delayTimeMs);
  std::array<double, numSamples> expectedLeft{};
  std::array<double, numSamples> expectedRight{};
  for (std::size_t i = 0; i < input.size(); ++i)
  {
    const double delayed = legacyDelayLine.readDelayedSample();
    legacyDelayLine.pushSample(input[i] + feedback * delayed);
    expectedLeft[i] = delayed * outputLevel;
  }

  dsp::DelayBand band(20.0);
  band.setDelayTimeMs(delayTimeMs);
  band.setFeedbackCoefficient(feedback);
  band.setOutputLevel(outputLevel);
  band.setPan(-1.0);
  band.setModulationRate(dsp::ModulationRateHz{17.0});
  band.setModulationDepth(dsp::ModulationDepthMs{0.0});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.37});
  band.prepare(sampleRate, 9);

  std::array<double, numSamples> actualLeft{};
  std::array<double, numSamples> actualRight{};
  constexpr std::array<std::size_t, 6> blockSizes{1, 9, 3, 7, 5, 6};
  std::size_t offset = 0;
  for (const std::size_t blockSize : blockSizes)
  {
    band.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                      std::span<double>(actualLeft).subspan(offset, blockSize),
                      std::span<double>(actualRight).subspan(offset, blockSize));
    offset += blockSize;
  }

  return expectSamples("zero-depth legacy left", actualLeft, expectedLeft, 0.0)
         && expectSamples("zero-depth legacy right", actualRight, expectedRight, 0.0)
         && expectNear("zero-depth LFO still advances", band.modulationPhase().value, 0.897);
}

bool testZeroRateAndFixedPhaseProduceStaticOffset()
{
  constexpr std::array<double, 12> input{
    1.0, -0.5, 0.25, 0.0, 0.75, -0.25, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  dsp::DelayBand modulatedBand(12.0);
  modulatedBand.setDelayTimeMs(3.0);
  modulatedBand.setFeedbackCoefficient(0.35);
  modulatedBand.setModulationRate(dsp::ModulationRateHz{0.0});
  modulatedBand.setModulationDepth(dsp::ModulationDepthMs{1.0});
  modulatedBand.setModulationPhase(dsp::ModulationPhaseCycles{0.25});
  configureHardLeft(modulatedBand);
  modulatedBand.prepare(1000.0, input.size());

  dsp::DelayBand staticBand(12.0);
  staticBand.setDelayTimeMs(4.0);
  staticBand.setFeedbackCoefficient(0.35);
  configureHardLeft(staticBand);
  staticBand.prepare(1000.0, input.size());

  std::array<double, input.size()> modulatedLeft{};
  std::array<double, input.size()> modulatedRight{};
  std::array<double, input.size()> staticLeft{};
  std::array<double, input.size()> staticRight{};
  modulatedBand.processBlock(input, modulatedLeft, modulatedRight);
  staticBand.processBlock(input, staticLeft, staticRight);

  return expectSamples("fixed-phase modulated left", modulatedLeft, staticLeft, 0.0)
         && expectSamples("fixed-phase modulated right", modulatedRight, staticRight, 0.0)
         && expectNear("fixed-phase current delay", modulatedBand.currentModulatedDelayTimeMs(), 4.0)
         && expectNear("zero-rate phase remains fixed", modulatedBand.modulationPhase().value, 0.25);
}

bool testModulationAdvancesPerSampleRatherThanPerBlock()
{
  dsp::DelayBand band(10.0);
  band.setDelayTimeMs(3.0);
  band.setModulationRate(dsp::ModulationRateHz{250.0});
  band.setModulationDepth(dsp::ModulationDepthMs{0.5});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.0});
  configureHardLeft(band);
  band.prepare(1000.0, 8);

  // The LFO delay sequence is 3, 3.5, 3, 2.5, ... samples. Besides proving
  // sample-rate movement, this explicitly exercises the interpolated moving
  // read rather than only hopping among integer history positions.
  const std::array<double, 8> input{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
  const std::array<double, 8> expectedLeft{0.0, 0.0, 0.0, 1.5, 2.0, 2.5, 4.0, 5.5};
  const std::array<double, 8> expectedRight{};
  std::array<double, 8> left{};
  std::array<double, 8> right{};
  band.processBlock(input, left, right);

  return expectSamples("sample-rate modulation left", left, expectedLeft)
         && expectSamples("sample-rate modulation right", right, expectedRight)
         && expectNear("sample-rate modulation phase", band.modulationPhase().value, 0.0)
         && expectNear("last applied moving delay", band.currentModulatedDelayTimeMs(), 2.5);
}

bool testMovingReadFeedbackHasExpectedRecurrence()
{
  dsp::DelayBand band(10.0);
  band.setDelayTimeMs(2.0);
  band.setFeedbackCoefficient(0.5);
  band.setModulationRate(dsp::ModulationRateHz{250.0});
  band.setModulationDepth(dsp::ModulationDepthMs{1.0});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.0});
  configureHardLeft(band);
  band.prepare(1000.0, 8);

  // Effective delays are 2, 3, 2, 1, ... samples. The delayed value is read
  // first, then input + feedback * delayed is pushed exactly once.
  const std::array<double, 8> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  const std::array<double, 8> expectedLeft{0.0, 0.0, 1.0, 0.5, 0.5, 0.5, 0.25, 0.125};
  std::array<double, 8> left{};
  std::array<double, 8> right{};
  band.processBlock(input, left, right);

  return expectSamples("moving-read feedback recurrence", left, expectedLeft);
}

bool testDepthIsLimitedSymmetricallyAtDelayBoundaries()
{
  dsp::DelayBand nearMinimum(10.0);
  nearMinimum.setDelayTimeMs(1.25);
  nearMinimum.setModulationRate(dsp::ModulationRateHz{0.0});
  nearMinimum.setModulationDepth(dsp::ModulationDepthMs{10.0});
  nearMinimum.setModulationPhase(dsp::ModulationPhaseCycles{0.75});
  nearMinimum.prepare(1000.0, 1);

  dsp::DelayBand nearMaximum(10.0);
  nearMaximum.setDelayTimeMs(9.75);
  nearMaximum.setModulationRate(dsp::ModulationRateHz{0.0});
  nearMaximum.setModulationDepth(dsp::ModulationDepthMs{10.0});
  nearMaximum.setModulationPhase(dsp::ModulationPhaseCycles{0.25});
  nearMaximum.prepare(1000.0, 1);

  dsp::DelayBand centered(10.0);
  centered.setDelayTimeMs(5.0);
  centered.setModulationRate(dsp::ModulationRateHz{0.0});
  centered.setModulationDepth(dsp::ModulationDepthMs{10.0});
  centered.setModulationPhase(dsp::ModulationPhaseCycles{0.25});
  centered.prepare(1000.0, 1);

  const std::array<double, 1> silence{};
  std::array<double, 1> left{};
  std::array<double, 1> right{};
  nearMinimum.processBlock(silence, left, right);
  const double minimumInstantaneousDelay = nearMinimum.currentModulatedDelayTimeMs();
  nearMaximum.processBlock(silence, left, right);
  const double maximumInstantaneousDelay = nearMaximum.currentModulatedDelayTimeMs();
  centered.processBlock(silence, left, right);

  return expectNear("minimum-bound requested depth",
                    nearMinimum.requestedModulationDepth().value,
                    10.0)
         && expectNear("minimum-bound effective depth",
                       nearMinimum.effectiveModulationDepth().value,
                       0.25)
         && expectNear("minimum-bound instantaneous delay", minimumInstantaneousDelay, 1.0)
         && expectNear("maximum-bound requested depth",
                       nearMaximum.requestedModulationDepth().value,
                       10.0)
         && expectNear("maximum-bound effective depth",
                       nearMaximum.effectiveModulationDepth().value,
                       0.25)
         && expectNear("maximum-bound instantaneous delay", maximumInstantaneousDelay, 10.0)
         && expectNear("centered effective depth", centered.effectiveModulationDepth().value, 4.0)
         && expectNear("centered instantaneous delay", centered.currentModulatedDelayTimeMs(), 9.0);
}

void configurePartitionBand(dsp::DelayBand& band, const std::size_t maximumBlockSize)
{
  band.setDelayTimeMs(5.4);
  band.setFeedbackCoefficient(0.62);
  band.setOutputLevel(0.81);
  band.setPan(0.2);
  band.setModulationRate(dsp::ModulationRateHz{27.5});
  band.setModulationDepth(dsp::ModulationDepthMs{1.3});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.137});
  band.prepare(1000.0, maximumBlockSize);
}

bool testModulatedOutputIsBlockPartitionInvariant()
{
  constexpr std::size_t numSamples = 47;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<double>(static_cast<int>((i * 11) % 23) - 11) * 0.0625;

  dsp::DelayBand wholeBand(20.0);
  configurePartitionBand(wholeBand, numSamples);
  std::array<double, numSamples> wholeLeft{};
  std::array<double, numSamples> wholeRight{};
  wholeBand.processBlock(input, wholeLeft, wholeRight);

  dsp::DelayBand partitionedBand(20.0);
  configurePartitionBand(partitionedBand, 11);
  std::array<double, numSamples> partitionedLeft{};
  std::array<double, numSamples> partitionedRight{};
  constexpr std::array<std::size_t, 8> blockSizes{1, 6, 3, 11, 2, 9, 8, 7};
  std::size_t offset = 0;
  for (const std::size_t blockSize : blockSizes)
  {
    partitionedBand.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                                 std::span<double>(partitionedLeft).subspan(offset, blockSize),
                                 std::span<double>(partitionedRight).subspan(offset, blockSize));
    offset += blockSize;
  }

  return expectSamples("modulated left block partitioning", partitionedLeft, wholeLeft, 0.0)
         && expectSamples("modulated right block partitioning", partitionedRight, wholeRight, 0.0)
         && expectNear("modulated partition phase",
                       partitionedBand.modulationPhase().value,
                       wholeBand.modulationPhase().value,
                       0.0);
}

bool testResetRestoresHistoryAndConfiguredLfoPhase()
{
  constexpr std::size_t numSamples = 48;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = std::sin(static_cast<double>(i) * 0.31) * 0.5;

  dsp::DelayBand band(20.0);
  band.setDelayTimeMs(5.3);
  band.setFeedbackCoefficient(0.72);
  band.setOutputLevel(0.83);
  band.setPan(-0.15);
  band.setModulationRate(dsp::ModulationRateHz{41.25});
  band.setModulationDepth(dsp::ModulationDepthMs{1.7});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.213});
  band.prepare(1000.0, numSamples);

  std::array<double, numSamples> firstLeft{};
  std::array<double, numSamples> firstRight{};
  std::array<double, numSamples> secondLeft{};
  std::array<double, numSamples> secondRight{};
  band.processBlock(input, firstLeft, firstRight);
  band.reset();

  if (!expectNear("reset configured LFO phase", band.modulationPhase().value, 0.213))
    return false;

  band.processBlock(input, secondLeft, secondRight);
  return expectSamples("modulated reset left", secondLeft, firstLeft, 0.0)
         && expectSamples("modulated reset right", secondRight, firstRight, 0.0);
}

void configureDisabledReferenceBand(dsp::DelayBand& band)
{
  band.setDelayTimeMs(2.5);
  band.setFeedbackCoefficient(0.7);
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
  band.setModulationRate(dsp::ModulationRateHz{125.0});
  band.setModulationDepth(dsp::ModulationDepthMs{1.0});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.1});
  band.prepare(1000.0, 16);
}

bool testDisabledStateAdvancesLfoAndTailButRejectsInput()
{
  dsp::DelayBand disabledBand(12.0);
  dsp::DelayBand silenceReference(12.0);
  configureDisabledReferenceBand(disabledBand);
  configureDisabledReferenceBand(silenceReference);

  const std::array<double, 1> impulse{1.0};
  std::array<double, 1> discardedLeft{};
  std::array<double, 1> discardedRight{};
  disabledBand.processBlock(impulse, discardedLeft, discardedRight);
  silenceReference.processBlock(impulse, discardedLeft, discardedRight);

  disabledBand.setEnabled(false);
  const std::array<double, 7> rejectedInput{9.0, -8.0, 7.0, -6.0, 5.0, -4.0, 3.0};
  const std::array<double, 7> silence{};
  std::array<double, 7> disabledLeft{};
  std::array<double, 7> disabledRight{};
  std::array<double, 7> referenceLeft{};
  std::array<double, 7> referenceRight{};
  disabledBand.processBlock(rejectedInput, disabledLeft, disabledRight);
  silenceReference.processBlock(silence, referenceLeft, referenceRight);

  if (!expectSamples("disabled modulated left is silent", disabledLeft, silence, 0.0)
      || !expectSamples("disabled modulated right is silent", disabledRight, silence, 0.0)
      || !expectNear("disabled state advances LFO phase",
                     disabledBand.modulationPhase().value,
                     silenceReference.modulationPhase().value,
                     0.0))
    return false;

  disabledBand.setEnabled(true);
  const std::array<double, 16> futureSilence{};
  std::array<double, 16> actualLeft{};
  std::array<double, 16> actualRight{};
  std::array<double, 16> expectedLeft{};
  std::array<double, 16> expectedRight{};
  disabledBand.processBlock(futureSilence, actualLeft, actualRight);
  silenceReference.processBlock(futureSilence, expectedLeft, expectedRight);

  double recoveredTailMagnitude = 0.0;
  for (const double sample : expectedLeft)
    recoveredTailMagnitude += std::abs(sample);
  if (recoveredTailMagnitude == 0.0)
  {
    std::cerr << "disabled-tail reference produced no observable tail\n";
    return false;
  }

  return expectSamples("disabled state advances feedback tail", actualLeft, expectedLeft, 0.0)
         && expectSamples("disabled state gates new input", actualRight, expectedRight, 0.0);
}

bool testModulatedProcessingDoesNotAllocate()
{
  dsp::DelayBand band(700.0);
  band.setDelayTimeMs(137.25);
  band.setFeedbackCoefficient(0.75);
  band.setOutputLevel(0.8);
  band.setPan(0.3);
  band.setModulationRate(dsp::ModulationRateHz{0.73});
  band.setModulationDepth(dsp::ModulationDepthMs{4.0});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.19});
  band.prepare(48000.0, 256);

  std::array<double, 256> input{};
  std::array<double, 256> left{};
  std::array<double, 256> right{};

  beginAllocationTracking();
  band.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: modulated DelayBand::processBlock made " << allocations
              << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"DelayBand modulation: zero depth is bit-exact with legacy feedback",
           testZeroDepthIsBitExactWithLegacyFeedbackAndPartitioning},
  TestCase{"DelayBand modulation: zero rate and fixed phase produce a static offset",
           testZeroRateAndFixedPhaseProduceStaticOffset},
  TestCase{"DelayBand modulation: delay advances at sample rate", testModulationAdvancesPerSampleRatherThanPerBlock},
  TestCase{"DelayBand modulation: moving read has the expected feedback recurrence",
           testMovingReadFeedbackHasExpectedRecurrence},
  TestCase{"DelayBand modulation: depth is symmetric at delay boundaries",
           testDepthIsLimitedSymmetricallyAtDelayBoundaries},
  TestCase{"DelayBand modulation: block partitioning is invariant", testModulatedOutputIsBlockPartitionInvariant},
  TestCase{"DelayBand modulation: reset restores history and LFO phase",
           testResetRestoresHistoryAndConfiguredLfoPhase},
  TestCase{"DelayBand modulation: disabled state advances LFO and tail",
           testDisabledStateAdvancesLfoAndTailButRejectsInput},
  TestCase{"DelayBand modulation: processing performs no allocations", testModulatedProcessingDoesNotAllocate},
};

} // namespace

TestSuite delayBandModulationTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
