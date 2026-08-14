#pragma once

#include <cmath>
#include <cstddef>
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

void beginAllocationTracking() noexcept;
[[nodiscard]] std::size_t endAllocationTracking() noexcept;

[[nodiscard]] TestSuite fractionalDelayLineTests() noexcept;
[[nodiscard]] TestSuite delayModulatorTests() noexcept;
[[nodiscard]] TestSuite delayBandTests() noexcept;
[[nodiscard]] TestSuite delayBandModulationTests() noexcept;
[[nodiscard]] TestSuite holdsworthDelayEngineTests() noexcept;
[[nodiscard]] TestSuite holdsworthDelayLiveIntegrationTests() noexcept;

} // namespace holdsworth::test
