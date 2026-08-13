#include "../dsp/DelayBand.h"
#include "TestHarness.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<holdsworth::dsp::DelayBand>);
static_assert(!std::is_copy_assignable_v<holdsworth::dsp::DelayBand>);
static_assert(!std::is_move_constructible_v<holdsworth::dsp::DelayBand>);
static_assert(!std::is_move_assignable_v<holdsworth::dsp::DelayBand>);

namespace holdsworth::test
{
namespace
{

void configureHardLeft(dsp::DelayBand& band) noexcept
{
  band.setOutputLevel(1.0);
  band.setPan(-1.0);
}

bool testSubSampleDelayClampsToOneSample()
{
  constexpr double sampleRate = 48000.0;
  dsp::DelayBand band(10.0);
  band.setDelayTimeMs(0.005);
  band.prepare(sampleRate, 4);
  configureHardLeft(band);

  if (!expectNear("minimum delay in milliseconds", band.delayTimeMs(), 1000.0 / sampleRate))
    return false;

  const std::array<double, 4> input{1.0, 0.0, 0.0, 0.0};
  const std::array<double, 4> expectedLeft{0.0, 1.0, 0.0, 0.0};
  const std::array<double, 4> expectedRight{};
  std::array<double, 4> left{};
  std::array<double, 4> right{};
  band.processBlock(input, left, right);

  return expectSamples("minimum one-sample delay left", left, expectedLeft)
         && expectSamples("minimum one-sample delay right", right, expectedRight);
}

bool testMinimumDelayFeedbackIsStable()
{
  dsp::DelayBand band(10.0);
  band.setDelayTimeMs(0.0);
  band.setFeedbackCoefficient(1.0); // Clamped below unity.
  band.prepare(1000.0, 6);
  configureHardLeft(band);

  if (!expectNear("maximum feedback clamp",
                  band.feedbackCoefficient(),
                  dsp::DelayBand::kMaximumFeedbackCoefficient))
    return false;

  const std::array<double, 6> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  const std::array<double, 6> expected{0.0, 1.0, 0.99, 0.9801, 0.970299, 0.96059601};
  std::array<double, 6> left{};
  std::array<double, 6> right{};
  band.processBlock(input, left, right);

  for (std::size_t i = 1; i < left.size(); ++i)
  {
    if (!std::isfinite(left[i]) || (i > 1 && left[i] >= left[i - 1]))
    {
      std::cerr << "minimum-delay feedback did not remain finite and decaying at sample " << i << '\n';
      return false;
    }
  }
  return expectSamples("minimum-delay feedback", left, expected);
}

bool testIntegerFeedbackHasNoExtraLoopSample()
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 9);
  band.setDelayTimeMs(2.0);
  band.setFeedbackCoefficient(0.5);
  configureHardLeft(band);

  const std::array<double, 9> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  const std::array<double, 9> expected{0.0, 0.0, 1.0, 0.0, 0.5, 0.0, 0.25, 0.0, 0.125};
  std::array<double, 9> left{};
  std::array<double, 9> right{};
  band.processBlock(input, left, right);

  return expectSamples("integer feedback repeat positions", left, expected);
}

bool testFractionalDelayFeedback()
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 8);
  band.setDelayTimeMs(1.5);
  band.setFeedbackCoefficient(0.5);
  configureHardLeft(band);

  const std::array<double, 8> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  const std::array<double, 8> expected{
    0.0, 0.5, 0.625, 0.28125, 0.2265625, 0.126953125, 0.08837890625, 0.0538330078125};
  std::array<double, 8> left{};
  std::array<double, 8> right{};
  band.processBlock(input, left, right);

  return expectSamples("fractional feedback", left, expected);
}

bool testOutputLevelIsOutsideFeedbackLoop()
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 6);
  band.setDelayTimeMs(1.0);
  band.setFeedbackCoefficient(0.5);
  band.setOutputLevel(0.25);
  band.setPan(-1.0);

  const std::array<double, 6> input{1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  const std::array<double, 6> expected{0.0, 0.25, 0.125, 0.0625, 0.03125, 0.015625};
  std::array<double, 6> left{};
  std::array<double, 6> right{};
  band.processBlock(input, left, right);

  return expectSamples("output level outside feedback", left, expected);
}

