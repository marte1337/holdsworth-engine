#include "../dsp/FractionalDelayLine.h"
#include "TestHarness.h"

#include <array>
#include <iostream>
#include <span>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<holdsworth::dsp::FractionalDelayLine>);
static_assert(!std::is_copy_assignable_v<holdsworth::dsp::FractionalDelayLine>);
static_assert(!std::is_move_constructible_v<holdsworth::dsp::FractionalDelayLine>);
static_assert(!std::is_move_assignable_v<holdsworth::dsp::FractionalDelayLine>);

namespace holdsworth::test
{
namespace
{

bool testZeroDelayReturnsCurrentSample()
{
  dsp::FractionalDelayLine delay(10.0);
  delay.setDelayTimeMs(0.0);
  delay.prepare(1000.0, 4);

  const std::array<double, 4> input{1.0, -2.0, 3.5, 0.25};
  std::array<double, 4> output{};
  delay.processBlock(input, output);

  return expectSamples("zero delay", output, input);
}

bool testOneSampleDelayReturnsPreviousSample()
{
  dsp::FractionalDelayLine delay(10.0);
  delay.prepare(1000.0, 3);
  delay.setDelayTimeMs(1.0); // One millisecond at 1 kHz is exactly one sample.

  const std::array<double, 3> input{1.0, -2.0, 4.0};
  const std::array<double, 3> expected{0.0, 1.0, -2.0};
  std::array<double, 3> output{};
  delay.processBlock(input, output);

  return expectSamples("one-sample delay", output, expected);
}

bool testFractionalDelayInterpolatesHistory()
{
  dsp::FractionalDelayLine delay(10.0);
  delay.prepare(1000.0, 4);
  delay.setDelayTimeMs(1.25); // 75% x[n-1] + 25% x[n-2].

  const std::array<double, 4> input{0.0, 4.0, 8.0, 12.0};
  const std::array<double, 4> expected{0.0, 0.0, 3.0, 7.0};
  std::array<double, 4> output{};
  delay.processBlock(input, output);

  return expectSamples("fractional delay", output, expected);
}

bool testSplitHistoryApiMatchesBlockProcessing()
{
  constexpr std::array<double, 4> input{1.0, 2.0, 3.0, 4.0};

  dsp::FractionalDelayLine blockDelay(10.0);
  blockDelay.prepare(1000.0, input.size());
  blockDelay.setDelayTimeMs(1.5);
  std::array<double, input.size()> blockOutput{};
  blockDelay.processBlock(input, blockOutput);

  dsp::FractionalDelayLine splitDelay(10.0);
  splitDelay.prepare(1000.0, input.size());
  splitDelay.setDelayTimeMs(1.5);
  std::array<double, input.size()> splitOutput{};
  for (std::size_t i = 0; i < input.size(); ++i)
  {
    splitOutput[i] = splitDelay.readDelayedSample();
    splitDelay.pushSample(input[i]);
  }

  const std::array<double, 4> expected{0.0, 0.5, 1.5, 2.5};
  return expectSamples("split history expected", splitOutput, expected)
         && expectSamples("split history block equivalence", splitOutput, blockOutput);
}

bool testResetClearsHistory()
{
  dsp::FractionalDelayLine delay(10.0);
  delay.prepare(1000.0, 3);
  delay.setDelayTimeMs(2.0);

  const std::array<double, 1> impulse{1.0};
  std::array<double, 1> discarded{};
  delay.processBlock(impulse, discarded);
  delay.reset();

  const std::array<double, 3> silence{};
  std::array<double, 3> output{};
  delay.processBlock(silence, output);
  return expectSamples("reset", output, silence);
}

bool testExactInPlaceProcessing()
{
  dsp::FractionalDelayLine delay(10.0);
  delay.prepare(1000.0, 3);
  delay.setDelayTimeMs(1.5);

  std::array<double, 3> samples{1.0, 2.0, 3.0};
  const std::array<double, 3> expected{0.0, 0.5, 1.5};
  delay.processBlock(std::span<const double>(samples.data(), samples.size()),
                     std::span<double>(samples.data(), samples.size()));

  return expectSamples("in-place processing", samples, expected);
}

bool testBlockPartitioningDoesNotChangeOutput()
{
  constexpr std::size_t numSamples = 17;
  std::array<double, numSamples> input{};
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = static_cast<double>((i * 7) % 11) - 5.0;

  dsp::FractionalDelayLine wholeBlockDelay(20.0);
  wholeBlockDelay.prepare(1000.0, numSamples);
  wholeBlockDelay.setDelayTimeMs(2.25);
  std::array<double, numSamples> wholeBlockOutput{};
  wholeBlockDelay.processBlock(input, wholeBlockOutput);

  dsp::FractionalDelayLine partitionedDelay(20.0);
  partitionedDelay.prepare(1000.0, numSamples);
  partitionedDelay.setDelayTimeMs(2.25);
  std::array<double, numSamples> partitionedOutput{};
  constexpr std::array<std::size_t, 5> blockSizes{1, 4, 2, 7, 3};
  std::size_t offset = 0;
  for (const std::size_t blockSize : blockSizes)
  {
    partitionedDelay.processBlock(std::span<const double>(input).subspan(offset, blockSize),
                                  std::span<double>(partitionedOutput).subspan(offset, blockSize));
    offset += blockSize;
  }

  return expectSamples("block partitioning", partitionedOutput, wholeBlockOutput);
}

bool testDelayTimeIsClampedToPreparedRange()
{
  dsp::FractionalDelayLine delay(3.0);
  delay.prepare(1000.0, 4);

  delay.setDelayTimeMs(-10.0);
  if (!expectNear("negative delay clamp", delay.delayTimeMs(), 0.0))
    return false;

  delay.setDelayTimeMs(100.0);
  if (!expectNear("maximum delay clamp", delay.delayTimeMs(), 3.0))
    return false;

  const std::array<double, 4> input{1.0, 0.0, 0.0, 0.0};
  const std::array<double, 4> expected{0.0, 0.0, 0.0, 1.0};
  std::array<double, 4> output{};
  delay.processBlock(input, output);
  return expectSamples("maximum delay", output, expected);
}

bool testProcessBlockDoesNotAllocate()
{
  dsp::FractionalDelayLine delay(1000.0);
  delay.prepare(48000.0, 256);
  delay.setDelayTimeMs(137.25);

  std::array<double, 256> input{};
  std::array<double, 256> output{};

  beginAllocationTracking();
  delay.processBlock(input, output);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: FractionalDelayLine::processBlock made " << allocations
              << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"FractionalDelayLine: 0 ms returns the current sample", testZeroDelayReturnsCurrentSample},
  TestCase{"FractionalDelayLine: 1 sample returns the previous sample", testOneSampleDelayReturnsPreviousSample},
  TestCase{"FractionalDelayLine: fractional delay interpolates history", testFractionalDelayInterpolatesHistory},
  TestCase{"FractionalDelayLine: split history API matches block processing", testSplitHistoryApiMatchesBlockProcessing},
  TestCase{"FractionalDelayLine: reset clears history", testResetClearsHistory},
  TestCase{"FractionalDelayLine: exact in-place processing", testExactInPlaceProcessing},
  TestCase{"FractionalDelayLine: block partitioning is invariant", testBlockPartitioningDoesNotChangeOutput},
  TestCase{"FractionalDelayLine: delay time is clamped", testDelayTimeIsClampedToPreparedRange},
  TestCase{"FractionalDelayLine: processing performs no allocations", testProcessBlockDoesNotAllocate},
};

} // namespace

TestSuite fractionalDelayLineTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
