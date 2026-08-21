#include "../dsp/DelayModulator.h"
#include "../presets/YamahaModulationSourceValues.h"
#include "TestHarness.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_convertible_v<double, holdsworth::dsp::ModulationRateHz>);
static_assert(!std::is_convertible_v<double, holdsworth::dsp::ModulationDepthMs>);
static_assert(!std::is_convertible_v<double, holdsworth::dsp::ModulationPhaseCycles>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaSpeedControlValue,
                                     holdsworth::dsp::ModulationRateHz>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaDepthControlValue,
                                     holdsworth::dsp::ModulationDepthMs>);
static_assert(!std::is_convertible_v<holdsworth::presets::YamahaWaveformControlValue,
                                     holdsworth::dsp::ModulationWaveform>);
static_assert(!std::is_convertible_v<holdsworth::dsp::ModulationWaveform,
                                     holdsworth::presets::YamahaWaveformControlValue>);

namespace holdsworth::test
{
namespace
{

bool testKnownSineSequenceAndPhaseConvention()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{1.0});
  modulator.setDepth(dsp::ModulationDepthMs{2.0});
  modulator.prepare(4.0);

  const std::array<double, 4> expectedOffsets{0.0, 2.0, 0.0, -2.0};
  const std::array<double, 4> expectedPhases{0.0, 0.25, 0.5, 0.75};
  for (std::size_t i = 0; i < expectedOffsets.size(); ++i)
  {
    if (!expectNear("known phase", modulator.currentPhase().value, expectedPhases[i])
        || !expectNear("known sine offset", modulator.nextOffsetMs(), expectedOffsets[i]))
      return false;
  }

  return expectNear("phase wrapped after one cycle", modulator.currentPhase().value, 0.0);
}

bool testExplicitSineIsBitExactWithLegacyDefault()
{
  constexpr std::size_t numSamples =
    2 * dsp::DelayModulator::kCacheResynchronizationInterval + 37;

  dsp::DelayModulator legacyDefault;
  dsp::DelayModulator explicitSine;
  for (dsp::DelayModulator* modulator : std::array{&legacyDefault, &explicitSine})
  {
    modulator->setRate(dsp::ModulationRateHz{7.123456789});
    modulator->setDepth(dsp::ModulationDepthMs{0.875});
    modulator->setPhase(dsp::ModulationPhaseCycles{0.137});
    modulator->prepare(48000.0);
  }
  explicitSine.setWaveform(dsp::ModulationWaveform::sine);

  for (std::size_t sample = 0; sample < numSamples; ++sample)
  {
    const std::array defaultOutput{legacyDefault.nextOffsetMs()};
    const std::array explicitOutput{explicitSine.nextOffsetMs()};
    if (!expectSamplesBitExact("explicit sine legacy output", explicitOutput, defaultOutput)
        || !expectNear("explicit sine legacy phase",
                       explicitSine.currentPhase().value,
                       legacyDefault.currentPhase().value,
                       0.0))
      return false;
  }

  return explicitSine.waveform() == dsp::ModulationWaveform::sine;
}

bool testTriangleQuarterCycleLandmarks()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{0.0});
  modulator.setDepth(dsp::ModulationDepthMs{2.0});
  modulator.setWaveform(dsp::ModulationWaveform::triangle);
  modulator.prepare(48000.0);

  constexpr std::array<double, 4> phases{0.0, 0.25, 0.5, 0.75};
  constexpr std::array<double, 4> expected{0.0, 2.0, 0.0, -2.0};
  for (std::size_t index = 0; index < phases.size(); ++index)
  {
    modulator.setPhase(dsp::ModulationPhaseCycles{phases[index]});
    if (!expectNear("triangle landmark", modulator.nextOffsetMs(), expected[index], 0.0))
      return false;
  }
  return true;
}

bool testSawDirectionsAndCycleDiscontinuities()
{
  constexpr std::array<double, 5> expectedSawUp{1.0, 0.5, 0.0, -0.5, 1.0};
  constexpr std::array<double, 5> expectedSawDown{-1.0, -0.5, 0.0, 0.5, -1.0};

  const auto render = [](const dsp::ModulationWaveform waveform) {
    dsp::DelayModulator modulator;
    modulator.setRate(dsp::ModulationRateHz{1.0});
    modulator.setDepth(dsp::ModulationDepthMs{1.0});
    modulator.setWaveform(waveform);
    modulator.prepare(4.0);

    std::array<double, 5> output{};
    for (double& sample : output)
      sample = modulator.nextOffsetMs();
    return output;
  };

  const auto sawUp = render(dsp::ModulationWaveform::sawUp);
  const auto sawDown = render(dsp::ModulationWaveform::sawDown);
  return expectSamples("Saw Up pitch-rise delay ramp and wrap", sawUp, expectedSawUp, 0.0)
         && expectSamples("Saw Down pitch-fall delay ramp and wrap", sawDown, expectedSawDown, 0.0);
}

