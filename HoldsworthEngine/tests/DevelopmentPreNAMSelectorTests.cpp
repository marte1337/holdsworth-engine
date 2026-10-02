#include "../integration/DevelopmentPreNAMSelector.h"
#include "../dsp/TCBLDCleanBoostProcessor.h"
#include "../dsp/MC402CleanBoostProcessor.h"
#include "../integration/DevelopmentJRockettAHControls.h"
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
  Selector s; Spy tc,mc,ah; tc.gain=2.;mc.gain=10.;
  for (Choice choice : {Choice::off,Choice::tcBld,Choice::mc402Boost,Choice::off})
  {
    const int t=tc.calls,m=mc.calls;
    s.beginBlock(choice,1,tc,mc,ah);
    std::array<double,1> x{.25*s.inputGain(3.,4.)};
    s.processSelected(x,.5,tc,mc,ah);
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
  Selector s;Spy tc,mc,ah;
  for (Choice choice : {Choice::tcBld,Choice::mc402Boost})
  {
    tc.ready=mc.ready=false;
    if (s.beginBlock(choice,1,tc,mc,ah)!=Choice::off) return false;
    tc.ready=mc.ready=true;
    if (s.beginBlock(choice,65,tc,mc,ah)!=Choice::off) return false;
  }
  s.beginBlock(Choice::tcBld,1,tc,mc,ah);
  const int resets = tc.resets;
  s.beginBlock(Choice::tcBld,65,tc,mc,ah);
  s.beginBlock(Choice::tcBld,1,tc,mc,ah);
  if (tc.resets != resets) return false;
  // A requested disengagement while unavailable is adopted on recovery,
  // exactly as in the existing TC-only wrapper.
  tc.ready = false;
  s.beginBlock(Choice::off,1,tc,mc,ah);
  if (tc.resets != resets) return false;
  tc.ready = true;
  s.beginBlock(Choice::off,1,tc,mc,ah);
  if (tc.resets != resets+1) return false;
  using integration::preNAMProcessorFromNormalized;
  return preNAMProcessorFromNormalized(0.)==Choice::off && preNAMProcessorFromNormalized(1./3.)==Choice::tcBld
    && preNAMProcessorFromNormalized(2./3.)==Choice::mc402Boost
    && preNAMProcessorFromNormalized(std::numeric_limits<double>::quiet_NaN())==Choice::off
    && s.beginBlock(static_cast<Choice>(999),1,tc,mc,ah)==Choice::off;
}

bool actualBridgeAndTCRegression()
{
  using TC=dsp::TCBLDCleanBoostProcessor;
  for (double rate : {44100.,48000.,88200.,96000.,176400.,192000.})
  {
    TC tc,direct;dsp::MC402CleanBoostProcessor mc;dsp::JRockettAHBoostProcessor ah;Selector s;
    tc.prepare(rate,64);direct.prepare(rate,64);mc.prepare(rate,64);ah.prepare(rate,64);mc.setBoostDb(10.);
    // Compare selected TC against the previous exact scalar/TC/scalar sequence.
    std::array<double,64> source{},actual{},expected{};
    for (std::size_t i=0;i<source.size();++i) source[i]=.02*std::sin(.3*static_cast<double>(i));
    for (bool calibrated : {false,true})
    {
      const double trim=2., host=calibrated ? trim*TC::fullScalePeakVolts(12.) : trim;
      const double model=calibrated ? 1./TC::fullScalePeakVolts(6.) : 1.;
      s.reset();s.beginBlock(Choice::tcBld,64,tc,mc,ah);direct.reset();
      for (std::size_t i=0;i<source.size();++i) actual[i]=expected[i]=source[i]*host;
      s.processSelected(actual,model,tc,mc,ah);direct.processBlock(expected,expected);
      for (auto& x:expected) x*=model;
      if (!expectSamplesBitExact("pre-NAM TC legacy arithmetic",actual,expected)) return false;
      s.beginBlock(Choice::mc402Boost,64,tc,mc,ah);
      for (std::size_t i=0;i<source.size();++i) actual[i]=source[i]*host;
      s.processSelected(actual,model,tc,mc,ah);
      for (std::size_t i=0;i<source.size();++i) expected[i]=(source[i]*host*std::sqrt(10.))*model;
      if (!expectSamples("pre-NAM MC402 calibrated/fallback volts",actual,expected)) return false;
      s.beginBlock(Choice::off,64,tc,mc,ah);
      actual=source;s.processSelected(actual,model,tc,mc,ah);
      if (!expectSamplesBitExact("pre-NAM Off untouched",actual,source)) return false;
    }
  }
  return true;
}

