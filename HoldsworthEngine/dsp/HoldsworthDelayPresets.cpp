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
  const double modulationPhaseCycles = 0.0,
  const double tapFraction = 1.0) noexcept
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
  configuration.tapFraction = TapFraction{tapFraction};
  configuration.modulationWaveform = ModulationWaveform::sine;
  configuration.delaySignalPolarity = DelaySignalPolarity::normal;
  return configuration;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedFeedback(
  const double controlValue,
  const ::holdsworth::presets::YamahaEffectBandNumber effectBand) noexcept
{
  DocumentedYamahaBandValues values;
  values.feedbackControlValue = YamahaFeedbackControlValue{controlValue};
  values.lowCutControlValue = ::holdsworth::presets::YamahaLowCutControlValue::off();
  values.highCutControlValue = ::holdsworth::presets::YamahaHighCutControlValue::off();
  values.tapPercentValue = ::holdsworth::presets::YamahaTapPercentValue{100.0};
  values.waveformControlValue = ::holdsworth::presets::YamahaWaveformControlValue::sine;
  values.delaySignalPhaseControlValue =
    ::holdsworth::presets::YamahaDelaySignalPhaseControlValue::normal;
  values.syncControlValue =
    ::holdsworth::presets::YamahaSyncControlValue::independentSelf(effectBand);
  return values;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedChorusBand(
  const ::holdsworth::presets::YamahaEffectBandNumber effectBand,
  const double delayTimeMs,
  const double feedbackControlValue,
  const double speedControlValue,
  const double depthControlValue,
  const YamahaPanDirection panDirection,
  const double panMagnitude,
  const double levelControlValue,
  const double tapPercentValue = 100.0) noexcept
{
  DocumentedYamahaBandValues values;
  values.feedbackControlValue = YamahaFeedbackControlValue{feedbackControlValue};
  values.speedControlValue = ::holdsworth::presets::YamahaSpeedControlValue{speedControlValue};
  values.depthControlValue = ::holdsworth::presets::YamahaDepthControlValue{depthControlValue};
  values.delayTimeMs = YamahaDelayTimeMs{delayTimeMs};
  values.panControlValue = YamahaPanControlValue{panDirection, panMagnitude};
  values.levelControlValue = YamahaLevelControlValue{levelControlValue};
  values.lowCutControlValue = ::holdsworth::presets::YamahaLowCutControlValue::off();
  values.highCutControlValue = ::holdsworth::presets::YamahaHighCutControlValue::off();
  values.tapPercentValue = ::holdsworth::presets::YamahaTapPercentValue{tapPercentValue};
  values.waveformControlValue = ::holdsworth::presets::YamahaWaveformControlValue::sine;
  values.delaySignalPhaseControlValue =
    ::holdsworth::presets::YamahaDelaySignalPhaseControlValue::normal;
  values.syncControlValue =
    ::holdsworth::presets::YamahaSyncControlValue::independentSelf(effectBand);
  return values;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedSync922ActiveBand(
  const ::holdsworth::presets::YamahaEffectBandNumber effectBand,
  const ::holdsworth::presets::YamahaSyncControlValue syncControlValue,
  const double speedControlValue,
  const YamahaPanDirection panDirection) noexcept
{
  DocumentedYamahaBandValues values;
  values.switchState = ::holdsworth::presets::YamahaEffectBandSwitchState::on;
  values.connectControlValue = ::holdsworth::presets::YamahaConnectControlValue::input();
  values.groupControlValue =
    ::holdsworth::presets::YamahaGroupControlValue::individual(effectBand);
  values.feedbackControlValue = YamahaFeedbackControlValue{0.0};
  values.speedControlValue = ::holdsworth::presets::YamahaSpeedControlValue{speedControlValue};
  values.depthControlValue = ::holdsworth::presets::YamahaDepthControlValue{6.1};
  values.delayTimeMs = YamahaDelayTimeMs{10.0};
  values.panControlValue = YamahaPanControlValue{panDirection, 10.0};
  values.levelControlValue = YamahaLevelControlValue{10.0};
  values.lowCutControlValue = ::holdsworth::presets::YamahaLowCutControlValue::off();
  values.highCutControlValue = ::holdsworth::presets::YamahaHighCutControlValue::off();
  values.tapPercentValue = ::holdsworth::presets::YamahaTapPercentValue{100.0};
  values.waveformControlValue = ::holdsworth::presets::YamahaWaveformControlValue::sine;
  values.delaySignalPhaseControlValue =
    ::holdsworth::presets::YamahaDelaySignalPhaseControlValue::normal;
  values.syncControlValue = syncControlValue;
  return values;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedSync922DisabledBand() noexcept
{
  DocumentedYamahaBandValues values;
  values.switchState = ::holdsworth::presets::YamahaEffectBandSwitchState::off;
  return values;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documented111ActiveBand(
  const ::holdsworth::presets::YamahaEffectBandNumber effectBand,
  const double delayTimeMs,
  const double feedbackControlValue,
  const double tapPercentValue,
  const double speedControlValue,
  const double depthControlValue,
  const YamahaPanDirection panDirection,
  const double levelControlValue) noexcept
{
  DocumentedYamahaBandValues values;
  values.switchState = ::holdsworth::presets::YamahaEffectBandSwitchState::on;
  values.connectControlValue = ::holdsworth::presets::YamahaConnectControlValue::input();
  values.groupControlValue =
    ::holdsworth::presets::YamahaGroupControlValue::individual(effectBand);
  values.feedbackControlValue = YamahaFeedbackControlValue{feedbackControlValue};
  values.speedControlValue = ::holdsworth::presets::YamahaSpeedControlValue{speedControlValue};
  values.depthControlValue = ::holdsworth::presets::YamahaDepthControlValue{depthControlValue};
  values.delayTimeMs = YamahaDelayTimeMs{delayTimeMs};
  values.panControlValue = YamahaPanControlValue{panDirection, 10.0};
  values.levelControlValue = YamahaLevelControlValue{levelControlValue};
  values.lowCutControlValue = ::holdsworth::presets::YamahaLowCutControlValue::off();
  values.highCutControlValue = ::holdsworth::presets::YamahaHighCutControlValue::off();
  values.tapPercentValue = ::holdsworth::presets::YamahaTapPercentValue{tapPercentValue};
  values.waveformControlValue = ::holdsworth::presets::YamahaWaveformControlValue::sine;
  values.delaySignalPhaseControlValue =
    ::holdsworth::presets::YamahaDelaySignalPhaseControlValue::normal;
  values.syncControlValue =
    ::holdsworth::presets::YamahaSyncControlValue::independentSelf(effectBand);
  return values;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documented111DisabledBand() noexcept
{
  DocumentedYamahaBandValues values;
  values.switchState = ::holdsworth::presets::YamahaEffectBandSwitchState::off;
  return values;
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration makeHoldsworth111DspConfiguration() noexcept
{
  HoldsworthDelayConfiguration configuration;
  configuration.bands[0] =
    makeBandConfiguration(19.4, 0.0, -1.0, 1.0, 0.66, 0.81, 0.0, 0.254);
  configuration.bands[1] =
    makeBandConfiguration(15.0, 0.0, 1.0, 1.0, 0.87, 0.81, 0.5, 0.254);
  configuration.bands[4] =
    makeBandConfiguration(250.0, 0.40, -1.0, 0.35, 0.46, 0.90, 0.125);
  configuration.bands[5] =
    makeBandConfiguration(351.0, 0.32, 1.0, 0.35, 0.58, 0.90, 0.625);
  configuration.bands[6] =
    makeBandConfiguration(300.0, 0.40, -1.0, 0.35, 0.38, 0.90, 0.375);
  configuration.bands[7] =
    makeBandConfiguration(400.0, 0.24, 1.0, 0.35, 0.81, 0.90, 0.875);
  configuration.globalWetOutputLevel = 1.0;
  return configuration;
}

[[nodiscard]] constexpr std::array<DocumentedYamahaBandValues,
                                   kHoldsworthDelayBandCount>
makeHoldsworth111DocumentedYamahaValues() noexcept
{
  using Band = ::holdsworth::presets::YamahaEffectBandNumber;
  return {documented111ActiveBand(
            Band::band1, 19.4, 0.0, 25.4, 4.5, 2.7, YamahaPanDirection::left, 10.0),
          documented111ActiveBand(
            Band::band2, 15.0, 0.0, 25.4, 5.2, 2.7, YamahaPanDirection::right, 10.0),
          documented111DisabledBand(),
          documented111DisabledBand(),
          documented111ActiveBand(
            Band::band5, 250.0, 5.0, 100.0, 3.8, 3.0, YamahaPanDirection::left, 3.5),
          documented111ActiveBand(
            Band::band6, 351.0, 4.0, 100.0, 4.2, 3.0, YamahaPanDirection::right, 3.5),
          documented111ActiveBand(
            Band::band7, 300.0, 5.0, 100.0, 3.5, 3.0, YamahaPanDirection::left, 3.5),
          documented111ActiveBand(
            Band::band8, 400.0, 3.0, 100.0, 5.0, 3.0, YamahaPanDirection::right, 3.5)};
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documented223ActiveBand(
  const ::holdsworth::presets::YamahaEffectBandNumber effectBand,
  const double delayTimeMs,
  const ::holdsworth::presets::YamahaDelaySignalPhaseControlValue phase,
  const double feedbackControlValue,
  const YamahaPanDirection panDirection,
  const double levelControlValue) noexcept
{
  DocumentedYamahaBandValues values;
  values.switchState = ::holdsworth::presets::YamahaEffectBandSwitchState::on;
  values.connectControlValue = ::holdsworth::presets::YamahaConnectControlValue::input();
  values.groupControlValue =
    ::holdsworth::presets::YamahaGroupControlValue::individual(effectBand);
  values.feedbackControlValue = YamahaFeedbackControlValue{feedbackControlValue};
  values.speedControlValue = ::holdsworth::presets::YamahaSpeedControlValue{0.0};
  values.depthControlValue = ::holdsworth::presets::YamahaDepthControlValue{0.0};
  values.delayTimeMs = YamahaDelayTimeMs{delayTimeMs};
  values.panControlValue = YamahaPanControlValue{panDirection, 10.0};
  values.levelControlValue = YamahaLevelControlValue{levelControlValue};
  values.lowCutControlValue = ::holdsworth::presets::YamahaLowCutControlValue::off();
  values.highCutControlValue = ::holdsworth::presets::YamahaHighCutControlValue::off();
  values.tapPercentValue = ::holdsworth::presets::YamahaTapPercentValue{100.0};
  values.waveformControlValue = ::holdsworth::presets::YamahaWaveformControlValue::sine;
  values.delaySignalPhaseControlValue = phase;
  values.syncControlValue =
    ::holdsworth::presets::YamahaSyncControlValue::independentSelf(effectBand);
  return values;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documented223DisabledBand() noexcept
{
  DocumentedYamahaBandValues values;
  values.switchState = ::holdsworth::presets::YamahaEffectBandSwitchState::off;
  return values;
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration makeHoldsworth223DspConfiguration() noexcept
{
  HoldsworthDelayConfiguration configuration;
  configuration.bands[0] = makeBandConfiguration(5.25, 0.0, -1.0, 0.7);
  configuration.bands[2] = makeBandConfiguration(5.48, 0.0, 1.0, 1.0);
  configuration.bands[2].delaySignalPolarity = DelaySignalPolarity::reverse;
  configuration.bands[4] = makeBandConfiguration(250.0, 0.40, -1.0, 0.37);
  configuration.bands[5] = makeBandConfiguration(341.0, 0.35, 1.0, 0.37);
  configuration.bands[6] = makeBandConfiguration(300.0, 0.40, -1.0, 0.37);
  configuration.bands[7] = makeBandConfiguration(400.0, 0.35, 1.0, 0.37);
  configuration.globalWetOutputLevel = 1.0;
  return configuration;
}

[[nodiscard]] constexpr std::array<DocumentedYamahaBandValues,
                                   kHoldsworthDelayBandCount>
makeHoldsworth223DocumentedYamahaValues() noexcept
{
  using Band = ::holdsworth::presets::YamahaEffectBandNumber;
  using Phase = ::holdsworth::presets::YamahaDelaySignalPhaseControlValue;
  return {documented223ActiveBand(
            Band::band1, 5.25, Phase::normal, 0.0, YamahaPanDirection::left, 7.0),
          documented223DisabledBand(),
          documented223ActiveBand(
            Band::band3, 5.48, Phase::reverse, 0.0, YamahaPanDirection::right, 10.0),
          documented223DisabledBand(),
          documented223ActiveBand(
            Band::band5, 250.0, Phase::normal, 4.0, YamahaPanDirection::left, 3.7),
          documented223ActiveBand(
            Band::band6, 341.0, Phase::normal, 3.5, YamahaPanDirection::right, 3.7),
          documented223ActiveBand(
            Band::band7, 300.0, Phase::normal, 4.0, YamahaPanDirection::left, 3.7),
          documented223ActiveBand(
            Band::band8, 400.0, Phase::normal, 3.5, YamahaPanDirection::right, 3.7)};
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration makeSync922DspConfiguration(
  const ModulationPhaseOffsetCycles phaseOffset) noexcept
{
  HoldsworthDelayConfiguration configuration;
  configuration.bands[0] =
    makeBandConfiguration(10.0, 0.0, -1.0, 1.0, 0.27, 1.5, 0.0);
  configuration.bands[1] =
    makeBandConfiguration(10.0, 0.0, 1.0, 1.0, 0.0, 1.5, 0.0);
  configuration.modulationSync.relationships[1] =
    SynchronizedModulationRelationship{DelayBandId::band1, phaseOffset};
  return configuration;
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration
makeSync922Band1ReverseDspConfiguration() noexcept
{
  HoldsworthDelayConfiguration configuration =
    makeSync922DspConfiguration(ModulationPhaseOffsetCycles{0.0});
  configuration.bands[0].delaySignalPolarity = DelaySignalPolarity::reverse;
  return configuration;
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration
makeSync922IndependentDspConfiguration() noexcept
{
  HoldsworthDelayConfiguration configuration =
    makeSync922DspConfiguration(ModulationPhaseOffsetCycles{0.0});
  configuration.modulationSync = {};
  return configuration;
}

[[nodiscard]] constexpr DocumentedYamahaBandValues documentedConnect913Band(
  const double delayTimeMs) noexcept
{
  DocumentedYamahaBandValues values;
  values.connectControlValue = ::holdsworth::presets::YamahaConnectControlValue::input();
  values.feedbackControlValue = YamahaFeedbackControlValue{0.0};
  values.delayTimeMs = YamahaDelayTimeMs{delayTimeMs};
  return values;
}

[[nodiscard]] constexpr std::array<DocumentedYamahaBandValues,
                                   kHoldsworthDelayBandCount>
makeConnect913DocumentedYamahaValues() noexcept
{
  // The manual's exercise establishes only these two delays, their zero
  // feedback, and their initial IN/IN routing. Other source controls remain
  // unknown rather than inheriting the diagnostic's physical DSP choices.
  return {documentedConnect913Band(600.0),
          documentedConnect913Band(80.0),
          {},
          {},
          {},
          {},
          {},
          {}};
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration
makeConnect913ParallelDspConfiguration() noexcept
{
  HoldsworthDelayConfiguration configuration;
  // Center pan is a neutral timing-audition choice, not Yamaha source data.
  configuration.bands[0] = makeBandConfiguration(600.0, 0.0, 0.0, 1.0);
  configuration.bands[1] = makeBandConfiguration(80.0, 0.0, 0.0, 1.0);
  configuration.globalWetOutputLevel = 1.0;
  return configuration;
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration
makeConnect913SerialDspConfiguration() noexcept
{
  HoldsworthDelayConfiguration configuration = makeConnect913ParallelDspConfiguration();
  configuration.audioRouting.inputs[1] = ConnectedBandAudioInput{DelayBandId::band1};
  return configuration;
}

[[nodiscard]] constexpr HoldsworthDelayConfiguration
makeGroup12RhythmDspConfiguration(const double groupBaseDelayTimeMs) noexcept
{
  HoldsworthDelayConfiguration configuration;
  configuration.bands[0] =
    makeBandConfiguration(groupBaseDelayTimeMs, 0.0, -1.0, 1.0, 0.0, 0.0, 0.0, 0.5);
  // The non-head DelayBand time is deliberately dormant. Band 2's audible
  // 100% output must observe the shared GROUP base delay owned by Band 1.
  configuration.bands[1] =
    makeBandConfiguration(0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0);
  configuration.delayGrouping.groupsByHead[0] =
    GroupedDelayRange{DelayBandId::band2};
  configuration.globalWetOutputLevel = 1.0;
  return configuration;
}

constexpr DocumentedYamahaManualExerciseReference kConnect913ManualExerciseReference{
  "Yamaha UD-Stomp Owner's Manual",
  "9.13",
  YamahaPatchMemoryArea::preset,
  9,
  1,
  3,
  18,
  19};

[[nodiscard]] constexpr std::array<DocumentedYamahaBandValues,
                                   kHoldsworthDelayBandCount>
makeSync922DocumentedYamahaValues() noexcept
{
  return {
    documentedSync922ActiveBand(
      ::holdsworth::presets::YamahaEffectBandNumber::band1,
      ::holdsworth::presets::YamahaSyncControlValue::independentSelf(
        ::holdsworth::presets::YamahaEffectBandNumber::band1),
      3.0,
      YamahaPanDirection::left),
    documentedSync922ActiveBand(
      ::holdsworth::presets::YamahaEffectBandNumber::band2,
      ::holdsworth::presets::YamahaSyncControlValue::synchronizedTo(
        ::holdsworth::presets::YamahaEffectBandNumber::band1),
      0.0,
      YamahaPanDirection::right),
    documentedSync922DisabledBand(),
    documentedSync922DisabledBand(),
    documentedSync922DisabledBand(),
    documentedSync922DisabledBand(),
    documentedSync922DisabledBand(),
    documentedSync922DisabledBand()};
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
  {documentedFeedback(0.0, ::holdsworth::presets::YamahaEffectBandNumber::band1),
   documentedFeedback(0.0, ::holdsworth::presets::YamahaEffectBandNumber::band2),
   documentedFeedback(0.0, ::holdsworth::presets::YamahaEffectBandNumber::band3),
   documentedFeedback(0.0, ::holdsworth::presets::YamahaEffectBandNumber::band4),
   documentedFeedback(4.5, ::holdsworth::presets::YamahaEffectBandNumber::band5),
   documentedFeedback(4.0, ::holdsworth::presets::YamahaEffectBandNumber::band6),
   documentedFeedback(3.5, ::holdsworth::presets::YamahaEffectBandNumber::band7),
   documentedFeedback(3.0, ::holdsworth::presets::YamahaEffectBandNumber::band8)},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  461.0,
  {},
  std::nullopt,
  std::nullopt,
  std::nullopt,
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
  {documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band1,
                        23.6, 0.0, 3.5, 2.5, YamahaPanDirection::left, 10.0, 10.0),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band2,
                        30.0, 0.0, 4.0, 2.5, YamahaPanDirection::right, 10.0, 10.0),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band3,
                        38.1, 0.0, 4.2, 2.5, YamahaPanDirection::right, 10.0, 10.0),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band4,
                        47.6, 0.0, 3.7, 2.5, YamahaPanDirection::left, 10.0, 10.0),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band5,
                        300.0, 4.5, 3.5, 2.5, YamahaPanDirection::left, 10.0, 6.5),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band6,
                        400.0, 3.5, 3.8, 2.5, YamahaPanDirection::right, 10.0, 6.5),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band7,
                        341.0, 4.3, 4.7, 2.5, YamahaPanDirection::left, 10.0, 6.5),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band8,
                        450.0, 3.4, 3.3, 2.5, YamahaPanDirection::right, 10.0, 6.5)},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  450.75,
  {YamahaLevelControlValue{8.5},
   YamahaLevelControlValue{5.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional},
  std::nullopt,
  std::nullopt,
  std::nullopt};

// Physical feedback, modulation, level, and phase values are unmeasured
// audition seeds. They are stored literally and are not calculated from the
// separately transcribed Yamaha source controls. TAP is likewise written
// explicitly in both source-percent and normalized DSP domains.
const HoldsworthDelayPresetDefinition kChorus031ProvisionalV1{
  "chorus031-provisional-v1",
  "Chorus 031 / Chorus 7 (Provisional v1)",
  HoldsworthDelayConfiguration{
    {makeBandConfiguration(31.5, 0.0, -1.0, 1.0, 0.66, 0.75, 0.0, 0.254),
     makeBandConfiguration(22.6, 0.0, 1.0, 1.0, 0.87, 0.75, 0.5, 0.254),
     makeBandConfiguration(40.0, 0.0, -1.0, 1.0, 0.52, 0.75, 0.25, 0.254),
     makeBandConfiguration(48.0, 0.0, 1.0, 0.5, 0.78, 0.75, 0.75, 0.254),
     makeBandConfiguration(250.0, 0.40, -1.0, 0.4, 0.46, 0.75, 0.125),
     makeBandConfiguration(361.0, 0.32, 1.0, 0.4, 0.58, 0.75, 0.625),
     makeBandConfiguration(300.0, 0.40, -1.0, 0.4, 0.38, 0.75, 0.375),
     makeBandConfiguration(400.0, 0.24, 1.0, 0.4, 0.81, 0.75, 0.875)},
    1.0},
  {documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band1,
                        31.5, 0.0, 4.5, 2.5, YamahaPanDirection::left, 10.0, 10.0, 25.4),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band2,
                        22.6, 0.0, 5.2, 2.5, YamahaPanDirection::right, 10.0, 10.0, 25.4),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band3,
                        40.0, 0.0, 4.0, 2.5, YamahaPanDirection::left, 10.0, 10.0, 25.4),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band4,
                        48.0, 0.0, 4.9, 2.5, YamahaPanDirection::right, 10.0, 5.0, 25.4),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band5,
                        250.0, 5.0, 3.8, 2.5, YamahaPanDirection::left, 10.0, 4.0),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band6,
                        361.0, 4.0, 4.2, 2.5, YamahaPanDirection::right, 10.0, 4.0),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band7,
                        300.0, 5.0, 3.5, 2.5, YamahaPanDirection::left, 10.0, 4.0),
   documentedChorusBand(::holdsworth::presets::YamahaEffectBandNumber::band8,
                        400.0, 3.0, 5.0, 2.5, YamahaPanDirection::right, 10.0, 4.0)},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  400.75,
  {YamahaLevelControlValue{8.0},
   YamahaLevelControlValue{8.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional},
  DocumentedYamahaPresetIdentity{"031", "Chorus 7", "Allan Holdsworth"},
  std::nullopt,
  std::nullopt};

// Rates, depths, phases, feedback coefficients, and output levels are literal
// unmeasured audition values. EFFECT LEVEL and Direct controls remain exact
// source metadata and are not mapped into the integration mixer.
const HoldsworthDelayPresetDefinition kHoldsworth111ProvisionalV1{
  "holdsworth111-provisional-v1",
  "Holdsworth 111 / Chorus 10 (Provisional v1)",
  makeHoldsworth111DspConfiguration(),
  makeHoldsworth111DocumentedYamahaValues(),
  FeedbackCalibrationStatus::provisionalUnmeasured,
  400.9,
  {YamahaLevelControlValue{8.5},
   YamahaLevelControlValue{6.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional},
  DocumentedYamahaPresetIdentity{"111", "Chorus 10", "Allan Holdsworth"},
  std::nullopt,
  std::nullopt};

// Physical feedback and output gains are literal unmeasured audition values.
// Delay time, polarity, enabled state, and relationship topology directly
// preserve the official patch-list structure. EFFECT LEVEL and Direct controls
// remain source-only because the integration has no established Yamaha mapping.
const HoldsworthDelayPresetDefinition kHoldsworth223ProvisionalV1{
  "holdsworth223-provisional-v1",
  "Holdsworth 223 (Provisional v1)",
  makeHoldsworth223DspConfiguration(),
  makeHoldsworth223DocumentedYamahaValues(),
  FeedbackCalibrationStatus::provisionalUnmeasured,
  400.0,
  {YamahaLevelControlValue{10.0},
   YamahaLevelControlValue{8.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional},
  DocumentedYamahaPresetIdentity{
    "223", "Single Source Point Stereo Microphone + Echos", "Allan Holdsworth"},
  std::nullopt,
  std::nullopt};

// Factory preset 922 stores synchronized Band 2 SPEED 0.0. The neutral DSP
// phase offset is an explicit unmeasured audition interpretation rather than a
// Yamaha-control conversion.
const HoldsworthDelayPresetDefinition kSync922BaselineProvisionalV1{
  "sync922-baseline-provisional-v1",
  "Yamaha 922 Sync Parameter Sample (Baseline, Provisional v1)",
  makeSync922DspConfiguration(ModulationPhaseOffsetCycles{0.0}),
  makeSync922DocumentedYamahaValues(),
  FeedbackCalibrationStatus::provisionalUnmeasured,
  11.5,
  {YamahaLevelControlValue{10.0},
   YamahaLevelControlValue{10.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional},
  DocumentedYamahaPresetIdentity{"922", "Sync Parameter Sample", ""},
  std::nullopt,
  std::nullopt};

// Yamaha's manual uses Band 1 PHASE Reverse as an audition instruction for
// preset 922. It is not the stored factory state: the source transcription
// below remains exactly NOR/NOR while only the DSP Band 1 polarity is reversed.
const HoldsworthDelayPresetDefinition kSync922Band1ReverseDiagnosticV1{
  "sync922-band1-reverse-diagnostic-v1",
  "Yamaha 922 Sync Parameter Sample (Band 1 Reverse Diagnostic v1)",
  makeSync922Band1ReverseDspConfiguration(),
  makeSync922DocumentedYamahaValues(),
  FeedbackCalibrationStatus::provisionalUnmeasured,
  11.5,
  {YamahaLevelControlValue{10.0},
   YamahaLevelControlValue{10.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional},
  DocumentedYamahaPresetIdentity{"922", "Sync Parameter Sample", ""},
  std::nullopt,
  std::nullopt};

// Yamaha's manual documents synchronized SPEED 5.0 as a 180-degree phase
// difference. This diagnostic uses that isolated reference point while the
// factory source transcription above remains SPEED 0.0.
const HoldsworthDelayPresetDefinition kSync922HalfCycleDiagnosticV1{
  "sync922-half-cycle-diagnostic-v1",
  "Yamaha 922 Sync Parameter Sample (180° Diagnostic v1)",
  makeSync922DspConfiguration(ModulationPhaseOffsetCycles{0.5}),
  makeSync922DocumentedYamahaValues(),
  FeedbackCalibrationStatus::provisionalUnmeasured,
  11.5,
  {YamahaLevelControlValue{10.0},
   YamahaLevelControlValue{10.0},
   YamahaPanControlValue{YamahaPanDirection::center, 0.0}},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::documentedReference},
  DocumentedYamahaPresetIdentity{"922", "Sync Parameter Sample", ""},
  ::holdsworth::presets::YamahaSyncAuditionReference{
    ::holdsworth::presets::YamahaEffectBandNumber::band2,
    ::holdsworth::presets::YamahaSpeedControlValue{5.0},
    ::holdsworth::presets::YamahaDocumentedPhaseDifferenceDegrees{180.0},
    false},
  std::nullopt};

// This is an audition-only control case, not Yamaha factory source data. Its
// physical band settings match the approved 922 baseline, but Band 2 runs its
// retained independent zero-Hz modulation clock and no SYNC graph is present.
const HoldsworthDelayPresetDefinition kSync922IndependentDiagnosticV1{
  "sync922-independent-diagnostic-v1",
  "Yamaha 922 Sync OFF Diagnostic",
  makeSync922IndependentDspConfiguration(),
  {},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  11.5,
  {},
  ModulationCalibrationMetadata{YamahaModulationMappingStatus::unmeasured,
                                YamahaModulationMappingStatus::unmeasured,
                                ModulationPhaseRelationshipStatus::provisional},
  std::nullopt,
  std::nullopt,
  std::nullopt};

// The stored 9.13 source state remains the documented parallel IN/IN state.
// All other physical parameters here are deliberately simple diagnostic
// choices and do not claim an unmeasured Yamaha-control conversion.
const HoldsworthDelayPresetDefinition kConnect913ParallelDiagnosticV1{
  "connect913-parallel-diagnostic-v1",
  "Yamaha 9.13 CONNECT Parallel Diagnostic",
  makeConnect913ParallelDspConfiguration(),
  makeConnect913DocumentedYamahaValues(),
  FeedbackCalibrationStatus::provisionalUnmeasured,
  600.0,
  {},
  std::nullopt,
  std::nullopt,
  std::nullopt,
  kConnect913ManualExerciseReference};

// This is the audition state produced by following manual step 19. Its source
// transcription intentionally remains the original 9.13 IN/IN state above;
// only the physical DSP routing records Band 2 <- Band 1.
const HoldsworthDelayPresetDefinition kConnect913SerialDiagnosticV1{
  "connect913-serial-diagnostic-v1",
  "Yamaha 9.13 CONNECT Serial Diagnostic",
  makeConnect913SerialDspConfiguration(),
  makeConnect913DocumentedYamahaValues(),
  FeedbackCalibrationStatus::provisionalUnmeasured,
  600.0,
  {},
  std::nullopt,
  std::nullopt,
  std::nullopt,
  kConnect913ManualExerciseReference};

// Yamaha documents the GROUP resource/output behavior used here, but not
// these literal physical audition values. Leave all Yamaha source metadata
// empty rather than presenting this project diagnostic as a factory preset or
// manual exercise transcription.
const HoldsworthDelayPresetDefinition kGroup12Rhythm1200DiagnosticV1{
  "group12-rhythm-1200-diagnostic-v1",
  "GROUP 1-2 Rhythm 1200 ms Diagnostic",
  makeGroup12RhythmDspConfiguration(1200.0),
  {},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  696.0,
  {},
  std::nullopt,
  std::nullopt,
  std::nullopt,
  std::nullopt,
  GroupedDelayPhysicalCapacityMs{1200.0}};

const HoldsworthDelayPresetDefinition kGroup12Rhythm900DiagnosticV1{
  "group12-rhythm-900-diagnostic-v1",
  "GROUP 1-2 Rhythm 900 ms Diagnostic",
  makeGroup12RhythmDspConfiguration(900.0),
  {},
  FeedbackCalibrationStatus::provisionalUnmeasured,
  696.0,
  {},
  std::nullopt,
  std::nullopt,
  std::nullopt,
  std::nullopt,
  GroupedDelayPhysicalCapacityMs{900.0}};

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

const HoldsworthDelayPresetDefinition& chorus031ProvisionalV1() noexcept
{
  return kChorus031ProvisionalV1;
}

const HoldsworthDelayPresetDefinition& holdsworth111ProvisionalV1() noexcept
{
  return kHoldsworth111ProvisionalV1;
}

const HoldsworthDelayPresetDefinition& holdsworth223ProvisionalV1() noexcept
{
  return kHoldsworth223ProvisionalV1;
}

const HoldsworthDelayPresetDefinition& sync922BaselineProvisionalV1() noexcept
{
  return kSync922BaselineProvisionalV1;
}

const HoldsworthDelayPresetDefinition& sync922Band1ReverseDiagnosticV1() noexcept
{
  return kSync922Band1ReverseDiagnosticV1;
}

const HoldsworthDelayPresetDefinition& sync922HalfCycleDiagnosticV1() noexcept
{
  return kSync922HalfCycleDiagnosticV1;
}

const HoldsworthDelayPresetDefinition& sync922IndependentDiagnosticV1() noexcept
{
  return kSync922IndependentDiagnosticV1;
}

const HoldsworthDelayPresetDefinition& connect913ParallelDiagnosticV1() noexcept
{
  return kConnect913ParallelDiagnosticV1;
}

const HoldsworthDelayPresetDefinition& connect913SerialDiagnosticV1() noexcept
{
  return kConnect913SerialDiagnosticV1;
}

const HoldsworthDelayPresetDefinition& group12Rhythm1200DiagnosticV1() noexcept
{
  return kGroup12Rhythm1200DiagnosticV1;
}

const HoldsworthDelayPresetDefinition& group12Rhythm900DiagnosticV1() noexcept
{
  return kGroup12Rhythm900DiagnosticV1;
}

} // namespace presets
} // namespace holdsworth::dsp
