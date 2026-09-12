// M1c R2 offline-only finite stress/state observation; production is untouched.
#include "R2Experiment.cpp"
#include <cassert>
#include <iostream>
#include <iomanip>

int main()
{
  using namespace mc402_m1c;
  std::cout << std::setprecision(17) << "[\n";
  bool firstRow = true;
  for (double rate : P::supportedSampleRates)
    for (const auto configuration : {std::array<unsigned, 2>{4U, 0U}, {8U, 0U}, {4U, 1U}, {4U, 2U}, {8U, 2U}})
      for (double tone : {0., 1.})
      {
        const auto factor = configuration[0];
        const int mode = static_cast<int>(configuration[1]);
        Experiment p;
        p.prepare(rate, factor, 1., tone, 1., mode, mode, 3, true, false);
        double peak = 0., statePeak = 0.;
        const auto samples = static_cast<std::size_t>(30. * rate);
        for (std::size_t i = 0; i < samples; ++i)
        {
          const double time = static_cast<double>(i) / rate;
          // Alternating DC, near-Nyquist, multitone and deterministic bursts.
          const int section = static_cast<int>(time) % 4;
          const double x = section == 0 ? (time < 15. ? 10. : -10.)
                           : section == 1 ? 10. * std::sin(2. * std::numbers::pi * .49 * static_cast<double>(i))
                           : section == 2 ? 5. * (std::sin(2. * std::numbers::pi * 997. * time)
                                                + std::sin(2. * std::numbers::pi * 7103. * time))
                                          : ((i % 997U) < 7U ? (i % 2U ? -10. : 10.) : 0.);
          const double y = p.tick(x);
          assert(std::isfinite(y));
          peak = std::max(peak, std::abs(y));
          for (const double state : {p.core.hp1.state, p.core.hp2.state, p.core.tone.state,
                                     p.core.hp3.state, p.core.s1.previous, p.core.s1.older,
                                     p.core.s2.previous, p.core.s2.older})
          {
            assert(std::isfinite(state));
            statePeak = std::max(statePeak, std::abs(state));
          }
        }
        double recovery = 0.;
        for (std::size_t i = 0; i < static_cast<std::size_t>(2. * rate); ++i)
        {
          const double y = p.tick(0.);
          assert(std::isfinite(y));
          if (i >= static_cast<std::size_t>(rate))
            recovery = std::max(recovery, std::abs(y));
        }
        assert(peak < 100. && statePeak < 1000. && recovery < 1e-12);
        if (!firstRow)
          std::cout << ",\n";
        firstRow = false;
        std::cout << "{\"sample_rate\":" << rate << ",\"factor\":" << factor
                  << ",\"mode\":" << mode << ",\"tone\":" << tone
                  << ",\"seconds\":30,\"maximum_output_peak\":" << peak
                  << ",\"maximum_observed_core_state_magnitude\":" << statePeak
                  << ",\"recovery_peak_after_one_second\":" << recovery << '}';
      }
  std::cout << "\n]\n";
}
