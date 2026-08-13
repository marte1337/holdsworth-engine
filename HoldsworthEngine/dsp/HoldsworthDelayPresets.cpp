#include "HoldsworthDelayPresets.h"

namespace holdsworth::dsp
{
namespace
{

[[nodiscard]] constexpr DelayBandConfiguration makeBandConfiguration(
  const double delayTimeMs,
  const double provisionalFeedbackCoefficient,
  const double pan,
  const double outputLevel) noexcept
{
  return {delayTimeMs,
          NormalizedFeedbackCoefficient{provisionalFeedbackCoefficient},
          outputLevel,
          pan,
          true};
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedFeedback(
  const double controlValue) noexcept
{
  return {YamahaFeedbackControlValue{controlValue}};
}

const HoldsworthDelayPresetDefinition kLead121UnmodulatedProvisional{
  "lead121-unmodulated-provisional-v1",
  "Lead 121 (Unmodulated, Provisional)",
  HoldsworthDelayConfiguration{
    {makeBandConfiguration(29.7, 0.0, -1.0, 1.0),
     makeBandConfiguration(40.0, 0.0, 1.0, 1.0),
     makeBandConfiguration(96.0, 0.0, -0.5, 0.4),
     makeBandConfiguration(110.0, 0.0, 0.5, 0.4),
     makeBandConfiguration(300.0, 0.45, -1.0, 0.5),
     makeBandConfiguration(400.0, 0.40, 1.0, 0.5),
     makeBandConfiguration(355.0, 0.35, -1.0, 0.5),
     makeBandConfiguration(461.0, 0.30, 1.0, 0.5)},
    1.0},
  {documentedFeedback(0.0),
   documentedFeedback(0.0),
   documentedFeedback(0.0),
   documentedFeedback(0.0),
   documentedFeedback(4.5),
   documentedFeedback(4.0),
   documentedFeedback(3.5),
   documentedFeedback(3.0)},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  461.0};

} // namespace

namespace presets
{

const HoldsworthDelayPresetDefinition& lead121UnmodulatedProvisional() noexcept
{
  return kLead121UnmodulatedProvisional;
}

} // namespace presets
} // namespace holdsworth::dsp
