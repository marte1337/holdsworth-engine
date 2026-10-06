#include "../integration/TunerAnalysisService.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <numbers>
#include <thread>
#include <vector>

namespace holdsworth::test
{
namespace
{
using Service = integration::TunerAnalysisService;
using Status = dsp::TunerPitchStatus;
constexpr std::array rates{44100., 48000., 88200., 96000., 176400., 192000.};

struct Stream
{
  double rate;
  std::uint64_t position = 0;
  std::vector<double> samples;
  explicit Stream(double fs) : rate(fs), samples(static_cast<std::size_t>(fs * .02)) {}
  void feed(Service& service, double hz = 110.)
  {
    for (auto& sample : samples)
      sample = .2 * std::sin(2. * std::numbers::pi * hz * static_cast<double>(position++) / rate);
    const double* input = samples.data();
    service.capture().captureBlock(&input, samples.size(), 1, false);
  }
};

void enable(Service& service, double rate)
{
  service.capture().prepare(rate);
  service.capture().setEnabled(true);
  service.capture().setEditorOpen(true);
  (void)service.service(0.); // Timer establishes wall provenance before audio.
}

bool acquisitionAndCadence()
{
  double maximum = 0.;
  for (double rate : rates)
  {
    Service service;
    enable(service, rate);
    Stream stream(rate);
    double first = 0.;
    std::uint64_t previous = 0;
    for (int tick = 1; tick <= 50; ++tick)
    {
      stream.feed(service);
      const double now = static_cast<double>(tick) * .02;
      (void)service.service(now);
      if (service.analysisCount() - previous > 1) return false;
      previous = service.analysisCount();
      if (service.estimate().highConfidence && first == 0.) first = now;
      // Repeated idle calls without new samples cannot re-analyze an old frame.
      (void)service.service(now);
      if (service.analysisCount() != previous) return false;
    }
    if (first == 0. || first > .15 || service.estimate().midiNote != 45
        || service.analysisCount() < 17 || service.analysisCount() > 20) return false;
    maximum = std::max(maximum, first);
  }
  std::cout << "METRIC tuner capture_to_analysis_max_ms=" << maximum * 1000. << '\n';
  return true;
}

bool lifecycle()
{
  Service service;
  enable(service, 48000.);
  Stream stream(48000.);
  double now = 0.;
  auto tick = [&](double hz = 110.) {
    stream.feed(service, hz);
    now += .02;
    (void)service.service(now);
  };
  for (int i = 0; i < 15; ++i) tick();
  if (!service.estimate().highConfidence) return false;

  service.capture().setEnabled(false);
  (void)service.service(now);
  if (service.active() || service.estimate().status == Status::valid) return false;
  const auto disabledCount = service.analysisCount();
  for (int i = 0; i < 10; ++i) tick();
  if (service.analysisCount() != disabledCount) return false;
  service.capture().setEnabled(true);
  tick(220.);
  if (service.estimate().status == Status::valid) return false;
  for (int i = 0; i < 15; ++i) tick(220.);
  if (service.estimate().midiNote != 57) return false;

  // Audio never sees the intervening closed state. Revision still forces a
  // new complete window, including when old chunks are already queued.
  stream.feed(service, 220.);
  service.capture().setEditorOpen(false);
  service.capture().setEditorOpen(true);
  tick(110.);
  if (service.estimate().status == Status::valid) return false;
  for (int i = 0; i < 15; ++i) tick();
  if (service.estimate().midiNote != 45) return false;

  // Same-rate reset must invalidate evidence just like a rate change.
  service.capture().prepare(48000.);
  (void)service.service(now);
  if (service.estimate().status == Status::valid) return false;
  for (int i = 0; i < 10; ++i) tick();
  service.capture().prepare(44100.);
  Stream changed(44100.);
  for (int i = 0; i < 15; ++i)
  {
    changed.feed(service, 440.);
    now += .02;
    (void)service.service(now);
    if (i == 0 && service.estimate().status == Status::valid) return false;
  }
  return service.estimate().midiNote == 69;
}

bool staleAndOverflow()
{
  Service service;
  enable(service, 192000.);
  Stream stream(192000.);
  double now = 0.;
  for (int i = 0; i < 15; ++i)
  {
    stream.feed(service);
    now += .02;
    (void)service.service(now);
  }
  if (!service.estimate().highConfidence) return false;
  const auto count = service.analysisCount();
  // Host stops providing samples while idle callbacks continue.
  for (int i = 0; i < 10; ++i)
  {
    now += .02;
    (void)service.service(now);
  }
  if (service.estimate().status == Status::valid || service.analysisCount() != count) return false;
  // Fill well beyond native FIFO capacity without an idle callback.
  for (int i = 0; i < 40; ++i) stream.feed(service, 220.);
  now += .8;
  (void)service.service(now);
  if (service.capture().overrunChunks() == 0 || service.estimate().status == Status::valid
      || service.analysisCount() != count || service.capture().available() != 0) return false;
  for (int i = 0; i < 15; ++i)
  {
    stream.feed(service, 220.);
    now += .02;
    (void)service.service(now);
  }
  if (service.estimate().midiNote != 57) return false;
  // A 180ms backlog fits this queue, but has no wall-time provenance after the
  // idle gap. Flush it and collect fresh audio instead of replaying old notes.
  const auto before = service.analysisCount();
  for (int i = 0; i < 9; ++i) stream.feed(service, 440.);
  now += .18;
  (void)service.service(now);
  if (service.analysisCount() != before || service.estimate().status == Status::valid
      || service.capture().available() != 0) return false;
  for (int i = 0; i < 10; ++i)
  {
    stream.feed(service, 440.);
    now += .02;
    const auto previous = service.analysisCount();
    (void)service.service(now);
    if (service.analysisCount() - previous > 1) return false;
  }
  return service.estimate().midiNote == 69 && service.estimate().highConfidence;
}

bool staleWallAndFirstServiceBacklog()
{
  for (double rate : rates)
  {
    Service first;
    first.capture().prepare(rate);
    first.capture().setEnabled(true);
    first.capture().setEditorOpen(true);
    Stream initial(rate);
    // A full, apparently current sample window can precede the first service
    // by an arbitrary interval. It must not acquire or create pitch evidence.
    for (int i = 0; i < 7; ++i) initial.feed(first, 440.);
    if (first.service(1.) || first.analysisCount() != 0 || first.estimate().status == Status::valid
        || first.capture().available() != 0) return false;
    for (int i = 1; i <= 10; ++i)
    {
      initial.feed(first, 440.);
      (void)first.service(1. + static_cast<double>(i) * .02);
    }
    if (first.estimate().midiNote != 69 || !first.estimate().highConfidence) return false;

    Service paused;
    enable(paused, rate);
    Stream stream(rate);
    for (int i = 1; i <= 15; ++i)
    {
      stream.feed(paused, 110.);
      (void)paused.service(static_cast<double>(i) * .02);
    }
    if (!paused.estimate().highConfidence) return false;
    const auto before = paused.analysisCount();
    for (int i = 0; i < 5; ++i) stream.feed(paused, 220.);
    // Audio ends at t=.4; the UI resumes at t=1.4. Sample-relative newest age
    // is zero, even though every queued sample is already one second old.
    if (paused.service(1.4) || paused.analysisCount() != before
        || paused.estimate().status == Status::valid || paused.capture().available() != 0) return false;
    for (int i = 1; i <= 10; ++i) (void)paused.service(1.4 + static_cast<double>(i) * .02);
    if (paused.analysisCount() != before || paused.estimate().status == Status::valid) return false;
    // The discarded A3 backlog cannot contaminate a newly resumed A4 window.
    for (int i = 1; i <= 10; ++i)
    {
      stream.feed(paused, 440.);
      (void)paused.service(1.6 + static_cast<double>(i) * .02);
    }
    if (paused.estimate().midiNote != 69 || !paused.estimate().highConfidence) return false;
  }
  return true;
}

bool concurrentService()
{
  Service service;
  enable(service, 48000.);
  std::atomic<bool> done{false};
  std::thread producer([&] {
    std::array<double, 256> input{};
    const double* pointer = input.data();
    for (std::uint32_t block = 0; block < 12000; ++block)
    {
      if (block % 1501 == 0) service.capture().prepare(block % 2 == 0 ? 48000. : 96000.);
      for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = .2 * std::sin(.014 * static_cast<double>(block * 256U + i));
      service.capture().captureBlock(&pointer, input.size(), 1, false);
    }
    done.store(true, std::memory_order_release);
  });
  bool passed = true;
  double now = 0.;
  std::uint64_t previous = 0;
  std::uint32_t calls = 0;
  while (!done.load(std::memory_order_acquire))
  {
    ++calls;
    if (calls % 19 == 0)
    {
      service.capture().setEnabled(false);
      service.capture().setEnabled(true);
    }
    now += .02;
    (void)service.service(now);
    if (service.analysisCount() - previous > 1) passed = false;
    previous = service.analysisCount();
  }
  producer.join();
  return passed;
}

bool unsupportedRateLifecycle()
{
  Service service;
  enable(service, 32000.);
  Stream stream(32000.);
  double now = 0.;
  for (int reset = 0; reset < 3; ++reset)
  {
    service.capture().prepare(32000.);
    for (int tick = 0; tick < 10; ++tick)
    {
      stream.feed(service);
      now += .02;
      (void)service.service(now);
      if (service.estimate().status != Status::unsupportedRate) return false;
    }
    now += .2;
    stream.feed(service);
    (void)service.service(now);
    if (service.estimate().status != Status::unsupportedRate) return false;
  }
  service.capture().prepare(48000.);
  Stream supported(48000.);
  for (int tick = 0; tick < 15; ++tick)
  {
    supported.feed(service);
    now += .02;
    (void)service.service(now);
  }
  return service.estimate().highConfidence && service.estimate().midiNote == 45;
}
} // namespace

TestSuite tunerAnalysisServiceTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Tuner service acquisition bounded cadence", acquisitionAndCadence},
    TestCase{"Tuner service enable editor and rate lifecycle", lifecycle},
    TestCase{"Tuner service stale backlog and overflow", staleAndOverflow},
    TestCase{"Tuner service stale wall and first idle backlog", staleWallAndFirstServiceBacklog},
    TestCase{"Tuner service concurrent reset capture drain", concurrentService},
    TestCase{"Tuner service unsupported rate lifecycle recovery", unsupportedRateLifecycle}};
  return tests;
}
} // namespace holdsworth::test