void renderSingleEchoAtPan(const double pan, double& leftEcho, double& rightEcho)
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 2);
  band.setDelayTimeMs(1.0);
  band.setPan(pan);

  const std::array<double, 2> input{1.0, 0.0};
  std::array<double, 2> left{};
  std::array<double, 2> right{};
  band.processBlock(input, left, right);
  leftEcho = left[1];
  rightEcho = right[1];
}

bool testEqualPowerPanLaw()
{
  double hardLeftL = 0.0;
  double hardLeftR = 0.0;
  double centerL = 0.0;
  double centerR = 0.0;
  double hardRightL = 0.0;
  double hardRightR = 0.0;
  renderSingleEchoAtPan(-1.0, hardLeftL, hardLeftR);
  renderSingleEchoAtPan(0.0, centerL, centerR);
  renderSingleEchoAtPan(1.0, hardRightL, hardRightR);

  const double centerGain = std::sqrt(0.5);
  return expectNear("hard-left left gain", hardLeftL, 1.0)
         && expectNear("hard-left right gain", hardLeftR, 0.0)
         && expectNear("center left gain", centerL, centerGain)
         && expectNear("center right gain", centerR, centerGain)
         && expectNear("center total power", centerL * centerL + centerR * centerR, 1.0)
         && expectNear("hard-right left gain", hardRightL, 0.0)
         && expectNear("hard-right right gain", hardRightR, 1.0);
}

bool testDisabledBandGatesInputAndAdvancesTail()
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 3);
  band.setDelayTimeMs(2.0);
  band.setFeedbackCoefficient(0.5);
  configureHardLeft(band);

  const std::array<double, 1> impulse{1.0};
  std::array<double, 1> firstLeft{};
  std::array<double, 1> firstRight{};
  band.processBlock(impulse, firstLeft, firstRight);

  band.setEnabled(false);
  const std::array<double, 3> disabledInput{9.0, 9.0, 9.0};
  const std::array<double, 3> silence{};
  std::array<double, 3> disabledLeft{1.0, 1.0, 1.0};
  std::array<double, 3> disabledRight{1.0, 1.0, 1.0};
  band.processBlock(disabledInput, disabledLeft, disabledRight);

  band.setEnabled(true);
  const std::array<double, 3> enabledInput{};
  const std::array<double, 3> expectedTail{0.5, 0.0, 0.25};
  std::array<double, 3> enabledLeft{};
  std::array<double, 3> enabledRight{};
  band.processBlock(enabledInput, enabledLeft, enabledRight);

  return expectSamples("disabled left output", disabledLeft, silence)
         && expectSamples("disabled right output", disabledRight, silence)
         && expectSamples("advanced tail after re-enable", enabledLeft, expectedTail)
         && expectSamples("disabled input was not captured", enabledRight, silence);
}

bool testResetClearsFeedbackHistoryAndPreservesParameters()
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 5);
  band.setDelayTimeMs(2.0);
  band.setFeedbackCoefficient(0.75);
  band.setOutputLevel(0.4);
  band.setPan(-1.0);

  const std::array<double, 1> impulse{1.0};
  std::array<double, 1> discardedLeft{};
  std::array<double, 1> discardedRight{};
  band.processBlock(impulse, discardedLeft, discardedRight);
  band.reset();

  const std::array<double, 5> silence{};
  std::array<double, 5> left{};
  std::array<double, 5> right{};
  band.processBlock(silence, left, right);

  return expectSamples("reset left", left, silence) && expectSamples("reset right", right, silence)
         && expectNear("reset preserved delay", band.delayTimeMs(), 2.0)
         && expectNear("reset preserved feedback", band.feedbackCoefficient(), 0.75)
         && expectNear("reset preserved level", band.outputLevel(), 0.4)
         && expectNear("reset preserved pan", band.pan(), -1.0);
}

