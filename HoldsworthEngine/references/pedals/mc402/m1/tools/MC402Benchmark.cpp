// Offline timing only. No benchmark instrumentation enters the processor.
#include "../../../../../tests/MC402TestAccess.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>

int main()
{
  using Processor = holdsworth::dsp::MC402BoostOverdriveProcessor;
  using Access = holdsworth::test::MC402TestAccess;
  std::cout << std::setprecision(12) << "[\n";
  bool first = true;
  for (double rate : Processor::Profile::supportedSampleRates)
  {
    Processor defaultProcessor;
    defaultProcessor.prepare(rate, 64);
    for (unsigned factor : {defaultProcessor.oversamplingFactor(), 8U, 64U})
      for (std::size_t block : {1U, 7U, 64U})
      {
        Processor p;
        p.setControls({true, true, 20., 1., 1., 1.});
        Access::prepare(p, rate, 64, factor);
        std::array<double, 4096> input{};
        std::array<double, 64> output{};
        for (std::size_t i = 0; i < input.size(); ++i)
          input[i] = .5 * std::sin(2. * std::numbers::pi * 997. * static_cast<double>(i) / rate);
        for (int i = 0; i < 1024; ++i)
          p.processBlock(std::span<const double>(input).first(64), output);
        const auto count = static_cast<std::size_t>(rate / 2.) / block;
        const auto begin = std::chrono::steady_clock::now();
        double checksum = 0.;
        std::size_t cursor = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
          p.processBlock(std::span<const double>(input).subspan(cursor, block), std::span<double>(output).first(block));
          cursor += block;
          if (cursor + block > input.size())
            cursor = 0;
          checksum += output[0];
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        const double samples = static_cast<double>(count * block);
        if (!first)
          std::cout << ",\n";
        first = false;
        std::cout << "  {\"sample_rate\":" << rate << ",\"factor\":" << factor << ",\"block\":" << block
                  << ",\"nanoseconds_per_sample\":" << elapsed * 1e9 / samples
                  << ",\"audio_budget_percent\":" << elapsed * rate / samples * 100. << ",\"checksum\":" << checksum
                  << "}";
      }
  }
  std::cout << "\n]\n";
}
