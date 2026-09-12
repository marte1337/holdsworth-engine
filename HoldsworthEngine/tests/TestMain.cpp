#include "TestHarness.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>

namespace
{

std::atomic<bool> gTrackAllocations = false;
std::atomic<std::size_t> gAllocationCount = 0;

void recordAllocation() noexcept
{
  if (gTrackAllocations.load(std::memory_order_relaxed))
    gAllocationCount.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

namespace holdsworth::test
{

void beginAllocationTracking() noexcept
{
  gAllocationCount.store(0, std::memory_order_relaxed);
  gTrackAllocations.store(true, std::memory_order_relaxed);
}

std::size_t endAllocationTracking() noexcept
{
  gTrackAllocations.store(false, std::memory_order_relaxed);
  return gAllocationCount.load(std::memory_order_relaxed);
}

} // namespace holdsworth::test

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

int main(int argc, char** argv)
{
  using namespace holdsworth::test;
  if (argc > 2)
  {
    std::cerr << "Usage: HoldsworthEngineTests [test-name substring]\n";
    return EXIT_FAILURE;
  }

  const std::array suites{fractionalDelayLineTests(),
                          delayModulatorTests(),
                          delayLoopFilterTests(),
                          delayBandTests(),
                          delayBandModulationTests(),
                          delayBandLoopFilterTests(),
                          delayBandTapTests(),
                          delayBandSignalPolarityTests(),
                          holdsworthDelayEngineTests(),
                          modulationSyncTests(),
                          audioRoutingTests(),
                          delayGroupingTests(),
                          group12RhythmDiagnosticTests(),
                          connect913PresetTests(),
                          sync922PresetTests(),
                          holdsworth111PresetTests(),
                          holdsworth122PresetTests(),
                          holdsworth223PresetTests(),
                          holdsworth231PresetTests(),
                          holdsworthDelayLiveIntegrationTests(),
                          tcBldCleanBoostProcessorTests(),
                          mc402CleanBoostProcessorTests(),
                          developmentPreNAMSelectorTests()};
  std::size_t totalTests = 0;
  int failures = 0;

  for (const TestSuite suite : suites)
  {
    for (const TestCase& test : suite)
    {
      if (argc == 2 && std::string_view(test.name).find(argv[1]) == std::string_view::npos)
        continue;
      const bool passed = test.run();
      std::cout << (passed ? "PASS: " : "FAIL: ") << test.name << '\n';
      failures += passed ? 0 : 1;
      ++totalTests;
    }
  }

  if (totalTests == 0)
  {
    std::cerr << "No matching tests.\n";
    return EXIT_FAILURE;
  }
  if (failures == 0)
  {
    std::cout << "All " << totalTests << " HoldsworthEngine tests passed.\n";
    return EXIT_SUCCESS;
  }

  std::cerr << failures << " of " << totalTests << " HoldsworthEngine test(s) failed.\n";
  return EXIT_FAILURE;
}
