#include "../dsp/MC402CleanBoostProcessor.h"
#include "TestHarness.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace holdsworth::test
{
namespace
{
using Processor = dsp::MC402CleanBoostProcessor;
constexpr std::array<double, 6> rates{44100., 48000., 88200., 96000., 176400., 192000.};

bool identity()
{
  const std::array<double, 9> input{0., -0., 1., -1., 3., -7., 1e100,
    std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min()};
  for (double rate : rates)
  {
    Processor p; p.prepare(rate, input.size());
    std::array<double, input.size()> output{};
    p.processBlock(input, output);
    if (!expectSamplesBitExact("MC402 0 dB exact finite identity", output, input) || p.latencySamples() != 0)
      return false;
  }
  return true;
}

bool levelsAndInPlace()
{
  for (double rate : rates)
    for (double db : {10., 20.})
    {
      Processor p; p.setBoostDb(db); p.prepare(rate, 8);
      std::array<double, 8> input{0., -0., .1, -.1, 1., -1., 2., -4.}, output{}, expected{};
      const double gain = db == 10. ? std::sqrt(10.) : 10.;
      for (std::size_t i = 0; i < input.size(); ++i) expected[i] = input[i] * gain;
      p.processBlock(input, output);
      if (!expectSamples("MC402 exact +10/+20 mapping", output, expected)) return false;
      p.processBlock(input, input);
      if (!expectSamplesBitExact("MC402 in place", input, output)) return false;
    }
  return true;
}

bool smoothingAndReset()
{
  for (double rate : rates)
  {
    const auto count = static_cast<std::size_t>(std::ceil(.010 * rate));
    Processor p; p.prepare(rate, count + 1); p.setBoostDb(20.);
    std::vector<double> x(count + 1, 1.), y(count + 1);
    p.processBlock(x, y);
    if (!(y.front() > 1. && y.front() < 10.) || y[count-1] != 10. || y[count] != 10.) return false;
    for (std::size_t i = 1; i < y.size(); ++i) if (y[i] < y[i-1]) return false;
    p.setBoostDb(0.); p.processBlock(x, y);
    if (y[count-1] != 1. || y[count] != 1.) return false;
    p.setBoostDb(10.); p.reset(); p.processBlock(x, y);
    if (!expectNear("MC402 reset adopts requested gain", y.front(), std::sqrt(10.))) return false;
    p.prepare(rate, count + 1); p.processBlock(x, y);
    if (!expectNear("MC402 reprepare retains boost", y.front(), std::sqrt(10.))) return false;
  }
  return true;
}

bool partitions()
{
  constexpr std::size_t size = 4096;
  for (double rate : rates)
  {
    Processor whole, split; whole.prepare(rate, size); split.prepare(rate, size);
    std::array<double, size> x{}, a{}, b{};
    for (std::size_t i=0; i<size; ++i) x[i] = 2.*std::sin(.173*static_cast<double>(i));
    for (double db : {20., 10., 0.})
    {
      whole.setBoostDb(db); split.setBoostDb(db);
      whole.processBlock(x,a);
      std::size_t offset=0, cursor=0;
      constexpr std::array<std::size_t,5> sizes{1,7,64,127,511};
      while (offset<size)
      {
        const auto n=std::min(sizes[cursor++%sizes.size()],size-offset);
        split.processBlock(std::span{x}.subspan(offset,n),std::span{b}.subspan(offset,n));
        offset+=n;
      }
      if (!expectSamplesBitExact("MC402 deterministic ramp partitions",a,b)) return false;
    }
  }
  return true;
}

bool finiteAndControls()
{
  const double maximum=std::numeric_limits<double>::max(), inf=std::numeric_limits<double>::infinity();
  Processor p; p.setBoostDb(100.); p.prepare(48000.,8);
  std::array<double,8> x{maximum,-maximum,maximum/100.,-maximum/100.,inf,-inf,
    std::numeric_limits<double>::quiet_NaN(),2.},y{};
  p.processBlock(x,y);
  for (double v:y) if (!std::isfinite(v)) return false;
  if (y[0]!=maximum || y[1]!=-maximum || y[4]!=0. || y[5]!=0. || y[6]!=0. || y[7]!=20.) return false;
  for (double db : {-20.,inf,std::numeric_limits<double>::quiet_NaN()})
  {
    p.setBoostDb(db); p.reset(); std::array<double,1> z{2.}; p.processBlock(z,z);
    if (z[0]!=2.) return false;
  }
  p.setBoostDb(20.); p.reset(); std::array<double,1> z{.5}; p.processBlock(z,z);
  return z[0]==5.;
}

bool realtime()
{
  Processor p; p.prepare(48000.,64); std::array<double,64> x{};
  beginAllocationTracking();
  for (int i=0;i<1000;++i)
  {
    p.setBoostDb(static_cast<double>(i%21));
    p.processBlock(x,x);
    if (i%17==0) p.reset();
  }
  return endAllocationTracking()==0;
}

bool handoff()
{
  Processor p; p.prepare(48000.,64); std::atomic<bool> start{false};
  std::thread controls([&] {
    while (!start.load(std::memory_order_acquire)) {}
    for (int i=0;i<20000;++i) p.setBoostDb(static_cast<double>(i%21));
    p.setBoostDb(20.);
  });
  start.store(true,std::memory_order_release);
  std::array<double,64> x{}, y{}; x.fill(1.); bool finite=true;
  for (int i=0;i<20000;++i)
  {
    p.processBlock(x,y);
    for (double v:y) finite &= std::isfinite(v) && v>=1.-1e-12 && v<=10.+1e-12;
  }
  controls.join();
  for (int i=0;i<10;++i) p.processBlock(x,y);
  return finite && y.back()==10.;
}

bool lifecycleValidation()
{
  Processor p;
  for (double rate : {0.,-1.,std::numeric_limits<double>::infinity()})
  {
    try { p.prepare(rate,64); return false; } catch (const std::invalid_argument&) {}
  }
  try { p.prepare(48000.,0); return false; } catch (const std::invalid_argument&) {}
  return true;
}
}
TestSuite mc402CleanBoostProcessorTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"MC402 Boost identity and zero latency",identity},
    TestCase{"MC402 Boost levels and in-place",levelsAndInPlace},
    TestCase{"MC402 Boost smoothing and reset",smoothingAndReset},
    TestCase{"MC402 Boost block partitions",partitions},
    TestCase{"MC402 Boost finite/extreme input and controls",finiteAndControls},
    TestCase{"MC402 Boost realtime allocations",realtime},
    TestCase{"MC402 Boost concurrent control handoff",handoff},
    TestCase{"MC402 Boost lifecycle validation",lifecycleValidation}};
  return tests;
}
} // namespace holdsworth::test