bool testBlockPartitioningDoesNotChangeOutput()
{
  constexpr std::size_t numSamples = 17;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<double>((i * 5) % 13) - 6.0;

  dsp::DelayBand wholeBand(20.0);
  wholeBand.prepare(1000.0, numSamples);
  wholeBand.setDelayTimeMs(2.25);
  wholeBand.setFeedbackCoefficient(0.6);
  wholeBand.setOutputLevel(0.73);
  wholeBand.setPan(0.2);
  std::array<double, numSamples> wholeLeft{};
  std::array<double, numSamples> wholeRight{};
  wholeBand.processBlock(input, wholeLeft, wholeRight);

  dsp::DelayBand partitionedBand(20.0);
  partitionedBand.prepare(1000.0, numSamples);
  partitionedBand.setDelayTimeMs(2.25);
  partitionedBand.setFeedbackCoefficient(0.6);
  partitionedBand.setOutputLevel(0.73);
  partitionedBand.setPan(0.2);
  std::array<double, numSamples> partitionedLeft{};
  std::array<double, numSamples> partitionedRight{};
  constexpr std::array<std::size_t, 5> blockSizes{1, 4, 2, 7, 3};
  std::size_t offset = 0;
  for (const std::size_t blockSize : blockSizes)
  {
    partitionedBand.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                                 std::span<double>(partitionedLeft).subspan(offset, blockSize),
                                 std::span<double>(partitionedRight).subspan(offset, blockSize));
    offset += blockSize;
  }

  return expectSamples("DelayBand left block partitioning", partitionedLeft, wholeLeft)
         && expectSamples("DelayBand right block partitioning", partitionedRight, wholeRight);
}

bool testExactInputOutputAliasing()
{
  constexpr std::array<double, 5> input{1.0, 0.0, 0.0, 0.0, 0.0};

  dsp::DelayBand referenceBand(10.0);
  referenceBand.prepare(1000.0, input.size());
  referenceBand.setDelayTimeMs(1.0);
  referenceBand.setFeedbackCoefficient(0.5);
  configureHardLeft(referenceBand);
  std::array<double, input.size()> referenceLeft{};
  std::array<double, input.size()> referenceRight{};
  referenceBand.processBlock(input, referenceLeft, referenceRight);

  dsp::DelayBand leftAliasBand(10.0);
  leftAliasBand.prepare(1000.0, input.size());
  leftAliasBand.setDelayTimeMs(1.0);
  leftAliasBand.setFeedbackCoefficient(0.5);
  configureHardLeft(leftAliasBand);
  std::array<double, input.size()> inputAndLeft = input;
  std::array<double, input.size()> leftAliasRight{};
  leftAliasBand.processBlock(std::span<const double>(inputAndLeft),
                             std::span<double>(inputAndLeft),
                             leftAliasRight);

  dsp::DelayBand rightAliasBand(10.0);
  rightAliasBand.prepare(1000.0, input.size());
  rightAliasBand.setDelayTimeMs(1.0);
  rightAliasBand.setFeedbackCoefficient(0.5);
  configureHardLeft(rightAliasBand);
  std::array<double, input.size()> inputAndRight = input;
  std::array<double, input.size()> rightAliasLeft{};
  rightAliasBand.processBlock(std::span<const double>(inputAndRight),
                              rightAliasLeft,
                              std::span<double>(inputAndRight));

  return expectSamples("input/left exact alias", inputAndLeft, referenceLeft)
         && expectSamples("input/left separate right", leftAliasRight, referenceRight)
         && expectSamples("input/right separate left", rightAliasLeft, referenceLeft)
         && expectSamples("input/right exact alias", inputAndRight, referenceRight);
}

bool testOutputsAreOverwrittenWithWetOnlySignal()
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 3);
  band.setDelayTimeMs(1.0);
  configureHardLeft(band);

  const std::array<double, 3> input{3.0, 0.0, 0.0};
  const std::array<double, 3> expectedLeft{0.0, 3.0, 0.0};
  const std::array<double, 3> expectedRight{};
  std::array<double, 3> left{99.0, 99.0, 99.0};
  std::array<double, 3> right{99.0, 99.0, 99.0};
  band.processBlock(input, left, right);

  return expectSamples("wet-only left overwrite", left, expectedLeft)
         && expectSamples("wet-only right overwrite", right, expectedRight);
}

bool testParameterClampingAndNonFiniteValues()
{
  dsp::DelayBand band(10.0);
  band.prepare(1000.0, 1);

  band.setFeedbackCoefficient(-1.0);
  if (!expectNear("negative feedback clamp", band.feedbackCoefficient(), 0.0))
    return false;
  band.setFeedbackCoefficient(2.0);
  if (!expectNear("excessive feedback clamp",
                  band.feedbackCoefficient(),
                  dsp::DelayBand::kMaximumFeedbackCoefficient))
    return false;
  band.setFeedbackCoefficient(std::numeric_limits<double>::quiet_NaN());
  if (!expectNear("non-finite feedback", band.feedbackCoefficient(), 0.0))
    return false;

  band.setOutputLevel(-1.0);
  if (!expectNear("negative level clamp", band.outputLevel(), 0.0))
    return false;
  band.setOutputLevel(2.0);
  if (!expectNear("excessive level clamp", band.outputLevel(), 1.0))
    return false;
  band.setOutputLevel(std::numeric_limits<double>::infinity());
  if (!expectNear("non-finite level", band.outputLevel(), 0.0))
    return false;

  band.setPan(-2.0);
  if (!expectNear("negative pan clamp", band.pan(), -1.0))
    return false;
  band.setPan(2.0);
  if (!expectNear("positive pan clamp", band.pan(), 1.0))
    return false;
  band.setPan(std::numeric_limits<double>::quiet_NaN());
  return expectNear("non-finite pan", band.pan(), 0.0);
}

