#include "../../../dsp/ChromaticTuner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

int main()
{
  using Clock = std::chrono::steady_clock;
  constexpr std::array rates{44100., 48000., 88200., 96000., 176400., 192000.};
  double checksum = 0.;
  for (double fs : rates)
  {
    holdsworth::dsp::ChromaticTuner tuner;
    if (!tuner.prepare(fs)) return 1;
    std::vector<float> data(static_cast<std::size_t>(fs * .15));
    for (std::size_t i = 0; i < data.size(); ++i)
      data[i] = static_cast<float>(.2 * std::sin(2. * std::numbers::pi * 41.2034446141 * static_cast<double>(i) / fs));
    tuner.pushSamples(data);
    for (int i = 0; i < 20; ++i) checksum += tuner.analyze().frequencyHz;
    std::array<double, 1000> analysis{}, feed{};
    const auto hop = std::span<const float>{data}.first(static_cast<std::size_t>(fs / 20.));
    for (auto& micros : analysis)
    {
      const auto start = Clock::now();
      const auto result = tuner.analyze();
      const auto end = Clock::now();
      checksum += result.frequencyHz;
      micros = std::chrono::duration<double, std::micro>(end - start).count();
    }
    for (auto& micros : feed)
    {
      const auto start = Clock::now();
      tuner.pushSamples(hop);
      const auto end = Clock::now();
      micros = std::chrono::duration<double, std::micro>(end - start).count();
    }
    std::sort(analysis.begin(), analysis.end());
    std::sort(feed.begin(), feed.end());
    std::cout << "{\"sample_rate\":" << fs << ",\"analysis_us_p50\":" << analysis[500]
              << ",\"analysis_us_p99\":" << analysis[990] << ",\"analysis_us_max\":" << analysis.back()
              << ",\"feed_50ms_us_p99\":" << feed[990] << "}\n";
  }
  return checksum > 0. ? 0 : 1;
}
