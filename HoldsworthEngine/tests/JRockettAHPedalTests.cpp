#include "TestHarness.h"
#include "../dsp/JRockettAHPedal.h"
#include "../dsp/TCBLDCleanBoostProcessor.h"
#include "../dsp/MC402CleanBoostProcessor.h"
#include "../integration/DevelopmentAHOuterTransition.h"
#include "../integration/DevelopmentJRockettAHControls.h"
#include <algorithm>
#include <array>
#include <limits>
#include <thread>
#include <vector>

namespace holdsworth::test
{
namespace
{
using Pedal = dsp::JRockettAHPedal;
using Choice = integration::DevelopmentPreNAMProcessor;
constexpr auto rates = dsp::JRockettAHDriveProfile::supportedRates;
constexpr std::array<std::size_t,7> blocks{1,2,4,8,32,64,128};
template<class P> void render(P& p, std::span<const double> x, std::span<double> y, std::size_t block)
{
  for (std::size_t i=0;i<x.size();i+=block)
  { const auto n=std::min(block,x.size()-i); p.processBlock(x.subspan(i,n),y.subspan(i,n)); }
}
bool routes()
{
  std::vector<double> x(2048),actual(x.size()),expected(x.size()),temp(x.size());
  for(std::size_t i=0;i<x.size();++i) x[i]=.2*std::sin(.031*static_cast<double>(i));
  for(double rate:rates) for(unsigned flags=0;flags<4;++flags) for(unsigned mode=0;mode<6;++mode)
  {
    const dsp::JRockettAHBoostControls b{7.,static_cast<dsp::JRockettAHBoostType>(mode/2),static_cast<dsp::JRockettAHEmphasis>(mode%2)};
    const auto d=integration::ahDriveControlsFromDevelopmentUI(.7,.2,.8,.6);
    Pedal p; p.setSections(flags&1,flags&2);p.setBoostControls(b);p.setDriveControls(d);p.prepare(rate,128);
    dsp::JRockettAHBoostProcessor boost;boost.setControls(b);boost.prepare(rate,128);
    dsp::JRockettAHDriveProcessor drive;drive.setControls(d);drive.prepare(rate,128);
    temp=x;
    if(flags&1) render(boost,x,temp,128);
    if(flags&2) render(drive,temp,expected,128);
    else { std::fill(expected.begin(),expected.end(),0.);std::copy(temp.begin(),temp.end()-32,expected.begin()+32); }
    render(p,x,actual,32);
    if(!expectSamplesBitExact("AH full fixed routing",actual,expected)) return false;
    p.reset();actual=x;render(p,actual,actual,1);
    if(!expectSamplesBitExact("AH full in-place/reset",actual,expected)) return false;
  }
  return true;
}
bool latencyAndVolume()
{
  for(double rate:rates) for(unsigned flags=0;flags<4;++flags)
  {
    Pedal p;p.setSections(flags&1,flags&2);p.setBoostControls({0.,dsp::JRockettAHBoostType::clean,dsp::JRockettAHEmphasis::low});
    p.setDriveControls({0.,.5,.5,dsp::JRockettAHDriveProfile::defaultVolume});p.prepare(rate,128);
    // Clean mode has a shelf; use peak timing as natural FIR latency, not onset.
    std::array<double,128>x{},y{};x[0]=1e-7;p.processBlock(x,y);
    const auto peak=std::max_element(y.begin(),y.end(),[](double a,double b){return std::abs(a)<std::abs(b);});
    if(p.latencySamples()!=32 || peak-y.begin()!=32) return false;
    if(flags&2)
    {
      std::array<double,128> reference=y;
      for(double volume:{0.,dsp::JRockettAHDriveProfile::defaultVolume,1.})
      {
        p.setDriveControls({0.,.5,.5,volume});p.reset();p.processBlock(x,y);
        for(std::size_t i=0;i<y.size();++i)
          if(!expectNear("AH post Drive Volume",y[i],reference[i]*dsp::JRockettAHDriveProcessor::volumeAmplitude(volume),1e-20)) return false;
      }
    }
  }
  return true;
}
std::vector<double> switched(double rate,std::size_t block)
{
  Pedal p;p.prepare(rate,128);
  std::vector<double> x(8192),y(x.size());
  for(std::size_t i=0;i<x.size();++i)x[i]=.15*std::sin(.005*static_cast<double>(i));
  for(std::size_t at=0;at<x.size();at+=128)
  {
    // Changes faster than the fade, then allow the final request to settle.
    if(at<4096)p.setSections((at/128)%3==0,(at/128)%4<2);
    else p.setSections(false,true);
    render(p,std::span{x}.subspan(at,128),std::span{y}.subspan(at,128),block);
  }
  return y;
}
bool transitions()
{
  for(double rate:rates)
  {
    const auto ref=switched(rate,1);
    for(auto block:blocks) if(!expectSamplesBitExact("AH section partition",switched(rate,block),ref)) return false;
    for(std::size_t i=1;i<ref.size();++i)
      if(!std::isfinite(ref[i]) || std::abs(ref[i]-ref[i-1])>.02) return false;
    Pedal p;p.setSections(false,false);p.prepare(rate,128);
    std::array<double,128>x{},y{};x.fill(.1);
    p.processBlock(x,y);p.setSections(false,true);
    for(int i=0;i<50;++i)p.processBlock(x,y);
    dsp::JRockettAHDriveProcessor d;d.prepare(rate,128);
    std::array<double,128> expected{};for(int i=0;i<51;++i)d.processBlock(x,expected);
    if(!expectSamples("AH reenabled Drive settles",y,expected,1e-12))return false;
  }
  return true;
}
struct Counter
{
  int calls=0,resets=0; double factor=2.;
  bool isPrepared() const {return true;}
  std::size_t maximumBlockSize() const {return 128;}
  std::size_t latencySamples() const {return 32;}
  void reset(){++resets;}
  void processBlock(std::span<const double>x,std::span<double>y)
  {++calls;for(std::size_t i=0;i<x.size();++i)y[i]=x[i]*factor;}
};
bool outer()
{
  for(double rate:rates)
  {
    integration::DevelopmentAHOuterTransition selector;selector.prepare(rate);
    Counter tc,mc,ah;mc.factor=3.;ah.factor=4.;
    for(int i=0;i<32;++i)
    {selector.beginBlock(Choice::off,1,tc,mc,ah);double x=.1;selector.processSelected({&x,1},.5,tc,mc,ah);}
    for(auto choice:{Choice::off,Choice::tcBld,Choice::mc402Boost,Choice::jRockettAH,Choice::off,Choice::jRockettAH,Choice::tcBld})
    {
      double prior=.1;
      for(int n=0;n<5000;++n)
      {
        selector.beginBlock(choice,1,tc,mc,ah);
        // Calibration path: host->volts 2, volts->NAM .5, stock 1.
        double x=.1*selector.inputGain(1.,2.);
        const int before=tc.calls+mc.calls+ah.calls;
        selector.processSelected({&x,1},.5,tc,mc,ah);
        if(tc.calls+mc.calls+ah.calls-before>1)return false;
        if(n>0 && (choice==Choice::jRockettAH || choice==Choice::off) && std::abs(x-prior)>.003)return false;
        prior=x;
      }
      if(selector.applied()!=choice || selector.transitioning())return false;
      if(!expectNear("AH outer selected route",prior,.1*(choice==Choice::off?1.:choice==Choice::tcBld?2.:choice==Choice::mc402Boost?3.:4.)))return false;
    }
    // Repeated requests eventually settle to the latest target.
    for(int i=0;i<12000;++i)
    {
      const auto choice=i<5000?(i%7<3?Choice::jRockettAH:Choice::mc402Boost):Choice::off;
      selector.beginBlock(choice,1,tc,mc,ah);double x=.1;
      selector.processSelected({&x,1},1.,tc,mc,ah);
    }
    if(selector.transitioning() || selector.applied()!=Choice::off)return false;
  }
  return true;
}
bool recoveryAndRealtime()
{
  Pedal p;std::array<double,128>x{},y{};x.fill(.1);
  for(double rate:rates)
  {
    p.prepare(rate,128);
    for(auto block:blocks)
    {
      beginAllocationTracking();
      for(int n=0;n<300;++n)
      {p.setSections(n%2,n%3);p.processBlock(std::span{x}.first(block),std::span{y}.first(block));}
      if(endAllocationTracking()!=0)return false;
    }
    x[1]=std::numeric_limits<double>::infinity();x[3]=std::numeric_limits<double>::quiet_NaN();
    p.processBlock(x,y);x.fill(.1);
    for(int n=0;n<100;++n)p.processBlock(x,y);
    for(double v:y)if(!std::isfinite(v))return false;
  }
  std::atomic<bool> running{true};
  std::thread writer([&]{unsigned i=0;while(running.load())
    {p.setSections(i%2,i%3);p.setDriveControls({(i%101)/100.,.4,.6,.7});
     p.setBoostControls({(i%21)*1.,static_cast<dsp::JRockettAHBoostType>(i%3),static_cast<dsp::JRockettAHEmphasis>(i%2)});++i;}});
  for(int i=0;i<2000;++i){if(i%7==0)p.reset();p.processBlock(x,y);}
  running=false;writer.join();
  for(double v:y)if(!std::isfinite(v))return false;
  return true;
}
bool controls()
{
  const auto c=integration::ahDriveControlsFromDevelopmentUI(-1.,.25,2.,std::numeric_limits<double>::quiet_NaN());
  return c.gain==0. && c.bass==.25 && c.treble==1. && c.volume==dsp::JRockettAHDriveProfile::defaultVolume;
}
bool legacyAndCalibration()
{
  for(double rate:rates)
  {
    integration::DevelopmentPreNAMSelector original;
    integration::DevelopmentAHOuterTransition current;current.prepare(rate);
    dsp::TCBLDCleanBoostProcessor tcA,tcB;tcA.prepare(rate,128);tcB.prepare(rate,128);
    dsp::MC402CleanBoostProcessor mcA,mcB;mcA.prepare(rate,128);mcB.prepare(rate,128);
    Pedal ahA,ahB;ahA.prepare(rate,128);ahB.prepare(rate,128);
    for(auto choice:{Choice::off,Choice::tcBld,Choice::mc402Boost,Choice::off})
    {
      original.reset(); current.reset();
      original.beginBlock(choice,128,tcA,mcA,ahA);
      // Finish the wrapper's fade on silence before comparing settled DSP.
      std::array<double,128> silence{};
      for(int j=0;j<40;++j)
      {current.beginBlock(choice,128,tcB,mcB,ahB);current.processSelected(silence,.35,tcB,mcB,ahB);}
      std::array<double,128>a{},b{},expected{};
      for(std::size_t i=0;i<a.size();++i)
      {const double x=.13*std::sin(.04*static_cast<double>(i));a[i]=x*original.inputGain(.7,2.);b[i]=x*current.inputGain(.7,2.);}
      original.processSelected(a,.35,tcA,mcA,ahA);current.processSelected(b,.35,tcB,mcB,ahB);
      std::copy(a.begin(),a.end()-32,expected.begin()+32);
      if(!expectSamplesBitExact("fixed domain preserves legacy after 32 samples",expected,b))return false;
    }
    for(bool metadata:{false,true})for(bool enabled:{false,true})for(unsigned flags=0;flags<4;++flags)
    {
      const double trim=std::pow(10.,-3./20.);
      const double hostToVolts=trim*(metadata&&enabled?dsp::TCBLDCleanBoostProcessor::fullScalePeakVolts(12.):1.);
      const double voltsToNam=metadata&&enabled?1./dsp::TCBLDCleanBoostProcessor::fullScalePeakVolts(8.):1.;
      Pedal selected,reference;selected.setSections(flags&1,flags&2);reference.setSections(flags&1,flags&2);
      selected.prepare(rate,128);reference.prepare(rate,128);
      integration::DevelopmentPreNAMSelector selector;selector.beginBlock(Choice::jRockettAH,128,tcA,mcA,selected);
      std::array<double,128>actual{},expected{};
      for(std::size_t i=0;i<actual.size();++i)actual[i]=expected[i]=.1*std::sin(.04*static_cast<double>(i))*hostToVolts;
      selector.processSelected(actual,voltsToNam,tcA,mcA,selected);reference.processBlock(expected,expected);
      for(std::size_t i=0;i<actual.size();++i)
      {
        expected[i]*=voltsToNam;
        // Gate detector sees this same calibrated, delayed pre-NAM signal.
        if(actual[i]!=expected[i] || std::abs(actual[i])!=std::abs(expected[i]))return false;
        actual[i]=std::tanh(3.*actual[i]);expected[i]=std::tanh(3.*expected[i]);
      }
      if(!expectSamplesBitExact("AH calibration before nonlinear NAM double",actual,expected))return false;
    }
  }
  return true;
}

bool fixedDomainAlignment()
{
  // Actual processors, all product rates/partitions, calibrated and fallback
  // paths. Compare exact sequences, not just an EQ-dependent impulse peak.
  for(double rate:rates)for(auto block:blocks)for(bool calibrated:{false,true})
    for(unsigned route=0;route<7;++route)
    {
      const auto choice=static_cast<Choice>(std::min(route,3U));
      dsp::TCBLDCleanBoostProcessor tc,refTC;tc.prepare(rate,128);refTC.prepare(rate,128);
      dsp::MC402CleanBoostProcessor mc,refMC;mc.setBoostDb(7.);refMC.setBoostDb(7.);
      mc.prepare(rate,128);refMC.prepare(rate,128);
      Pedal ah,refAH;const unsigned flags=route<3?0:route-3;
      ah.setSections(flags&1,flags&2);refAH.setSections(flags&1,flags&2);
      ah.prepare(rate,128);refAH.prepare(rate,128);
      integration::DevelopmentAHOuterTransition selector;selector.prepare(rate);
      integration::DevelopmentPreNAMSelector reference;
      reference.beginBlock(choice,block,refTC,refMC,refAH);
      const double host=calibrated?2.3:1.,post=calibrated?.37:1.,stock=host*post;
      std::array<double,128> silence{};
      for(int j=0;j<40;++j)
      {selector.beginBlock(choice,128,tc,mc,ah);selector.processSelected(silence,post,tc,mc,ah);}
      if(selector.transitioning() || selector.applied()!=choice || selector.latencySamples()!=32)return false;
      std::array<double,512> actual{},direct{},expected{};
      for(std::size_t i=0;i<actual.size();++i)
      {
        const double x=i==64?.00001:0.;
        actual[i]=x*selector.inputGain(stock,host);direct[i]=x*reference.inputGain(stock,host);
      }
      for(std::size_t i=0;i<actual.size();i+=block)
      {
        selector.beginBlock(choice,block,tc,mc,ah);
        selector.processSelected(std::span{actual}.subspan(i,block),post,tc,mc,ah);
        reference.processSelected(std::span{direct}.subspan(i,block),post,refTC,refMC,refAH);
      }
      if(choice==Choice::jRockettAH)expected=direct;
      else std::copy(direct.begin(),direct.end()-32,expected.begin()+32);
      if(!expectSamplesBitExact("fixed32 actual route/calibration, no second AH delay",actual,expected))return false;
      if(choice==Choice::off || choice==Choice::mc402Boost || (route==3))
      {
        const auto peak=std::max_element(actual.begin(),actual.end(),[](double a,double b){return std::abs(a)<std::abs(b);});
        if(peak-actual.begin()!=96)return false; // input at 64 + exactly 32
      }
    }
  return true;
}
}
TestSuite jRockettAHPedalTests() noexcept
{
  static const std::array tests{TestCase{"AH full four routes and six Boost modes",routes},
    TestCase{"AH full latency and post Volume",latencyAndVolume}, TestCase{"AH full section transitions",transitions},
    TestCase{"AH full outer exclusivity and coalescing",outer},TestCase{"AH full recovery realtime concurrency",recoveryAndRealtime},
    TestCase{"AH full control mapping",controls},TestCase{"AH full legacy and calibration paths",legacyAndCalibration},
    TestCase{"AH fixed32 domain exact real routes and calibration",fixedDomainAlignment}};
  return tests;
}
}
