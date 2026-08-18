#include "../dsp/DelayLoopFilter.h"
#include "TestHarness.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_convertible_v<double, holdsworth::dsp::LowCutFrequencyHz>);
static_assert(!std::is_convertible_v<double, holdsworth::dsp::HighCutFrequencyHz>);
static_assert(!std::is_convertible_v<holdsworth::dsp::LowCutFrequencyHz,
                                     holdsworth::dsp::HighCutFrequencyHz>);
static_assert(!std::is_convertible_v<holdsworth::dsp::HighCutFrequencyHz,
                                     holdsworth::dsp::LowCutFrequencyHz>);

namespace holdsworth::test
{
namespace
{

constexpr double kSampleRate = 48000.0;

bool bitwiseEqual(const double first, const double second) noexcept
{
  return std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(second);
}

double measureSineGain(const dsp::DelayLoopFilterConfiguration& configuration,
                       const double frequencyHz)
{
  constexpr std::size_t warmupSamples = 24000;
  constexpr std::size_t measurementSamples = 48000;

  dsp::DelayLoopFilter filter;
  filter.setConfiguration(configuration);
  filter.prepare(kSampleRate);

  double inputEnergy = 0.0;
  double outputEnergy = 0.0;
  for (std::size_t i = 0; i < warmupSamples + measurementSamples; ++i)
  {
    const double phase = 2.0 * std::numbers::pi_v<double> * frequencyHz
                         * static_cast<double>(i) / kSampleRate;
    const double input = std::sin(phase);
    const double output = filter.processSample(input);
    if (i >= warmupSamples)
    {
      inputEnergy += input * input;
      outputEnergy += output * output;
    }
  }

  return std::sqrt(outputEnergy / inputEnergy);
}

bool testBypassedPathReturnsInputBitForBit()
{
  dsp::DelayLoopFilter filter;
  filter.prepare(kSampleRate);

  const std::array inputs{0.0,
                          -0.0,
                          1.0,
                          -2.5,
                          std::numeric_limits<double>::denorm_min(),
                          std::numeric_limits<double>::infinity()};
  for (const double input : inputs)
  {
    const double output = filter.processSample(input);
    if (!bitwiseEqual(output, input))
    {
      std::cerr << "bypassed loop filter changed an input bit pattern\n";
      return false;
    }
  }

  return filter.isBypassed();
}

bool testInvalidEnabledCutoffsSanitizeToOff()
{
  dsp::DelayLoopFilter filter;
  filter.setConfiguration(
    {dsp::LowCutFrequencyHz{-1.0},
     dsp::HighCutFrequencyHz{std::numeric_limits<double>::quiet_NaN()}});

  if (!filter.isBypassed() || filter.requestedConfiguration().lowCut.has_value()
      || filter.requestedConfiguration().highCut.has_value())
  {
    std::cerr << "invalid cutoffs did not sanitize to OFF before prepare\n";
    return false;
  }

  filter.prepare(kSampleRate);
  filter.setHighCut(dsp::HighCutFrequencyHz{1000.0});
  filter.setHighCut(dsp::HighCutFrequencyHz{0.0});
  if (filter.effectiveConfiguration().highCut.has_value())
  {
    std::cerr << "invalid High Cut became an active technical-minimum cutoff\n";
    return false;
  }

  filter.setLowCut(dsp::LowCutFrequencyHz{std::numeric_limits<double>::infinity()});
  return filter.isBypassed() && !filter.effectiveConfiguration().lowCut.has_value();
}

bool testRequestedAndEffectiveCutoffsRemainDistinct()
{
  dsp::DelayLoopFilter filter;
  filter.setConfiguration(
    {dsp::LowCutFrequencyHz{1.0e-12}, dsp::HighCutFrequencyHz{100000.0}});
  filter.prepare(48000.0);

  auto requested = filter.requestedConfiguration();
  auto effective = filter.effectiveConfiguration();
  if (!requested.lowCut.has_value() || !requested.highCut.has_value()
      || !effective.lowCut.has_value() || !effective.highCut.has_value()
      || !expectNear("requested low cutoff", requested.lowCut->value, 1.0e-12)
      || !expectNear("requested high cutoff", requested.highCut->value, 100000.0)
      || !expectNear("48 kHz minimum cutoff", effective.lowCut->value, 0.048)
      || !expectNear("48 kHz maximum cutoff", effective.highCut->value, 23520.0))
    return false;

  filter.prepare(96000.0);
  requested = filter.requestedConfiguration();
  effective = filter.effectiveConfiguration();
  return requested.lowCut.has_value() && requested.highCut.has_value()
         && effective.lowCut.has_value() && effective.highCut.has_value()
         && expectNear("requested low survives reprepare", requested.lowCut->value, 1.0e-12)
         && expectNear("requested high survives reprepare", requested.highCut->value, 100000.0)
         && expectNear("96 kHz minimum cutoff", effective.lowCut->value, 0.096)
         && expectNear("96 kHz maximum cutoff", effective.highCut->value, 47040.0);
}

bool testCutoffsClampIndependentlyWithoutReordering()
{
  dsp::DelayLoopFilter filter;
  filter.setConfiguration(
    {dsp::LowCutFrequencyHz{5000.0}, dsp::HighCutFrequencyHz{1000.0}});
  filter.prepare(kSampleRate);

  const auto effective = filter.effectiveConfiguration();
  return effective.lowCut.has_value() && effective.highCut.has_value()
         && expectNear("literal overlapping Low Cut", effective.lowCut->value, 5000.0)
         && expectNear("literal overlapping High Cut", effective.highCut->value, 1000.0);
}

bool testKnownLowCutThenHighCutTopology()
{
  dsp::DelayLoopFilter filter;
  filter.setConfiguration(
    {dsp::LowCutFrequencyHz{1.0}, dsp::HighCutFrequencyHz{1.0}});
  filter.prepare(4.0);

  // At fc = Fs / 4, g = 1 and G = 1/2. The first Low Cut output for
  // an impulse is 1/2, then High Cut returns half of that: 1/4.
  return expectNear("Low Cut precedes High Cut", filter.processSample(1.0), 0.25, 2.0e-16);
}

bool testLowAndHighCutFrequencyResponses()
{
  const dsp::DelayLoopFilterConfiguration lowCutOnly{dsp::LowCutFrequencyHz{1000.0},
                                                      std::nullopt};
  const double lowCutAt50Hz = measureSineGain(lowCutOnly, 50.0);
  const double lowCutAt5kHz = measureSineGain(lowCutOnly, 5000.0);
  if (lowCutAt5kHz <= 0.8 || lowCutAt5kHz <= 10.0 * lowCutAt50Hz)
  {
    std::cerr << "Low Cut response did not prefer high frequency: 50 Hz="
              << lowCutAt50Hz << ", 5 kHz=" << lowCutAt5kHz << '\n';
    return false;
  }

  const dsp::DelayLoopFilterConfiguration highCutOnly{std::nullopt,
                                                       dsp::HighCutFrequencyHz{1000.0}};
  const double highCutAt50Hz = measureSineGain(highCutOnly, 50.0);
  const double highCutAt5kHz = measureSineGain(highCutOnly, 5000.0);
  if (highCutAt50Hz <= 0.8 || highCutAt50Hz <= 4.0 * highCutAt5kHz)
  {
    std::cerr << "High Cut response did not prefer low frequency: 50 Hz="
              << highCutAt50Hz << ", 5 kHz=" << highCutAt5kHz << '\n';
    return false;
  }

  return true;
}

bool testBothSectionsFormExpectedBandPass()
{
  const dsp::DelayLoopFilterConfiguration configuration{dsp::LowCutFrequencyHz{200.0},
                                                        dsp::HighCutFrequencyHz{5000.0}};
  const double lowGain = measureSineGain(configuration, 50.0);
  const double middleGain = measureSineGain(configuration, 1000.0);
  const double highGain = measureSineGain(configuration, 15000.0);

  if (middleGain <= 2.5 * lowGain || middleGain <= 2.5 * highGain)
  {
    std::cerr << "combined filter did not preserve its middle band: low=" << lowGain
              << ", middle=" << middleGain << ", high=" << highGain << '\n';
    return false;
  }
  return true;
}

bool testIdenticalConfigurationIsStateIdempotent()
{
  const dsp::DelayLoopFilterConfiguration configuration{dsp::LowCutFrequencyHz{300.0},
                                                        dsp::HighCutFrequencyHz{4000.0}};
  dsp::DelayLoopFilter unchanged;
  dsp::DelayLoopFilter reapplied;
  unchanged.setConfiguration(configuration);
  reapplied.setConfiguration(configuration);
  unchanged.prepare(kSampleRate);
  reapplied.prepare(kSampleRate);

  for (std::size_t i = 0; i < 256; ++i)
  {
    const double input = (i % 7 == 0) ? 0.75 : -0.125;
    if (!bitwiseEqual(unchanged.processSample(input), reapplied.processSample(input)))
      return false;
  }

  reapplied.setConfiguration(configuration);
  for (std::size_t i = 0; i < 256; ++i)
  {
    const double input = (i % 5 == 0) ? -0.5 : 0.25;
    if (!bitwiseEqual(unchanged.processSample(input), reapplied.processSample(input)))
    {
      std::cerr << "reapplying identical filter configuration disturbed state\n";
      return false;
    }
  }
  return true;
}

bool testActiveCutoffChangesRetainState()
{
  dsp::DelayLoopFilter lowCut;
  lowCut.setLowCut(dsp::LowCutFrequencyHz{100.0});
  lowCut.prepare(kSampleRate);
  for (std::size_t i = 0; i < 20000; ++i)
    static_cast<void>(lowCut.processSample(1.0));
  lowCut.setLowCut(dsp::LowCutFrequencyHz{500.0});
  const double retainedLowCutOutput = lowCut.processSample(1.0);

  dsp::DelayLoopFilter freshLowCut;
  freshLowCut.setLowCut(dsp::LowCutFrequencyHz{500.0});
  freshLowCut.prepare(kSampleRate);
  const double freshLowCutOutput = freshLowCut.processSample(1.0);
  if (std::abs(retainedLowCutOutput) >= 1.0e-6 || freshLowCutOutput <= 0.9)
  {
    std::cerr << "active Low Cut change did not retain its state\n";
    return false;
  }

  dsp::DelayLoopFilter highCut;
  highCut.setHighCut(dsp::HighCutFrequencyHz{100.0});
  highCut.prepare(kSampleRate);
  for (std::size_t i = 0; i < 20000; ++i)
    static_cast<void>(highCut.processSample(1.0));
  highCut.setHighCut(dsp::HighCutFrequencyHz{500.0});
  const double retainedHighCutOutput = highCut.processSample(0.0);

  dsp::DelayLoopFilter freshHighCut;
  freshHighCut.setHighCut(dsp::HighCutFrequencyHz{500.0});
  freshHighCut.prepare(kSampleRate);
  const double freshHighCutOutput = freshHighCut.processSample(0.0);
  if (retainedHighCutOutput <= 0.9 || !bitwiseEqual(freshHighCutOutput, 0.0))
  {
    std::cerr << "active High Cut change did not retain its state\n";
    return false;
  }
  return true;
}

bool testOffClearsOnlyItsOwnStateAndReenableStartsFresh()
{
  dsp::DelayLoopFilter cleared;
  cleared.setLowCut(dsp::LowCutFrequencyHz{400.0});
  cleared.prepare(kSampleRate);
  for (std::size_t i = 0; i < 1024; ++i)
    static_cast<void>(cleared.processSample(1.0));
  cleared.setLowCut(std::nullopt);
  cleared.setLowCut(dsp::LowCutFrequencyHz{400.0});

  dsp::DelayLoopFilter fresh;
  fresh.setLowCut(dsp::LowCutFrequencyHz{400.0});
  fresh.prepare(kSampleRate);
  if (!bitwiseEqual(cleared.processSample(1.0), fresh.processSample(1.0)))
  {
    std::cerr << "re-enabled section restored stale filter memory\n";
    return false;
  }

  dsp::DelayLoopFilter toggledOtherSection;
  dsp::DelayLoopFilter untouched;
  toggledOtherSection.setHighCut(dsp::HighCutFrequencyHz{1200.0});
  untouched.setHighCut(dsp::HighCutFrequencyHz{1200.0});
  toggledOtherSection.prepare(kSampleRate);
  untouched.prepare(kSampleRate);
  for (std::size_t i = 0; i < 512; ++i)
  {
    const double input = (i % 3 == 0) ? 1.0 : -0.25;
    static_cast<void>(toggledOtherSection.processSample(input));
    static_cast<void>(untouched.processSample(input));
  }

  toggledOtherSection.setLowCut(dsp::LowCutFrequencyHz{300.0});
  toggledOtherSection.setLowCut(std::nullopt);
  const double input = 0.375;
  if (!bitwiseEqual(toggledOtherSection.processSample(input), untouched.processSample(input)))
  {
    std::cerr << "switching Low Cut OFF disturbed High Cut state\n";
    return false;
  }
  return true;
}

bool testResetIsDeterministicAndRetainsConfiguration()
{
  const dsp::DelayLoopFilterConfiguration configuration{dsp::LowCutFrequencyHz{250.0},
                                                        dsp::HighCutFrequencyHz{3500.0}};
  dsp::DelayLoopFilter filter;
  filter.setConfiguration(configuration);
  filter.prepare(kSampleRate);

  std::array<double, 64> firstRender{};
  std::array<double, 64> secondRender{};
  for (std::size_t i = 0; i < firstRender.size(); ++i)
    firstRender[i] = filter.processSample(i == 0 ? 1.0 : 0.0);

  filter.reset();
  for (std::size_t i = 0; i < secondRender.size(); ++i)
    secondRender[i] = filter.processSample(i == 0 ? 1.0 : 0.0);

  if (!expectSamples("loop-filter reset determinism", secondRender, firstRender, 0.0))
    return false;

  const auto requested = filter.requestedConfiguration();
  return requested.lowCut.has_value() && requested.highCut.has_value()
         && expectNear("reset retains Low Cut", requested.lowCut->value, 250.0)
         && expectNear("reset retains High Cut", requested.highCut->value, 3500.0);
}

bool testPrepareValidation()
{
  for (const double invalidSampleRate : std::array{
         0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
  {
    dsp::DelayLoopFilter filter;
    try
    {
      filter.prepare(invalidSampleRate);
    }
    catch (const std::invalid_argument&)
    {
      continue;
    }
    catch (...)
    {
      std::cerr << "invalid filter sample rate threw an unexpected exception type\n";
      return false;
    }

    std::cerr << "invalid filter sample rate was accepted\n";
    return false;
  }
  return true;
}

bool testConfigurationAndHotPathDoNotAllocate()
{
  dsp::DelayLoopFilter filter;
  filter.setConfiguration(
    {dsp::LowCutFrequencyHz{250.0}, dsp::HighCutFrequencyHz{5000.0}});
  filter.prepare(kSampleRate);

  beginAllocationTracking();
  for (std::size_t i = 0; i < 10000; ++i)
  {
    if (i == 2500)
      filter.setLowCut(dsp::LowCutFrequencyHz{300.0});
    if (i == 5000)
      filter.setHighCut(std::nullopt);
    if (i == 7500)
      filter.setHighCut(dsp::HighCutFrequencyHz{4500.0});
    static_cast<void>(filter.processSample((i % 2 == 0) ? 0.25 : -0.25));
  }
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: DelayLoopFilter made " << allocations
              << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"DelayLoopFilter: bypass returns input bit-for-bit", testBypassedPathReturnsInputBitForBit},
  TestCase{"DelayLoopFilter: invalid enabled cutoffs sanitize to OFF",
           testInvalidEnabledCutoffsSanitizeToOff},
  TestCase{"DelayLoopFilter: requested and effective cutoffs remain distinct",
           testRequestedAndEffectiveCutoffsRemainDistinct},
  TestCase{"DelayLoopFilter: cutoffs clamp independently without reordering",
           testCutoffsClampIndependentlyWithoutReordering},
  TestCase{"DelayLoopFilter: Low Cut precedes High Cut", testKnownLowCutThenHighCutTopology},
  TestCase{"DelayLoopFilter: individual sections have expected frequency response",
           testLowAndHighCutFrequencyResponses},
  TestCase{"DelayLoopFilter: both sections form the expected band-pass",
           testBothSectionsFormExpectedBandPass},
  TestCase{"DelayLoopFilter: identical configuration is state-idempotent",
           testIdenticalConfigurationIsStateIdempotent},
  TestCase{"DelayLoopFilter: active cutoff changes retain state",
           testActiveCutoffChangesRetainState},
  TestCase{"DelayLoopFilter: OFF clears only its section and ON starts fresh",
           testOffClearsOnlyItsOwnStateAndReenableStartsFresh},
  TestCase{"DelayLoopFilter: reset is deterministic and retains configuration",
           testResetIsDeterministicAndRetainsConfiguration},
  TestCase{"DelayLoopFilter: prepare validates sample rate", testPrepareValidation},
  TestCase{"DelayLoopFilter: configuration and hot path perform no allocations",
           testConfigurationAndHotPathDoNotAllocate},
};

} // namespace

TestSuite delayLoopFilterTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
