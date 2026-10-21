#include "../dsp/PostAmpEQProcessor.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <vector>

namespace holdsworth::test
{
namespace
{
using EQ = dsp::PostAmpEQProcessor;
using Controls = dsp::PostAmpEQControls;
constexpr std::array rates{44100., 48000., 88200., 96000., 176400., 192000.};
constexpr double outputTolerance = 1.e-11;
enum class Source : std::size_t { tone, noise, impulse, silence };
constexpr std::array<const char*, 4> sourceNames{"tone", "noise", "impulse", "silence"};

std::vector<double> continuousSource(double fs, double seconds, Source source)
{
  std::vector<double> samples(static_cast<std::size_t>(std::ceil(fs * seconds)));
  std::uint32_t random = 0x7a22814U;
  const auto period = static_cast<std::size_t>(std::ceil(fs * .011));
  for (std::size_t n = 0; n < samples.size(); ++n)
  {
    const double time = static_cast<double>(n) / fs;
    random = 1664525U * random + 1013904223U;
    switch (source)
    {
      case Source::tone:
        samples[n] = .1 * std::sin(2. * std::numbers::pi * 440. * time)
          + .03 * std::sin(2. * std::numbers::pi * 1777. * time);
        break;
      case Source::noise: samples[n] = .06 * (2. * static_cast<double>(random) / 4294967295. - 1.); break;
      case Source::impulse: samples[n] = n % period == 0 ? .1 : 0.; break;
      case Source::silence: break;
    }
  }
  return samples;
}

void prepare(EQ& eq, double fs, const Controls& controls)
{
  eq.prepare(fs, 1);
  eq.setControls(controls);
  eq.reset();
}

double tick(EQ& eq, double input)
{
  std::array<double, 1> x{input}, y{};
  eq.processBlock(x, y);
  return y[0];
}

// Independent, closed-form elapsed-time envelope. Production uses accumulated
// increments; this computes every value from the reversal origin and position.
struct LinearEnvelope final
{
  double current = 1., origin = 1., target = 1.;
  std::size_t position = 0, duration = 1;
  void set(bool enabled, std::size_t samples)
  {
    origin = current;
    target = enabled ? 1. : 0.;
    position = 0;
    duration = samples;
  }
  double next()
  {
    if (position < duration)
    {
      ++position;
      current = position == duration ? target
        : origin + (target - origin) * static_cast<double>(position) / static_cast<double>(duration);
    }
    return current;
  }
};

Controls colored()
{
  Controls controls;
  controls.enabled = controls.highPassEnabled = controls.lowPassEnabled = true;
  controls.highPassHz = 95.;
  controls.lowPassHz = 6500.;
  controls.bands = {{{240., 5., .8}, {1100., -4., 1.1}, {3600., 3., 1.4}}};
  controls.trimDb = -2.;
  return controls;
}

// mode 0: complete colored EQ; 1: isolated HP; 2: isolated LP. Its always-on
// parallel render retains exactly the same filter history, independently of
// the switched output. Events reverse fades before they can finish.
bool fadeReference(unsigned mode, double& worst, double& largestJump, std::size_t& comparisons)
{
  constexpr std::array eventSeconds{.012, .0137, .017, .021, .0213, .024, .040, .050, .051, .065};
  for (double fs : rates)
    for (std::size_t source = 0; source < sourceNames.size(); ++source)
    {
      auto controls = mode == 0 ? colored() : Controls{};
      controls.enabled = true;
      if (mode == 1) { controls.highPassEnabled = true; controls.highPassHz = 120.; }
      if (mode == 2) { controls.lowPassEnabled = true; controls.lowPassHz = 5000.; }
      EQ switched, alwaysOn;
      prepare(switched, fs, controls);
      prepare(alwaysOn, fs, controls);
      const auto input = continuousSource(fs, .085, static_cast<Source>(source));
      std::array<std::size_t, eventSeconds.size()> events{};
      for (std::size_t n = 0; n < events.size(); ++n)
        events[n] = static_cast<std::size_t>(std::ceil(eventSeconds[n] * fs));
      const auto fadeSamples = static_cast<std::size_t>(std::ceil(.010 * fs));
      LinearEnvelope envelope;
      std::size_t event = 0;
      double previous = 0.;
      for (std::size_t n = 0; n < input.size(); ++n)
      {
        if (event < events.size() && n == events[event])
        {
          const bool enabled = event % 2 != 0;
          if (mode == 0) controls.enabled = enabled;
          else if (mode == 1) controls.highPassEnabled = enabled;
          else controls.lowPassEnabled = enabled;
          switched.setControls(controls);
          envelope.set(enabled, fadeSamples);
          ++event;
        }
        const double actual = tick(switched, input[n]);
        const double wet = tick(alwaysOn, input[n]);
        const double amount = envelope.next();
        const double expected = amount == 0. ? input[n]
          : amount == 1. ? wet : input[n] + amount * (wet - input[n]);
        const double error = std::abs(actual - expected);
        worst = std::max(worst, error);
        if (n > 0) largestJump = std::max(largestJump, std::abs(actual - previous));
        previous = actual;
        if (!std::isfinite(actual) || !expectNear("EQ independent reversed fade output", actual, expected, outputTolerance))
          return false;
        ++comparisons;
      }
      if (event != events.size() || envelope.current != envelope.target) return false;
    }
  return true;
}

bool coloredGlobalFadeReversals()
{
  double worst = 0., largestJump = 0.;
  std::size_t comparisons = 0;
  const bool passed = fadeReference(0, worst, largestJump, comparisons);
  std::cout << std::setprecision(17) << "METRIC eq colored_global_reversals samples=" << comparisons
            << " max_envelope_error=" << worst << " max_source_inclusive_adjacent_jump=" << largestJump << '\n';
  return passed;
}

bool isolatedCutFadeReversals()
{
  for (unsigned mode : {1U, 2U})
  {
    double worst = 0., largestJump = 0.;
    std::size_t comparisons = 0;
    if (!fadeReference(mode, worst, largestJump, comparisons)) return false;
    std::cout << "METRIC eq " << (mode == 1 ? "hp" : "lp") << "_reversals samples=" << comparisons
              << " max_envelope_error=" << worst << " max_source_inclusive_adjacent_jump=" << largestJump << '\n';
  }
  return true;
}

struct BellPrimitives final { double g, k, m; };
BellPrimitives primitives(double fs, const dsp::PostAmpEQBandControls& band)
{
  const double amplitude = std::pow(10., band.gainDb / 40.);
  const double damping = 1. / (band.q * amplitude);
  return {std::tan(std::numbers::pi * band.frequencyHz / fs), damping, damping * (amplitude * amplitude - 1.)};
}
double firstImpulseGain(const BellPrimitives& primitive)
{
  // The zero-history first output of a peaking SVF is its direct gain.
  return 1. + primitive.m * primitive.g / (1. + primitive.g * (primitive.g + primitive.k));
}

bool normalBandMoves()
{
  std::array<double, 4> firstDelta{}, adjacentJump{}, residualJump{}, settledError{};
  double firstStepError = 0., largestScaledFirstFraction = 0.;
  for (double fs : rates)
    for (unsigned parameter = 0; parameter < 3; ++parameter)
    {
      Controls original;
      original.enabled = true;
      original.bands[0] = {1000., 6., .7};
      auto changed = original;
      if (parameter == 0) changed.bands[0].frequencyHz = 1200.;
      else if (parameter == 1) changed.bands[0].gainDb = 9.;
      else changed.bands[0].q = 1.2;
      const auto rampSamples = static_cast<std::size_t>(std::ceil(.020 * fs));
      const auto oldPrimitive = primitives(fs, original.bands[0]);
      const auto newPrimitive = primitives(fs, changed.bands[0]);
      const double count = static_cast<double>(rampSamples);
      const BellPrimitives first{
        oldPrimitive.g + (newPrimitive.g - oldPrimitive.g) / count,
        oldPrimitive.k + (newPrimitive.k - oldPrimitive.k) / count,
        oldPrimitive.m + (newPrimitive.m - oldPrimitive.m) / count};
      EQ impulse;
      prepare(impulse, fs, original);
      impulse.setControls(changed);
      const double actual = tick(impulse, .1);
      const double expected = .1 * firstImpulseGain(first);
      const double oldOutput = .1 * firstImpulseGain(oldPrimitive);
      const double instantOutput = .1 * firstImpulseGain(newPrimitive);
      firstStepError = std::max(firstStepError, std::abs(actual - expected));
      if (!expectNear("EQ actual first sample follows 1/N primitive move", actual, expected, outputTolerance)) return false;
      // These fixtures have distinct direct gains. This rejects an immediate
      // coefficient swap while measuring the observed 20 ms step fraction.
      if (std::abs(instantOutput - oldOutput) < 1.e-8
          || std::abs(actual - oldOutput) >= std::abs(instantOutput - oldOutput) / 100.) return false;
      largestScaledFirstFraction = std::max(largestScaledFirstFraction,
        count * std::abs((actual - oldOutput) / (instantOutput - oldOutput)));

      for (std::size_t source = 0; source < sourceNames.size(); ++source)
      {
        EQ moving, frozenOld, frozenTarget;
        prepare(moving, fs, original);
        prepare(frozenOld, fs, original);
        prepare(frozenTarget, fs, changed);
        const auto input = continuousSource(fs, .24, static_cast<Source>(source));
        const auto event = static_cast<std::size_t>(std::ceil(.040 * fs));
        const auto finalWindow = static_cast<std::size_t>(std::ceil(.010 * fs));
        double previous = 0., previousResidual = 0.;
        for (std::size_t n = 0; n < input.size(); ++n)
        {
          if (n == event) moving.setControls(changed);
          const double output = tick(moving, input[n]);
          const double old = tick(frozenOld, input[n]);
          const double target = tick(frozenTarget, input[n]);
          const double residual = output - old;
          if (!std::isfinite(output)) return false;
          if (n == event) firstDelta[source] = std::max(firstDelta[source], std::abs(residual));
          if (n >= event)
          {
            adjacentJump[source] = std::max(adjacentJump[source], std::abs(output - previous));
            residualJump[source] = std::max(residualJump[source], std::abs(residual - previousResidual));
          }
          if (n + finalWindow >= input.size())
          {
            settledError[source] = std::max(settledError[source], std::abs(output - target));
            if (!expectNear("EQ normal move settles to unchanged target reference", output, target, outputTolerance)) return false;
          }
          previous = output;
          previousResidual = residual;
        }
      }
    }
  std::cout << "METRIC eq normal_moves first_impulse_step_error=" << firstStepError
            << " max_first_fraction_times_20ms_samples=" << largestScaledFirstFraction << '\n';
  for (std::size_t source = 0; source < sourceNames.size(); ++source)
    std::cout << "METRIC eq normal_moves source=" << sourceNames[source]
              << " first_event_delta_from_frozen=" << firstDelta[source]
              << " max_source_inclusive_adjacent_jump=" << adjacentJump[source]
              << " max_adjacent_residual_change=" << residualJump[source]
              << " final_10ms_target_error=" << settledError[source] << '\n';
  return true;
}
} // namespace

TestSuite postAmpEQTransitionTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Post-amp EQ colored bypass independent envelope and reversals", coloredGlobalFadeReversals},
    TestCase{"Post-amp EQ isolated HP LP independent envelope and reversals", isolatedCutFadeReversals},
    TestCase{"Post-amp EQ normal control output continuity and settling", normalBandMoves}
  };
  return tests;
}
} // namespace holdsworth::test
