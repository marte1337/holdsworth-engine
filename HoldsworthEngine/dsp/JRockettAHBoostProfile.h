#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace holdsworth::dsp
{
// Frozen software audition constants. UNMEASURED: not AH hardware specifications.
struct JRockettAHBoostProfile final
{
  static constexpr std::string_view id = "JROCKETT-AH-BOOST-BEHAVIORAL-V1";
  static constexpr double lowHz = 250.0, highHz = 2500.0;
  static constexpr double maximumBoostDb = 20.0, defaultBoostDb = 0.0;
  static constexpr double levelSeconds = 0.010, crossfadeSeconds = 0.010;
  // Incoming response is primed from recent input, without delaying live audio.
  // Slowest pole is about 177 Hz; 20 ms bounds discarded-history residue.
  static constexpr double historySeconds = 0.020;
  static constexpr double minimumSampleRate = 8000.0, maximumSampleRate = 768000.0;
  static constexpr std::size_t maximumHistorySamples = 15360;
  static constexpr std::size_t defaultMode = 2; // C/L, not a flat wire.
  struct Shelves { double lowDb, highDb; };
  // F/L, F/H, C/L, C/H, T/L, T/H. No level normalization between modes.
  static constexpr std::array<Shelves, 6> shelves{{{6.,0.},{3.,3.},{3.,0.},
                                                             {0.,3.},{-3.,3.},{-6.,6.}}};
};
} // namespace holdsworth::dsp
