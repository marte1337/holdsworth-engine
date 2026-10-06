#pragma once

#ifdef NDEBUG
#undef NDEBUG // Validation assertions remain active in an optimized executable.
#endif

#include "IPlugConstants.h"
using iplug::DEFAULT_BLOCK_SIZE; // Existing resampler headers rely on this scope.
// The plugin header defines this free function without inline. Give the unused
// runner copy its own name when linking the production APP object containing it.
#define GetNAMSampleRate TunerAcceptanceGetNAMSampleRate
#include "../../../NeuralAmpModeler/NeuralAmpModeler.h"
#undef GetNAMSampleRate
#include "rt-audit.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <thread>
#include <vector>

inline constexpr std::array<double, 6> tunerRates{44100., 48000., 88200., 96000., 176400., 192000.};
inline constexpr std::array<int, 11> tunerBlocks{1, 2, 4, 8, 32, 64, 128, 256, 512, 1024, 37};
inline constexpr int tunerMaximumBlock = 1024;

namespace iplug
{
// Reuse the framework's existing latency-validation friend; no product change.
struct IPlugLatencyTestAccess
{
  static void prepare(IPlugProcessor& p, const double rate, const int block)
  { p.SetSampleRate(rate); p.SetBlockSize(block); }
  static void frames(IPlugProcessor& p, const int block) { p.SetBlockSize(block); }
  static void bypass(IPlugProcessor& p, const bool value) { p.SetBypassed(value); }
  static void renderBypass(IPlugProcessor& p, double** input, double** output, const int frames)
  {
    p.AttachBuffers(ERoute::kInput, 0, 1, input, frames);
    p.AttachBuffers(ERoute::kOutput, 0, 2, output, frames);
    p.PassThroughBuffers(0., frames);
  }
};
}

struct AudioScope
{
  AudioScope() { assert(!inAudio); inAudio = true; }
  ~AudioScope() { inAudio = false; }
};

inline void auditSelfTest()
{
  void* (*volatile allocate)(size_t) = &malloc;
  void (*volatile release)(void*) = &free;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  {
    AudioScope scope;
    void* memory = allocate(17);
    release(memory);
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
  }
  pthread_mutex_destroy(&mutex);
  assert(rtViolations.load() >= 3 && "RT interposition must catch allocations, frees and locks");
  rtViolations.store(0);
}

inline void signal(std::span<double> data, const std::uint64_t start, const double rate, const int fixture)
{
  for (std::size_t i = 0; i < data.size(); ++i)
  {
    const double t = static_cast<double>(start + i) / rate;
    if (fixture == 0)
      data[i] = .15 * std::sin(2. * std::numbers::pi * 110. * t);
    else if (fixture == 1)
      data[i] = start + i == 0 ? .25 : 0.;
    else
    {
      const auto hash = (start + i) * 6364136223846793005ULL + 1442695040888963407ULL;
      data[i] = .15 * (static_cast<double>(hash >> 33) / 2147483648. - .5);
    }
  }
}

inline void expectBitExact(std::span<const double> a, std::span<const double> b)
{
  assert(a.size() == b.size());
  assert(std::memcmp(a.data(), b.data(), a.size_bytes()) == 0);
  for (double sample : a)
    assert(std::isfinite(sample));
}

struct NoDrain { void operator()() const {} };

template <typename Render, typename Drain = NoDrain>
void measureCallbacks(const char* format, const double rate, const int block, const char* mode,
                      Render&& render, Drain&& drain = {})
{
  using Clock = std::chrono::steady_clock;
  std::vector<double> times;
  times.reserve(2048);
  for (int i = 0; i < 256; ++i)
  { render(); drain(); }
  for (int i = 0; i < 2048; ++i)
  {
    const auto begin = Clock::now();
    render();
    const auto end = Clock::now();
    times.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    drain(); // Consumer/control work is excluded from capture callback timing.
  }
  std::sort(times.begin(), times.end());
  std::printf("%s,%.0f,%d,%s,%.6f,%.6f,%.6f,%.6f\n", format, rate, block, mode,
              times[1024], times[2027], times.back(), 1.e6 * block / rate);
}
