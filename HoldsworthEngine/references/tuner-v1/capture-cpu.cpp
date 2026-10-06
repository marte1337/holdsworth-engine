// Measures production capture only; consumer draining is outside every interval.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../../integration/TunerCaptureBuffer.h"
#include "rt-audit.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>

thread_local bool inAudio = false;
std::atomic<unsigned> rtViolations{0};

int main()
{
  using Buffer = holdsworth::integration::TunerCaptureBuffer;
  std::puts("format,rate,block,mode,p50_us,p99_us,max_us,deadline_us");
  for (double rate : {44100., 48000., 88200., 96000., 176400., 192000.})
    for (std::size_t block : {1, 2, 4, 8, 32, 64, 128, 256, 512, 1024, 37})
      for (int mode = 0; mode < 4; ++mode)
      {
        Buffer buffer;
        buffer.prepare(rate); buffer.setEnabled(mode != 0); buffer.setEditorOpen(mode != 1);
        std::array<double, 1024> samples{}; samples.fill(.1);
        const double* pointers[] = {samples.data()};
        Buffer::Chunk chunk;
        auto drain = [&] { if (mode != 3) while (buffer.pop(chunk)) {} };
        auto capture = [&] {
          inAudio = true; buffer.captureBlock(pointers, block, 1, true); inAudio = false;
        };
        for (int i = 0; i < 70000 / static_cast<int>(block) + 256; ++i) { capture(); drain(); }
        std::vector<double> times; times.reserve(2048);
        for (int i = 0; i < 2048; ++i)
        {
          const auto start = std::chrono::steady_clock::now();
          capture();
          const auto end = std::chrono::steady_clock::now();
          times.push_back(std::chrono::duration<double, std::micro>(end - start).count());
          drain();
        }
        std::sort(times.begin(), times.end());
        const char* name[] = {"disabled", "editor-closed", "enabled-drained", "enabled-full"};
        std::printf("CAPTURE,%.0f,%zu,%s,%.6f,%.6f,%.6f,%.6f\n", rate, block, name[mode],
                    times[1024], times[2027], times.back(), 1.e6 * static_cast<double>(block) / rate);
      }
  assert(rtViolations.load() == 0);
}