bool routingRealtime()
{
  dsp::TCBLDCleanBoostProcessor tc;dsp::MC402CleanBoostProcessor mc;dsp::JRockettAHBoostProcessor ah;Selector s;
  tc.prepare(48000.,64);mc.prepare(48000.,64);ah.prepare(48000.,64);std::array<double,64> x{};
  beginAllocationTracking();
  for (int i=0;i<100;++i)
  {
    s.beginBlock(static_cast<Choice>(i%3),64,tc,mc,ah);
    s.processSelected(x,1.,tc,mc,ah);
  }
  return endAllocationTracking()==0;
}
bool fourChoices()
{
  static_assert(static_cast<unsigned>(Choice::off)==0 && static_cast<unsigned>(Choice::tcBld)==1
             && static_cast<unsigned>(Choice::mc402Boost)==2 && static_cast<unsigned>(Choice::jRockettAH)==3);
  Selector s; Spy tc,mc,ah; tc.gain=2.;mc.gain=3.;ah.gain=5.;
  for(unsigned i=0;i<4;++i)
  {
    const auto choice=static_cast<Choice>(i);
    if(integration::preNAMProcessorFromNormalized(static_cast<double>(i)/3.)!=choice)return false;
    const int t=tc.calls,m=mc.calls,a=ah.calls;
    if(s.beginBlock(choice,1,tc,mc,ah)!=choice)return false;
    std::array<double,1>x{.25*s.inputGain(3.,4.)};s.processSelected(x,.5,tc,mc,ah);
    const double expected=i==0?.75:i==1?1.:i==2?1.5:2.5;
    if(x[0]!=expected || tc.calls-t!=(i==1) || mc.calls-m!=(i==2) || ah.calls-a!=(i==3))return false;
    const double gateInput=x[0],namInput=x[0],namOutput=namInput*namInput*namInput;
    if(gateInput!=expected || namOutput!=expected*expected*expected)return false;
  }
  using integration::preNAMProcessorFromNormalized;
  if(preNAMProcessorFromNormalized(1./6.)!=Choice::tcBld || preNAMProcessorFromNormalized(.5)!=Choice::mc402Boost
     || preNAMProcessorFromNormalized(5./6.)!=Choice::jRockettAH)return false;
  for(double v:{-1.,1.01,std::numeric_limits<double>::infinity()})
    if(preNAMProcessorFromNormalized(v)!=Choice::off)return false;
  ah.ready=false;if(s.beginBlock(Choice::jRockettAH,1,tc,mc,ah)!=Choice::off)return false;
  ah.ready=true;if(s.beginBlock(Choice::jRockettAH,65,tc,mc,ah)!=Choice::off)return false;
  return s.beginBlock(Choice::jRockettAH,1,tc,mc,ah,false)==Choice::off;
}

bool ahControlsAndMessages()
{
  using integration::ahControlsFromDevelopmentUI;
  using integration::ahReadControlMessage;
  for(unsigned type=0;type<3;++type)for(unsigned emphasis=0;emphasis<2;++emphasis)
    for(double boost:{0.,.5,1.})
    {
      const auto controls=ahControlsFromDevelopmentUI(boost,static_cast<double>(type)/2.,emphasis);
      if(controls.boostDb!=boost*20. || static_cast<unsigned>(controls.type)!=type
         || static_cast<unsigned>(controls.emphasis)!=emphasis)return false;
      double decoded=-1.;
      if(!ahReadControlMessage(10,10,sizeof(double),&boost,decoded) || decoded!=boost)return false;
      if(ahReadControlMessage(11,10,sizeof(double),&boost,decoded)
         || ahReadControlMessage(10,10,1,&boost,decoded)
         || ahReadControlMessage(10,10,sizeof(double),nullptr,decoded))return false;
    }
  const auto invalid=ahControlsFromDevelopmentUI(std::numeric_limits<double>::quiet_NaN(),-1.,2.);
  if(invalid.boostDb!=0. || invalid.type!=dsp::JRockettAHBoostType::clean || invalid.emphasis!=dsp::JRockettAHEmphasis::low)return false;
  for(double rate:integration::kDevelopmentAHRealtimeRates)if(!integration::ahRealtimeRateSupported(rate))return false;
  for(double rate:{8000.,22050.,384000.,768000.,48001.})if(integration::ahRealtimeRateSupported(rate))return false;
  // The BoostSlider's double-click publishes normalized zero; other controls persist.
  return ahControlsFromDevelopmentUI(0.,1.,1.).boostDb==0.;
}

