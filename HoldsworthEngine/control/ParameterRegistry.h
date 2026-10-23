#pragma once

// M0 compatibility contract only. Production IParams and state serialization
// deliberately do not include this header yet.
#include "../dsp/JRockettAHDriveProfile.h"
#include "../dsp/PostAmpEQControls.h"
#include "../integration/DevelopmentControlDefaults.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace holdsworth::control
{
// Explicit persistent IDs, independent of host indices, display order, UI tags,
// strings and hashes. Retired IDs/keys/host slots must never be reassigned.
// The high nibble identifies a domain; lower bits are explicitly allocated.
// Semantic IDs deliberately live above ordinary host IDs and the current VST3
// bypass/preset/CC range (65536..67617). They must never be used as host IDs.
enum class ParameterId : std::uint32_t
{
  inputGain = 0x10000000,
  gateThreshold = 0x10000001,
  toneBass = 0x10000002,
  toneMiddle = 0x10000003,
  toneTreble = 0x10000004,
  outputGain = 0x10000005,
  gateEnabled = 0x10000006,
  toneEnabled = 0x10000007,
  irEnabled = 0x10000008,
  inputCalibrationEnabled = 0x10000009,
  inputCalibrationLevel = 0x1000000a,
  outputMode = 0x1000000b,
  slim = 0x1000000c,
  preNAMSelector = 0x20000000,
  tcGain = 0x20000100,
  tcBass = 0x20000101,
  tcTreble = 0x20000102,
  mcBoost = 0x20000200,
  ahBoost = 0x20000300,
  ahType = 0x20000301,
  ahEmphasis = 0x20000302,
  ahBoostEnabled = 0x20000303,
  ahDriveEnabled = 0x20000304,
  ahDriveGain = 0x20000305,
  ahBass = 0x20000306,
  ahTreble = 0x20000307,
  ahVolume = 0x20000308,
  eqEnabled = 0x30000000,
  eqBand1Frequency = 0x30000001,
  eqBand1Gain = 0x30000002,
  eqBand1Q = 0x30000003,
  eqBand2Frequency = 0x30000004,
  eqBand2Gain = 0x30000005,
  eqBand2Q = 0x30000006,
  eqBand3Frequency = 0x30000007,
  eqBand3Gain = 0x30000008,
  eqBand3Q = 0x30000009,
  eqHighPassEnabled = 0x3000000a,
  eqHighPassFrequency = 0x3000000b,
  eqLowPassEnabled = 0x3000000c,
  eqLowPassFrequency = 0x3000000d,
  eqTrim = 0x3000000e,
  delayEnabled = 0x40000000,
  delayWet = 0x40000001,
  delayPreset = 0x40000002,
  tunerEnabled = 0x50000000
};

enum class ParameterKind { continuous, boolean, enumeration };
enum class Normalization { linear, postAmpEQCanonical };
enum class ApplicationDomain { scalarTarget, dspTuple, coefficientPreparation, structuralModel, tunerCapture };
enum class AutomationReadiness
{
  validatedExistingDSPPath,
  requiresTransitionValidation,
  requiresSafeCoefficientUpdates,
  requiresSafeStructuralModelUpdates
};

struct ParameterDescriptor final
{
  ParameterId id;
  std::size_t hostIndex;
  std::string_view key;
  std::string_view name;
  std::string_view unit;
  ParameterKind kind;
  Normalization normalization;
  double minimum;
  double maximum;
  double defaultValue;
  double displayStep; // Legacy IParam step is not quantization for continuous values.
  bool isCurrentlyHostParameter;
  bool isCurrentlyHostAutomatable;
  ApplicationDomain applicationDomain;
  AutomationReadiness automationReadiness; // Future safety gate, not existing host flags.
  std::span<const std::string_view> enumLabels;
  dsp::PostAmpEQParameter eqParameter = dsp::PostAmpEQParameter::count;
};

inline constexpr std::size_t kLegacyParameterCount = 13;
inline constexpr std::size_t kProposedParameterCount = 46;
inline constexpr std::array<std::string_view, 2> kSwitchLabels{"off", "on"};
inline constexpr std::array<std::string_view, 3> kOutputModeLabels{"Raw", "Normalized", "Calibrated"};
inline constexpr std::array<std::string_view, 4> kPreNAMLabels{"Off", "TC BLD", "MC402", "J. Rockett AH"};
inline constexpr std::array<std::string_view, 3> kAHTypeLabels{"F", "C", "T"};
inline constexpr std::array<std::string_view, 2> kAHEmphasisLabels{"L", "H"};
// Preserve DSP/persistent order, which differs from the panel's visual grouping.
inline constexpr std::array<std::string_view, 5> kDelayPresetLabels{
  "Lead 121", "Chorus 011", "Chorus 031", "Holdsworth 223", "Holdsworth 122"};

namespace detail
{
[[nodiscard]] constexpr ParameterDescriptor descriptor(
  ParameterId id, std::size_t index, std::string_view key, std::string_view name,
  std::string_view unit, ParameterKind kind, double minimum, double maximum,
  double defaultValue, double displayStep, bool legacy,
  ApplicationDomain domain = ApplicationDomain::scalarTarget,
  AutomationReadiness readiness = AutomationReadiness::requiresTransitionValidation,
  std::span<const std::string_view> labels = {}) noexcept
{
  return {id, index, key, name, unit, kind, Normalization::linear,
          minimum, maximum, defaultValue, displayStep, legacy, legacy,
          domain, readiness, labels, dsp::PostAmpEQParameter::count};
}

[[nodiscard]] constexpr ParameterDescriptor eqDescriptor(
  ParameterId id, std::size_t index, std::string_view key, std::string_view name,
  dsp::PostAmpEQParameter eqParameter) noexcept
{
  const auto& info = dsp::postAmpEQParameterInfo(eqParameter);
  return {id, index, key, name, info.unit,
          info.boolean ? ParameterKind::boolean : ParameterKind::continuous,
          Normalization::postAmpEQCanonical, info.minimum, info.maximum,
          info.defaultValue, info.boolean ? 1.0 : 0.0, false, false,
          ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath,
          info.boolean ? std::span<const std::string_view>{kSwitchLabels}
                       : std::span<const std::string_view>{}, eqParameter};
}
} // namespace detail

// Slots 0..12 freeze actual production parameters. Slots 13..45 reserve the
// reviewed append-only plan; they do not register parameters with any format.
inline constexpr std::array<ParameterDescriptor, kProposedParameterCount> kParameterRegistry{{
  detail::descriptor(ParameterId::inputGain, 0, "he.input.gain_db", "Input", "dB", ParameterKind::continuous, -20, 20, 0, .1, true),
  detail::descriptor(ParameterId::gateThreshold, 1, "he.gate.threshold_db", "Threshold", "dB", ParameterKind::continuous, -100, 0, -80, .1, true),
  detail::descriptor(ParameterId::toneBass, 2, "he.tone.bass", "Bass", "", ParameterKind::continuous, 0, 10, 5, .1, true),
  detail::descriptor(ParameterId::toneMiddle, 3, "he.tone.middle", "Middle", "", ParameterKind::continuous, 0, 10, 5, .1, true),
  detail::descriptor(ParameterId::toneTreble, 4, "he.tone.treble", "Treble", "", ParameterKind::continuous, 0, 10, 5, .1, true),
  detail::descriptor(ParameterId::outputGain, 5, "he.output.gain_db", "Output", "dB", ParameterKind::continuous, -40, 40, 0, .1, true),
  detail::descriptor(ParameterId::gateEnabled, 6, "he.gate.enabled", "NoiseGateActive", "", ParameterKind::boolean, 0, 1, 1, 1, true, ApplicationDomain::scalarTarget, AutomationReadiness::requiresTransitionValidation, kSwitchLabels),
  detail::descriptor(ParameterId::toneEnabled, 7, "he.tone.enabled", "ToneStack", "", ParameterKind::boolean, 0, 1, 1, 1, true, ApplicationDomain::scalarTarget, AutomationReadiness::requiresTransitionValidation, kSwitchLabels),
  detail::descriptor(ParameterId::irEnabled, 8, "he.cabinet.enabled", "IRToggle", "", ParameterKind::boolean, 0, 1, 1, 1, true, ApplicationDomain::scalarTarget, AutomationReadiness::requiresTransitionValidation, kSwitchLabels),
  detail::descriptor(ParameterId::inputCalibrationEnabled, 9, "he.input.calibration_enabled", "CalibrateInput", "", ParameterKind::boolean, 0, 1, 0, 1, true, ApplicationDomain::scalarTarget, AutomationReadiness::requiresTransitionValidation, kSwitchLabels),
  detail::descriptor(ParameterId::inputCalibrationLevel, 10, "he.input.calibration_dbu", "InputCalibrationLevel", "dBu", ParameterKind::continuous, -60, 60, 12, .1, true),
  detail::descriptor(ParameterId::outputMode, 11, "he.output.mode", "OutputMode", "", ParameterKind::enumeration, 0, 2, 1, 1, true, ApplicationDomain::scalarTarget, AutomationReadiness::requiresTransitionValidation, kOutputModeLabels),
  detail::descriptor(ParameterId::slim, 12, "he.nam.slim", "Slim", "", ParameterKind::continuous, 0, 1, 0, .01, true, ApplicationDomain::structuralModel, AutomationReadiness::requiresSafeStructuralModelUpdates),
  detail::descriptor(ParameterId::preNAMSelector, 13, "he.pre_nam.selection", "Pre-NAM Processor", "", ParameterKind::enumeration, 0, 3, 0, 1, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath, kPreNAMLabels),
  detail::descriptor(ParameterId::tcGain, 14, "he.tc_bld.gain_position", "TC BLD Gain", "", ParameterKind::continuous, 0, 1, integration::kDevelopmentGainDefault, 0, false, ApplicationDomain::coefficientPreparation, AutomationReadiness::requiresSafeCoefficientUpdates),
  detail::descriptor(ParameterId::tcBass, 15, "he.tc_bld.bass_position", "TC BLD Bass", "", ParameterKind::continuous, 0, 1, integration::kDevelopmentToneDefault, 0, false, ApplicationDomain::coefficientPreparation, AutomationReadiness::requiresSafeCoefficientUpdates),
  detail::descriptor(ParameterId::tcTreble, 16, "he.tc_bld.treble_position", "TC BLD Treble", "", ParameterKind::continuous, 0, 1, integration::kDevelopmentToneDefault, 0, false, ApplicationDomain::coefficientPreparation, AutomationReadiness::requiresSafeCoefficientUpdates),
  detail::descriptor(ParameterId::mcBoost, 17, "he.mc402.boost_db", "MC402 Boost", "dB", ParameterKind::continuous, 0, 20, 0, 0, false, ApplicationDomain::scalarTarget, AutomationReadiness::validatedExistingDSPPath),
  detail::descriptor(ParameterId::ahBoost, 18, "he.ah.boost_db", "AH Boost", "dB", ParameterKind::continuous, 0, 20, 0, 0, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath),
  detail::descriptor(ParameterId::ahType, 19, "he.ah.boost_type", "AH Boost Type", "", ParameterKind::enumeration, 0, 2, 1, 1, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath, kAHTypeLabels),
  detail::descriptor(ParameterId::ahEmphasis, 20, "he.ah.boost_emphasis", "AH Boost Emphasis", "", ParameterKind::enumeration, 0, 1, 0, 1, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath, kAHEmphasisLabels),
  detail::descriptor(ParameterId::ahBoostEnabled, 21, "he.ah.boost_enabled", "AH Boost Enable", "", ParameterKind::boolean, 0, 1, 1, 1, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath, kSwitchLabels),
  detail::descriptor(ParameterId::ahDriveEnabled, 22, "he.ah.drive_enabled", "AH Drive Enable", "", ParameterKind::boolean, 0, 1, 0, 1, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath, kSwitchLabels),
  detail::descriptor(ParameterId::ahDriveGain, 23, "he.ah.drive_gain_position", "AH Drive Gain", "", ParameterKind::continuous, 0, 1, dsp::JRockettAHDriveProfile::defaultGain, 0, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath),
  detail::descriptor(ParameterId::ahBass, 24, "he.ah.bass_db", "AH Drive Bass", "dB", ParameterKind::continuous, -6, 6, 0, 0, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath),
  detail::descriptor(ParameterId::ahTreble, 25, "he.ah.treble_db", "AH Drive Treble", "dB", ParameterKind::continuous, -9, 9, 0, 0, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath),
  detail::descriptor(ParameterId::ahVolume, 26, "he.ah.volume_position", "AH Drive Volume", "", ParameterKind::continuous, 0, 1, dsp::JRockettAHDriveProfile::defaultVolume, 0, false, ApplicationDomain::dspTuple, AutomationReadiness::validatedExistingDSPPath),
  detail::eqDescriptor(ParameterId::eqEnabled, 27, "he.eq.enabled", "EQ Enable", dsp::PostAmpEQParameter::enable),
  detail::eqDescriptor(ParameterId::eqBand1Frequency, 28, "he.eq.band1.frequency_hz", "EQ Band 1 Frequency", dsp::PostAmpEQParameter::band1Frequency),
  detail::eqDescriptor(ParameterId::eqBand1Gain, 29, "he.eq.band1.gain_db", "EQ Band 1 Gain", dsp::PostAmpEQParameter::band1Gain),
  detail::eqDescriptor(ParameterId::eqBand1Q, 30, "he.eq.band1.q", "EQ Band 1 Q", dsp::PostAmpEQParameter::band1Q),
  detail::eqDescriptor(ParameterId::eqBand2Frequency, 31, "he.eq.band2.frequency_hz", "EQ Band 2 Frequency", dsp::PostAmpEQParameter::band2Frequency),
  detail::eqDescriptor(ParameterId::eqBand2Gain, 32, "he.eq.band2.gain_db", "EQ Band 2 Gain", dsp::PostAmpEQParameter::band2Gain),
  detail::eqDescriptor(ParameterId::eqBand2Q, 33, "he.eq.band2.q", "EQ Band 2 Q", dsp::PostAmpEQParameter::band2Q),
  detail::eqDescriptor(ParameterId::eqBand3Frequency, 34, "he.eq.band3.frequency_hz", "EQ Band 3 Frequency", dsp::PostAmpEQParameter::band3Frequency),
  detail::eqDescriptor(ParameterId::eqBand3Gain, 35, "he.eq.band3.gain_db", "EQ Band 3 Gain", dsp::PostAmpEQParameter::band3Gain),
  detail::eqDescriptor(ParameterId::eqBand3Q, 36, "he.eq.band3.q", "EQ Band 3 Q", dsp::PostAmpEQParameter::band3Q),
  detail::eqDescriptor(ParameterId::eqHighPassEnabled, 37, "he.eq.hp.enabled", "EQ HP Enable", dsp::PostAmpEQParameter::highPassEnable),
  detail::eqDescriptor(ParameterId::eqHighPassFrequency, 38, "he.eq.hp.frequency_hz", "EQ HP Frequency", dsp::PostAmpEQParameter::highPassFrequency),
  detail::eqDescriptor(ParameterId::eqLowPassEnabled, 39, "he.eq.lp.enabled", "EQ LP Enable", dsp::PostAmpEQParameter::lowPassEnable),
  detail::eqDescriptor(ParameterId::eqLowPassFrequency, 40, "he.eq.lp.frequency_hz", "EQ LP Frequency", dsp::PostAmpEQParameter::lowPassFrequency),
  detail::eqDescriptor(ParameterId::eqTrim, 41, "he.eq.trim_db", "EQ Trim", dsp::PostAmpEQParameter::trim),
  detail::descriptor(ParameterId::delayEnabled, 42, "he.delay.enabled", "Delay Enable", "", ParameterKind::boolean, 0, 1, 0, 1, false, ApplicationDomain::dspTuple, AutomationReadiness::requiresTransitionValidation, kSwitchLabels),
  detail::descriptor(ParameterId::delayWet, 43, "he.delay.wet", "Delay Wet", "", ParameterKind::continuous, 0, 1, integration::kDevelopmentWetDefault, 0, false, ApplicationDomain::scalarTarget, AutomationReadiness::requiresTransitionValidation),
  detail::descriptor(ParameterId::delayPreset, 44, "he.delay.preset", "Delay Preset", "", ParameterKind::enumeration, 0, 4, 0, 1, false, ApplicationDomain::dspTuple, AutomationReadiness::requiresTransitionValidation, kDelayPresetLabels),
  detail::descriptor(ParameterId::tunerEnabled, 45, "he.tuner.enabled", "Tuner Enable", "", ParameterKind::boolean, 0, 1, 0, 1, false, ApplicationDomain::tunerCapture, AutomationReadiness::validatedExistingDSPPath, kSwitchLabels)
}};

[[nodiscard]] constexpr const ParameterDescriptor* parameterAtHostIndex(std::size_t index) noexcept
{
  return index < kParameterRegistry.size() ? &kParameterRegistry[index] : nullptr;
}

[[nodiscard]] constexpr const ParameterDescriptor* findParameter(ParameterId id) noexcept
{
  for (const auto& descriptor : kParameterRegistry)
    if (descriptor.id == id) return &descriptor;
  return nullptr;
}

// Finite values match the legacy linear IParam mapping, including its rounding
// of stepped enums/booleans. Nonfinite fallback is an explicit future-ingress
// policy; it is not a claim about legacy IParam handling of malformed input.
[[nodiscard]] inline double toNormalized(const ParameterDescriptor& descriptor,
                                         double physical) noexcept
{
  if (descriptor.normalization == Normalization::postAmpEQCanonical)
    return dsp::postAmpEQParameterToNormalized(descriptor.eqParameter, physical);
  const double finite = std::isfinite(physical) ? physical : descriptor.defaultValue;
  const double value = std::clamp(descriptor.kind == ParameterKind::continuous
                                   ? finite : std::round(finite),
                                 descriptor.minimum, descriptor.maximum);
  return (value - descriptor.minimum) / (descriptor.maximum - descriptor.minimum);
}

[[nodiscard]] inline double fromNormalized(const ParameterDescriptor& descriptor,
                                           double normalized) noexcept
{
  if (descriptor.normalization == Normalization::postAmpEQCanonical)
    return dsp::postAmpEQParameterFromNormalized(descriptor.eqParameter, normalized);
  if (!std::isfinite(normalized)) return descriptor.defaultValue;
  const double value = descriptor.minimum
                     + std::clamp(normalized, 0.0, 1.0) * (descriptor.maximum - descriptor.minimum);
  return std::clamp(descriptor.kind == ParameterKind::continuous ? value : std::round(value),
                    descriptor.minimum, descriptor.maximum);
}

static_assert(kParameterRegistry[kLegacyParameterCount - 1].id == ParameterId::slim);
static_assert(kParameterRegistry[27].eqParameter == dsp::PostAmpEQParameter::enable);
static_assert(kParameterRegistry[41].eqParameter == dsp::PostAmpEQParameter::trim);
static_assert(kParameterRegistry.back().hostIndex == 45);
static_assert([] {
  for (const auto& parameter : kParameterRegistry)
  {
    if (parameter.hostIndex >= 65536) return false;
    const auto semanticId = static_cast<std::uint32_t>(parameter.id);
    if (semanticId < 0x10000000) return false;
    if (semanticId >= 65536 && semanticId <= 67617) return false;
  }
  return true;
}(), "Semantic IDs, ordinary host indices and VST3 special parameters must remain disjoint");
} // namespace holdsworth::control
