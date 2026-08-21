#include "../dsp/DelayBand.h"
#include "TestHarness.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace holdsworth::test
{
namespace
{

constexpr std::uint64_t kDoubleSignBit = std::uint64_t{1} << 63;

template <typename ReverseRange, typename NormalRange>
bool expectExactPolarityInversion(const std::string_view testName,
                                  const ReverseRange& reverse,
                                  const NormalRange& normal)
{
  if (reverse.size() != normal.size())
  {
    std::cerr << testName << ": size mismatch\n";
    return false;
  }

  for (std::size_t index = 0; index < reverse.size(); ++index)
  {
    const auto reverseBits = std::bit_cast<std::uint64_t>(static_cast<double>(reverse[index]));
    const auto expectedBits =
      std::bit_cast<std::uint64_t>(static_cast<double>(normal[index])) ^ kDoubleSignBit;
    if (reverseBits != expectedBits)
    {
      std::cerr << testName << ": sample " << index << " bits were 0x" << std::hex
                << reverseBits << ", expected 0x" << expectedBits << std::dec << '\n';
      return false;
    }
  }
  return true;
}

dsp::DelayLoopFilterConfiguration activeFilters()
{
  dsp::DelayLoopFilterConfiguration configuration;
  configuration.lowCut = dsp::LowCutFrequencyHz{37.0};
  configuration.highCut = dsp::HighCutFrequencyHz{311.0};
  return configuration;
}

struct PathScenario
{
  double modulationDepthMs;
  bool filtersEnabled;
  double tapFraction;
};

void configureBand(dsp::DelayBand& band,
                   const PathScenario scenario,
                   const std::size_t maximumBlockSize) noexcept
{
  band.setDelayTimeMs(9.25);
  band.setFeedbackCoefficient(0.68);
  band.setOutputLevel(0.73);
  band.setPan(0.27);
  band.setModulationRate(dsp::ModulationRateHz{17.5});
  band.setModulationDepth(dsp::ModulationDepthMs{scenario.modulationDepthMs});
  band.setModulationPhase(dsp::ModulationPhaseCycles{0.137});
  band.setModulationWaveform(dsp::ModulationWaveform::triangle);
  if (scenario.filtersEnabled)
    band.setLoopFilterConfiguration(activeFilters());
  band.setTapFraction(dsp::TapFraction{scenario.tapFraction});
  band.prepare(1000.0, maximumBlockSize);
}

template <std::size_t Size>
std::array<double, Size> deterministicInput()
{
  std::array<double, Size> input{};
  for (std::size_t index = 0; index < input.size(); ++index)
    input[index] = static_cast<double>(static_cast<int>((index * 19) % 41) - 20) * 0.025;
  return input;
}

bool testNormalIsBitExactAcrossEstablishedProcessingPaths()
{
  constexpr std::size_t numSamples = 257;
  const auto input = deterministicInput<numSamples>();
  constexpr std::array scenarios{PathScenario{0.0, false, 1.0},
                                 PathScenario{1.1, false, 1.0},
                                 PathScenario{0.0, true, 1.0},
                                 PathScenario{1.1, true, 1.0},
                                 PathScenario{1.1, true, 0.43}};

  for (const PathScenario scenario : scenarios)
  {
    dsp::DelayBand legacyDefault(30.0);
    dsp::DelayBand explicitNormal(30.0);
    configureBand(legacyDefault, scenario, numSamples);
    configureBand(explicitNormal, scenario, numSamples);
    explicitNormal.setDelaySignalPolarity(dsp::DelaySignalPolarity::normal);

    std::array<double, numSamples> legacyLeft{};
    std::array<double, numSamples> legacyRight{};
    std::array<double, numSamples> normalLeft{};
    std::array<double, numSamples> normalRight{};
    legacyDefault.processBlock(input, legacyLeft, legacyRight);
    explicitNormal.processBlock(input, normalLeft, normalRight);

    if (!expectSamplesBitExact("Normal legacy path left", normalLeft, legacyLeft)
        || !expectSamplesBitExact("Normal legacy path right", normalRight, legacyRight))
      return false;
  }

  dsp::DelayBand invalidPolarity(30.0);
  invalidPolarity.setDelaySignalPolarity(static_cast<dsp::DelaySignalPolarity>(999));
  return invalidPolarity.delaySignalPolarity() == dsp::DelaySignalPolarity::normal;
}

bool testReverseInvertsOnlyAudibleEarlyTapAndPreservesCorrespondingState()
{
  constexpr std::size_t excitationSamples = 513;
  constexpr std::size_t tailSamples = 384;
  constexpr PathScenario scenario{1.35, true, 0.37};

  dsp::DelayBand normal(30.0);
  dsp::DelayBand reverse(30.0);
  configureBand(normal, scenario, excitationSamples);
  configureBand(reverse, scenario, excitationSamples);
  reverse.setDelaySignalPolarity(dsp::DelaySignalPolarity::reverse);

  const auto input = deterministicInput<excitationSamples>();
  std::array<double, excitationSamples> normalLeft{};
  std::array<double, excitationSamples> normalRight{};
  std::array<double, excitationSamples> reverseLeft{};
  std::array<double, excitationSamples> reverseRight{};
  normal.processBlock(input, normalLeft, normalRight);
  reverse.processBlock(input, reverseLeft, reverseRight);

  if (!expectExactPolarityInversion("Reverse early-TAP left", reverseLeft, normalLeft)
      || !expectExactPolarityInversion("Reverse early-TAP right", reverseRight, normalRight)
      || !expectNear("Reverse early-TAP modulation phase",
                     reverse.modulationPhase().value,
                     normal.modulationPhase().value,
                     0.0))
    return false;

  // Changing only the output polarity to Normal makes all future behavior
  // bit-identical. This proves corresponding delay, feedback, modulation,
  // loop-filter, and TAP-output-filter states evolved identically without
  // exposing or comparing the two different filter instances to each other.
  reverse.setDelaySignalPolarity(dsp::DelaySignalPolarity::normal);
  const std::array<double, tailSamples> silence{};
  std::array<double, tailSamples> normalTailLeft{};
  std::array<double, tailSamples> normalTailRight{};
  std::array<double, tailSamples> reverseTailLeft{};
  std::array<double, tailSamples> reverseTailRight{};
  normal.processBlock(silence, normalTailLeft, normalTailRight);
  reverse.processBlock(silence, reverseTailLeft, reverseTailRight);

  return expectSamplesBitExact("corresponding early-TAP states left",
                               reverseTailLeft,
                               normalTailLeft)
         && expectSamplesBitExact("corresponding early-TAP states right",
                                  reverseTailRight,
                                  normalTailRight)
         && expectNear("corresponding early-TAP final phase",
                       reverse.modulationPhase().value,
                       normal.modulationPhase().value,
                       0.0);
}

bool testReverseFullTapPreservesFeedbackTimingAndAmplitude()
{
  constexpr std::size_t numSamples = 128;
  constexpr PathScenario scenario{0.0, false, 1.0};
  dsp::DelayBand normal(30.0);
  dsp::DelayBand reverse(30.0);
  configureBand(normal, scenario, numSamples);
  configureBand(reverse, scenario, numSamples);
  reverse.setDelaySignalPolarity(dsp::DelaySignalPolarity::reverse);

  const std::array<double, numSamples> impulse{1.0};
  std::array<double, numSamples> normalLeft{};
  std::array<double, numSamples> normalRight{};
  std::array<double, numSamples> reverseLeft{};
  std::array<double, numSamples> reverseRight{};
  normal.processBlock(impulse, normalLeft, normalRight);
  reverse.processBlock(impulse, reverseLeft, reverseRight);

  double audibleMagnitude = 0.0;
  for (const double sample : normalLeft)
    audibleMagnitude += std::abs(sample);
  if (audibleMagnitude == 0.0)
  {
    std::cerr << "full-TAP feedback reference produced no audible output\n";
    return false;
  }

  return expectExactPolarityInversion("Reverse full-TAP feedback left", reverseLeft, normalLeft)
         && expectExactPolarityInversion("Reverse full-TAP feedback right", reverseRight, normalRight);
}

bool testDisabledSemanticsRemainUnchangedByPolarity()
{
  constexpr PathScenario scenario{0.8, true, 0.55};
  dsp::DelayBand normal(30.0);
  dsp::DelayBand reverse(30.0);
  configureBand(normal, scenario, 128);
  configureBand(reverse, scenario, 128);
  reverse.setDelaySignalPolarity(dsp::DelaySignalPolarity::reverse);

  const std::array<double, 32> impulse{1.0};
  std::array<double, 32> discardedLeft{};
  std::array<double, 32> discardedRight{};
  normal.processBlock(impulse, discardedLeft, discardedRight);
  reverse.processBlock(impulse, discardedLeft, discardedRight);

  normal.setEnabled(false);
  reverse.setEnabled(false);
  const auto rejectedInput = deterministicInput<64>();
  std::array<double, 64> normalMutedLeft{};
  std::array<double, 64> normalMutedRight{};
  std::array<double, 64> reverseMutedLeft{};
  std::array<double, 64> reverseMutedRight{};
  normal.processBlock(rejectedInput, normalMutedLeft, normalMutedRight);
  reverse.processBlock(rejectedInput, reverseMutedLeft, reverseMutedRight);
  const std::array<double, 64> silence{};
  if (!expectSamplesBitExact("Normal disabled output left", normalMutedLeft, silence)
      || !expectSamplesBitExact("Normal disabled output right", normalMutedRight, silence)
      || !expectSamplesBitExact("Reverse disabled output left", reverseMutedLeft, silence)
      || !expectSamplesBitExact("Reverse disabled output right", reverseMutedRight, silence))
    return false;

  reverse.setDelaySignalPolarity(dsp::DelaySignalPolarity::normal);
  normal.setEnabled(true);
  reverse.setEnabled(true);
  std::array<double, 128> normalTailLeft{};
  std::array<double, 128> normalTailRight{};
  std::array<double, 128> reverseTailLeft{};
  std::array<double, 128> reverseTailRight{};
  const std::array<double, 128> futureSilence{};
  normal.processBlock(futureSilence, normalTailLeft, normalTailRight);
  reverse.processBlock(futureSilence, reverseTailLeft, reverseTailRight);

  return expectSamplesBitExact("polarity-independent disabled history left",
                               reverseTailLeft,
                               normalTailLeft)
         && expectSamplesBitExact("polarity-independent disabled history right",
                                  reverseTailRight,
                                  normalTailRight);
}

bool testPolarityProcessingDoesNotAllocate()
{
  constexpr PathScenario scenario{1.35, true, 0.37};
  dsp::DelayBand band(700.0);
  configureBand(band, scenario, 256);
  std::array<double, 256> input{};
  std::array<double, 256> left{};
  std::array<double, 256> right{};

  beginAllocationTracking();
  band.setDelaySignalPolarity(dsp::DelaySignalPolarity::reverse);
  band.processBlock(input, left, right);
  band.setDelaySignalPolarity(dsp::DelaySignalPolarity::normal);
  band.processBlock(input, left, right);
  const std::size_t allocations = endAllocationTracking();
  if (allocations != 0)
  {
    std::cerr << "real-time allocation: polarity processing made " << allocations
              << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"DelayBand polarity: Normal is bit-exact across established paths",
           testNormalIsBitExactAcrossEstablishedProcessingPaths},
  TestCase{"DelayBand polarity: Reverse only inverts early-TAP output and preserves corresponding state",
           testReverseInvertsOnlyAudibleEarlyTapAndPreservesCorrespondingState},
  TestCase{"DelayBand polarity: Reverse preserves full-TAP feedback timing and amplitude",
           testReverseFullTapPreservesFeedbackTimingAndAmplitude},
  TestCase{"DelayBand polarity: disabled semantics and history are unchanged",
           testDisabledSemanticsRemainUnchangedByPolarity},
  TestCase{"DelayBand polarity: processing performs no allocations",
           testPolarityProcessingDoesNotAllocate},
};

} // namespace

TestSuite delayBandSignalPolarityTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
