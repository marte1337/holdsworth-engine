#include "../dsp/PostAmpEQControls.h"
#include "TestHarness.h"

#include <array>
#include <cmath>
#include <limits>

namespace holdsworth::test
{
namespace
{
using Parameter = dsp::PostAmpEQParameter;
using Controls = dsp::PostAmpEQControls;

// Independently transcribed from the approved v1 specification. These values
// protect the physical contract shared by the processor and development UI.
struct Contract final
{
  double minimum;
  double maximum;
  double initial;
  bool logarithmic;
  bool boolean;
};
constexpr std::array<Contract, 15> contract{{
  {0., 1., 0., false, true},
  {80., 8000., 250., true, false}, {-12., 12., 0., false, false}, {0.35, 4., 0.7, true, false},
  {80., 8000., 1000., true, false}, {-12., 12., 0., false, false}, {0.35, 4., 0.7, true, false},
  {80., 8000., 3500., true, false}, {-12., 12., 0., false, false}, {0.35, 4., 0.7, true, false},
  {0., 1., 0., false, true}, {20., 200., 80., true, false},
  {0., 1., 0., false, true}, {3000., 16000., 8000., true, false},
  {-24., 6., 0., false, false}
}};

bool specificationAndDefaults()
{
  static_assert(dsp::kPostAmpEQParameterCount == contract.size());
  const Controls controls;
  for (std::size_t index = 0; index < contract.size(); ++index)
  {
    const auto parameter = static_cast<Parameter>(index);
    const auto& actual = dsp::postAmpEQParameterInfo(parameter);
    const auto& expected = contract[index];
    if (actual.minimum != expected.minimum || actual.maximum != expected.maximum
        || actual.defaultValue != expected.initial || actual.logarithmic != expected.logarithmic
        || actual.boolean != expected.boolean
        || dsp::postAmpEQParameterValue(controls, parameter) != expected.initial)
      return false;
  }
  return !controls.enabled && !controls.highPassEnabled && !controls.lowPassEnabled
    && controls.bands[0].frequencyHz == 250. && controls.bands[1].frequencyHz == 1000.
    && controls.bands[2].frequencyHz == 3500. && controls.highPassHz == 80.
    && controls.lowPassHz == 8000. && controls.trimDb == 0.;
}

bool semanticFieldMapping()
{
  constexpr std::array<double, 15> values{{
    1., 123., -2., 1.23, 456., 4., 2.34, 789., -6., 3.45, 1., 91., 1., 7890., -3.
  }};
  Controls controls;
  for (std::size_t index = 0; index < values.size(); ++index)
    if (!dsp::setPostAmpEQParameter(controls, static_cast<Parameter>(index), values[index])) return false;
  if (!controls.enabled || controls.bands[0] != dsp::PostAmpEQBandControls{123., -2., 1.23}
      || controls.bands[1] != dsp::PostAmpEQBandControls{456., 4., 2.34}
      || controls.bands[2] != dsp::PostAmpEQBandControls{789., -6., 3.45}
      || !controls.highPassEnabled || controls.highPassHz != 91.
      || !controls.lowPassEnabled || controls.lowPassHz != 7890. || controls.trimDb != -3.)
    return false;
  for (std::size_t index = 0; index < values.size(); ++index)
    if (dsp::postAmpEQParameterValue(controls, static_cast<Parameter>(index)) != values[index]) return false;
  return true;
}

bool normalizedPhysicalConsistency()
{
  for (std::size_t index = 0; index < contract.size(); ++index)
  {
    const auto parameter = static_cast<Parameter>(index);
    const auto& expected = contract[index];
    for (double normalized : {0., 0.001, 0.1, 0.25, 0.499, 0.5, 0.75, 0.9, 0.999, 1.})
    {
      const double physical = dsp::postAmpEQParameterFromNormalized(parameter, normalized);
      const double reference = expected.boolean ? (normalized >= 0.5 ? 1. : 0.)
        : expected.logarithmic ? std::exp(std::log(expected.minimum)
            + normalized * (std::log(expected.maximum) - std::log(expected.minimum)))
        : expected.minimum + normalized * (expected.maximum - expected.minimum);
      if (!expectNear("EQ independent normalized coordinate", physical, reference,
                      2.e-14 * std::max(1., std::abs(reference)))) return false;
      Controls controls;
      if (!dsp::setPostAmpEQParameter(controls, parameter, physical)
          || dsp::postAmpEQParameterValue(controls, parameter) != physical) return false;
      const double roundTrip = dsp::postAmpEQParameterToNormalized(parameter, physical);
      if (!expectNear("EQ physical / normalized round trip", roundTrip,
                      expected.boolean ? reference : normalized, 2.e-15)) return false;
    }
    if (dsp::postAmpEQParameterFromNormalized(parameter, 0.) != expected.minimum
        || dsp::postAmpEQParameterFromNormalized(parameter, 1.) != expected.maximum
        || dsp::postAmpEQParameterToNormalized(parameter, expected.minimum) != 0.
        || dsp::postAmpEQParameterToNormalized(parameter, expected.maximum) != 1.) return false;
    const double defaultPosition = dsp::postAmpEQParameterToNormalized(parameter, expected.initial);
    if (!expectNear("EQ default coordinate round trip",
                    dsp::postAmpEQParameterFromNormalized(parameter, defaultPosition), expected.initial,
                    2.e-14 * std::max(1., std::abs(expected.initial)))) return false;
  }
  // Exact 0 dB coordinates protect neutral resets after previous non-neutral
  // settings. Frequency and Q round trips may differ by ordinary log rounding.
  for (Parameter gain : {Parameter::band1Gain, Parameter::band2Gain, Parameter::band3Gain})
    if (dsp::postAmpEQParameterToNormalized(gain, 0.) != 0.5
        || dsp::postAmpEQParameterFromNormalized(gain, 0.5) != 0.) return false;
  return dsp::postAmpEQParameterToNormalized(Parameter::trim, 0.) == 0.8
    && dsp::postAmpEQParameterFromNormalized(Parameter::trim, 0.8) == 0.;
}

bool boundsAndInvalidValues()
{
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double infinity = std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < contract.size(); ++index)
  {
    const auto parameter = static_cast<Parameter>(index);
    const auto& expected = contract[index];
    Controls controls;
    if (!dsp::setPostAmpEQParameter(controls, parameter, -1.e100)
        || dsp::postAmpEQParameterValue(controls, parameter) != expected.minimum
        || !dsp::setPostAmpEQParameter(controls, parameter, 1.e100)
        || dsp::postAmpEQParameterValue(controls, parameter) != expected.maximum
        || dsp::postAmpEQParameterFromNormalized(parameter, -1.) != expected.minimum
        || dsp::postAmpEQParameterFromNormalized(parameter, 2.) != expected.maximum
        || dsp::postAmpEQParameterToNormalized(parameter, -1.e100) != 0.
        || dsp::postAmpEQParameterToNormalized(parameter, 1.e100) != 1.) return false;
    const auto saved = controls;
    for (double invalid : {nan, infinity, -infinity})
    {
      if (dsp::setPostAmpEQParameter(controls, parameter, invalid) || controls != saved
          || dsp::postAmpEQParameterFromNormalized(parameter, invalid) != expected.initial
          || dsp::postAmpEQParameterToNormalized(parameter, invalid)
              != dsp::postAmpEQParameterToNormalized(parameter, expected.initial)) return false;
    }
  }
  Controls unchanged;
  const auto before = unchanged;
  for (Parameter invalid : {Parameter::count, static_cast<Parameter>(999)})
    if (dsp::isPostAmpEQParameter(invalid)
        || dsp::setPostAmpEQParameter(unchanged, invalid, 1.) || unchanged != before) return false;
  for (Parameter boolean : {Parameter::enable, Parameter::highPassEnable, Parameter::lowPassEnable})
  {
    if (dsp::postAmpEQParameterFromNormalized(boolean, std::nextafter(0.5, 0.)) != 0.
        || dsp::postAmpEQParameterFromNormalized(boolean, 0.5) != 1.) return false;
  }
  return true;
}

bool transactionalPhysicalSnapshot()
{
  Controls requested;
  requested.enabled = requested.highPassEnabled = requested.lowPassEnabled = true;
  requested.bands = {{{-10., 100., 0.}, {9000., -100., 100.}, {1000., 0., 0.7}}};
  requested.highPassHz = 1.;
  requested.lowPassHz = 100000.;
  requested.trimDb = -100.;
  Controls accepted;
  if (!dsp::sanitizePostAmpEQControls(requested, accepted) || !accepted.enabled
      || !accepted.highPassEnabled || !accepted.lowPassEnabled
      || accepted.bands[0] != dsp::PostAmpEQBandControls{80., 12., 0.35}
      || accepted.bands[1] != dsp::PostAmpEQBandControls{8000., -12., 4.}
      || accepted.bands[2] != requested.bands[2] || accepted.highPassHz != 20.
      || accepted.lowPassHz != 16000. || accepted.trimDb != -24.) return false;
  const auto previous = accepted;
  for (std::size_t index = 0; index < contract.size(); ++index)
  {
    if (contract[index].boolean) continue;
    Controls invalid;
    const auto parameter = static_cast<Parameter>(index);
    // Direct physical storage intentionally simulates a malformed complete
    // producer snapshot, bypassing the already validated per-parameter setter.
    double* field = nullptr;
    if (index >= 1 && index <= 9)
    {
      auto& band = invalid.bands[(index - 1) / 3];
      field = (index - 1) % 3 == 0 ? &band.frequencyHz
        : (index - 1) % 3 == 1 ? &band.gainDb : &band.q;
    }
    else if (parameter == Parameter::highPassFrequency) field = &invalid.highPassHz;
    else if (parameter == Parameter::lowPassFrequency) field = &invalid.lowPassHz;
    else if (parameter == Parameter::trim) field = &invalid.trimDb;
    if (field == nullptr) return false;
    for (double value : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()})
    {
      *field = value;
      if (dsp::sanitizePostAmpEQControls(invalid, accepted) || accepted != previous) return false;
    }
  }
  return true;
}
} // namespace

TestSuite postAmpEQControlTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Post-amp EQ approved parameter ranges and defaults", specificationAndDefaults},
    TestCase{"Post-amp EQ semantic physical field mapping", semanticFieldMapping},
    TestCase{"Post-amp EQ canonical normalized / physical coordinates", normalizedPhysicalConsistency},
    TestCase{"Post-amp EQ bounds and invalid control values", boundsAndInvalidValues},
    TestCase{"Post-amp EQ transactional physical snapshots", transactionalPhysicalSnapshot}
  };
  return tests;
}
} // namespace holdsworth::test
