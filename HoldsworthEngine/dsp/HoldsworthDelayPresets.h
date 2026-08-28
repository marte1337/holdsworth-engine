#pragma once

#include "HoldsworthDelayEngine.h"
#include "../presets/YamahaBandStructureSourceValues.h"
#include "../presets/YamahaFilterSourceValues.h"
#include "../presets/YamahaModulationSourceValues.h"
#include "../presets/YamahaSyncSourceValues.h"
#include "../presets/YamahaTapSourceValues.h"

#include <array>
#include <optional>
#include <string_view>

namespace holdsworth::dsp
{

// A value transcribed from a Yamaha-style feedback control. It is source
// metadata only and deliberately cannot be passed to the DSP as a normalized
// feedback coefficient.
struct YamahaFeedbackControlValue final
{
  explicit constexpr YamahaFeedbackControlValue(const double controlValue = 0.0)
  : value(controlValue)
  {
  }

  double value = 0.0;
};

// Direct transcriptions of Yamaha display values. These source-metadata types
// deliberately provide no conversion to the DSP configuration units.
struct YamahaDelayTimeMs final
{
  explicit constexpr YamahaDelayTimeMs(const double milliseconds = 0.0)
  : value(milliseconds)
  {
  }

  double value = 0.0;
};

struct YamahaLevelControlValue final
{
  explicit constexpr YamahaLevelControlValue(const double controlValue = 0.0)
  : value(controlValue)
  {
  }

  double value = 0.0;
};

enum class YamahaPanDirection
{
  left,
  center,
  right
};

struct YamahaPanControlValue final
{
  explicit constexpr YamahaPanControlValue(
    const YamahaPanDirection panDirection = YamahaPanDirection::center,
    const double controlMagnitude = 0.0)
  : direction(panDirection)
  , magnitude(controlMagnitude)
  {
  }

