#pragma once

#include "HoldsworthDelayEngine.h"

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

enum class FeedbackCalibrationStatus
{
  provisionalUnmeasured,
  measured
};

struct DocumentedYamahaBandValues final
{
  std::optional<YamahaFeedbackControlValue> feedbackControlValue;
};

struct HoldsworthDelayPresetDefinition final
{
  std::string_view id;
  std::string_view displayName;
  HoldsworthDelayConfiguration dspConfiguration;
  std::array<DocumentedYamahaBandValues, kHoldsworthDelayBandCount> documentedYamahaValues;
  FeedbackCalibrationStatus feedbackCalibration = FeedbackCalibrationStatus::provisionalUnmeasured;
  double requiredMaximumDelayTimeMs = 0.0;
};

namespace presets
{

// An unmodulated Lead 121-style topology. Its non-zero DSP feedback
// coefficients are explicit provisional audition values, not conversions from
// the separately recorded Yamaha control values.
[[nodiscard]] const HoldsworthDelayPresetDefinition& lead121UnmodulatedProvisional() noexcept;

} // namespace presets
} // namespace holdsworth::dsp
