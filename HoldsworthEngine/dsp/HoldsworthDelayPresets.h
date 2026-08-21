#pragma once

#include "HoldsworthDelayEngine.h"
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

} // namespace presets
} // namespace holdsworth::dsp
