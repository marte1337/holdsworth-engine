#ifdef NDEBUG
#undef NDEBUG
#endif
#include "IPlugProcessor.h"
#include "../../../../../dsp/JRockettAHPedal.h"
#include "../../../../../integration/DevelopmentAHOuterTransition.h"
#include "rt-audit.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

thread_local bool inAudio=false;
std::atomic<unsigned> rtViolations{0};
using namespace iplug;
using namespace holdsworth;
struct Wire
{
  bool isPrepared()const{return true;}
  std::size_t maximumBlockSize()const{return 128;}
  void reset(){}
  void processBlock(std::span<const double>x,std::span<double>y){std::copy(x.begin(),x.end(),y.begin());}
};
class PedalHost final: public IPlugProcessor
{
public:
  PedalHost(double rate):IPlugProcessor(Config(0,0,"1-1","test","test","test",0,0,0,32,false,false,false,
    false,0,false,0,0,false,0,0,0,0,"test"),kAPIAPP)
  {
    PrepareLatency(131104);SetSampleRate(rate);SetBlockSize(128);
    SetChannelConnections(ERoute::kInput,0,1,true);SetChannelConnections(ERoute::kOutput,0,1,true);
    pedal.setSections(false,false);pedal.prepare(rate,128);selector.prepare(rate);
  }
  bool SendMidiMsg(const IMidiMsg&)override{return false;}
  void OnLatencyPrepareBlock(int)override
  { assert(RequestLatency(modelLatency.load()+32)); }
  void NotifyLatencyChange()override { assert(!inAudio); ++notifications; }
  void pre(double* input,double* output,int n)
  {
    selector.beginBlock(static_cast<integration::DevelopmentPreNAMProcessor>(request.load()),n,tc,mc,pedal);
    if(input!=output)std::copy(input,input+n,output);
    selector.processSelected({output,static_cast<std::size_t>(n)},1.,tc,mc,pedal);
  }
  void ProcessBlock(sample** in,sample** out,int n)override{pre(in[0],out[0],n);}
  void OnLatencyBypassBlock(sample** in,int n)override
  {std::array<double,128> discarded{};pre(in[0],discarded.data(),n);}
  void render(double* input,double* output,int n,bool bypass)
  {
    inAudio=true;
    AttachBuffers(ERoute::kInput,0,1,&input,n);AttachBuffers(ERoute::kOutput,0,1,&output,n);
    SetBypassed(bypass);if(bypass)PassThroughBuffers(0.,n);else ProcessBuffers(0.,n);
    inAudio=false;
  }
  std::atomic<unsigned>request{0};
  std::atomic<int>modelLatency{0};
  unsigned notifications=0;
  dsp::JRockettAHPedal pedal;
  integration::DevelopmentAHOuterTransition selector;
  Wire tc,mc;
};
void integrationChecks()
{
  for(double rate:dsp::JRockettAHDriveProfile::supportedRates)for(int block:{1,2,4,8,32,64,128})
  {
    PedalHost p(rate);assert(p.GetLatency()==32);
    std::array<double,128>x{},y{};long at=0;double prior=0.;
    for(int stage=0;stage<8;++stage)
    {
      p.request.store(stage%4);
      for(int n=0;n<static_cast<int>(rate*.06);n+=block)
      {
        for(int i=0;i<block;++i)x[i]=.2*std::sin(.04*(at+i));
        // Switching bypass inside the outer delay fade must also be bounded.
        const bool bypass=(n/137)%3!=0;
        // No service call: selection must progress without latency permission.
        p.render(x.data(),y.data(),block,bypass);
        for(int i=0;i<block;++i){assert(std::isfinite(y[i]));assert(std::abs(y[i]-prior)<.012);prior=y[i];}
        at+=block;
      }
      assert(p.GetLatency()==32 && p.notifications==0);assert(!p.selector.transitioning());
      assert(p.selector.applied()==static_cast<integration::DevelopmentPreNAMProcessor>(stage%4));
    }
    // Both histories stayed current: settled local bypass equals host bypass.
    for(bool bypass:{false,true,false})
      for(int n=0;n<static_cast<int>(rate*.03);n+=block)
      {
        for(int i=0;i<block;++i)x[i]=.2*std::sin(.04*(at+i));
        p.render(x.data(),y.data(),block,bypass);p.ServiceLatencyUpdates();
        for(int i=0;i<block;++i)assert(std::abs(y[i]-.2*std::sin(.04*(at+i-32)))<1e-14);
        at+=block;
      }
    // A genuinely pending MODEL request must not prevent AH from running.
    // Deliberately never service the request, so reported latency stays at 32.
    PedalHost pending(rate);pending.modelLatency=75;pending.request=3;
    pending.pedal.setSections(true,false);
    pending.pedal.setBoostControls({12.,dsp::JRockettAHBoostType::clean,dsp::JRockettAHEmphasis::low});
    x.fill(.1);
    for(int n=0;n<6000;n+=block)pending.render(x.data(),y.data(),block,false);
    assert(pending.GetLatency()==32 && pending.notifications==0);
    assert(pending.selector.applied()==integration::DevelopmentPreNAMProcessor::jRockettAH);
    assert(!pending.selector.transitioning() && y[block-1]>.2);
  }
  PedalHost p(192000.);std::atomic<bool>done{false};
  std::thread audio([&]{std::array<double,8>x{},y{};x.fill(.1);
    for(int i=0;i<10000;++i)p.render(x.data(),y.data(),8,i%73<40);done=true;});
  unsigned i=0;while(!done.load())
  {
    p.request=i%4;p.pedal.setSections(i%2,i%3);
    p.pedal.setBoostControls({double(i%21),static_cast<dsp::JRockettAHBoostType>(i%3),static_cast<dsp::JRockettAHEmphasis>(i%2)});
    p.pedal.setDriveControls({double(i%101)/100.,.4,.7,.6});p.ServiceLatencyUpdates();++i;
  }
  audio.join();p.request=3;double x=.1,y=0.;
  for(int n=0;n<10000;++n){p.render(&x,&y,1,true);p.ServiceLatencyUpdates();}
  assert(p.GetLatency()==32 && !p.selector.transitioning());
  assert(rtViolations.load()==0);
}
void benchmark()
{
  using Clock=std::chrono::steady_clock;
  std::cout<<"rate,block,route,p50_us,p99_us,max_us,deadline_us\n";
  for(double rate:dsp::JRockettAHDriveProfile::supportedRates)for(int block:{1,2,4,8,32,64,128})for(int route=0;route<5;++route)
  {
    PedalHost p(rate);p.request=3;p.pedal.setSections(route&1,route&2);
    std::array<double,128>x{},y{};for(int i=0;i<128;++i)x[i]=.1*std::sin(.12*i);
    for(int i=0;i<5000;++i){p.render(x.data(),y.data(),block,false);p.ServiceLatencyUpdates();}
    std::vector<double>times;times.reserve(2048);
    for(int i=0;i<2048;++i)
    {
      if(route==4)
      {
        p.pedal.setSections(i%7<4,i%11<7);
        if(i%31==0)p.pedal.setBoostControls({6.,static_cast<dsp::JRockettAHBoostType>((i/31)%3),static_cast<dsp::JRockettAHEmphasis>((i/31)%2)});
      }
      auto start=Clock::now();p.render(x.data(),y.data(),block,false);auto end=Clock::now();
      times.push_back(std::chrono::duration<double,std::micro>(end-start).count());
    }
    std::sort(times.begin(),times.end());
    std::cout<<rate<<','<<block<<','<<route<<','<<times[1024]<<','<<times[2027]<<','<<times.back()<<','<<1e6*block/rate<<'\n';
  }
  assert(rtViolations.load()==0);
}
int main(int argc,char**)
{
#ifdef AH_TRACK_REALTIME
  inAudio=true;void*(*volatile alloc)(size_t)=&malloc;void(*volatile release)(void*)=&free;
  void* probe=alloc(17);release(probe);inAudio=false;assert(rtViolations.load()>=2);rtViolations=0;
#endif
  if(argc>1)benchmark();else {integrationChecks();std::cout<<"PASS: full pedal + real framework latency/bypass, 6 rates x 7 blocks, rapid toggles, settled agreement, concurrent UI/audio\n";}
}
