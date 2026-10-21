// Standalone native DSP timing and RT call audit. UI/control publishing and
// clock/vector bookkeeping are outside every audio interval. Production target
// adoption/coefficient work is included in continuously changing callbacks.
#include "../../dsp/PostAmpEQProcessor.h"
#include "rt-audit.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

thread_local bool inAudio = false;
std::atomic<unsigned> rtViolations{0};

int main(int argc, char** argv)
{
  if (argc == 2 && std::strcmp(argv[1], "--interposer-self-test") == 0)
  {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    inAudio = true;
    void* volatile allocation = std::malloc(19);
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    std::free(allocation);
    inAudio = false;
    pthread_mutex_destroy(&mutex);
    std::printf("SELFTEST intercepted=%u expected=3\n", rtViolations.load());
    return rtViolations.load() == 3 ? 0 : 1;
  }
  using EQ = holdsworth::dsp::PostAmpEQProcessor;
  using Controls = holdsworth::dsp::PostAmpEQControls;
  std::puts("format,rate,block,mode,callbacks,p50_us,p99_us,max_us,deadline_us,p99_percent,max_percent");
  volatile double checksum = 0.;
  for (double fs : {44100., 48000., 88200., 96000., 176400., 192000.})
    for (std::size_t block : {1U, 7U, 32U, 64U, 127U, 1024U, 4096U})
      for (unsigned mode = 0; mode < 5; ++mode)
      {
        EQ eq;
        eq.prepare(fs, 4096);
        Controls controls;
        controls.enabled = mode != 0;
        if (mode >= 2)
        {
          controls.highPassEnabled = controls.lowPassEnabled = true;
          controls.bands = {{{80., 12., 4.}, {1000., -12., .35}, {8000., 12., 4.}}};
          controls.trimDb = -12.;
        }
        eq.setControls(controls); eq.reset();
        std::array<double, 4096> input{}, output{};
        for (std::size_t i = 0; i < input.size(); ++i) input[i] = .001 * std::sin(.017 * static_cast<double>(i));
        const auto in = std::span<const double>{input}.first(block);
        const auto out = std::span<double>{output}.first(block);
        const auto count = block == 32 ? 4096U : 1024U;
        std::vector<double> times; times.reserve(count);
        const auto publish = [&](unsigned tick) {
          if (mode < 3) return;
          const double fraction = static_cast<double>(tick % 997U) / 996.;
          const double reverse = 1. - fraction;
          for (std::size_t band = 0; band < 3; ++band)
          {
            const double position = band % 2 == 0 ? fraction : reverse;
            controls.bands[band] = {80. * std::pow(100., position), -12. + 24. * position, .35 * std::pow(4. / .35, position)};
          }
          controls.highPassHz = 20. + 180. * fraction;
          controls.lowPassHz = 3000. + 13000. * reverse;
          controls.trimDb = -24. + 30. * fraction;
          if (mode == 4)
          {
            controls.enabled = tick % 211U < 155U;
            controls.highPassEnabled = tick % 113U < 55U;
            controls.lowPassEnabled = tick % 151U < 75U;
          }
          eq.setControls(controls);
        };
        const auto warm = static_cast<unsigned>(fs * .06 / static_cast<double>(block)) + 32U;
        for (unsigned tick = 0; tick < warm; ++tick)
        {
          publish(tick);
          inAudio = true; eq.processBlock(in, out); inAudio = false;
        }
        for (unsigned tick = 0; tick < count; ++tick)
        {
          publish(tick + warm);
          const auto start = std::chrono::steady_clock::now();
          inAudio = true; eq.processBlock(in, out); inAudio = false;
          const auto stop = std::chrono::steady_clock::now();
          times.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
          checksum = checksum + output[block - 1];
        }
        std::sort(times.begin(), times.end());
        const double deadline = 1.e6 * static_cast<double>(block) / fs;
        const double p99 = times[(times.size() * 99U) / 100U];
        constexpr std::array names{"bypass-tracking", "neutral-tracking", "settled-five-sections", "continuous-sweep", "continuous-sweep-enables"};
        std::printf("EQ,%.0f,%zu,%s,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", fs, block, names[mode], count,
                    times[times.size() / 2U], p99, times.back(), deadline, 100. * p99 / deadline,
                    100. * times.back() / deadline);
      }
  std::fprintf(stderr, "RT_AUDIT violations=%u checksum=%.17g\n", rtViolations.load(), static_cast<double>(checksum));
  return rtViolations.load() == 0 ? 0 : 1;
}
