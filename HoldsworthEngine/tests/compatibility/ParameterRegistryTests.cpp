#include "../../control/ParameterRegistry.h"
#include "../../integration/DevelopmentDelayPresetSelector.h"
#include "../../integration/DevelopmentJRockettAHControls.h"
#include "../../integration/DevelopmentPreNAMSelector.h"
#include "../../integration/DevelopmentTCBLDControlMapping.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
namespace control = holdsworth::control;
namespace dsp = holdsworth::dsp;
namespace integration = holdsworth::integration;
std::uint64_t checks = 0;
std::uint64_t failures = 0;

void check(bool condition, std::string_view description)
{
  ++checks;
  if (!condition)
  {
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
  }
}

bool close(double actual, double expected, double tolerance = 1.0e-12)
{
  return std::abs(actual - expected) <= tolerance * std::max(1.0, std::abs(expected));
}

bool sameBits(double a, double b)
{
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

void testFrozenIdentity()
{
  // Deliberately independent literals: this is an append-only contract snapshot.
  constexpr std::array<std::uint32_t, 46> expectedIds{
    0x10000000, 0x10000001, 0x10000002, 0x10000003, 0x10000004, 0x10000005,
    0x10000006, 0x10000007, 0x10000008, 0x10000009, 0x1000000a, 0x1000000b,
    0x1000000c, 0x20000000, 0x20000100, 0x20000101, 0x20000102, 0x20000200,
    0x20000300, 0x20000301, 0x20000302, 0x20000303, 0x20000304, 0x20000305,
    0x20000306, 0x20000307, 0x20000308, 0x30000000, 0x30000001, 0x30000002,
    0x30000003, 0x30000004, 0x30000005, 0x30000006, 0x30000007, 0x30000008,
    0x30000009, 0x3000000a, 0x3000000b, 0x3000000c, 0x3000000d, 0x3000000e,
    0x40000000, 0x40000001, 0x40000002, 0x50000000};
  constexpr std::array<std::string_view, 46> expectedKeys{
    "he.input.gain_db", "he.gate.threshold_db", "he.tone.bass", "he.tone.middle",
    "he.tone.treble", "he.output.gain_db", "he.gate.enabled", "he.tone.enabled",
    "he.cabinet.enabled", "he.input.calibration_enabled", "he.input.calibration_dbu",
    "he.output.mode", "he.nam.slim", "he.pre_nam.selection", "he.tc_bld.gain_position",
    "he.tc_bld.bass_position", "he.tc_bld.treble_position", "he.mc402.boost_db",
    "he.ah.boost_db", "he.ah.boost_type", "he.ah.boost_emphasis", "he.ah.boost_enabled",
    "he.ah.drive_enabled", "he.ah.drive_gain_position", "he.ah.bass_db", "he.ah.treble_db",
    "he.ah.volume_position", "he.eq.enabled", "he.eq.band1.frequency_hz",
    "he.eq.band1.gain_db", "he.eq.band1.q", "he.eq.band2.frequency_hz",
    "he.eq.band2.gain_db", "he.eq.band2.q", "he.eq.band3.frequency_hz",
    "he.eq.band3.gain_db", "he.eq.band3.q", "he.eq.hp.enabled", "he.eq.hp.frequency_hz",
    "he.eq.lp.enabled", "he.eq.lp.frequency_hz", "he.eq.trim_db", "he.delay.enabled",
    "he.delay.wet", "he.delay.preset", "he.tuner.enabled"};
  check(control::kLegacyParameterCount == 13, "legacy parameter count");
  check(control::kParameterRegistry.size() == 46, "reviewed host slot count");
  for (std::size_t i = 0; i < control::kParameterRegistry.size(); ++i)
  {
    const auto& parameter = control::kParameterRegistry[i];
    check(parameter.hostIndex == i, "host slots retain exact append-only allocation");
    check(parameter.hostIndex < 65536, "ordinary host slots avoid VST3 special parameter IDs");
    check(static_cast<std::uint32_t>(parameter.id) == expectedIds[i], "frozen semantic ID");
    const auto semanticId = static_cast<std::uint32_t>(parameter.id);
    check(semanticId >= 0x10000000, "semantic IDs occupy separate high domains");
    check(semanticId < 65536 || semanticId > 67617,
          "semantic IDs cannot alias VST3 bypass, preset or CC IDs");
    check(parameter.key == expectedKeys[i], "frozen semantic key");
    check(control::parameterAtHostIndex(i) == &parameter, "host index lookup");
    check(control::findParameter(parameter.id) == &parameter, "semantic ID lookup");
    check(parameter.isCurrentlyHostParameter == (i < 13), "actual versus proposed host exposure");
    check(parameter.isCurrentlyHostAutomatable == (i < 13), "actual versus proposed automation");
    for (std::size_t j = 0; j < i; ++j)
    {
      check(parameter.id != control::kParameterRegistry[j].id, "unique semantic IDs");
      check(parameter.key != control::kParameterRegistry[j].key, "unique semantic keys");
    }
  }
  check(control::parameterAtHostIndex(46) == nullptr, "reject first invalid host slot");
  check(control::parameterAtHostIndex(std::numeric_limits<std::size_t>::max()) == nullptr,
        "reject maximal invalid host slot");
  check(control::findParameter(static_cast<control::ParameterId>(0xffffffff)) == nullptr,
        "reject unknown semantic ID");
}

void testFrozenLegacyDescriptors()
{
  using Kind = control::ParameterKind;
  struct Expected
  {
    std::string_view name, unit;
    Kind kind;
    double minimum, maximum, defaultValue, step, normalizedDefault;
  };
  constexpr std::array<Expected, 13> expected{{
    {"Input", "dB", Kind::continuous, -20, 20, 0, .1, .5},
    {"Threshold", "dB", Kind::continuous, -100, 0, -80, .1, .2},
    {"Bass", "", Kind::continuous, 0, 10, 5, .1, .5},
    {"Middle", "", Kind::continuous, 0, 10, 5, .1, .5},
    {"Treble", "", Kind::continuous, 0, 10, 5, .1, .5},
    {"Output", "dB", Kind::continuous, -40, 40, 0, .1, .5},
    {"NoiseGateActive", "", Kind::boolean, 0, 1, 1, 1, 1},
    {"ToneStack", "", Kind::boolean, 0, 1, 1, 1, 1},
    {"IRToggle", "", Kind::boolean, 0, 1, 1, 1, 1},
    {"CalibrateInput", "", Kind::boolean, 0, 1, 0, 1, 0},
    {"InputCalibrationLevel", "dBu", Kind::continuous, -60, 60, 12, .1, .6},
    {"OutputMode", "", Kind::enumeration, 0, 2, 1, 1, .5},
    {"Slim", "", Kind::continuous, 0, 1, 0, .01, 0}
  }};
  for (std::size_t i = 0; i < expected.size(); ++i)
  {
    const auto& parameter = control::kParameterRegistry[i];
    const auto& frozen = expected[i];
    check(parameter.name == frozen.name, "legacy name");
    check(parameter.unit == frozen.unit, "legacy unit");
    check(parameter.kind == frozen.kind, "legacy type");
    check(parameter.minimum == frozen.minimum, "legacy minimum");
    check(parameter.maximum == frozen.maximum, "legacy maximum");
    check(sameBits(parameter.defaultValue, frozen.defaultValue), "legacy exact default");
    check(parameter.displayStep == frozen.step, "legacy display step");
    check(parameter.normalization == control::Normalization::linear, "legacy linear normalization");
    check(close(control::toNormalized(parameter, frozen.defaultValue), frozen.normalizedDefault),
          "legacy normalized default");
    // Deliberately not step-aligned: continuous IParams do not acquire stepping.
    if (parameter.kind == Kind::continuous)
    {
      constexpr double n = .123456789;
      const double physical = frozen.minimum + n * (frozen.maximum - frozen.minimum);
      check(close(control::fromNormalized(parameter, n), physical), "continuous value is not quantized");
    }
  }
  const auto& mode = control::kParameterRegistry[11];
  check(mode.enumLabels.size() == 3 && mode.enumLabels[0] == "Raw"
        && mode.enumLabels[1] == "Normalized" && mode.enumLabels[2] == "Calibrated",
        "frozen legacy output enum order");
}

void testAllConversions()
{
  using Kind = control::ParameterKind;
  constexpr std::array<double, 3> nonfinite{
    std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity()};
  for (const auto& parameter : control::kParameterRegistry)
  {
    check(parameter.minimum < parameter.maximum, "valid descriptor range");
    check(parameter.defaultValue >= parameter.minimum && parameter.defaultValue <= parameter.maximum,
          "default inside range");
    check(control::fromNormalized(parameter, 0) == parameter.minimum, "normalized zero endpoint");
    check(control::fromNormalized(parameter, 1) == parameter.maximum, "normalized one endpoint");
    check(control::toNormalized(parameter, parameter.minimum) == 0, "physical minimum endpoint");
    check(control::toNormalized(parameter, parameter.maximum) == 1, "physical maximum endpoint");
    check(control::fromNormalized(parameter, -10) == parameter.minimum, "low normalized clamp");
    check(control::fromNormalized(parameter, 10) == parameter.maximum, "high normalized clamp");
    check(control::toNormalized(parameter, parameter.minimum - 100) == 0, "low physical clamp");
    check(control::toNormalized(parameter, parameter.maximum + 100) == 1, "high physical clamp");
    check(close(control::fromNormalized(parameter, control::toNormalized(parameter, parameter.defaultValue)),
                parameter.defaultValue), "default round trip");
    for (double value : nonfinite)
    {
      check(sameBits(control::fromNormalized(parameter, value), parameter.defaultValue),
            "future nonfinite ingress falls back to exact default");
      check(control::toNormalized(parameter, value)
              == control::toNormalized(parameter, parameter.defaultValue),
            "future nonfinite physical fallback");
    }
    double previous = parameter.minimum;
    for (std::size_t step = 0; step <= 1024; ++step)
    {
      const double normalized = static_cast<double>(step) / 1024.0;
      const double physical = control::fromNormalized(parameter, normalized);
      check(physical >= previous, "normalization monotonicity");
      check(physical >= parameter.minimum && physical <= parameter.maximum, "normalized result bounded");
      if (parameter.kind == Kind::continuous)
        check(close(control::toNormalized(parameter, physical), normalized), "continuous inverse mapping");
      else
      {
        check(physical == std::round(physical), "discrete mapping yields integral states");
        const double expected = std::round(normalized * (parameter.maximum - parameter.minimum))
                              + parameter.minimum;
        check(physical == expected, "discrete half-up rounding semantics");
      }
      previous = physical;
    }
    if (parameter.kind == Kind::boolean)
    {
      check(parameter.enumLabels.size() == 2, "boolean labels");
      check(control::fromNormalized(parameter, std::nextafter(.5, 0.0)) == 0, "boolean below threshold");
      check(control::fromNormalized(parameter, .5) == 1, "boolean exact threshold");
      check(control::toNormalized(parameter, .499) == 0, "boolean physical below threshold");
      check(control::toNormalized(parameter, .5) == 1, "boolean physical exact threshold");
    }
    if (parameter.kind == Kind::enumeration)
    {
      check(parameter.enumLabels.size() == static_cast<std::size_t>(parameter.maximum) + 1,
            "enum labels cover every stable value");
      for (std::size_t index = 0; index < parameter.enumLabels.size(); ++index)
      {
        const double physical = static_cast<double>(index);
        check(control::fromNormalized(parameter, control::toNormalized(parameter, physical)) == physical,
              "enum value round trip");
      }
    }
  }
}

void testDraftDefaultsAndSafetyGates()
{
  struct Range { double minimum, maximum, defaultValue; };
  constexpr std::array<Range, 33> expected{{
    {0,3,0}, {0,1,.132430924210101}, {0,1,.5}, {0,1,.5}, {0,20,0}, {0,20,0},
    {0,2,1}, {0,1,0}, {0,1,1}, {0,1,0}, {0,1,.5}, {-6,6,0}, {-9,9,0},
    {0,1,.6309573444801932},
    {0,1,0}, {80,8000,250}, {-12,12,0}, {.35,4,.7},
    {80,8000,1000}, {-12,12,0}, {.35,4,.7},
    {80,8000,3500}, {-12,12,0}, {.35,4,.7},
    {0,1,0}, {20,200,80}, {0,1,0}, {3000,16000,8000}, {-24,6,0},
    {0,1,0}, {0,1,.1}, {0,4,0}, {0,1,0}
  }};
  for (std::size_t i = 0; i < expected.size(); ++i)
  {
    const auto& parameter = control::kParameterRegistry[i + 13];
    check(parameter.minimum == expected[i].minimum && parameter.maximum == expected[i].maximum,
          "frozen proposed range");
    check(sameBits(parameter.defaultValue, expected[i].defaultValue), "frozen proposed default");
  }
  const auto& slim = control::kParameterRegistry[12];
  check(slim.isCurrentlyHostAutomatable, "record existing Slim flag without claiming safety");
  check(slim.applicationDomain == control::ApplicationDomain::structuralModel,
        "Slim requires structural application");
  check(slim.automationReadiness == control::AutomationReadiness::requiresSafeStructuralModelUpdates,
        "Slim safety gate remains closed");
  for (std::size_t i : {std::size_t{14}, std::size_t{15}, std::size_t{16}})
  {
    check(control::kParameterRegistry[i].applicationDomain
            == control::ApplicationDomain::coefficientPreparation, "TC coefficient preparation domain");
    check(control::kParameterRegistry[i].automationReadiness
            == control::AutomationReadiness::requiresSafeCoefficientUpdates, "TC safety gate remains closed");
  }
  for (std::size_t i : {std::size_t{42}, std::size_t{43}, std::size_t{44}})
    check(control::kParameterRegistry[i].automationReadiness
            == control::AutomationReadiness::requiresTransitionValidation, "delay transitions are not promised safe yet");
}

void testEQCanonicalContract()
{
  dsp::PostAmpEQControls restored;
  restored.enabled = true;
  restored.bands[0].gainDb = 12;
  restored.trimDb = -12;
  const dsp::PostAmpEQControls defaults;
  for (std::size_t i = 0; i < dsp::kPostAmpEQParameterCount; ++i)
  {
    const auto eq = static_cast<dsp::PostAmpEQParameter>(i);
    const auto& parameter = control::kParameterRegistry[i + 27];
    const auto& info = dsp::postAmpEQParameterInfo(eq);
    check(parameter.eqParameter == eq, "preserve accepted EQ semantic order");
    check(parameter.normalization == control::Normalization::postAmpEQCanonical,
          "reuse accepted EQ mapping");
    check(parameter.minimum == info.minimum && parameter.maximum == info.maximum,
          "EQ range matches canonical controls");
    check(sameBits(parameter.defaultValue, info.defaultValue), "EQ exact default matches canonical controls");
    for (std::size_t step = 0; step <= 1024; ++step)
    {
      const double n = static_cast<double>(step) / 1024;
      const double physical = dsp::postAmpEQParameterFromNormalized(eq, n);
      check(sameBits(control::fromNormalized(parameter, n), physical), "EQ normalized conversion is bit-identical");
      check(sameBits(control::toNormalized(parameter, physical),
                     dsp::postAmpEQParameterToNormalized(eq, physical)), "EQ physical conversion is bit-identical");
    }
    const double normalizedDefault = control::toNormalized(parameter, parameter.defaultValue);
    const double restoredDefault = control::fromNormalized(parameter, normalizedDefault);
    check(sameBits(restoredDefault, parameter.defaultValue), "EQ exact default coordinate snap");
    check(dsp::setPostAmpEQParameter(restored, eq, restoredDefault), "restore EQ physical default");
  }
  check(restored == defaults, "restored EQ snapshot remains exactly neutral");
  check(sameBits(restored.trimDb, 0.0), "trim default is positive exact zero");
  check(sameBits(control::fromNormalized(control::kParameterRegistry[41], .8), 0.0),
        "trim .8 normalized cannot acquire a tiny FMA residue");
}

void testExistingControlConventions()
{
  for (std::size_t step = 0; step <= 1024; ++step)
  {
    const double n = static_cast<double>(step) / 1024;
    check(control::fromNormalized(control::kParameterRegistry[13], n)
            == static_cast<double>(static_cast<std::uint32_t>(integration::preNAMProcessorFromNormalized(n))),
          "selector finite normalized mapping matches existing control");
    const auto ah = integration::ahControlsFromDevelopmentUI(0, n, n);
    check(control::fromNormalized(control::kParameterRegistry[19], n)
            == static_cast<double>(static_cast<std::uint32_t>(ah.type)),
          "AH Type normalized mapping matches existing control");
    check(control::fromNormalized(control::kParameterRegistry[20], n)
            == static_cast<double>(static_cast<std::uint32_t>(ah.emphasis)),
          "AH Emphasis normalized mapping matches existing control");
  }
  const auto& tcGain = control::kParameterRegistry[14];
  check(control::fromNormalized(tcGain, .132430924210101) == .132430924210101,
        "TC Gain retains full shaft position rather than an invented dB law");
  const auto tc = integration::tcBldControlsFromDevelopmentUI(.2, .25, .75);
  check(tc.gain == .2 && tc.bass == .75 && tc.treble == .25,
        "TC tone inversion remains confined to existing electrical adapter");
  const auto& bass = control::kParameterRegistry[24];
  const auto& treble = control::kParameterRegistry[25];
  for (double n : {0.0, .25, .5, .75, 1.0})
  {
    const double bassDb = control::fromNormalized(bass, n);
    const double trebleDb = control::fromNormalized(treble, n);
    const auto ah = integration::ahDriveControlsFromDevelopmentUI(
      .5, control::toNormalized(bass, bassDb), control::toNormalized(treble, trebleDb), .5);
    check(ah.bass == n && ah.treble == n, "physical AH tone units preserve existing DSP positions");
  }
  const auto& volume = control::kParameterRegistry[26];
  check(volume.minimum == 0 && volume.maximum == 1, "AH Volume retains position axis including exact mute");
  check(dsp::JRockettAHDriveProfile::volumeExponent == 3, "accepted cubic Volume law");
  check(close(dsp::JRockettAHDriveProfile::maximumVolumeGain
                * volume.defaultValue * volume.defaultValue * volume.defaultValue, 1),
        "accepted Volume position default remains unity amplitude");
  check(close(20 * std::log10(dsp::JRockettAHDriveProfile::maximumVolumeGain), 12),
        "accepted Volume endpoint remains +12 dB");
  for (std::uint32_t i = 0; i < integration::kDevelopmentDelayPresetCount; ++i)
  {
    const auto preset = integration::developmentDelayPresetFromIndex(i);
    check(static_cast<std::uint32_t>(preset) == i, "delay values preserve actual DSP order");
    const auto& parameter = control::kParameterRegistry[44];
    check(control::fromNormalized(parameter,
            integration::developmentDelayPresetNormalizedControlValue(preset)) == static_cast<double>(i),
          "delay mapping preserves existing normalized values");
  }
  check(control::kParameterRegistry[44].enumLabels[1] == "Chorus 011"
        && control::kParameterRegistry[44].enumLabels[4] == "Holdsworth 122",
        "delay labels preserve persistent order rather than visual grouping");
}
} // namespace

int main()
{
  testFrozenIdentity();
  testFrozenLegacyDescriptors();
  testAllConversions();
  testDraftDefaultsAndSafetyGates();
  testEQCanonicalContract();
  testExistingControlConventions();
  std::cout << "ParameterRegistryTests: " << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
