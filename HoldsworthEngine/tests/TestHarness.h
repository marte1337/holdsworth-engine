#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

namespace holdsworth::test
{

struct TestCase
{
  std::string_view name;
  bool (*run)();
};

using TestSuite = std::span<const TestCase>;

inline bool nearlyEqual(const double actual, const double expected, const double tolerance = 1.0e-12)
{
  return std::abs(actual - expected) <= tolerance;
}

inline bool expectNear(const std::string_view testName,
                       const double actual,
                       const double expected,
                       const double tolerance = 1.0e-12)
{
  if (nearlyEqual(actual, expected, tolerance))
    return true;

  std::cerr << testName << ": value was " << actual << ", expected " << expected << '\n';
  return false;
}

template <typename ActualRange, typename ExpectedRange>
bool expectSamples(const std::string_view testName,
                   const ActualRange& actual,
                   const ExpectedRange& expected,
                   const double tolerance = 1.0e-12)
{
  if (actual.size() != expected.size())
  {
    std::cerr << testName << ": size mismatch (actual " << actual.size() << ", expected " << expected.size() << ")\n";
    return false;
  }

  for (std::size_t i = 0; i < actual.size(); ++i)
  {
    if (!nearlyEqual(actual[i], expected[i], tolerance))
    {
      std::cerr << testName << ": sample " << i << " was " << actual[i] << ", expected " << expected[i] << '\n';
      return false;
    }
  }
  return true;
}

// Exact floating-point regression helper. Unlike numeric equality, this also
// distinguishes signed zero and catches any other change in the sample bits.
template <typename ActualRange, typename ExpectedRange>
bool expectSamplesBitExact(const std::string_view testName,
                           const ActualRange& actual,
                           const ExpectedRange& expected)
{
  if (actual.size() != expected.size())
  {
    std::cerr << testName << ": size mismatch (actual " << actual.size()
              << ", expected " << expected.size() << ")\n";
    return false;
  }

  for (std::size_t i = 0; i < actual.size(); ++i)
  {
    const auto actualBits = std::bit_cast<std::uint64_t>(static_cast<double>(actual[i]));
    const auto expectedBits = std::bit_cast<std::uint64_t>(static_cast<double>(expected[i]));
    if (actualBits != expectedBits)
    {
      std::cerr << testName << ": sample " << i << " bits were 0x" << std::hex
                << actualBits << ", expected 0x" << expectedBits << std::dec << '\n';
      return false;
    }
  }
  return true;
}

void beginAllocationTracking() noexcept;
[[nodiscard]] std::size_t endAllocationTracking() noexcept;

[[nodiscard]] TestSuite fractionalDelayLineTests() noexcept;
[[nodiscard]] TestSuite delayModulatorTests() noexcept;
[[nodiscard]] TestSuite delayLoopFilterTests() noexcept;
[[nodiscard]] TestSuite delayBandTests() noexcept;
[[nodiscard]] TestSuite delayBandModulationTests() noexcept;
[[nodiscard]] TestSuite delayBandLoopFilterTests() noexcept;
[[nodiscard]] TestSuite delayBandTapTests() noexcept;
[[nodiscard]] TestSuite delayBandSignalPolarityTests() noexcept;
[[nodiscard]] TestSuite holdsworthDelayEngineTests() noexcept;
[[nodiscard]] TestSuite holdsworthDelayLiveIntegrationTests() noexcept;

} // namespace holdsworth::test