bool testReprepareRecomputesOneSampleMinimum()
{
  dsp::DelayBand band(1.0);
  band.setDelayTimeMs(0.0);
  configureHardLeft(band);

  constexpr std::array sampleRates{44100.0, 48000.0, 96000.0};
  for (const double sampleRate : sampleRates)
  {
    band.prepare(sampleRate, 2);
    if (!expectNear("sample-rate-dependent minimum", band.delayTimeMs(), 1000.0 / sampleRate)
        || !expectNear("preserved zero-delay request", band.requestedDelayTimeMs(), 0.0))
      return false;

    const std::array<double, 2> input{1.0, 0.0};
    const std::array<double, 2> expectedLeft{0.0, 1.0};
    std::array<double, 2> left{};
    std::array<double, 2> right{};
    band.processBlock(input, left, right);
    if (!expectSamples("reprepared one-sample response", left, expectedLeft))
      return false;
  }
  return true;
}

bool testPrepareRejectsMaximumShorterThanOneSample()
{
  dsp::DelayBand band(0.01);
  try
  {
    band.prepare(48000.0, 1);
  }
  catch (const std::invalid_argument&)
  {
    return true;
  }
  catch (...)
  {
    std::cerr << "short maximum delay threw an unexpected exception type\n";
    return false;
  }

  std::cerr << "short maximum delay was accepted\n";
  return false;
}

bool testProcessingDoesNotAllocate()
{
  dsp::DelayBand band(1000.0);
  band.prepare(48000.0, 256);
  band.setDelayTimeMs(137.25);
  band.setFeedbackCoefficient(0.75);
  band.setOutputLevel(0.8);
  band.setPan(0.3);

  std::array<double, 256> input{};
  std::array<double, 256> left{};
  std::array<double, 256> right{};

  beginAllocationTracking();
  band.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: DelayBand::processBlock made " << allocations << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"DelayBand: sub-sample requests clamp to one sample", testSubSampleDelayClampsToOneSample},
  TestCase{"DelayBand: minimum-delay feedback remains stable", testMinimumDelayFeedbackIsStable},
  TestCase{"DelayBand: integer feedback has no extra loop sample", testIntegerFeedbackHasNoExtraLoopSample},
  TestCase{"DelayBand: fractional delay works with feedback", testFractionalDelayFeedback},
  TestCase{"DelayBand: output level is outside the feedback loop", testOutputLevelIsOutsideFeedbackLoop},
  TestCase{"DelayBand: equal-power pan convention", testEqualPowerPanLaw},
  TestCase{"DelayBand: disabled state gates input and advances tails", testDisabledBandGatesInputAndAdvancesTail},
  TestCase{"DelayBand: reset clears history and preserves parameters", testResetClearsFeedbackHistoryAndPreservesParameters},
  TestCase{"DelayBand: block partitioning is invariant", testBlockPartitioningDoesNotChangeOutput},
  TestCase{"DelayBand: exact input/output aliasing", testExactInputOutputAliasing},
  TestCase{"DelayBand: outputs are overwritten with wet-only signal", testOutputsAreOverwrittenWithWetOnlySignal},
  TestCase{"DelayBand: parameters clamp and sanitize non-finite values", testParameterClampingAndNonFiniteValues},
  TestCase{"DelayBand: reprepare recomputes one-sample minimum", testReprepareRecomputesOneSampleMinimum},
  TestCase{"DelayBand: prepare rejects an insufficient maximum", testPrepareRejectsMaximumShorterThanOneSample},
  TestCase{"DelayBand: processing performs no allocations", testProcessingDoesNotAllocate},
};

} // namespace

TestSuite delayBandTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
