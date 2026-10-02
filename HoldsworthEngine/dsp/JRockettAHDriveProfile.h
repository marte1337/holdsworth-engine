#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace holdsworth::dsp
{
// Frozen, UNMEASURED software behavior. No circuit, rail or hardware-taper claim.
struct JRockettAHDriveProfile final
{
  static constexpr std::string_view id = "JROCKETT-AH-DRIVE-BEHAVIORAL-V1";
  static constexpr double referenceVolts = 1.0;
  static constexpr double maximumGainDb = 24.0, compensationExponent = -0.35;
  static constexpr double defaultGain = 0.5, defaultTone = 0.5;
  static constexpr double bassHz = 250.0, trebleHz = 2500.0;
  static constexpr double minimumBassDb = -6.0, maximumBassDb = 6.0;
  static constexpr double minimumTrebleDb = -9.0, maximumTrebleDb = 9.0;
  static constexpr double maximumVolumeDb = 12.0;
  static constexpr double maximumVolumeGain = 3.9810717055349722;
  static constexpr double volumeExponent = 3.0;
  static constexpr double defaultVolume = 0.6309573444801932; // 10^(-12/60), 0 dB
  static constexpr double smoothingSeconds = 0.010;
  static constexpr std::size_t oversampling = 4, firTaps = 129;
  static constexpr double firKaiserBeta = 8.6, firCutoffBaseRatio = 0.5;
  static constexpr std::size_t latency = (firTaps - 1) / oversampling; // two FIRs
  static constexpr std::array<double, 6> supportedRates{44100.,48000.,88200.,96000.,176400.,192000.};
};
} // namespace holdsworth::dsp