bool testWaveformsShareClockAndRemainBounded()
{
  constexpr std::array waveforms{dsp::ModulationWaveform::sine,
                                 dsp::ModulationWaveform::triangle,
                                 dsp::ModulationWaveform::sawUp,
                                 dsp::ModulationWaveform::sawDown};
  std::array<dsp::DelayModulator, waveforms.size()> modulators{};
  for (std::size_t index = 0; index < modulators.size(); ++index)
  {
    modulators[index].setRate(dsp::ModulationRateHz{17.25});
    modulators[index].setDepth(dsp::ModulationDepthMs{1.0});
    modulators[index].setPhase(dsp::ModulationPhaseCycles{0.123});
    modulators[index].setWaveform(waveforms[index]);
    modulators[index].prepare(1000.0);
  }

  for (std::size_t sample = 0;
       sample < 3 * dsp::DelayModulator::kCacheResynchronizationInterval + 17;
       ++sample)
  {
    const double referencePhase = modulators.front().currentPhase().value;
    for (std::size_t index = 0; index < modulators.size(); ++index)
    {
      dsp::DelayModulator& modulator = modulators[index];
      if (!expectNear("waveform-independent phase", modulator.currentPhase().value, referencePhase, 0.0))
        return false;
      const double value = modulator.nextOffsetMs();
      // The legacy recursive sine cache has its existing tiny floating-point
      // drift between resynchronizations. Keep that path bit-exact and apply a
      // numerical tolerance only to it; the new arithmetic waves are bounded
      // exactly by construction.
      const double bound = waveforms[index] == dsp::ModulationWaveform::sine ? 1.0 + 5.0e-12 : 1.0;
      if (value < -bound || value > bound)
      {
        std::cerr << "waveform output left [-1, 1] at sample " << sample << '\n';
        return false;
      }
    }
  }
  return true;
}

bool testWaveformChangesPreserveParametersPhaseAndReset()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{3.25});
  modulator.setDepth(dsp::ModulationDepthMs{0.75});
  modulator.setPhase(dsp::ModulationPhaseCycles{0.2});
  modulator.prepare(100.0);
  static_cast<void>(modulator.nextOffsetMs());
  const double phaseBeforeChange = modulator.currentPhase().value;

  modulator.setWaveform(dsp::ModulationWaveform::triangle);
  if (modulator.waveform() != dsp::ModulationWaveform::triangle
      || !expectNear("waveform change requested rate", modulator.requestedRate().value, 3.25, 0.0)
      || !expectNear("waveform change effective rate", modulator.effectiveRate().value, 3.25, 0.0)
      || !expectNear("waveform change depth", modulator.depth().value, 0.75, 0.0)
      || !expectNear("waveform change current phase", modulator.currentPhase().value, phaseBeforeChange, 0.0)
      || !expectNear("waveform change reset phase", modulator.resetPhase().value, 0.2, 0.0))
    return false;

  std::array<double, 16> first{};
  std::array<double, 16> reset{};
  modulator.reset();
  for (double& sample : first)
    sample = modulator.nextOffsetMs();
  modulator.reset();
  for (double& sample : reset)
    sample = modulator.nextOffsetMs();

  modulator.setWaveform(static_cast<dsp::ModulationWaveform>(999));
  return expectSamplesBitExact("waveform reset deterministic", reset, first)
         && modulator.waveform() == dsp::ModulationWaveform::sine;
}

bool testSetPhaseWrappingAndReset()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{1.0});
  modulator.setDepth(dsp::ModulationDepthMs{3.0});
  modulator.setPhase(dsp::ModulationPhaseCycles{-0.25});
  modulator.prepare(8.0);

  if (!expectNear("negative phase wraps", modulator.currentPhase().value, 0.75)
      || !expectNear("wrapped phase waveform", modulator.nextOffsetMs(), -3.0))
    return false;

  modulator.reset();
  if (!expectNear("reset restores configured phase", modulator.currentPhase().value, 0.75)
      || !expectNear("reset restores configured waveform", modulator.nextOffsetMs(), -3.0))
    return false;

  modulator.setPhase(dsp::ModulationPhaseCycles{1.25});
  return expectNear("positive phase wraps", modulator.resetPhase().value, 0.25)
         && expectNear("setPhase immediately rephases", modulator.currentPhase().value, 0.25)
         && expectNear("rephased waveform", modulator.nextOffsetMs(), 3.0);
}

