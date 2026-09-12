#include "TestHarness.h"
#include "MC402TestAccess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>

namespace holdsworth::test
{
namespace
{
using Processor = dsp::MC402BoostOverdriveProcessor;
using Profile = dsp::MC402ProvisionalProfile;
using Controls = dsp::MC402Controls;

void run(Processor& p, std::span<double> samples, std::size_t block = 64)
{
  for (std::size_t offset = 0; offset < samples.size(); offset += block)
  {
    auto part = samples.subspan(offset, std::min(block, samples.size() - offset));
    p.processBlock(part, part);
  }
}

std::vector<double> tone(double rate, std::size_t count, double amplitude = .2, double hz = 997.)
{
  std::vector<double> result(count);
  for (std::size_t i = 0; i < count; ++i)
    result[i] = amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate);
  return result;
}

double rms(std::span<const double> samples)
{
  double sum = 0.0;
  for (double value : samples)
    sum += value * value;
  return std::sqrt(sum / static_cast<double>(samples.size()));
}

bool testPrepareAndSanitization()
{
  Processor p;
  p.setControls({true, true, 99., -1., 2., std::numeric_limits<double>::quiet_NaN()});
  const auto controls = p.controls();
  if (controls.boostDb != 20. || controls.gain != 0. || controls.tone != 1. || controls.output != .5)
    return false;
  for (std::size_t i = 0; i < Profile::supportedSampleRates.size(); ++i)
  {
    p.prepare(Profile::supportedSampleRates[i], 511);
    if (!p.isPrepared() || p.maximumBlockSize() != 511 || p.oversamplingFactor() != Profile::oversamplingFactors[i])
      return false;
  }
  for (double invalid :
       {0., -1., 32000., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
  {
    try
    {
      p.prepare(invalid, 64);
      return false;
    }
    catch (const std::invalid_argument&)
    {
    }
  }
  try
  {
    p.prepare(48000., 0);
    return false;
  }
  catch (const std::invalid_argument&)
  {
  }
  p.setControls(
    {false, false, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), -.5, 1.5});
  return p.controls().boostDb == 0. && p.controls().gain == .5 && p.controls().tone == 0. && p.controls().output == 1.;
}

bool testBypassAndLatency()
{
  for (const double rate : Profile::supportedSampleRates)
  {
    Processor p;
    p.prepare(rate, 511);
    const auto latency = p.latencySamples();
    std::array<double, 511> x{}, y{}, expected{};
    for (std::size_t i = 0; i < x.size(); ++i)
      x[i] = i % 3 == 0 ? -0.0 : static_cast<double>(i) * .125;
    p.processBlock(x, y);
    for (std::size_t i = latency; i < x.size(); ++i)
      expected[i] = x[i - latency];
    if (!expectSamplesBitExact("MC402 delayed bypass", y, expected))
      return false;
    p.reset();
    p.processBlock(x, x);
    if (!expectSamplesBitExact("MC402 bypass in place", x, expected))
      return false;
    // Measure the actual FIR impulse center; no audio filter phase is included.
    p.reset();
    for (std::size_t i = 0; i < y.size(); ++i)
      y[i] = MC402TestAccess::wireSample(p, i == 0 ? 1.0 : 0.0);
    const auto peak = static_cast<std::size_t>(std::max_element(y.begin(), y.end()) - y.begin());
    if (peak != latency)
      return false;
    std::cout << "MC402 measured FIR latency " << rate << " Hz: " << peak << " samples\n";
  }
  return true;
}

bool testBoostAndSectionIndependence()
{
  for (double db : {0., 10., 20.})
  {
    Processor p;
    p.setControls({true, false, db, 0., 0., 0.});
    p.prepare(48000., 512);
    auto x = tone(48000., 512, 2.0);
    const auto original = x;
    run(p, x);
    for (std::size_t i = p.latencySamples(); i < x.size(); ++i)
    {
      const double expected = original[i - p.latencySamples()] * std::pow(10., db / 20.);
      if (!expectNear("MC402 Boost", x[i], expected, 1e-12 * std::max(1., std::abs(expected))))
        return false;
    }
    p.setControls({true, false, db, 1., 1., 1.});
    p.reset();
    auto changed = original;
    run(p, changed);
    if (!expectSamplesBitExact("MC402 Boost ignores OD controls", x, changed))
      return false;
  }
  auto x = tone(48000., 4096);
  auto a = x, b = x;
  Processor od, both;
  od.setControls({false, true, 0., .8, .7, 1.});
  both.setControls({true, true, 20., .8, .7, 1.});
  od.prepare(48000., 64);
  both.prepare(48000., 64);
  run(od, a);
  run(both, b);
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!expectNear("MC402 OD then Boost", b[i], a[i] * 10., 1e-12))
      return false;
  return true;
}

bool testProvisionalKnees()
{
  const double h = Profile::stage1SwingVolts;
  for (double x : {0., .1, 2.25, 2.5, 2.75, 3., 1e100})
  {
    const double y = MC402TestAccess::saturate(x, h);
    if (y > h || y < 0. || y != -MC402TestAccess::saturate(-x, h))
      return false;
    if (x <= 2.25 && y != x)
      return false;
    if (x >= 2.75 && y != h)
      return false;
  }
  constexpr double epsilon = 1e-6;
  for (double join : {2.25, 2.75})
  {
    const double left = (MC402TestAccess::saturate(join, h) - MC402TestAccess::saturate(join - epsilon, h)) / epsilon;
    const double right = (MC402TestAccess::saturate(join + epsilon, h) - MC402TestAccess::saturate(join, h)) / epsilon;
    if (std::abs(left - right) > 3e-6)
      return false;
  }
  return true;
}

bool testProvisionalGainAndOutputMuteEndpoints()
{
  // Regression of THIS named profile. No claim that hardware Gain zero mutes.
  Processor p;
  p.setControls({false, true, 0., 1., 1., 1.});
  p.prepare(48000., 64);
  auto x = tone(48000., 48000);
  run(p, x);
  p.setControls({false, true, 0., 0., 1., 1.});
  x = tone(48000., 48000);
  run(p, x);
  if (rms(std::span<const double>(x).last(4096)) > 1e-12)
    return false;
  p.setControls({false, true, 0., 1., 1., 0.});
  x = tone(48000., 4096);
  run(p, x);
  if (!std::all_of(x.end() - 1024, x.end(), [](double v) { return v == 0.; }))
    return false;
  for (double g : {0., 1.})
    for (double t : {0., 1.})
      for (double o : {0., 1.})
      {
        p.setControls({false, true, 0., g, t, o});
        p.reset();
        x = tone(48000., 4096);
        run(p, x);
        if (!std::all_of(x.begin(), x.end(), [](double v) { return std::isfinite(v); }))
          return false;
        if ((g == 0. || o == 0.) && rms(x) != 0.)
          return false;
      }
  return true;
}

bool testToneAndInputCleanup()
{
  std::array<double, 3> levels{};
  std::size_t index = 0;
  for (double t : {0., .5, 1.})
  {
    Processor p;
    p.setControls({false, true, 0., .5, t, 1.});
    p.prepare(48000., 64);
    auto x = tone(48000., 8192, 1e-5, 6000.);
    run(p, x);
    levels[index++] = rms(std::span<const double>(x).last(4096));
  }
  if (!(levels[0] < levels[1] && levels[1] < levels[2]))
    return false;
  Processor a, b;
  a.setControls({false, true, 0., 1., 1., 1.});
  b.setControls(a.controls());
  a.prepare(48000., 64);
  b.prepare(48000., 64);
  auto low = tone(48000., 8192, 1e-5);
  auto twice = tone(48000., 8192, 2e-5);
  run(a, low);
  run(b, twice);
  for (std::size_t i = 0; i < low.size(); ++i)
    if (std::abs(twice[i] - 2. * low[i]) > 1e-12)
      return false;
  // High input is compressed locally; Gain is not an input-drive multiplier.
  a.reset();
  b.reset();
  auto high = tone(48000., 8192, 1.);
  auto higher = tone(48000., 8192, 2.);
  run(a, high);
  run(b, higher);
  return rms(std::span<const double>(higher).last(4096)) < 1.5 * rms(std::span<const double>(high).last(4096));
}

bool testStageClippingWithReducedGain()
{
  // Reduced interstage Gain does not undo first-stage saturation. These are
  // transfer properties of the named provisional profile, not measured hardware.
  auto level = [](double gain, double amplitude) {
    Processor p;
    p.setControls({false, true, 0., gain, 1., 1.});
    p.prepare(48000., 64);
    auto x = tone(48000., 24000, amplitude, 1000.);
    run(p, x);
    return rms(std::span<const double>(x).last(4096));
  };
  const double lowGainRatio = level(.1, .04) / level(.1, .02);
  const double highGainRatio = level(1., .04) / level(1., .02);
  const double firstStageRatio = level(.05, 1.) / level(.05, .5);
  return std::abs(lowGainRatio - 2.) < 1e-6 && highGainRatio < 1.5 && firstStageRatio < 1.5;
}

bool testDcBurstsAndRepeatedReset()
{
  Processor p;
  for (double rate : Profile::supportedSampleRates)
  {
    for (unsigned corner = 0; corner < 8; ++corner)
    {
      p.setControls({true, true, 20., (corner & 1) ? 1. : 0., (corner & 2) ? 1. : 0., (corner & 4) ? 1. : 0.});
      p.prepare(rate, 256);
      std::vector<double> x(static_cast<std::size_t>(rate), 0.);
      for (std::size_t i = 0; i < static_cast<std::size_t>(rate / 5.); ++i)
        x[i] = i < static_cast<std::size_t>(rate / 10.)
                 ? 10.
                 : 5.
                     * (std::sin(2. * std::numbers::pi * 997. * static_cast<double>(i) / rate)
                        + std::sin(2. * std::numbers::pi * 5039. * static_cast<double>(i) / rate));
      x[static_cast<std::size_t>(rate / 4.)] = -10.;
      const auto stimulus = x;
      run(p, x, 256);
      if (!std::all_of(x.begin(), x.end(), [](double value) { return std::isfinite(value) && std::abs(value) < 100.; })
          || rms(std::span<const double>(x).last(1024)) > 1e-10)
        return false;
      p.reset();
      auto repeated = stimulus;
      run(p, repeated, 7);
      if (!expectSamplesBitExact("MC402 reset repeat after DC/bursts", x, repeated))
        return false;
    }
  }
  return true;
}


bool testPartitionsAndTransitions()
{
  constexpr std::array<std::size_t, 8> events{0, 191, 401, 1201, 1333, 1701, 2200, 4096};
  const std::array<Controls, 7> targets{{{false, false, 0., .5, .5, .5},
                                         {true, true, 12., 1., 0., 1.},
                                         {true, true, 5., .2, 1., .3},
                                         {false, false, 0., .5, .5, .5},
                                         {true, true, 20., .7, .8, .9},
                                         {true, false, 10., 1., 1., 1.},
                                         {false, false, 0., 0., 0., 0.}}};
  for (double rate : Profile::supportedSampleRates)
  {
    const auto x = tone(rate, events.back());
    std::array<std::vector<double>, 3> results{x, x, x};
    for (std::size_t mode = 0; mode < results.size(); ++mode)
    {
      Processor p;
      p.prepare(rate, 4096);
      constexpr std::array<std::size_t, 7> blocks{1, 7, 64, 3, 129, 11, 296};
      for (std::size_t e = 0; e < targets.size(); ++e)
      {
        p.setControls(targets[e]);
        std::size_t offset = events[e], blockIndex = 0;
        while (offset < events[e + 1])
        {
          const std::size_t size =
            std::min(events[e + 1] - offset, mode == 0 ? 4096 : (mode == 1 ? 1 : blocks[blockIndex++ % blocks.size()]));
          auto part = std::span<double>(results[mode]).subspan(offset, size);
          if (mode == 0)
            p.processBlock(std::span<const double>(x).subspan(offset, size), part);
          else
            p.processBlock(part, part);
          offset += size;
        }
      }
    }
    if (!expectSamplesBitExact("MC402 single-sample partitions", results[0], results[1])
        || !expectSamplesBitExact("MC402 irregular partitions/in-place", results[0], results[2]))
      return false;
  }
  return true;
}

bool testSmoothingAndFadeLifecycle()
{
  Processor p;
  p.prepare(48000., 1);
  p.setControls({true, true, 20., 1., 1., 1.});
  std::array<double, 1> x{.1};
  for (std::size_t i = 0; i < 2 * p.latencySamples() - 1; ++i)
  {
    x[0] = .1;
    p.processBlock(x, x);
    if (MC402TestAccess::mix(p) != 0.)
      return false;
  }
  for (int i = 0; i < 1000; ++i)
  {
    x[0] = .1;
    p.processBlock(x, x);
  }
  if (MC402TestAccess::mix(p) != 1.)
    return false;
  p.setControls({false, false, 0., 0., 0., 0.});
  for (int i = 0; i < 1000; ++i)
  {
    x[0] = .1;
    p.processBlock(x, x);
  }
  if (MC402TestAccess::mix(p) != 0. || MC402TestAccess::running(p))
    return false;
  return x[0] == .1;
}

bool testControlCoalescingAndConcurrentSnapshots()
{
  Processor p;
  p.prepare(48000., 1);
  for (int i = 0; i < 1000; ++i)
  {
    const double value = static_cast<double>(i) / 1000.;
    p.setControls({false, false, 0., value, value, value});
  }
  std::array<double, 1> x{};
  p.processBlock(x, x);
  if (MC402TestAccess::audioControls(p).gain != .999)
    return false;
  std::atomic<bool> done{false};
  std::thread producer([&] {
    for (int i = 0; i < 20000; ++i)
    {
      const double value = static_cast<double>(i % 1001) / 1000.;
      p.setControls({false, false, 0., value, value, value});
    }
    p.setControls({false, false, 0., .123, .123, .123});
    done.store(true, std::memory_order_release);
  });
  bool coherent = true;
  do
  {
    p.processBlock(x, x);
    const auto c = MC402TestAccess::audioControls(p);
    coherent = coherent && c.gain == c.tone && c.tone == c.output;
  } while (!done.load(std::memory_order_acquire));
  producer.join();
  p.processBlock(x, x);
  return coherent && MC402TestAccess::audioControls(p).gain == .123;
}

bool testRealtimeAndExtremeValues()
{
  Processor p;
  p.prepare(48000., 64);
  std::array<double, 64> x{};
  x[0] = std::numeric_limits<double>::max();
  x[1] = -x[0];
  x[2] = std::numeric_limits<double>::infinity();
  x[3] = std::numeric_limits<double>::quiet_NaN();
  x[4] = std::numeric_limits<double>::denorm_min();
  beginAllocationTracking();
  for (int i = 0; i < 100; ++i)
  {
    p.setControls({true, (i % 2) == 0, 20., 1., 1., 1.});
    p.processBlock(x, x);
    if (i % 7 == 0)
      p.reset();
  }
  const auto allocations = endAllocationTracking();
  if (allocations != 0 || !std::all_of(x.begin(), x.end(), [](double v) { return std::isfinite(v); }))
    return false;
  p.setControls({false, true, 0., 1., 1., 1.});
  p.reset();
  x.fill(0.);
  x[0] = std::numeric_limits<double>::max();
  p.processBlock(x, x);
  for (int i = 0; i < 20000; ++i)
  {
    x.fill(0.);
    p.processBlock(x, x);
  }
  return rms(x) < 1e-100;
}

bool testThirtySecondStressAtEveryRate()
{
  std::uint32_t random = 0x31415926;
  for (double rate : Profile::supportedSampleRates)
  {
    Processor p;
    p.prepare(rate, 64);
    std::array<double, 64> x{};
    const auto frames = static_cast<std::size_t>(rate * 30.);
    for (std::size_t offset = 0; offset < frames; offset += x.size())
    {
      if (offset % 4096 == 0)
      {
        const auto corner = (offset / 4096) % 8;
        p.setControls({true, true, 20., (corner & 1) ? 1. : 0., (corner & 2) ? 1. : 0., (corner & 4) ? 1. : 0.});
      }
      for (auto& value : x)
      {
        random = 1664525U * random + 1013904223U;
        value = (static_cast<double>(random) / 4294967295. - .5) * 20.;
      }
      p.processBlock(x, x);
      for (double value : x)
        if (!std::isfinite(value) || std::abs(value) > 100.)
          return false;
    }
    p.reset();
    x.fill(0.);
    p.processBlock(x, x);
    if (rms(x) != 0.)
      return false;
  }
  return true;
}

const std::array tests{
  TestCase{"MC402: prepare contract and control sanitization", testPrepareAndSanitization},
  TestCase{"MC402: exact delayed bypass and measured FIR latency", testBypassAndLatency},
  TestCase{"MC402: Boost gain and independent sections in reviewed order", testBoostAndSectionIndependence},
  TestCase{"MC402 provisional profile: saturation knees", testProvisionalKnees},
  TestCase{"MC402 provisional profile: Gain-minimum and Output mute", testProvisionalGainAndOutputMuteEndpoints},
  TestCase{"MC402: Tone direction and input cleanup", testToneAndInputCleanup},
  TestCase{"MC402 provisional profile: first-stage clipping with reduced Gain", testStageClippingWithReducedGain},
  TestCase{"MC402: DC, multitone bursts, impulse recovery and repeated reset", testDcBurstsAndRepeatedReset},
  TestCase{"MC402: exact partition/in-place invariance through transitions", testPartitionsAndTransitions},
  TestCase{"MC402: smoothing and section fade lifecycle", testSmoothingAndFadeLifecycle},
  TestCase{"MC402: coalescing and concurrent coherent snapshots", testControlCoalescingAndConcurrentSnapshots},
  TestCase{"MC402: allocation-free realtime calls and extreme input recovery", testRealtimeAndExtremeValues},
  TestCase{"MC402: 30-second stress at every supported rate", testThirtySecondStressAtEveryRate}};
} // namespace
TestSuite mc402BoostOverdriveProcessorTests() noexcept
{
  return tests;
}
} // namespace holdsworth::test
