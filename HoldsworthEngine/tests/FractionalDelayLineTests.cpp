#include "../dsp/FractionalDelayLine.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible_v<holdsworth::dsp::FractionalDelayLine>);
static_assert(!std::is_copy_assignable_v<holdsworth::dsp::FractionalDelayLine>);
static_assert(!std::is_move_constructible_v<holdsworth::dsp::FractionalDelayLine>);
static_assert(!std::is_move_assignable_v<holdsworth::dsp::FractionalDelayLine>);

namespace
{

std::atomic<bool> gTrackAllocations = false;
std::atomic<std::size_t> gAllocationCount = 0;

void recordAllocation() noexcept
{
  if (gTrackAllocations.load(std::memory_order_relaxed))
    gAllocationCount.fetch_add(1, std::memory_order_relaxed);
}

bool nearlyEqual(const double actual, const double expected, const double tolerance = 1.0e-12)
{
  return std::abs(actual - expected) <= tolerance;
}

template <typename ActualRange, typename ExpectedRange>
bool expectSamples(const std::string_view testName, const ActualRange& actual, const ExpectedRange& expected)
{
  if (actual.size() != expected.size())
  {
    std::cerr << testName << ": size mismatch (actual " << actual.size() << ", expected " << expected.size() << ")\n";
    return false;
  }

  for (std::size_t i = 0; i < actual.size(); ++i)
  {
    if (!nearlyEqual(actual[i], expected[i]))
    {
      std::cerr << testName << ": sample " << i << " was " << actual[i] << ", expected " << expected[i] << '\n';
      return false;
    }
  }
  return true;
}

bool testZeroDelayReturnsCurrentSample()
{
  holdsworth::dsp::FractionalDelayLine delay(10.0);
  delay.setDelayTimeMs(0.0);
  delay.prepare(1000.0, 4);

  const std::array<double, 4> input{1.0, -2.0, 3.5, 0.25};
  std::array<double, 4> output{};
  delay.processBlock(input, output);

  return expectSamples("zero delay", output, input);
}

bool testOneSampleDelayReturnsPreviousSample()
{
  holdsworth::dsp::FractionalDelayLine delay(10.0);
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
  holdsworth::dsp::FractionalDelayLine delay(10.0);
  delay.prepare(1000.0, 4);
  delay.setDelayTimeMs(1.25); // 75% x[n-1] + 25% x[n-2].

  const std::array<double, 4> input{0.0, 4.0, 8.0, 12.0};
  const std::array<double, 4> expected{0.0, 0.0, 3.0, 7.0};
  std::array<double, 4> output{};
  delay.processBlock(input, output);

  return expectSamples("fractional delay", output, expected);
}

bool testResetClearsHistory()
{
  holdsworth::dsp::FractionalDelayLine delay(10.0);
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
  holdsworth::dsp::FractionalDelayLine delay(10.0);
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

  holdsworth::dsp::FractionalDelayLine wholeBlockDelay(20.0);
  wholeBlockDelay.prepare(1000.0, numSamples);
  wholeBlockDelay.setDelayTimeMs(2.25);
  std::array<double, numSamples> wholeBlockOutput{};
  wholeBlockDelay.processBlock(input, wholeBlockOutput);

  holdsworth::dsp::FractionalDelayLine partitionedDelay(20.0);
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
  holdsworth::dsp::FractionalDelayLine delay(3.0);
  delay.prepare(1000.0, 4);

  delay.setDelayTimeMs(-10.0);
  if (!nearlyEqual(delay.delayTimeMs(), 0.0))
  {
    std::cerr << "delay clamp: negative delay was not clamped to zero\n";
    return false;
  }

  delay.setDelayTimeMs(100.0);
  if (!nearlyEqual(delay.delayTimeMs(), 3.0))
  {
    std::cerr << "delay clamp: excessive delay was not clamped to the maximum\n";
    return false;
  }

  const std::array<double, 4> input{1.0, 0.0, 0.0, 0.0};
  const std::array<double, 4> expected{0.0, 0.0, 0.0, 1.0};
  std::array<double, 4> output{};
  delay.processBlock(input, output);
  return expectSamples("maximum delay", output, expected);
}

bool testProcessBlockDoesNotAllocate()
{
  holdsworth::dsp::FractionalDelayLine delay(1000.0);
  delay.prepare(48000.0, 256);
  delay.setDelayTimeMs(137.25);

  std::array<double, 256> input{};
  std::array<double, 256> output{};

  gAllocationCount.store(0, std::memory_order_relaxed);
  gTrackAllocations.store(true, std::memory_order_relaxed);
  delay.processBlock(input, output);
  gTrackAllocations.store(false, std::memory_order_relaxed);

  const std::size_t allocations = gAllocationCount.load(std::memory_order_relaxed);
  if (allocations != 0)
  {
    std::cerr << "real-time allocation: processBlock made " << allocations << " allocation(s)\n";
    return false;
  }
  return true;
}

struct TestCase
{
  std::string_view name;
  bool (*run)();
};

} // namespace

void* operator new(const std::size_t size)
{
  recordAllocation();
  if (void* memory = std::malloc(size == 0 ? 1 : size))
    return memory;
  throw std::bad_alloc();
}

void* operator new[](const std::size_t size)
{
  recordAllocation();
  if (void* memory = std::malloc(size == 0 ? 1 : size))
    return memory;
  throw std::bad_alloc();
}

void operator delete(void* memory) noexcept
{
  std::free(memory);
}

void operator delete[](void* memory) noexcept
{
  std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
  std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
  std::free(memory);
}

int main()
{
  constexpr std::array tests{
    TestCase{"0 ms returns the current sample", testZeroDelayReturnsCurrentSample},
    TestCase{"1 sample returns the previous sample", testOneSampleDelayReturnsPreviousSample},
    TestCase{"fractional delay interpolates history", testFractionalDelayInterpolatesHistory},
    TestCase{"reset clears history", testResetClearsHistory},
    TestCase{"exact in-place processing", testExactInPlaceProcessing},
    TestCase{"block partitioning is invariant", testBlockPartitioningDoesNotChangeOutput},
    TestCase{"delay time is clamped", testDelayTimeIsClampedToPreparedRange},
    TestCase{"processing performs no allocations", testProcessBlockDoesNotAllocate},
  };

  int failures = 0;
  for (const auto& test : tests)
  {
    const bool passed = test.run();
    std::cout << (passed ? "PASS: " : "FAIL: ") << test.name << '\n';
    failures += passed ? 0 : 1;
  }

  if (failures == 0)
  {
    std::cout << "All " << tests.size() << " HoldsworthEngine tests passed.\n";
    return EXIT_SUCCESS;
  }

  std::cerr << failures << " HoldsworthEngine test(s) failed.\n";
  return EXIT_FAILURE;
}