bool testZeroRateHoldsPhase()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{0.0});
  modulator.setDepth(dsp::ModulationDepthMs{2.0});
  modulator.setPhase(dsp::ModulationPhaseCycles{0.125});
  modulator.prepare(48000.0);

  const double expectedOffset = std::sqrt(2.0);
  for (std::size_t i = 0; i < dsp::DelayModulator::kCacheResynchronizationInterval + 17; ++i)
  {
    if (!expectNear("zero-rate phase", modulator.currentPhase().value, 0.125)
        || !expectNear("zero-rate waveform", modulator.nextOffsetMs(), expectedOffset, 2.0e-12))
      return false;
  }
  return true;
}

bool testRateChangePreservesAuthoritativePhase()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{10.0});
  modulator.setDepth(dsp::ModulationDepthMs{1.0});
  modulator.prepare(100.0);

  if (!expectNear("rate-change initial output", modulator.nextOffsetMs(), 0.0)
      || !expectNear("phase before rate change", modulator.currentPhase().value, 0.1))
    return false;

  modulator.setRate(dsp::ModulationRateHz{20.0});
  return expectNear("rate change preserves phase", modulator.currentPhase().value, 0.1)
         && expectNear("rate change preserves waveform",
                       modulator.nextOffsetMs(),
                       std::sin(0.2 * std::numbers::pi_v<double>))
         && expectNear("new rate advances phase", modulator.currentPhase().value, 0.3);
}

bool testParameterSanitizationAndTechnicalRateLimit()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{800.0});
  modulator.prepare(1000.0);

  if (!expectNear("requested rate remains physical value", modulator.requestedRate().value, 800.0)
      || !expectNear("technical rate limit", modulator.effectiveRate().value, 500.0))
    return false;

  modulator.setRate(dsp::ModulationRateHz{-1.0});
  if (!expectNear("negative requested rate", modulator.requestedRate().value, 0.0)
      || !expectNear("negative effective rate", modulator.effectiveRate().value, 0.0))
    return false;

  modulator.setRate(dsp::ModulationRateHz{std::numeric_limits<double>::infinity()});
  if (!expectNear("non-finite rate", modulator.requestedRate().value, 0.0))
    return false;

  modulator.setDepth(dsp::ModulationDepthMs{4.5});
  if (!expectNear("finite depth", modulator.depth().value, 4.5))
    return false;
  modulator.setDepth(dsp::ModulationDepthMs{-1.0});
  if (!expectNear("negative depth", modulator.depth().value, 0.0))
    return false;
  modulator.setDepth(dsp::ModulationDepthMs{std::numeric_limits<double>::quiet_NaN()});
  if (!expectNear("non-finite depth", modulator.depth().value, 0.0))
    return false;

  modulator.setPhase(dsp::ModulationPhaseCycles{std::numeric_limits<double>::quiet_NaN()});
  return expectNear("non-finite reset phase", modulator.resetPhase().value, 0.0)
         && expectNear("non-finite current phase", modulator.currentPhase().value, 0.0);
}

