#include "../../dsp/LegatoCompressorProcessor.h"
#include "../../dsp/TCBLDCleanBoostProcessor.h"
#include "../../integration/DevelopmentLegatoCompressorControls.h"
#include "../../tests/LegatoCompressorFixtures.h"
#include "../tuner-v1/rt-audit.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

thread_local bool inAudio=false;
std::atomic<unsigned> rtViolations{0};
using P=holdsworth::dsp::LegatoCompressorProcessor;
namespace fixture=holdsworth::test::legato;
namespace holdsworth::dsp
{
struct LegatoCompressorTestAccess
{
  static double tick(P& p,double level){p.mPower=std::pow(10.,level/10.);p.controlTick();return p.mReductionDb;}
  static double interval(const P& p){return static_cast<double>(p.mControlSamples)/p.mSampleRate;}
  static void fullRateReference(P& p){p.mControlSamples=1;p.mTarget=p.makeTarget(p.mTarget.controls);}
};
}
struct Scope { Scope(){inAudio=true;}~Scope(){inAudio=false;} };
void selfTest()
{
  void* (*volatile allocate)(size_t)=&malloc;void (*volatile release)(void*)=&free;
  pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
  {Scope scope;void* ptr=allocate(8);release(ptr);pthread_mutex_lock(&mutex);pthread_mutex_unlock(&mutex);}
  pthread_mutex_destroy(&mutex);
  if(rtViolations.load()<3)throw std::runtime_error("RT interposer self-test failed");rtViolations.store(0);
}
double peak(const std::vector<double>& x,std::size_t a,std::size_t b)
{double p=0.;for(auto i=a;i<b;++i)p=std::max(p,std::abs(x[i]));return p;}
double gainDb(double gain){return 20.*std::log10(gain);}
std::vector<double> render(const fixture::Fixture& f,double rate,std::ofstream* trace=nullptr,bool fullRate=false)
{
  P p;holdsworth::dsp::LegatoCompressorControls c;c.enabled=true;p.setControls(c);p.prepare(rate,1024);
  if(fullRate)holdsworth::dsp::LegatoCompressorTestAccess::fullRateReference(p);p.beginBlock({true,1.});
  for(std::size_t i=0;i<fixture::frames(.050,rate);++i){Scope scope;(void)p.processSample(0.);}
  std::vector<double> y(f.input.size());
  for(std::size_t i=0;i<f.input.size();++i)
  {
    holdsworth::dsp::LegatoCompressorDiagnostics d;
    {Scope scope;y[i]=p.processSample(f.input[i]);d=p.diagnostics();}
    if(trace){const std::array<double,8> row{f.input[i],y[i],d.power,d.levelDb,d.requestedReductionDb,d.reductionDb,d.appliedGain,d.enableMix};
      trace->write(reinterpret_cast<const char*>(row.data()),sizeof(row));}
  }
  return y;
}
void measurements(const std::filesystem::path& directory)
{
  std::ofstream metrics(directory/"notes.csv");
  metrics<<"fixture,rate_hz,note,picked,duration_ms,input_rms_dbu,output_rms_dbu,gain_db,plain_boost_gain_db,contrast_improvement_db,first5ms_gain_db,first3ms_peak_vs_boost_db\n";
  metrics.precision(15);
  std::ofstream referenceRate(directory/"control-rate.csv");referenceRate.precision(15);
  referenceRate<<"rate_hz,nominal_pick_body_error_db,nominal_weak_body_error_db,max_sample_gain_error_db\n";
  for(double rate:fixture::rates)
  {
   auto all=fixture::fixtures(rate);
   const auto reduced=render(all.front(),rate),full=render(all.front(),rate,nullptr,true);
   const auto bodyPickStart=fixture::frames(.100,rate),bodyPickEnd=fixture::frames(.280,rate);
   const auto bodyWeakStart=fixture::frames(.340,rate),bodyWeakEnd=fixture::frames(.440,rate);
   double maxError=0.;
   for(std::size_t i=0;i<full.size();++i)if(std::abs(full[i])>1.e-15)
     maxError=std::max(maxError,std::abs(gainDb(std::abs(reduced[i]/full[i]))));
   referenceRate<<rate<<','<<fixture::rmsDb(reduced,bodyPickStart,bodyPickEnd)-fixture::rmsDb(full,bodyPickStart,bodyPickEnd)
     <<','<<fixture::rmsDb(reduced,bodyWeakStart,bodyWeakEnd)-fixture::rmsDb(full,bodyWeakStart,bodyWeakEnd)<<','<<maxError<<'\n';
   // Additional level stress, separate from the frozen nominal acceptance
   // fixture. Keep the approved threshold/timing/makeup unchanged.
   for(double louder:{6.,12.})
   {
     auto f=all.front();f.name="stress-"+std::to_string(static_cast<int>(louder))+"dB-pick-weak";
     for(auto& x:f.input)x*=fixture::amplitude(louder);all.push_back(std::move(f));
     for(const auto& source:fixture::fixtures(rate))if(source.name=="rapid-20ms")
     {auto run=source;run.name="stress-"+std::to_string(static_cast<int>(louder))+"dB-rapid-20ms";
      for(auto& x:run.input)x*=fixture::amplitude(louder);all.push_back(std::move(run));}
   }
   for(const auto& f:all)
   {
    std::ofstream trace;
    if(rate==48000.)trace.open(directory/(f.name+".f64"),std::ios::binary);
    const auto y=render(f,rate,trace.is_open()?&trace:nullptr);
    double pickedGain=0.;
    if(!f.notes.empty()&&f.notes.front().picked&&f.notes.front().end>=fixture::frames(.280,rate))
      pickedGain=fixture::rmsDb(y,fixture::frames(.100,rate),fixture::frames(.280,rate))-
        fixture::rmsDb(f.input,fixture::frames(.100,rate),fixture::frames(.280,rate));
    for(std::size_t i=0;i<f.notes.size();++i)
    {
      const auto& n=f.notes[i];const auto first=n.start+std::min(n.end-n.start,fixture::frames(.005,rate));
      const auto transient=n.start+std::min(n.end-n.start,fixture::frames(.003,rate));
      const double input=fixture::rmsDb(f.input,n.start,n.end),output=fixture::rmsDb(y,n.start,n.end);
      metrics<<f.name<<','<<rate<<','<<i<<','<<n.picked<<','<<1000.*static_cast<double>(n.end-n.start)/rate<<','
        <<input-20.*std::log10(.775)<<','<<output-20.*std::log10(.775)<<','<<output-input<<",3,"<<output-input-pickedGain<<','
        <<fixture::rmsDb(y,n.start,first)-fixture::rmsDb(f.input,n.start,first)<<','
        <<gainDb(peak(y,n.start,transient)/(peak(f.input,n.start,transient)*fixture::amplitude(3.)))<<'\n';
    }
    if(f.name=="pick-weak-150ms"||f.name.ends_with("dB-pick-weak"))
    {
      const auto a=f.notes[1].start+fixture::frames(.040,rate),b=f.notes[1].start+fixture::frames(.140,rate);
      const double input=fixture::rmsDb(f.input,a,b),output=fixture::rmsDb(y,a,b);
      metrics<<(f.name=="pick-weak-150ms"?"first-weak-body":f.name+"-body")<<','<<rate<<",1,0,100,"<<input-20.*std::log10(.775)<<','<<output-20.*std::log10(.775)
        <<','<<output-input<<",3,"<<output-input-pickedGain<<",0,0\n";
    }
   }
  }
  for(double hz:{82.4069,110.,220.,440.,1000.})
  {
    fixture::Fixture f{"sine-"+std::to_string(hz),{}, {}};
    for(std::size_t i=0;i<96000;++i)f.input.push_back(.775*fixture::amplitude(-12.)*std::sqrt(2.)*std::sin(2.*std::numbers::pi*hz*static_cast<double>(i)/48000.));
    std::ofstream trace(directory/(f.name+".f64"),std::ios::binary);(void)render(f,48000.,&trace);
  }
  std::ofstream timing(directory/"timing.csv");timing.precision(15);
  std::ofstream detectorTiming(directory/"detector-timing.csv");detectorTiming.precision(15);
  detectorTiming<<"rate_hz,setting_ms,measured_ms,error_percent\n";
  for(double rate:fixture::rates)
  {
    P p;p.prepare(rate,1024);p.beginBlock({});double power=0.,previous=0.;std::size_t n=0;
    const double boundary=1.-std::exp(-1.);
    while(power<boundary){previous=power;{Scope scope;(void)p.processSample(1.);power=p.diagnostics().power;}++n;}
    const double measured=(static_cast<double>(n-1)+(boundary-previous)/(power-previous))*1000./rate;
    detectorTiming<<rate<<",4,"<<measured<<','<<100.*(measured/4.-1.)<<'\n';
  }
  timing<<"stage,rate_hz,setting_ms,measured_ms,error_percent,control_interval_ms\n";
  using A=holdsworth::dsp::LegatoCompressorTestAccess;
  for(double rate:fixture::rates)for(double attack:{8.,15.,30.})for(double release:{40.,60.,180.})
  {
    P p;holdsworth::dsp::LegatoCompressorControls c;c.attackMs=attack;c.releaseMs=release;p.setControls(c);p.prepare(rate,1024);p.beginBlock({});
    const double dt=A::interval(p);double r=0.,previous=0.;std::size_t n=0;const double boundary=3.*(1.-std::exp(-1.));
    while(r<boundary){previous=r;r=A::tick(p,-24.);++n;}
    const double measured=(static_cast<double>(n-1)+(boundary-previous)/(r-previous))*dt*1000.;
    timing<<"attack,"<<rate<<','<<attack<<','<<measured<<','<<100.*(measured/attack-1.)<<','<<dt*1000.<<'\n';
    for(int k=0;k<3000;++k)r=A::tick(p,-24.);const double start=r;n=0;
    while(r>start/std::exp(1.)){previous=r;r=A::tick(p,-60.);++n;}
    const double recovered=(static_cast<double>(n-1)+(previous-start/std::exp(1.))/(previous-r))*dt*1000.;
    timing<<"release,"<<rate<<','<<release<<','<<recovered<<','<<100.*(recovered/release-1.)<<','<<dt*1000.<<'\n';
  }
  std::ofstream calibration(directory/"calibration.csv");calibration.precision(15);
  calibration<<"rate_hz,model_dbu,host_dbu,pedal_off,max_level_error_db,max_gain_error,max_power_error\n";
  using TC=holdsworth::dsp::TCBLDCleanBoostProcessor;
  for(double rate:fixture::rates)for(double model:{-6.,6.,12.,18.})for(bool off:{false,true})
  {
    const auto f=fixture::fixtures(rate).front();P ref;holdsworth::dsp::LegatoCompressorControls c;c.enabled=true;
    ref.setControls(c);ref.prepare(rate,1024);ref.beginBlock({true,1.});
    std::vector<holdsworth::dsp::LegatoCompressorDiagnostics> reference;reference.reserve(f.input.size());
    for(double x:f.input){(void)ref.processSample(x);reference.push_back(ref.diagnostics());}
    for(double host:{-6.,0.,6.,12.,18.,24.})
    {
      P p;p.setControls(c);p.prepare(rate,1024);const double h=fixture::amplitude(3.)*TC::fullScalePeakVolts(host);
      const double selected=off ? fixture::amplitude(3.+host-model) : h;
      p.beginBlock(holdsworth::integration::legatoDetectorContext(true,h,selected));double level=0.,gain=0.,power=0.;
      for(std::size_t i=0;i<f.input.size();++i){(void)p.processSample((f.input[i]/h)*selected);const auto d=p.diagnostics();
        level=std::max(level,std::abs(d.levelDb-reference[i].levelDb));gain=std::max(gain,std::abs(d.appliedGain-reference[i].appliedGain));power=std::max(power,std::abs(d.power-reference[i].power));}
      calibration<<rate<<','<<model<<','<<host<<','<<off<<','<<level<<','<<gain<<','<<power<<'\n';
    }
  }
}
void benchmark(const std::filesystem::path& directory)
{
  std::ofstream csv(directory/"cpu.csv");csv.precision(12);csv<<"rate_hz,block,mode,p50_us,p99_us,max_us,deadline_us\n";
  for(double rate:fixture::rates)for(std::size_t block:{1U,2U,4U,8U,32U,37U,64U,128U,256U,512U,1024U})
    for(const std::string mode:{"off","on","silence","subnormal","full-rate-reference"})
    {
      P p;holdsworth::dsp::LegatoCompressorControls c;c.enabled=mode!="off";p.setControls(c);p.prepare(rate,block);
      if(mode=="full-rate-reference")holdsworth::dsp::LegatoCompressorTestAccess::fullRateReference(p);
      std::vector<double>x(block),y(block),times;times.reserve(2048);
      for(std::size_t i=0;i<block;++i)x[i]=mode=="silence"?0.:mode=="subnormal"?std::numeric_limits<double>::denorm_min():.4*std::sin(.17*static_cast<double>(i));
      for(int i=0;i<256;++i){Scope scope;p.processBlock(x,y,{true,1.});}
      for(int i=0;i<2048;++i){const auto start=std::chrono::steady_clock::now();
        {Scope scope;p.processBlock(x,y,{true,1.});}
        times.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}
      std::sort(times.begin(),times.end());csv<<rate<<','<<block<<','<<mode<<','<<times[1024]<<','<<times[2027]<<','<<times.back()<<','<<1e6*static_cast<double>(block)/rate<<'\n';
    }
}
int main(int argc,char** argv)
{
  if(argc!=2)return 2;selfTest();const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
  measurements(directory);benchmark(directory);
  std::printf("PASS Legato objective measurements; audited allocations/frees/locks/waits: %u\n",rtViolations.load());
  return rtViolations.load()!=0 ? 1 : 0;
}
