#include "../dsp/LegatoCompressorProcessor.h"
#include "LegatoCompressorFixtures.h"
#include "TestHarness.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <limits>
#include <thread>

namespace holdsworth::dsp
{
struct LegatoCompressorTestAccess
{
  static double tick(LegatoCompressorProcessor& p, double level)
  {
    p.mPower = std::pow(10.,level/10.); // Normalized reference; bypass RMS acquisition.
    p.controlTick();
    return p.mReductionDb;
  }
  static double interval(const LegatoCompressorProcessor& p)
  { return static_cast<double>(p.mControlSamples)/p.mSampleRate; }
};
}
namespace holdsworth::test
{
namespace
{
using P = dsp::LegatoCompressorProcessor;
using Access = dsp::LegatoCompressorTestAccess;
std::vector<double> render(P& p, const std::vector<double>& x, bool calibrated = true)
{
  std::vector<double> y(x.size());
  for(std::size_t pos=0;pos<x.size();)
  {
    const auto n=std::min(p.maximumBlockSize(),x.size()-pos);
    p.processBlock(std::span<const double>{x}.subspan(pos,n),std::span{y}.subspan(pos,n),{calibrated,1.});pos+=n;
  }
  return y;
}
bool curve()
{
  if (P::reductionForLevel(-21.,-18.) != 0. || P::reductionForLevel(-15.,-18.) != 1.5
      || P::reductionForLevel(-6.,-18.) != 6.) return false;
  for (double boundary : {-21.,-15.,-6.})
    if (std::abs(P::reductionForLevel(boundary+1e-8,-18.)-P::reductionForLevel(boundary-1e-8,-18.)) > 2e-8) return false;
  for (double level=-180.;level<180.;level+=.1)
  {
    const double r=P::reductionForLevel(level,-18.);
    if (r<0. || r>6.) return false;
  }
  return expectNear("Legato static knee center",P::reductionForLevel(-18.,-18.),.375)
      && expectNear("Legato static 2:1 slope",P::reductionForLevel(-10.,-18.)-P::reductionForLevel(-12.,-18.),1.);
}
bool timing()
{
  for (double rate : legato::rates)
    for (double attack : {8.,15.,30.}) for (double release : {40.,60.,180.})
    {
      P p; dsp::LegatoCompressorControls c; c.attackMs=attack; c.releaseMs=release;
      p.setControls(c);p.prepare(rate,64);p.beginBlock({false,1.});
      const double interval=Access::interval(p);
      double r=0.,previous=0.;std::size_t ticks=0;
      while (r<3.*(1.-std::exp(-1.))) { previous=r;r=Access::tick(p,-24.);++ticks; }
      if (std::abs((static_cast<double>(ticks-1)+(3.*(1.-std::exp(-1.))-previous)/(r-previous))*interval-attack*.001)>attack*.001*.05) return false;
      for(int i=0;i<3000;++i) Access::tick(p,-24.);
      const double start=p.diagnostics().reductionDb;r=start;ticks=0;
      while (r>start/std::exp(1.)) { previous=r;r=Access::tick(p,-60.);++ticks; }
      if (std::abs((static_cast<double>(ticks-1)+(previous-start/std::exp(1.))/(previous-r))*interval-release*.001)>release*.001*.05) return false;
    }
  return true;
}
bool powerTiming()
{
  for (double rate : legato::rates)
  {
    P p;p.prepare(rate,4096);p.beginBlock({});
    const auto n=legato::frames(.004,rate);
    for(std::size_t i=0;i<n;++i) (void)p.processSample(1.);
    if (!expectNear("Legato isolated RMS power constant",p.diagnostics().power,1.-std::exp(-static_cast<double>(n)/(rate*.004)),1.e-12)) return false;
  }
  return true;
}
bool benefit()
{
  for(double rate : legato::rates)
  {
    auto f=legato::fixtures(rate).front();P p;dsp::LegatoCompressorControls c;c.enabled=true;
    p.setControls(c);p.prepare(rate,f.input.size());p.beginBlock({true,1.});
    for(std::size_t i=0;i<legato::frames(.050,rate);++i)(void)p.processSample(0.);
    const auto y=render(p,f.input);
    const auto& weak=f.notes[1];
    const auto a=legato::frames(.100,rate),b=legato::frames(.280,rate);
    const auto u=weak.start+legato::frames(.040,rate),v=weak.start+legato::frames(.140,rate);
    const double pickGain=legato::rmsDb(y,a,b)-legato::rmsDb(f.input,a,b);
    const double weakGain=legato::rmsDb(y,u,v)-legato::rmsDb(f.input,u,v);
    if (weakGain<=0. || weakGain-pickGain<1.5) return false;
    // Plain +3 dB does not change within-phrase contrast.
    auto boost=f.input;for(auto& x:boost)x*=legato::amplitude(3.);
    if (!expectNear("Legato scalar boost unchanged contrast",
      (legato::rmsDb(boost,a,b)-legato::rmsDb(boost,u,v))-
      (legato::rmsDb(f.input,a,b)-legato::rmsDb(f.input,u,v)),0.,1e-11)) return false;
  }
  return true;
}
bool rapidAndTransient()
{
  for(double rate : legato::rates) for(const auto& f:legato::fixtures(rate))
  {
    P p;dsp::LegatoCompressorControls c;c.enabled=true;p.setControls(c);p.prepare(rate,f.input.size());
    p.beginBlock({true,1.});for(std::size_t i=0;i<legato::frames(.050,rate);++i)(void)p.processSample(0.);
    const auto y=render(p,f.input);
    if(f.name.rfind("rapid-",0)==0)
      for(const auto& note:f.notes) if(!note.picked)
        if(legato::rmsDb(y,note.start,note.end)-legato::rmsDb(f.input,note.start,note.end)<-.5) return false;
    if(f.name=="pick-weak-150ms")
    {
      double rawPeak=0.,outPeak=0.;
      for(std::size_t i=0;i<legato::frames(.003,rate);++i)
      {rawPeak=std::max(rawPeak,std::abs(f.input[i])*legato::amplitude(3.));outPeak=std::max(outPeak,std::abs(y[i]));}
      if(20.*std::log10(outPeak/rawPeak)<=-1.) return false;
    }
  }
  return true;
}
bool identityAndWarmBypass()
{
  for(double rate : legato::rates)
  {
    P p;p.prepare(rate,64);
    std::array<double,9>x{0.,-0.,1.,-1.,1e100,-1e100,3.,std::numeric_limits<double>::max(),std::numeric_limits<double>::denorm_min()},y{};
    p.processBlock(x,y,{true,1.});if(!expectSamplesBitExact("Legato bypass finite identity",x,y))return false;
    p.reset();p.beginBlock({true,1.});
    for(std::size_t i=0;i<legato::frames(.300,rate);++i)(void)p.processSample(.4);
    if(p.diagnostics().reductionDb<5. || p.diagnostics().appliedGain!=1.)return false;
    auto c=dsp::LegatoCompressorControls{};c.enabled=true;p.setControls(c);p.beginBlock({true,1.});
    const double first=p.processSample(.4);if(std::abs(first-.4)>.001)return false;
    c.enabled=false;p.setControls(c);p.beginBlock({true,1.});
    for(std::size_t i=0;i<legato::frames(.010,rate)+1;++i)(void)p.processSample(.4);
    p.processBlock(x,y,{true,1.});if(!expectSamplesBitExact("Legato settled disable exact identity",x,y))return false;
    if(p.latencySamples()!=0)return false;
  }
  return true;
}
bool partitionsResetInPlace()
{
  for(double rate:legato::rates)
  {
    auto f=legato::fixtures(rate).front();P whole,split,inplace;dsp::LegatoCompressorControls c;c.enabled=true;
    for(auto* p:{&whole,&split,&inplace}){p->setControls(c);p->prepare(rate,f.input.size());}
    const auto a=render(whole,f.input);auto b=f.input,ip=f.input;
    constexpr std::array<std::size_t,6>sizes{1,7,37,64,127,511};std::size_t pos=0,k=0;
    while(pos<b.size()){auto n=std::min(sizes[k++%sizes.size()],b.size()-pos);
      split.processBlock(std::span<const double>{f.input}.subspan(pos,n),std::span{b}.subspan(pos,n),{true,1.});pos+=n;}
    inplace.processBlock(ip,ip,{true,1.});
    if(!expectSamplesBitExact("Legato irregular partitions",a,b)||!expectSamplesBitExact("Legato in-place",a,ip))return false;
    whole.reset();const auto fresh=render(whole,f.input);if(!expectSamplesBitExact("Legato deterministic reset",a,fresh))return false;
  }
  return true;
}
bool boundsNoiseRecovery()
{
  for(double rate:legato::rates)
  {
    P p;dsp::LegatoCompressorControls c;c.enabled=true;c.makeupDb=4.;p.setControls(c);p.prepare(rate,64);p.beginBlock({true,1.});
    for(std::size_t i=0;i<legato::frames(.500,rate);++i)
    {
      (void)p.processSample(i<legato::frames(.200,rate) ? 20. : 1e-6);
      auto d=p.diagnostics();if(d.appliedGain>legato::amplitude(4.)||d.reductionDb>6.||d.reductionDb<0.)return false;
    }
    for(std::size_t i=0;i<legato::frames(2.,rate);++i)(void)p.processSample(0.);
    if(p.diagnostics().reductionDb!=0.||p.diagnostics().power!=0.)return false;
    if(!expectNear("Legato recovery leaves manual makeup",p.diagnostics().appliedGain,legato::amplitude(4.),1e-14))return false;
    const auto all=legato::fixtures(rate);const auto& f=all[all.size()-2];c.makeupDb=3.;p.setControls(c);p.reset();const auto y=render(p,f.input);
    auto a=legato::frames(.200,rate);if(!expectNear("Legato settled noise gain",legato::rmsDb(y,a,y.size())-legato::rmsDb(f.input,a,y.size()),3.,1e-10))return false;
  }
  return true;
}
bool finiteControlsAndRealtime()
{
  P p;dsp::LegatoCompressorControls c;c.enabled=true;c.attackMs=-1.;c.releaseMs=1e100;c.makeupDb=1e100;
  c.calibratedThresholdDb=std::numeric_limits<double>::quiet_NaN();p.setControls(c);p.prepare(48000.,64);
  std::array<double,64>x{};x[0]=std::numeric_limits<double>::max();x[1]=std::numeric_limits<double>::infinity();
  x[2]=std::numeric_limits<double>::quiet_NaN();x[3]=std::numeric_limits<double>::denorm_min();
  beginAllocationTracking();
  for(int i=0;i<1000;++i){p.processBlock(x,x,{i%2==0,1.});if(i%11==0)p.reset();}
  const auto count=endAllocationTracking();for(double value:x)if(!std::isfinite(value))return false;
  return count==0;
}
bool concurrentHandoff()
{
  P p;p.prepare(48000.,64);std::atomic<bool> start{false};
  std::thread producer([&]{while(!start.load(std::memory_order_acquire)){}
    for(int i=0;i<20000;++i){dsp::LegatoCompressorControls c;c.enabled=i%2==0;c.makeupDb=i%11-6.;c.attackMs=8.+i%23;p.setControls(c);}});
  start.store(true,std::memory_order_release);std::array<double,64>x{},y{};x.fill(.1);bool valid=true;
  for(int i=0;i<20000;++i){p.processBlock(x,y,{true,1.});if(i%47==0)p.reset();for(double v:y)valid &= std::isfinite(v)&&std::abs(v)<=.1*legato::amplitude(4.);}
  producer.join();return valid;
}
bool lifecycle()
{
  P p;for(double rate:{0.,-1.,std::numeric_limits<double>::infinity()})
    {try{p.prepare(rate,64);return false;}catch(const std::invalid_argument&){}}
  try{p.prepare(48000.,0);return false;}catch(const std::invalid_argument&){}
  return true;
}
}
TestSuite legatoCompressorProcessorTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Legato static curve and knee",curve},TestCase{"Legato separate attack/release constants",timing},
    TestCase{"Legato separate RMS constant",powerTiming},TestCase{"Legato first weak body and scalar control",benefit},
    TestCase{"Legato every rapid weak note and transient",rapidAndTransient},TestCase{"Legato exact warm bypass and latency",identityAndWarmBypass},
    TestCase{"Legato partitions reset and in-place",partitionsResetInPlace},TestCase{"Legato bounds noise and recovery",boundsNoiseRecovery},
    TestCase{"Legato finite controls and realtime allocations",finiteControlsAndRealtime},TestCase{"Legato concurrent handoff",concurrentHandoff},
    TestCase{"Legato lifecycle",lifecycle}};
  return tests;
}
}