bool testPrepareValidationAndReprepareReset()
{
  dsp::DelayModulator modulator;
  for (const double invalidSampleRate : std::array{
         0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
  {
    try
    {
      modulator.prepare(invalidSampleRate);
    }
    catch (const std::invalid_argument&)
    {
      continue;
    }
    catch (...)
    {
      std::cerr << "invalid sample rate threw an unexpected exception type\n";
      return false;
    }

    std::cerr << "invalid sample rate was accepted\n";
    return false;
  }

  modulator.setRate(dsp::ModulationRateHz{1.0});
  modulator.setPhase(dsp::ModulationPhaseCycles{0.25});
  modulator.prepare(8.0);
  static_cast<void>(modulator.nextOffsetMs());
  modulator.prepare(16.0);
  return modulator.isPrepared()
         && expectNear("reprepare restores configured phase", modulator.currentPhase().value, 0.25)
         && expectNear("reprepare updates effective rate", modulator.effectiveRate().value, 1.0);
}

bool testLongRunWaveformTracksAuthoritativePhase()
{
  constexpr double sampleRate = 48000.0;
  constexpr double rate = 7.123456789;
  constexpr double depth = 0.75;
  constexpr double initialPhase = 0.137;
  constexpr std::size_t numSamples = dsp::DelayModulator::kCacheResynchronizationInterval * 24 + 137;

  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{rate});
  modulator.setDepth(dsp::ModulationDepthMs{depth});
  modulator.setPhase(dsp::ModulationPhaseCycles{initialPhase});
  modulator.prepare(sampleRate);

  const long double phaseIncrement = static_cast<long double>(rate) / sampleRate;
  for (std::size_t i = 0; i < numSamples; ++i)
  {
    const double reportedPhase = modulator.currentPhase().value;
    if (reportedPhase < 0.0 || reportedPhase >= 1.0)
    {
      std::cerr << "long-run phase left [0, 1) at sample " << i << '\n';
      return false;
    }

    const double expectedOffset = depth * std::sin(2.0 * std::numbers::pi_v<double> * reportedPhase);
    const double generatedOffset = modulator.nextOffsetMs();
    if (!nearlyEqual(generatedOffset, expectedOffset, 5.0e-12))
    {
      std::cerr << "waveform diverged from reported phase at sample " << i << ": value was "
                << generatedOffset << ", expected " << expectedOffset << '\n';
      return false;
    }

    const long double idealUnwrapped = static_cast<long double>(initialPhase)
                                       + static_cast<long double>(i + 1) * phaseIncrement;
    const double idealPhase = static_cast<double>(idealUnwrapped - std::floor(idealUnwrapped));
    // Repeated binary64 accumulation is the authoritative clock, so allow its
    // small rounding difference from this independently multiplied long-double
    // reference while still detecting any cache/clock-scale divergence.
    if (!nearlyEqual(modulator.currentPhase().value, idealPhase, 1.0e-11))
    {
      std::cerr << "reported phase diverged from logical time at sample " << i << '\n';
      return false;
    }
  }
  return true;
}

bool testHotPathDoesNotAllocate()
{
  dsp::DelayModulator modulator;
  modulator.setRate(dsp::ModulationRateHz{0.6});
  modulator.setDepth(dsp::ModulationDepthMs{0.75});
  modulator.prepare(48000.0);

  beginAllocationTracking();
  for (const dsp::ModulationWaveform waveform : std::array{dsp::ModulationWaveform::sine,
                                                           dsp::ModulationWaveform::triangle,
                                                           dsp::ModulationWaveform::sawUp,
                                                           dsp::ModulationWaveform::sawDown})
  {
    modulator.setWaveform(waveform);
    for (std::size_t i = 0; i < 2 * dsp::DelayModulator::kCacheResynchronizationInterval + 1; ++i)
      static_cast<void>(modulator.nextOffsetMs());
  }
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
  {
    std::cerr << "real-time allocation: DelayModulator::nextOffsetMs made " << allocations
              << " allocation(s)\n";
    return false;
  }
  return true;
}

constexpr std::array kTests{
  TestCase{"DelayModulator: phase convention and known sine sequence", testKnownSineSequenceAndPhaseConvention},
  TestCase{"DelayModulator: explicit sine is bit-exact with legacy default",
           testExplicitSineIsBitExactWithLegacyDefault},
  TestCase{"DelayModulator: triangle has quarter-cycle landmarks", testTriangleQuarterCycleLandmarks},
  TestCase{"DelayModulator: saw directions and discontinuities are explicit",
           testSawDirectionsAndCycleDiscontinuities},
  TestCase{"DelayModulator: waveforms share one bounded authoritative clock",
           testWaveformsShareClockAndRemainBounded},
  TestCase{"DelayModulator: waveform changes preserve parameters, phase, and reset",
           testWaveformChangesPreserveParametersPhaseAndReset},
  TestCase{"DelayModulator: setPhase wrapping and reset", testSetPhaseWrappingAndReset},
  TestCase{"DelayModulator: zero rate holds phase", testZeroRateHoldsPhase},
  TestCase{"DelayModulator: rate changes preserve logical time", testRateChangePreservesAuthoritativePhase},
  TestCase{"DelayModulator: parameters sanitize and rate has a separate technical limit",
           testParameterSanitizationAndTechnicalRateLimit},
  TestCase{"DelayModulator: prepare validation and reprepare reset", testPrepareValidationAndReprepareReset},
  TestCase{"DelayModulator: long-run cache follows authoritative phase",
           testLongRunWaveformTracksAuthoritativePhase},
  TestCase{"DelayModulator: hot path performs no allocations", testHotPathDoesNotAllocate},
};

} // namespace

TestSuite delayModulatorTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
