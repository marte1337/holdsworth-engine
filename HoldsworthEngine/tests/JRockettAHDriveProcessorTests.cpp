#include "../dsp/JRockettAHDriveProcessor.h"
#include "TestHarness.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>

namespace holdsworth::dsp
{
struct JRockettAHDriveTestAccess
{
  static auto current(const JRockettAHDriveProcessor& p) { return p.mCurrent; }
  static auto remaining(const JRockettAHDriveProcessor& p) { return p.mRemaining; }
  static auto controls(const JRockettAHDriveProcessor& p) { return p.mTarget.controls; }
};
}
namespace holdsworth::test
{
namespace
{
using P=dsp::JRockettAHDriveProcessor;
using C=dsp::JRockettAHDriveControls;
using A=dsp::JRockettAHDriveTestAccess;
constexpr std::array<double,6> rates{44100.,48000.,88200.,96000.,176400.,192000.};
constexpr double unityVolume=0.6309573444801932;
constexpr double pi=std::numbers::pi;

void render(P& p, std::span<const double> x, std::span<double> y, std::size_t block=128)
{
  for (std::size_t i=0;i<x.size();i+=block)
  { const auto n=std::min(block,x.size()-i); p.processBlock(x.subspan(i,n),y.subspan(i,n)); }
}
std::complex<double> dft(std::span<const double> x, double hz, double fs)
{
  const auto step=std::polar(1.,-2*pi*hz/fs);
  std::complex<double> phase{1.,0.},sum{};
  for (double v:x) { sum+=v*phase; phase*=step; }
  return sum;
}
std::complex<double> analyticShelf(double hz,double fs,double db,bool high)
{
  const double a=std::pow(10.,db/20.);
  const std::complex<double> s{0.,std::tan(pi*hz/fs)/std::tan(pi*(high?2500.:250.)/fs)};
  return high?a*(s+1./std::sqrt(a))/(s+std::sqrt(a)):(s+std::sqrt(a))/(s+1./std::sqrt(a));
}
bool volume()
{
  if (P::volumeAmplitude(0.)!=0. || !expectNear("AH Drive default Volume",P::volumeAmplitude(unityVolume),1.,1e-15)) return false;
  double prior=0.;
  for (int i=0;i<=1000;++i)
  {
    const double v=static_cast<double>(i)/1000.;
    const double a=P::volumeAmplitude(v);
    if (a<prior || !expectNear("AH Drive cubic taper",a,std::pow(10.,.6)*v*v*v,1e-14)) return false;
    prior=a;
  }
  for (double fs:rates)
  {
    std::vector<double>x(4096),baseline(4096),y(4096);
    for (std::size_t i=0;i<x.size();++i) x[i]=.2*std::sin(.037*static_cast<double>(i));
    P p;p.prepare(fs,128);render(p,x,baseline);
    for (double v:{0.,.1,.25,unityVolume,1.})
    {
      p.setControls({.5,.5,.5,v});p.reset();render(p,x,y);
      for (std::size_t i=0;i<y.size();++i)
        if (!expectNear("AH Drive post-only Volume",y[i],baseline[i]*P::volumeAmplitude(v),1e-14)) return false;
      if (v==0.) for (double value:y) if (value!=0. || std::signbit(value)) return false;
    }
  }
  return true;
}
bool toneResponses()
{
  std::vector<double>x(8192),y(8192);x[0]=1e-8;
  for (double fs:rates) for (double bass:{0.,.5,1.}) for (double treble:{0.,.5,1.})
  {
    P p;p.setControls({.5,bass,treble,unityVolume});p.prepare(fs,128);render(p,x,y);
    for (double hz:{20.,80.,250.,1000.,2500.,8000.,10000.})
    {
      const auto measured=dft(y,hz,fs)/1e-8;
      const auto expected=std::pow(10.,7.8/20.)*analyticShelf(hz,fs,-6.+12.*bass,false)*
        analyticShelf(hz,fs,-6.+12.*treble,true)*std::polar(1.,-2*pi*hz*32./fs);
      if (!expectNear("AH Drive analytic small-signal EQ dB",20.*std::log10(std::abs(measured/expected)),0.,.002)) return false;
      if (!expectNear("AH Drive EQ and FIR phase",std::arg(measured/expected),0.,1e-8)) return false;
    }
  }
  return true;
}
bool transferCleanup()
{
  for (double fs:rates)
  {
    std::array<double,128>x{},y{};
    for (double gain:{0.,.5,1.}) for (double amplitude:{.02,.05,.2,.5})
    {
      P p;p.setControls({gain,.5,.5,unityVolume});p.prepare(fs,128);x.fill(amplitude);
      for (int j=0;j<20;++j) p.processBlock(x,y);
      const double d=std::pow(10.,24.*gain/20.);
      const double expected=std::pow(d,-.35)*std::asinh(d*amplitude);
      if (!expectNear("AH Drive frozen settled transfer",y.back(),expected,2e-9)) return false;
    }
  }
  constexpr std::size_t n=8192;
  std::vector<double>x(3*n),y(3*n);
  double previous=0.;
  for (double gain:{0.,.5,1.})
  {
    double loudThird=0.;
    for (double amplitude:{.2,.05})
    {
      for (std::size_t i=0;i<x.size();++i) x[i]=amplitude*std::sin(2*pi*75.*static_cast<double>(i)/static_cast<double>(n));
      P p;p.setControls({gain,.5,.5,unityVolume});p.prepare(48000.,128);render(p,x,y);
      const auto settled=std::span{y}.last(n);
      const double third=std::abs(dft(settled,3.*75.*48000./static_cast<double>(n),48000.)/dft(settled,75.*48000./static_cast<double>(n),48000.));
      if (amplitude==.2) { if (third<=previous) return false; previous=third;loudThird=third; }
      else if (third>=loudThird*.3) return false;
    }
  }
  return true;
}
bool latency()
{
  for (double fs:rates)
  {
    P p;p.setControls({0.,.5,.5,unityVolume});p.prepare(fs,128);
    std::array<double,128>x{},y{};x[0]=1e-8;p.processBlock(x,y);
    const auto peak=std::max_element(y.begin(),y.end());
    if (p.latencySamples()!=32 || peak-y.begin()!=32) return false;
    // Linear-phase FIR pair has symmetric small-signal impulse about 32.
    for (std::size_t i=0;i<32;++i)
      if (!expectNear("AH Drive impulse symmetry",y[i],y[64-i],1e-20)) return false;
  }
  return true;
}
bool ramps()
{
  for (double fs:rates)
  {
    const auto n=static_cast<std::size_t>(std::ceil(.01*fs));
    P p;p.prepare(fs,128);std::array<double,128>warm{};warm.fill(.2);
    for (int i=0;i<100;++i) { std::array<double,128>y{};p.processBlock(warm,y); }
    std::array<double,1>x{.2},y{};p.processBlock(x,y);const double settled=y[0];
    p.setControls({.5,.5,.5,0.});
    for (std::size_t i=1;i<=n;++i)
    {
      if (i%17==0) p.setControls({.5,.5,.5,0.}); // duplicates must not restart
      p.processBlock(x,y);
      if (!expectNear("AH Drive 10ms Volume ramp",y[0],settled*(1.-static_cast<double>(i)/static_cast<double>(n)),2e-12)) return false;
    }
    if (y[0]!=0.) return false;
    p.setControls({1.,1.,0.,0.});
    for (std::size_t i=0;i<n;++i) { p.processBlock(x,y);if (y[0]!=0.) return false; }
    const auto values=A::current(p);
    if (!expectNear("AH Drive gain ramp target",values[0],std::pow(10.,1.2),1e-14)) return false;
    for (auto remaining:A::remaining(p)) if (remaining!=0) return false;
    p.setControls({1.,1.,0.,unityVolume});
    for (std::size_t i=0;i<n;++i) p.processBlock(x,y);
    if (!(y[0]>.1 && std::isfinite(y[0]))) return false;
  }
  return true;
}
bool partitions()
{
  for (double fs:rates)
  {
    constexpr std::size_t total=7000;
    std::vector<double>x(total),a(total),b(total),inplace;
    for (std::size_t i=0;i<total;++i) x[i]=.2*std::sin(2*pi*110.*static_cast<double>(i)/fs);
    auto perform=[&](std::span<const double> input,std::span<double> output,bool tiny) {
      P p;p.prepare(fs,128);
      std::size_t i=0,sequence=0;
      while (i<total)
      {
        if (i==1000) p.setControls({1.,0.,1.,.4});
        if (i==1123) p.setControls({.1,1.,0.,1.});
        if (i==1246) p.setControls({.8,.3,.7,unityVolume});
        if (i==3000) p.setControls({.8,.3,.7,0.});
        if (i==5000) p.setControls({.5,.5,.5,unityVolume});
        std::size_t end=total;
        for (std::size_t event:{1000U,1123U,1246U,3000U,5000U}) if (event>i) { end=event;break; }
        const std::size_t size=tiny?std::array<std::size_t,6>{1,2,4,8,32,127}[sequence++%6]:128;
        const auto count=std::min(size,end-i);
        p.processBlock(input.subspan(i,count),output.subspan(i,count));i+=count;
      }
    };
    perform(x,a,false);perform(x,b,true);inplace=x;perform(inplace,inplace,true);
    if (!expectSamplesBitExact("AH Drive sample-timed partitioning",a,b) ||
        !expectSamplesBitExact("AH Drive in-place",b,inplace)) return false;
    double jump=0.;
    for (std::size_t i=1;i<total;++i) jump=std::max(jump,std::abs(a[i]-a[i-1]));
    if (jump>.04) return false;
  }
  return true;
}
bool lifecycle()
{
  P p;p.setControls({.8,.2,.9,.4});
  std::array<double,128>x{},a{},b{};x.fill(.2);
  for (double fs:rates)
  {
    p.prepare(fs,128);P q;q.setControls({.8,.2,.9,.4});q.prepare(fs,128);
    p.processBlock(x,a);q.processBlock(x,b);
    if (!expectSamplesBitExact("AH Drive reprepare retained controls",a,b)) return false;
    p.processBlock(x,a);p.reset();p.processBlock(x,a);
    if (!expectSamplesBitExact("AH Drive reset clears histories",a,b)) return false;
    p.processBlock({},{});
  }
  for (double fs:{0.,8000.,768000.,std::numeric_limits<double>::quiet_NaN()})
  {
    bool caught=false;try { p.prepare(fs,128); } catch (const std::invalid_argument&) { caught=true; }
    if (!caught || !p.isPrepared()) return false;
  }
  try { p.prepare(48000.,0); } catch (const std::invalid_argument&) { return true; }
  return false;
}
bool finiteRecovery()
{
  const double nan=std::numeric_limits<double>::quiet_NaN(), inf=std::numeric_limits<double>::infinity();
  for (double fs:rates)
  {
    P p,q;p.prepare(fs,128);q.prepare(fs,128);
    std::array<double,128>x{},clean{},a{},b{};x.fill(.2);clean=x;x[11]=nan;x[37]=inf;x[99]=-inf;
    clean[11]=clean[37]=clean[99]=0.;p.processBlock(x,a);q.processBlock(clean,b);
    if (!expectSamplesBitExact("AH Drive nonfinite becomes zero excitation",a,b)) return false;
    p.setControls({nan,inf,-inf,nan});p.reset();q.reset();p.processBlock(clean,a);q.processBlock(clean,b);
    if (!expectSamplesBitExact("AH Drive invalid controls default",a,b)) return false;
    p.setControls({99.,-99.,99.,99.});p.reset();q.setControls({1.,0.,1.,1.});q.reset();
    p.processBlock(clean,a);q.processBlock(clean,b);
    if (!expectSamplesBitExact("AH Drive out-of-range controls clamp",a,b)) return false;
    for (int block=0;block<12;++block)
    {
      for (std::size_t i=0;i<x.size();++i) x[i]=(i%2?1.:-1.)*std::numeric_limits<double>::max();
      p.processBlock(x,a);for (double v:a) if (!std::isfinite(v)) return false;
    }
    x.fill(0.);
    for (int block=0;block<2000;++block) { p.processBlock(x,a);for (double v:a) if (!std::isfinite(v)) return false; }
    if (std::abs(a.back())>1e-12) return false;
    p.reset();p.processBlock(x,a);for (double v:a) if (v!=0.) return false;
  }
  return true;
}
bool realtime()
{
  for (double fs:rates) for (std::size_t n:{1U,2U,4U,8U,32U,64U,128U})
  {
    P p;p.prepare(fs,128);std::array<double,128>x{},y{};x.fill(.2);
    beginAllocationTracking();
    for (unsigned i=0;i<200;++i)
    {
      p.setControls(i%2?C{1.,0.,1.,1.}:C{0.,1.,0.,0.});
      p.processBlock(std::span{x}.first(n),std::span{y}.first(n));
      if (i%37==0) p.reset();
    }
    if (endAllocationTracking()!=0) return false;
  }
  return true;
}
bool concurrent()
{
  P p;p.prepare(48000.,128);std::atomic<bool> start{false};
  std::thread producer([&] {
    while (!start.load(std::memory_order_acquire)) {}
    for (unsigned i=0;i<20000;++i) p.setControls(i%2?C{1.,0.,1.,1.}:C{0.,1.,0.,0.});
    p.setControls({1.,0.,1.,1.});
  });
  bool valid=true;std::array<double,8>x{},y{};x.fill(.2);start.store(true,std::memory_order_release);
  for (unsigned i=0;i<20000;++i)
  {
    p.processBlock(x,y);const auto c=A::controls(p);
    valid &= (c.gain==1.&&c.bass==0.&&c.treble==1.&&c.volume==1.) ||
             (c.gain==0.&&c.bass==1.&&c.treble==0.&&c.volume==0.) ||
             (c.gain==.5&&c.bass==.5&&c.treble==.5&&c.volume==unityVolume);
    for (double v:y) valid &= std::isfinite(v);
  }
  producer.join();
  for (int i=0;i<100;++i) p.processBlock(x,y);
  valid &= A::controls(p).volume==1.;
  for (auto remain:A::remaining(p)) valid &= remain==0;
  return valid;
}
}
TestSuite jRockettAHDriveProcessorTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"AH Drive Volume mute default maximum and taper",volume},
    TestCase{"AH Drive analytic Bass Treble curves",toneResponses},
    TestCase{"AH Drive frozen transfer Gain and cleanup",transferCleanup},
    TestCase{"AH Drive measured 32 sample latency",latency},
    TestCase{"AH Drive control smoothing exact mute duplicate requests",ramps},
    TestCase{"AH Drive repeated controls partitions continuity in-place",partitions},
    TestCase{"AH Drive reset reprepare rate contract",lifecycle},
    TestCase{"AH Drive finite nonfinite recovery",finiteRecovery},
    TestCase{"AH Drive zero realtime allocations all rates callbacks",realtime},
    TestCase{"AH Drive concurrent coherent handoff",concurrent}};
  return tests;
}
} // namespace holdsworth::test
