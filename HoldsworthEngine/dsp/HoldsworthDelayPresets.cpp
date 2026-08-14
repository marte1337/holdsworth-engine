#include "HoldsworthDelayPresets.h"

namespace holdsworth::dsp
{
namespace
{

[[nodiscard]] constexpr DelayBandConfiguration makeBandConfiguration(
  const double delayTimeMs,
  const double provisionalFeedbackCoefficient,
  const double pan,
  const double outputLevel,
  const double modulationRateHz = 0.0,
  const double modulationDepthMs = 0.0,
  const double modulationPhaseCycles = 0.0) noexcept
{
  DelayBandConfiguration configuration;
  configuration.delayTimeMs = delayTimeMs;
  configuration.feedback = NormalizedFeedbackCoefficient{provisionalFeedbackCoefficient};
  configuration.outputLevel = outputLevel;
  configuration.pan = pan;
  configuration.enabled = true;
  configuration.modulationRate = ModulationRateHz{modulationRateHz};
  configuration.modulationDepth = ModulationDepthMs{modulationDepthMs};
  configuration.modulationPhase = ModulationPhaseCycles{modulationPhaseCycles};
  return configuration;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedFeedback(
  const double controlValue) noexcept
{
  DocumentedYamahaBandValues values;
  values.feedbackControlValue = YamahaFeedbackControlValue{controlValue};
  return values;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedChorusBand(
  const double delayTimeMs,
  const double feedbackControlValue,
  const double speedControlValue,
  const double depthControlValue,
  const YamahaPanDirection panDirection,
  const double panMagnitude,
  const double levelControlValue) noexcept
{
  return {YamahaFeedbackControlValue{feedbackControlValue},
          ::holdsworth::presets::YamahaSpeedControlValue{speedControlValue},
          ::holdsworth::presets::YamahaDepthControlValue{depthControlValue},
          YamahaDelayTimeMs{delayTimeMs},
          YamahaPanControlValue{panDirection, panMagnitude},
          YamahaLevelControlValue{levelControlValue}};
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
  461.0,
  {},
  std::nullopt};

// The modulation rates, depths, phases, normalized feedback coefficients, and
// output levels below are conservative unmeasured audition seeds. They are
// intentionally written as literal physical DSP values rather than calculated
// from the separately transcribed Yamaha controls.
const HoldsworthDelayPresetDefinition kChorus011ProvisionalV1{
  "chorus011-provisional-v1",
  "Chorus 011 (Provisional v1)",
  HoldsworthDelayConfiguration{
    {makeBandConfiguration(23.6, 0.0, -1.0, 1.0, 0.38, 0.75, 0.0),
     makeBandConfiguration(30.0, 0.0, 1.0, 1.0, 0.52, 0.75, 0.5),
     makeBandConfiguration(38.1, 0.0, 1.0, 1.0, 0.58, 0.75, 0.25),
     makeBandConfiguration(47.6, 0.0, -1.0, 1.0, 0.43, 0.75, 0.75),
     makeBandConfiguration(300.0, 0.36, -1.0, 0.65, 0.38, 0.75, 0.125),
     makeBandConfiguration(400.0, 0.28, 1.0, 0.65, 0.46, 0.75, 0.625),
     makeBandConfiguration(341.0, 0.34, -1.0, 0.65, 0.72, 0.75, 0.375),
     makeBandConfiguration(450.0, 0.26, 1.0, 0.65, 0.33, 0.75, 0.875)},
    1.0},
  {documentedChorusBand(23.6, 0.0, 3.5, 2.5, YamahaPanDirection::left, 10.0, 10.0),
   documentedChorusBand(30.0, 0.0, 4.0, 2.5, YamahaPanDirection::right, 10.0, 10.0),
   documentedChorusBand(38.1, 0.0, 4.2, 2.5, YamahaPanDirection::right, 10.0, 10.0),
   documentedChorusBand(47.6, 0.0, 3.7, 2.5, YamahaPanDirection::left, 10.0, 10.0),
   documentedChorusBand(300.0, 4.5, 3.5, 2.5, YamahaPanDirection::left, 10.0, 6.5),
   documentedChorusBand(400.0, 3.5, 3.8, 2.5, YamahaPanDirection::right, 10.0, 6.5),
   documentedChorusBand(341.0, 4.3, 4.7, 2.5, YamahaPanDirection::left, 10.0, 6.5),
   documentedChorusBand(450.0, 3.4, 3.3, 2.5, YamahaPanDirection::right, 10.0, 6.5)},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  450.75,
  {YamahaLevelControlValue{8.5},
   YamahaLevelControlValue{5.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional}};

} // namespace

namespace presets
{

const HoldsworthDelayPresetDefinition& lead121UnmodulatedProvisional() noexcept
{
  return kLead121UnmodulatedProvisional;
}

const HoldsworthDelayPresetDefinition& chorus011ProvisionalV1() noexcept
{
  return kChorus011ProvisionalV1;
}

} // namespace presets
} // namespace holdsworth::dsp