  YamahaPanDirection direction = YamahaPanDirection::center;
  double magnitude = 0.0;
};

enum class FeedbackCalibrationStatus
{
  provisionalUnmeasured,
  measured
};

struct DocumentedYamahaBandValues final
{
  std::optional<::holdsworth::presets::YamahaEffectBandSwitchState> switchState;
  std::optional<::holdsworth::presets::YamahaConnectControlValue> connectControlValue;
  std::optional<::holdsworth::presets::YamahaGroupControlValue> groupControlValue;
  std::optional<YamahaFeedbackControlValue> feedbackControlValue;
  std::optional<::holdsworth::presets::YamahaSpeedControlValue> speedControlValue;
  std::optional<::holdsworth::presets::YamahaDepthControlValue> depthControlValue;
  std::optional<YamahaDelayTimeMs> delayTimeMs;
  std::optional<YamahaPanControlValue> panControlValue;
  std::optional<YamahaLevelControlValue> levelControlValue;
  std::optional<::holdsworth::presets::YamahaLowCutControlValue> lowCutControlValue;
  std::optional<::holdsworth::presets::YamahaHighCutControlValue> highCutControlValue;
  std::optional<::holdsworth::presets::YamahaTapPercentValue> tapPercentValue;
  std::optional<::holdsworth::presets::YamahaWaveformControlValue> waveformControlValue;
  std::optional<::holdsworth::presets::YamahaDelaySignalPhaseControlValue>
    delaySignalPhaseControlValue;
  std::optional<::holdsworth::presets::YamahaSyncControlValue> syncControlValue;
};

struct DocumentedYamahaGlobalValues final
{
  std::optional<YamahaLevelControlValue> effectLevel;
  std::optional<YamahaLevelControlValue> directLevel;
  std::optional<YamahaPanControlValue> directPan;
};

enum class YamahaModulationMappingStatus
{
  unmeasured,
  measured
};

enum class ModulationPhaseRelationshipStatus
{
  provisional,
  documentedReference,
  measured
};

struct ModulationCalibrationMetadata final
{
  YamahaModulationMappingStatus speedMapping = YamahaModulationMappingStatus::unmeasured;
  YamahaModulationMappingStatus depthMapping = YamahaModulationMappingStatus::unmeasured;
  ModulationPhaseRelationshipStatus phaseRelationship =
    ModulationPhaseRelationshipStatus::provisional;
};

// Literal identity fields from the Yamaha patch list. They are descriptive
// source metadata and are not used by the DSP engine.
struct DocumentedYamahaPresetIdentity final
{
  std::string_view presetNumber;
  std::string_view presetName;
  std::string_view author;
};

// Identity of a patch recalled by a numbered exercise in the official Yamaha
// owner's manual. This is deliberately separate from patch-list identity:
// punctuation in displayedPatch distinguishes PRESET-area displays such as
// "9.13" from USER-area displays such as "913".
enum class YamahaPatchMemoryArea
{
  user,
  preset
};

struct DocumentedYamahaManualExerciseReference final
{
  std::string_view documentTitle;
  std::string_view displayedPatch;
  YamahaPatchMemoryArea memoryArea = YamahaPatchMemoryArea::user;
  unsigned int groupNumber = 0;
  unsigned int bankNumber = 0;
  unsigned int patchNumber = 0;
  unsigned int initialStateStep = 0;
  unsigned int modifiedStateStep = 0;
};

struct HoldsworthDelayPresetDefinition final
{
  std::string_view id;
  std::string_view displayName;
  HoldsworthDelayConfiguration dspConfiguration;
  std::array<DocumentedYamahaBandValues, kHoldsworthDelayBandCount> documentedYamahaValues;
  FeedbackCalibrationStatus feedbackCalibration = FeedbackCalibrationStatus::provisionalUnmeasured;
  double requiredMaximumDelayTimeMs = 0.0;
  DocumentedYamahaGlobalValues documentedYamahaGlobalValues;
  std::optional<ModulationCalibrationMetadata> modulationCalibration;
  std::optional<DocumentedYamahaPresetIdentity> documentedYamahaPresetIdentity;
  std::optional<::holdsworth::presets::YamahaSyncAuditionReference>
    documentedYamahaSyncAuditionReference;
  std::optional<DocumentedYamahaManualExerciseReference>
    documentedYamahaManualExerciseReference;
  // Physical GROUP history required to audition this definition. This is
  // project DSP metadata, not a documented Yamaha maximum.
  std::optional<GroupedDelayPhysicalCapacityMs>
    requiredGroupedDelayPhysicalCapacity;
};

namespace presets
{

// An unmodulated Lead 121-style topology. Its non-zero DSP feedback
// coefficients are explicit provisional audition values, not conversions from
// the separately recorded Yamaha control values.
[[nodiscard]] const HoldsworthDelayPresetDefinition& lead121UnmodulatedProvisional() noexcept;

// A provisional physical-DSP audition configuration for the documented
// Chorus 011 source settings. No Yamaha SPEED, DEPTH, or feedback mapping is
// implied by its physical values.
[[nodiscard]] const HoldsworthDelayPresetDefinition& chorus011ProvisionalV1() noexcept;

// A provisional physical-DSP audition configuration for Yamaha preset 031,
// Chorus 7 by Allan Holdsworth. Its source values and DSP values remain
// independent literal data; no Yamaha-control conversion is implied.
[[nodiscard]] const HoldsworthDelayPresetDefinition& chorus031ProvisionalV1() noexcept;

// A provisional physical-DSP audition configuration for Yamaha preset 111,
// Chorus 10 by Allan Holdsworth. Documented source controls remain independent
// from its literal unmeasured DSP audition values.
[[nodiscard]] const HoldsworthDelayPresetDefinition& holdsworth111ProvisionalV1() noexcept;

// A provisional physical-DSP audition configuration for Yamaha preset 223,
// Single Source Point Stereo Microphone + Echos by Allan Holdsworth. The
// documented 5.25 ms NOR / 5.48 ms REV stereo pair is preserved exactly in
// source metadata and represented by DelaySignalPolarity in the DSP preset.
[[nodiscard]] const HoldsworthDelayPresetDefinition& holdsworth223ProvisionalV1() noexcept;

// A provisional physical-DSP audition configuration for Yamaha factory preset
// 922, Sync Parameter Sample. Band 2 follows Band 1 with a neutral physical
// phase offset while retaining the exact factory SPEED 0.0 source value.
[[nodiscard]] const HoldsworthDelayPresetDefinition& sync922BaselineProvisionalV1() noexcept;

// A manual-guided PHASE audition derived from factory preset 922. The stored
// Yamaha PHASE source values remain NOR/NOR; only the physical DSP signal
// polarity for Band 1 is reversed.
[[nodiscard]] const HoldsworthDelayPresetDefinition& sync922Band1ReverseDiagnosticV1() noexcept;

// A manual-guided diagnostic variant of factory preset 922. The factory source
// transcription remains SPEED 0.0, while the DSP relationship uses the exact
// half-cycle point Yamaha documents for synchronized SPEED 5.0.
[[nodiscard]] const HoldsworthDelayPresetDefinition& sync922HalfCycleDiagnosticV1() noexcept;

// An audition-only comparison derived from the physical DSP settings used by
// the 922 baseline, with Band 2 left on its dormant independent zero-Hz clock.
// It deliberately carries no Yamaha factory source transcription.
[[nodiscard]] const HoldsworthDelayPresetDefinition& sync922IndependentDiagnosticV1() noexcept;

// A timing-focused physical-DSP diagnostic of the parallel source state in
// the official UD-Stomp owner's manual's patch 9.13 CONNECT exercise. Only the
// source values explicitly documented by that exercise are transcribed.
[[nodiscard]] const HoldsworthDelayPresetDefinition& connect913ParallelDiagnosticV1() noexcept;

// The manual-guided serial diagnostic derived from the same documented 9.13
// source state by changing Band 2 CONNECT from IN to Band 1. The stored source
// transcription remains IN/IN and is not presented as a factory serial state.
[[nodiscard]] const HoldsworthDelayPresetDefinition& connect913SerialDiagnosticV1() noexcept;

// Project timing diagnostics derived from Yamaha's documented GROUP behavior.
// Their physical delay/TAP/PAN/LEVEL choices are not factory source metadata.
[[nodiscard]] const HoldsworthDelayPresetDefinition& group12Rhythm1200DiagnosticV1() noexcept;
[[nodiscard]] const HoldsworthDelayPresetDefinition& group12Rhythm900DiagnosticV1() noexcept;

} // namespace presets
} // namespace holdsworth::dsp
