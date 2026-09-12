#include "../integration/DevelopmentPreNAMSelector.h"
#include "../dsp/TCBLDCleanBoostProcessor.h"
#include "../dsp/MC402CleanBoostProcessor.h"
#include "TestHarness.h"
#include <array>
#include <limits>

namespace holdsworth::test
{
namespace
{
using Selector=integration::DevelopmentPreNAMSelector;
using Choice=integration::DevelopmentPreNAMProcessor;
struct Spy
{
  bool ready=true;
  std::size_t maxBlock=64;
  int calls=0, resets=0;
  double gain=1.;
  bool isPrepared() const { return ready; }
  std::size_t maximumBlockSize() const { return maxBlock; }
  void reset() { ++resets; }
  void processBlock(std::span<const double> x,std::span<double> y)
  { ++calls; for (std::size_t i=0;i<x.size();++i) y[i]=x[i]*gain; }
};

bool exclusivity()
{
  Selector s; Spy tc,mc; tc.gain=2.;mc.gain=10.;
  for (Choice choice : {Choice::off,Choice::tcBld,Choice::mc402Boost,Choice::off})
  {
    const int t=tc.calls,m=mc.calls;
    s.beginBlock(choice,1,tc,mc);
    std::array<double,1> x{.25*s.inputGain(3.,4.)};
    s.processSelected(x,.5,tc,mc);
    const double expected=choice==Choice::off ? .75 : choice==Choice::tcBld ? 1. : 5.;
    if (x[0]!=expected || tc.calls-t!=(choice==Choice::tcBld ? 1:0)
        || mc.calls-m!=(choice==Choice::mc402Boost ? 1:0)) return false;
    // Observe what both the gate trigger and a nonlinear NAM test double receive.
    const double trigger=x[0], nam=x[0]*x[0]*x[0];
    if (trigger!=expected || nam!=expected*expected*expected) return false;
  }
  return tc.resets==2 && mc.resets==2;
}

bool fallbackAndMapping()
{
  Selector s;Spy tc,mc;
  for (Choice choice : {Choice::tcBld,Choice::mc402Boost})
  {
    tc.ready=mc.ready=false;
    if (s.beginBlock(choice,1,tc,mc)!=Choice::off) return false;
    tc.ready=mc.ready=true;
    if (s.beginBlock(choice,65,tc,mc)!=Choice::off) return false;
  }
  s.beginBlock(Choice::tcBld,1,tc,mc);
  const int resets = tc.resets;
  s.beginBlock(Choice::tcBld,65,tc,mc);
  s.beginBlock(Choice::tcBld,1,tc,mc);
  if (tc.resets != resets) return false;
  // A requested disengagement while unavailable is adopted on recovery,
  // exactly as in the existing TC-only wrapper.
  tc.ready = false;
  s.beginBlock(Choice::off,1,tc,mc);
  if (tc.resets != resets) return false;
  tc.ready = true;
  s.beginBlock(Choice::off,1,tc,mc);
  if (tc.resets != resets+1) return false;
  using integration::preNAMProcessorFromNormalized;
  return preNAMProcessorFromNormalized(0.)==Choice::off && preNAMProcessorFromNormalized(.5)==Choice::tcBld
    && preNAMProcessorFromNormalized(1.)==Choice::mc402Boost
    && preNAMProcessorFromNormalized(std::numeric_limits<double>::quiet_NaN())==Choice::off
    && s.beginBlock(static_cast<Choice>(999),1,tc,mc)==Choice::off;
}

bool actualBridgeAndTCRegression()
{
  using TC=dsp::TCBLDCleanBoostProcessor;
  for (double rate : {44100.,48000.,88200.,96000.,176400.,192000.})
  {
    TC tc,direct;dsp::MC402CleanBoostProcessor mc;Selector s;
    tc.prepare(rate,64);direct.prepare(rate,64);mc.prepare(rate,64);mc.setBoostDb(10.);
    // Compare selected TC against the previous exact scalar/TC/scalar sequence.
    std::array<double,64> source{},actual{},expected{};
    for (std::size_t i=0;i<source.size();++i) source[i]=.02*std::sin(.3*static_cast<double>(i));
    for (bool calibrated : {false,true})
    {
      const double trim=2., host=calibrated ? trim*TC::fullScalePeakVolts(12.) : trim;
      const double model=calibrated ? 1./TC::fullScalePeakVolts(6.) : 1.;
      s.reset();s.beginBlock(Choice::tcBld,64,tc,mc);direct.reset();
      for (std::size_t i=0;i<source.size();++i) actual[i]=expected[i]=source[i]*host;
      s.processSelected(actual,model,tc,mc);direct.processBlock(expected,expected);
      for (auto& x:expected) x*=model;
      if (!expectSamplesBitExact("pre-NAM TC legacy arithmetic",actual,expected)) return false;
      s.beginBlock(Choice::mc402Boost,64,tc,mc);
      for (std::size_t i=0;i<source.size();++i) actual[i]=source[i]*host;
      s.processSelected(actual,model,tc,mc);
      for (std::size_t i=0;i<source.size();++i) expected[i]=(source[i]*host*std::sqrt(10.))*model;
      if (!expectSamples("pre-NAM MC402 calibrated/fallback volts",actual,expected)) return false;
      s.beginBlock(Choice::off,64,tc,mc);
      actual=source;s.processSelected(actual,model,tc,mc);
      if (!expectSamplesBitExact("pre-NAM Off untouched",actual,source)) return false;
    }
  }
  return true;
}

bool routingRealtime()
{
  dsp::TCBLDCleanBoostProcessor tc;dsp::MC402CleanBoostProcessor mc;Selector s;
  tc.prepare(48000.,64);mc.prepare(48000.,64);std::array<double,64> x{};
  beginAllocationTracking();
  for (int i=0;i<100;++i)
  {
    s.beginBlock(static_cast<Choice>(i%3),64,tc,mc);
    s.processSelected(x,1.,tc,mc);
  }
  return endAllocationTracking()==0;
}
}
TestSuite developmentPreNAMSelectorTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"pre-NAM selector exclusive routing before gate/NAM",exclusivity},
    TestCase{"pre-NAM selector unavailable/invalid/oversize fallback",fallbackAndMapping},
    TestCase{"pre-NAM selector calibrated bridge and TC preservation",actualBridgeAndTCRegression},
    TestCase{"pre-NAM selector realtime allocations",routingRealtime}};
  return tests;
}
} // namespace holdsworth::test