bool ahActualCalibrationAndReselection()
{
  using TC=dsp::TCBLDCleanBoostProcessor;
  using AH=dsp::JRockettAHBoostProcessor;
  for(double rate:integration::kDevelopmentAHRealtimeRates)
  {
    Spy tc,mc; AH ah,direct;Selector s;ah.prepare(rate,64);direct.prepare(rate,64);
    for(unsigned type=0;type<3;++type)for(unsigned emphasis=0;emphasis<2;++emphasis)
      for(bool calibration:{false,true})for(bool modelPresent:{false,true})for(bool metadata:{false,true})
      {
        const auto controls=integration::ahControlsFromDevelopmentUI(.5,static_cast<double>(type)/2.,emphasis);
        ah.setControls(controls);direct.setControls(controls);
        const bool enabled=calibration && modelPresent && metadata;
        const double trim=2.,host=enabled?trim*TC::fullScalePeakVolts(12.):trim;
        const double post=enabled?1./TC::fullScalePeakVolts(6.):1.;
        s.beginBlock(Choice::off,64,tc,mc,ah);s.beginBlock(Choice::jRockettAH,64,tc,mc,ah);direct.reset();
        std::array<double,64>x{},expected{};
        for(std::size_t i=0;i<x.size();++i)x[i]=expected[i]=.1*std::sin(.127*static_cast<double>(i))*host;
        s.processSelected(x,post,tc,mc,ah);direct.processBlock(expected,expected);for(auto& v:expected)v*=post;
        if(!expectSamplesBitExact("AH actual controls bridge and reselection",x,expected))return false;
        // No implicit reset on a continued selection. Switching controls uses M1 fades.
        ah.setControls(integration::ahControlsFromDevelopmentUI(.5,1.,1.));
        direct.setControls(integration::ahControlsFromDevelopmentUI(.5,1.,1.));
        s.beginBlock(Choice::jRockettAH,64,tc,mc,ah);
        x.fill(.25);expected=x;s.processSelected(x,post,tc,mc,ah);direct.processBlock(expected,expected);for(auto&v:expected)v*=post;
        if(!expectSamplesBitExact("AH continued selection keeps crossfade states",x,expected))return false;
        if(tc.calls!=0 || mc.calls!=0)return false;
      }
  }
  return true;
}

bool ahSmallBlockAllocations()
{
  for(double rate:integration::kDevelopmentAHRealtimeRates)
  {
    Spy tc,mc;dsp::JRockettAHBoostProcessor ah;Selector s;ah.prepare(rate,64);
    std::array<double,64>x{};x.fill(.1);
    beginAllocationTracking();
    for(unsigned i=0;i<10000;++i)
    {
      const std::size_t n=std::array<std::size_t,4>{1,7,16,64}[i%4];
      ah.setControls(integration::ahControlsFromDevelopmentUI(.5,static_cast<double>(i%3)/2.,i%2));
      s.beginBlock(i%1000==0?Choice::off:Choice::jRockettAH,n,tc,mc,ah);
      s.processSelected(std::span{x}.first(n),1.,tc,mc,ah);x.fill(.1);
    }
    if(endAllocationTracking()!=0)return false;
  }
  return true;
}

}
TestSuite developmentPreNAMSelectorTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"pre-NAM selector exclusive routing before gate/NAM",exclusivity},
    TestCase{"pre-NAM selector unavailable/invalid/oversize fallback",fallbackAndMapping},
    TestCase{"pre-NAM selector calibrated bridge and TC preservation",actualBridgeAndTCRegression},
    TestCase{"pre-NAM selector realtime allocations",routingRealtime},
    TestCase{"AH integration four-choice mapping and exclusive placement",fourChoices},
    TestCase{"AH integration control messages combinations and product rates",ahControlsAndMessages},
    TestCase{"AH integration actual calibration paths and reselection",ahActualCalibrationAndReselection},
    TestCase{"AH integration small-callback realtime allocations",ahSmallBlockAllocations}};
  return tests;
}
} // namespace holdsworth::test
