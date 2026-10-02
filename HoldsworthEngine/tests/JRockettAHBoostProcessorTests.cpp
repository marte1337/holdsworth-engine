#include "../dsp/JRockettAHBoostProcessor.h"
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
struct JRockettAHBoostTestAccess
{
  static std::size_t mode(const JRockettAHBoostProcessor& p) { return p.mCurrentMode; }
  static bool fading(const JRockettAHBoostProcessor& p) { return p.mFading; }
  static auto dormantState(const JRockettAHBoostProcessor& p) { return p.mIncoming.state; }
  static double replayError(JRockettAHBoostProcessor& p)
  {
    double worst=0.;
    for(std::size_t mode=0;mode<6;++mode)
    {
      JRockettAHBoostProcessor::Response serial{p.mCoefficients[mode],{}};
      auto index=(p.mHistoryWrite+p.mHistoryLength-p.mHistoryCount)%p.mHistoryLength;
      for(std::size_t i=0;i<p.mHistoryCount;++i)
      { serial.tick(p.mHistory[index]); if(++index==p.mHistoryLength)index=0; }
      p.mTarget.mode=mode;p.startTransition();
      for(std::size_t i=0;i<2;++i)worst=std::max(worst,std::abs(serial.state[i]-p.mIncoming.state[i]));
    }
    p.mTarget.mode=p.mCurrentMode;p.mFading=false;
    return worst;
  }
  static bool coherent(const JRockettAHBoostProcessor& p)
  {
    return (p.mTarget.mode == 0 && p.mTarget.gain == 1.) ||
           (p.mTarget.mode == 5 && p.mTarget.gain == 10.) ||
           (p.mTarget.mode == 2 && p.mTarget.gain == 1.);
  }
};
}
namespace holdsworth::test
{
namespace
{
using P = dsp::JRockettAHBoostProcessor;
using C = dsp::JRockettAHBoostControls;
using A = dsp::JRockettAHBoostTestAccess;
constexpr std::array<double, 6> rates{44100.,48000.,88200.,96000.,176400.,192000.};
// Independent frozen fixtures, intentionally not read from production constants.
constexpr std::array<std::array<double,2>,6> gains{{{6.,0.},{3.,3.},{3.,0.},
                                                  {0.,3.},{-3.,3.},{-6.,6.}}};
C controls(std::size_t mode, double db = 0.)
{
  return {db, static_cast<dsp::JRockettAHBoostType>(mode/2),
              static_cast<dsp::JRockettAHEmphasis>(mode%2)};
}
std::complex<double> target(double rate, std::size_t mode, double hz)
{
  std::complex<double> h{1.,0.};
  for (std::size_t i=0;i<2;++i)
  {
    const double a=std::pow(10.,gains[mode][i]/20.);
    const double ratio=std::tan(std::numbers::pi*hz/rate)/
                       std::tan(std::numbers::pi*(i==0?250.:2500.)/rate);
    const std::complex<double> s{0.,ratio};
    h *= i==0 ? (s+std::sqrt(a))/(s+1./std::sqrt(a))
              : a*(s+1./std::sqrt(a))/(s+std::sqrt(a));
  }
  return h;
}
std::complex<double> transform(std::span<const double> signal, double rate, double hz)
{
  const auto step=std::polar(1.,-2.*std::numbers::pi*hz/rate);
  std::complex<double> phase{1.,0.}, sum{};
  for(double x:signal) { sum+=x*phase; phase*=step; }
  return sum;
}
bool curves()
{
  for(double rate:rates)
    for(std::size_t mode=0;mode<6;++mode)
    {
      const auto n=static_cast<std::size_t>(rate*.1);
      std::vector<double> x(n),y(n); x[0]=1.;
      P p; p.setControls(controls(mode)); p.prepare(rate,n); p.processBlock(x,y);
      if(p.latencySamples()!=0 || y[0]==0.) return false;
      for(int k=0;k<=40;++k)
      {
        const double hz=20.*std::pow(500.,static_cast<double>(k)/40.);
        const auto measured=transform(y,rate,hz), expected=target(rate,mode,hz);
        if(!expectNear("AH complex analytic response",std::abs(measured-expected),0.,2e-10)) return false;
      }
    }
  return true;
}
bool levelsAndScale()
{
  for(double rate:rates)
    for(std::size_t mode=0;mode<6;++mode)
    {
      constexpr std::size_t n=4096;
      std::array<double,n> x{},base{},scaled{},inplace{};
      for(std::size_t i=0;i<n;++i) x[i]=.3*std::sin(.127*static_cast<double>(i))+.2*std::cos(.03*static_cast<double>(i));
      P p; p.setControls(controls(mode)); p.prepare(rate,n); p.processBlock(x,base);
      for(double db:{0.,10.,20.})
      {
        P q; q.setControls(controls(mode,db)); q.prepare(rate,n); q.processBlock(x,scaled);
        const double level=std::pow(10.,db/20.);
        for(std::size_t i=0;i<n;++i)
          if(!expectNear("AH level preserves complete EQ",scaled[i],base[i]*level,2e-12)) return false;
      }
      for(double scale:{-7.,1e-8,20.})
      {
        for(std::size_t i=0;i<n;++i) inplace[i]=x[i]*scale;
        p.reset(); p.processBlock(inplace,inplace);
        for(std::size_t i=0;i<n;++i)
          if(!expectNear("AH linear scale invariance and no unity clipping",inplace[i],base[i]*scale,3e-12)) return false;
      }
      inplace=x; p.reset(); p.processBlock(inplace,inplace);
      if(!expectSamplesBitExact("AH in place",base,inplace)) return false;
    }
  return true;
}
bool levelRamps()
{
  for(double rate:rates)
  {
    const auto n=static_cast<std::size_t>(std::ceil(rate*.01));
    P base,p; base.prepare(rate,n+1); p.prepare(rate,n+1);
    std::vector<double> x(n+1,1.),ref(n+1),y(n+1);
    double previous=1.;
    for(double db:{20.,10.,0.})
    {
      p.setControls(controls(2,db));
      base.processBlock(x,ref); p.processBlock(x,y);
      const double next=std::pow(10.,db/20.);
      for(std::size_t i=0;i<=n;++i)
      {
        const double g=i<n ? previous+(next-previous)*static_cast<double>(i+1)/static_cast<double>(n) : next;
        if(!expectNear("AH 10 ms amplitude ramp",y[i],ref[i]*g,3e-12)) return false;
      }
      previous=next;
    }
    // Repeated identical target must not restart a ramp.
    p.setControls(controls(2,20.));
    for(std::size_t i=0;i<n;++i) { p.setControls(controls(2,20.)); p.processBlock(std::span{x}.first(1),std::span{y}.first(1)); }
    p.processBlock(x,y); base.processBlock(x,ref);
    if(!expectNear("AH identical request completes level ramp",y.back(),ref.back()*10.,3e-12)) return false;
  }
  return true;
}
bool harmonics()
{
  for(double rate:rates)
    for(std::size_t mode=0;mode<6;++mode)
    {
      const auto n=static_cast<std::size_t>(rate/10.);
      std::vector<double> x(n),y(n);
      for(std::size_t i=0;i<n;++i) x[i]=.7*std::sin(2.*std::numbers::pi*1000.*static_cast<double>(i)/rate);
      P p; p.setControls(controls(mode,20.)); p.prepare(rate,n); p.processBlock(x,y); p.processBlock(x,y);
      const double fundamental=std::abs(transform(y,rate,1000.));
      for(int harmonic=2;harmonic<=10;++harmonic)
        if(!expectNear("AH no generated harmonics",std::abs(transform(y,rate,1000.*harmonic))/fundamental,0.,1e-10)) return false;
    }
  return true;
}
// Compare crossfades against independently running, fully warm complete responses.
// Also covers DC (no silence dip), sine/chords, all ordered mode pairs and rates.
bool transitions()
{
  for(double rate:rates)
  {
    const auto fade=static_cast<std::size_t>(std::ceil(.01*rate));
    const auto warm=static_cast<std::size_t>(.04*rate), n=warm+fade+16;
    for(int stimulus=0;stimulus<2;++stimulus)
    {
      std::vector<double> x(n);
      for(std::size_t i=0;i<n;++i)
        x[i]=stimulus==0?1.:.4*std::sin(2.*std::numbers::pi*83.*static_cast<double>(i)/rate)+.3*std::cos(2.*std::numbers::pi*731.*static_cast<double>(i)/rate);
      std::array<std::vector<double>,6> ref;
      for(std::size_t mode=0;mode<6;++mode)
      {
        P p; p.setControls(controls(mode)); p.prepare(rate,n); ref[mode].resize(n); p.processBlock(x,ref[mode]);
      }
      for(std::size_t from=0;from<6;++from) for(std::size_t to=0;to<6;++to)
      {
        P p; p.setControls(controls(from)); p.prepare(rate,n); std::vector<double> y(n);
        p.processBlock(std::span{x}.first(warm),std::span{y}.first(warm));
        p.setControls(controls(to));
        p.processBlock(std::span{x}.subspan(warm),std::span{y}.subspan(warm));
        for(std::size_t i=warm;i<n;++i)
        {
          const double t=std::min(1.,static_cast<double>(i-warm)/static_cast<double>(fade-1));
          const double expected=(1.-t)*ref[from][i]+t*ref[to][i];
          if(!expectNear("AH warm crossfade (no reset/silence dip)",y[i],expected,2e-9)) return false;
        }
        if(A::fading(p)||A::mode(p)!=to) return false;
        const auto dormant=A::dormantState(p);
        p.processBlock(x,y);
        if(dormant!=A::dormantState(p)) return false; // Inactive EQ no longer runs.
      }
    }
  }
  return true;
}
bool repeatedAndPartitions()
{
  for(double rate:rates)
  {
    const auto fade=static_cast<std::size_t>(std::ceil(.01*rate));
    const std::size_t eventStep=fade/4, n=fade*8;
    std::vector<double> x(n),whole(n),split(n),reference(n);
    for(std::size_t i=0;i<n;++i) x[i]=.2+std::sin(.047*static_cast<double>(i));
    std::array<std::vector<double>,6> refs;
    for(std::size_t mode=0;mode<6;++mode)
    { P p; p.setControls(controls(mode)); p.prepare(rate,n); refs[mode].resize(n); p.processBlock(x,refs[mode]); }
    // Independent timing oracle: complete the current fade; replace pending only.
    constexpr std::array<std::size_t,9> modes{0,5,1,4,2,3,0,5,3};
    std::size_t current=2,destination=2,pending=2,pos=0;
    bool fading=false;
    for(std::size_t i=0;i<n;++i)
    {
      if(i%eventStep==0 && i/eventStep<modes.size()) pending=modes[i/eventStep];
      if(!fading && current!=pending) { destination=pending; pos=0; fading=true; }
      double y=refs[current][i];
      if(fading)
      {
        const double t=static_cast<double>(pos)/static_cast<double>(fade-1);
        y=(1.-t)*y+t*refs[destination][i];
        if(++pos==fade) { current=destination; fading=false; }
      }
      reference[i]=y;
    }
    for(bool partitioned:{false,true})
    {
      P p; p.prepare(rate,n); auto& y=partitioned?split:whole;
      for(std::size_t offset=0;offset<n;)
      {
        const auto event=offset/eventStep;
        if(event<modes.size()) p.setControls(controls(modes[event]));
        const auto end=std::min(n,offset+eventStep);
        while(offset<end)
        {
          const auto count=std::min(end-offset,partitioned?std::array<std::size_t,4>{1,7,64,127}[offset%4]:n);
          p.processBlock(std::span{x}.subspan(offset,count),std::span{y}.subspan(offset,count)); offset+=count;
        }
      }
      if(A::mode(p)!=3 || A::fading(p)) return false;
    }
    if(!expectSamplesBitExact("AH event-aligned block determinism",whole,split) ||
       !expectSamples("AH latest pending complete response",whole,reference,2e-9)) return false;
  }
  return true;
}
bool lifecycle()
{
  P p;
  for(double rate:{0.,7999.,768001.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
  { try { p.prepare(rate,64); return false; } catch(const std::invalid_argument&) {} }
  try { p.prepare(48000.,0); return false; } catch(const std::invalid_argument&) {}
  for(double rate:rates)
  {
    std::array<double,64> x{},a{},b{}; x.fill(.2);
    p.prepare(rate,64); p.processBlock(x,a); p.setControls(controls(5,20.)); p.processBlock(x,a); p.reset(); p.processBlock(x,a);
    P fresh; fresh.setControls(controls(5,20.)); fresh.prepare(rate,64); fresh.processBlock(x,b);
    if(!expectSamplesBitExact("AH reset clears audio history and adopts request",a,b)) return false;
    p.prepare(rate,64); p.processBlock(x,a);
    if(!expectSamplesBitExact("AH reprepare",a,b)) return false;
  }
  // Continuous supported range and endpoints, in addition to the six nominal rates.
  for(double rate:{8000.,22050.,123456.,384000.,768000.})
  {
    p.prepare(rate,64); std::array<double,64> x{}; x.fill(1.);
    for(std::size_t i=0;i<400;++i) { p.setControls(controls(i%6)); p.processBlock(x,x); x.fill(1.); }
  }
  return true;
}
bool finiteAndSanitize()
{
  const double inf=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN();
  for(double rate:rates)
  {
    P p,q; p.prepare(rate,64); q.prepare(rate,64);
    std::array<double,64> x{},clean{},a{},b{}; x[0]=.5; x[3]=nan;x[7]=inf;x[8]=-inf; clean[0]=.5;
    p.processBlock(x,a);q.processBlock(clean,b);
    if(!expectSamplesBitExact("AH nonfinite audio equals zero excitation",a,b)) return false;
    for(double db:{-10.,nan,inf,100.})
    {
      p.setControls({db,static_cast<dsp::JRockettAHBoostType>(99),static_cast<dsp::JRockettAHEmphasis>(99)}); p.reset();
      q.setControls(controls(2,db==100.?20.:0.)); q.reset();
      p.processBlock(clean,a); q.processBlock(clean,b);
      if(!expectSamplesBitExact("AH sanitized control tuple",a,b)) return false;
    }
    x.fill(std::numeric_limits<double>::max());
    for(std::size_t mode=0;mode<6;++mode)
    { p.setControls(controls(mode,20.)); p.processBlock(x,a); for(double v:a) if(!std::isfinite(v)) return false; }
    x.fill(0.);
    for(int i=0;i<2000;++i) { p.processBlock(x,a); for(double v:a) if(!std::isfinite(v)) return false; }
    p.reset();p.processBlock(x,a);for(double v:a) if(v!=0.) return false;
  }
  return true;
}
bool realtime()
{
  P p; p.prepare(192000.,64); std::array<double,64> x{}; x.fill(.1);
  beginAllocationTracking();
  for(std::size_t i=0;i<5000;++i)
  { p.setControls(controls(i%6,static_cast<double>(i%21))); p.processBlock(x,x); x.fill(.1); if(i%1001==0) p.reset(); }
  return endAllocationTracking()==0;
}
bool concurrent()
{
  P p; p.prepare(48000.,64); std::atomic<bool> start{false};
  std::thread producer([&] {
    while(!start.load(std::memory_order_acquire)) {}
    for(int i=0;i<50000;++i) p.setControls(i%2==0?controls(0):controls(5,20.));
    p.setControls(controls(5,20.));
  });
  start.store(true,std::memory_order_release);
  std::array<double,64> x{},y{}; x.fill(.1); bool valid=true;
  for(int i=0;i<50000;++i)
  { p.processBlock(x,y); valid &= A::coherent(p); for(double v:y) valid &= std::isfinite(v); }
  producer.join();
  for(int i=0;i<100;++i) p.processBlock(x,y);
  return valid && A::mode(p)==5 && !A::fading(p) && expectNear("AH latest concurrent tuple",y.back(),.1*10.*std::pow(10.,-6./20.),1e-12);
}
bool replayPreservesM1()
{
  for(double rate:rates)
  {
    P p;p.prepare(rate,127);std::array<double,127>x{};unsigned random=42;
    for(unsigned block=0;block<300;++block)
    {
      const auto n=std::array<std::size_t,5>{1,7,16,64,127}[block%5];
      for(std::size_t i=0;i<n;++i){random=1664525U*random+1013904223U;x[i]=static_cast<double>(random)/4294967296.-.5;}
      p.processBlock(std::span{x}.first(n),std::span{x}.first(n));
      if(!expectNear("AH batched replay preserves M1 serial state",A::replayError(p),0.,1e-12))return false;
    }
  }
  return true;
}

bool mailboxStopped()
{
  P p; for(std::size_t i=0;i<1000;++i)p.setControls(controls(i%6,20.));
  p.setControls(controls(4,10.));p.prepare(48000.,1);
  P q;q.setControls(controls(4,10.));q.prepare(48000.,1);
  std::array<double,1>x{1.},a{},b{};p.processBlock(x,a);q.processBlock(x,b);
  return expectSamplesBitExact("AH stopped producer latest wins",a,b);
}
}
TestSuite jRockettAHBoostProcessorTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"AH Boost six analytic curves and zero latency",curves},
    TestCase{"AH Boost levels EQ shape scale invariance in-place",levelsAndScale},
    TestCase{"AH Boost level ramps",levelRamps},
    TestCase{"AH Boost no nonlinear harmonics",harmonics},
    TestCase{"AH Boost warm complete-response crossfades",transitions},
    TestCase{"AH Boost repeated modes and block partitions",repeatedAndPartitions},
    TestCase{"AH Boost reset reprepare and supported rates",lifecycle},
    TestCase{"AH Boost finite audio and control sanitization",finiteAndSanitize},
    TestCase{"AH Boost zero realtime allocations",realtime},
    TestCase{"AH Boost concurrent coherent handoff",concurrent},
    TestCase{"AH Boost stopped mailbox coalescing",mailboxStopped},
    TestCase{"AH Boost optimized priming preserves M1 serial recurrence",replayPreservesM1}};
  return tests;
}
} // namespace holdsworth::test
