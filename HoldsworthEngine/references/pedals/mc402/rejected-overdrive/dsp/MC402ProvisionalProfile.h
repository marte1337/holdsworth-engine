#pragma once

#include <array>
#include <cstddef>
#include <numbers>
#include <string_view>

namespace holdsworth::dsp
{
// One coherent, reviewed behavioral profile. These are NOT hardware calibration
// data or an OP275 device model. See references/pedals/mc402/dsp-design.md.
struct MC402ProvisionalProfile final
{
  static constexpr std::string_view id = "MC402-BOUNDED-V1-PROVISIONAL";
  static constexpr double inputResistanceOhms = 22'000.0;
  static constexpr double inputCapacitanceFarads = 47.0e-9;
  static constexpr double feedbackResistanceOhms = 470'000.0;
  static constexpr double stage1Gain = -feedbackResistanceOhms / inputResistanceOhms;
  // Provisional adoption of the reported R24 correction, not a verified trace.
  static constexpr double stage2Gain = stage1Gain;
  static constexpr double inputHighPassHz =
    1.0 / (2.0 * std::numbers::pi * inputResistanceOhms * inputCapacitanceFarads);
  static constexpr double interstageHighPassHz = 154.0;
  static constexpr double outputHighPassHz = 10.0;
  static constexpr double stage1SwingVolts = 2.5;
  static constexpr double stage2SwingVolts = 2.5;
  static constexpr double kneeStart = 0.9;
  static constexpr double kneeEnd = 1.1;
  static constexpr double kneeWidth = 0.2;
  static constexpr double kneeQuadratic = 0.1;
  static constexpr double attenuationExponent = 3.321928094887362;
  // Gain zero is a provisional interstage-mute endpoint, not hardware truth.
  static constexpr double toneDarkHz = 500.0;
  static constexpr double toneFrequencyRatio = 16.0;
  static constexpr double boostMaximumDb = 20.0;
  static constexpr double defaultPosition = 0.5;
  static constexpr double smoothingSeconds = 0.010;
  static constexpr double sectionFadeSeconds = 0.005;
  static constexpr std::array<double, 6> supportedSampleRates{
    44'100.0, 48'000.0, 88'200.0, 96'000.0, 176'400.0, 192'000.0};
  // Reviewed starting factors retained for isolated evaluation only. M1 measured
  // alias rejection FAILS the -70 dBc gate; no factor is qualified for M2.
  // See references/pedals/mc402/m1/README.md; no runtime quality knob.
  static constexpr std::array<unsigned, 6> oversamplingFactors{4, 4, 2, 2, 1, 1};
  static constexpr std::array<std::size_t, 6> firLengths{65, 33, 33, 33, 33, 65};
  static constexpr std::size_t maximumFirLength = 65;
  static constexpr std::size_t maximumStages = firLengths.size();
  static constexpr std::size_t maximumDelaySamples = 64;
  // Numerical guards far outside the audio/voltage domain, not analog clipping.
  static constexpr double stateInputLimit = 1.0e100;
  static constexpr double stateSilenceFloor = 1.0e-300;
};
} // namespace holdsworth::dsp
