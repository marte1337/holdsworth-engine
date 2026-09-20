// Runs the real IPlugProcessor buffer/bypass implementation with a small host
// double. Format wrapper ordering is additionally audited and built separately.
#ifdef NDEBUG
#undef NDEBUG // Assertions in this test stay enabled in an optimized build.
#endif
#include "IPlugProcessor.h"
#include "rt-audit.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>

using namespace iplug;
thread_local bool inAudio = false;
std::atomic<unsigned> rtViolations{0};

namespace iplug
{
struct IPlugLatencyTestAccess
{
  static void checkTargets(const IPlugProcessor& p)
  { assert(LatencyState::Samples(p.mLatencyState.AudioTarget()) == static_cast<int>(p.mLatencyDelay->DelayTime())); }
  static unsigned delayRemaining(const IPlugProcessor& p) { return p.mLatencyDelay->Remaining(); }
  static int audioTarget(const IPlugProcessor& p) { return LatencyState::Samples(p.mLatencyState.AudioTarget()); }
  // Fault injection: coordinator finishes early, but the real delay still fades
  // toward the SAME target. Adoption must wait for the lagging delay to finish.
  static void finishCoordinatorEarly(IPlugProcessor& p)
  { p.mLatencyState.Advance(p.mLatencyState.Remaining()); }
};
}

class Probe final : public IPlugProcessor
{
public:
  Probe(double rate = 48000., bool vst = false)
  : IPlugProcessor(Config(0,0,"1-1","test","test","test",0,0,0,0,false,false,false,
                          false,0,false,0,0,false,0,0,0,0,"test"), kAPIAPP), vst3(vst)
  {
    PrepareLatency(131104);
    SetSampleRate(rate); SetBlockSize(128);
    SetChannelConnections(ERoute::kInput,0,1,true);
    SetChannelConnections(ERoute::kOutput,0,1,true);
  }
  bool SendMidiMsg(const IMidiMsg&) override { return false; }
  void ProcessBlock(sample** in, sample** out, int n) override
  { ++normalCalls; for(int i=0;i<n;++i) out[0][i]=in[0][i]; }
  void OnLatencyPrepareBlock(int) override
  { assert(RequestLatency(request.load(std::memory_order_relaxed))); }
  void NotifyLatencyChange() override
  {
    assert(!inAudio);
    const int first = GetLatency();
    // Give rendering opportunities while a synchronous host query is pending.
    for(int i=0;i<100;++i) { assert(GetLatency()==first); std::this_thread::yield(); }
    ++notifications;
  }
  void OnLatencyRequest(LatencyState::Word word) override
  {
    assert(!inAudio);
    if(!vst3) IPlugProcessor::OnLatencyRequest(word);
    else if(!active) PublishLatencyWhileInactive();
    else if(LatencyState::Samples(word)==GetLatency()) IPlugProcessor::OnLatencyRequest(word);
    else ++restarts; // host deliberately delays the inactive lifecycle
  }
  void activate(bool value)
  { assert(!inAudio); active=value; if(value) PublishLatencyWhileInactive(); }
  void render(double* input, double* output, int n, bool bypass)
  {
    inAudio=true;
    AttachBuffers(ERoute::kInput,0,1,&input,n);
    AttachBuffers(ERoute::kOutput,0,1,&output,n);
    SetBypassed(bypass);
    if(GetBypassed()) PassThroughBuffers(0.,n); else ProcessBuffers(0.,n);
    IPlugLatencyTestAccess::checkTargets(*this);
    inAudio=false;
  }
  std::atomic<int> request{0};
  int notifications=0, restarts=0, normalCalls=0;
  bool vst3=false, active=false;
};

void sequential()
{
  for(double rate : {44100.,48000.,88200.,96000.,176400.,192000.})
    for(int frames : {1,2,4,8,32,64,128})
    {
      Probe p(rate);
      std::array<double,128> input{}, output{};
      long sample=0;
      const int fade=int(std::round(.010*rate));
      for(int latency : {0,32,0,29,61,75,107,32,0})
      {
        p.request.store(latency);
        // The initial request is published by the real framework render hook.
        for(int n=0;n<fade+256;n+=frames)
        {
          for(int j=0;j<frames;++j) input[j]=std::sin(.01*double(sample+j));
          p.render(input.data(),output.data(),frames,true);
          sample+=frames; p.ServiceLatencyUpdates();
        }
        assert(p.GetLatency()==latency);
        // Exact settled compensation, including the in-place case.
        for(int j=0;j<frames;++j) input[j]=std::sin(.01*double(sample+j));
        p.render(input.data(),input.data(),frames,true);
        for(int j=0;j<frames;++j)
          assert(std::abs(input[j]-std::sin(.01*double(sample+j-latency)))<1e-12);
        sample+=frames;
      }
      assert(p.normalCalls>0); // bounded fade into framework bypass renders wet temporarily
      // Normal processing must continuously refresh bypass history.
      p.request.store(32);
      for(int n=0;n<fade+256;n+=frames)
      {
        for(int j=0;j<frames;++j) input[j]=std::sin(.01*double(sample+j));
        p.render(input.data(),output.data(),frames,false);
        if(n>=fade) for(int j=0;j<frames;++j) assert(input[j]==output[j]);
        sample+=frames; p.ServiceLatencyUpdates();
      }
      for(int n=0;n<fade+256;n+=frames)
      {
        for(int j=0;j<frames;++j) input[j]=std::sin(.01*double(sample+j));
        p.render(input.data(),output.data(),frames,true);
        if(n>=fade) for(int j=0;j<frames;++j)
          assert(std::abs(output[j]-std::sin(.01*double(sample+j-32)))<1e-12);
        sample+=frames;
      }
    }
}

void lifecycle()
{
  Probe p(48000.,true); p.activate(true);
  double input=.25,output=0.;
  p.request.store(32); p.render(&input,&output,1,true);
  p.ServiceLatencyUpdates();
  assert(p.restarts==1 && p.GetLatency()==0);
  for(int i=0;i<600;++i) p.render(&input,&output,1,true);
  assert(p.GetLatency()==0); // no permit merely because transport is stopped
  p.activate(false); p.activate(true);
  assert(p.GetLatency()==32); // synchronous post-reactivation query
  for(int i=0;i<100;++i) p.render(&input,&output,1,true);
  p.request.store(0); p.render(&input,&output,1,true); p.ServiceLatencyUpdates();
  assert(p.restarts==2);
  p.activate(false); p.activate(true); assert(p.GetLatency()==0);
  for(int i=0;i<1100;++i) { p.render(&input,&output,1,true); p.ServiceLatencyUpdates(); }
  assert(p.GetLatency()==0 && output==input);
}

void continuity()
{
  Probe p;
  double previous=0.;
  for(int i=0;i<5000;++i)
  {
    if(i==1024 || i==2500) p.request.store(32);
    if(i==1200 || i==3500) p.request.store(0);
    double input=.25*std::sin(2.*3.141592653589793*750.*i/48000.), output=0.;
    p.render(&input,&output,1,true); p.ServiceLatencyUpdates();
    assert(std::abs(output-previous)<.027); // ordinary tone slope + bounded fade term
    previous=output;
  }
  assert(p.GetLatency()==0);
  assert(!p.RequestLatency(-1) && !p.RequestLatency(131105));
}

std::array<double,4096> partitionRender(int frames)
{
  Probe p;
  std::array<double,4096> result{};
  std::array<double,128> input{};
  for(int n=0;n<4096;n+=frames)
  {
    if(n==512 || n==2048)
    {
      const int latency=n==512?32:0;
      p.request.store(latency);
      const bool accepted=p.RequestLatency(latency);
      assert(accepted);
      p.ServiceLatencyUpdates();
    }
    for(int i=0;i<frames;++i) input[i]=.25*std::sin(.073*double(n+i));
    p.render(input.data(),result.data()+n,frames,true);
    p.ServiceLatencyUpdates();
  }
  return result;
}

void partitions()
{
  const auto reference=partitionRender(1);
  for(int frames:{2,4,8,32,64,128}) assert(reference==partitionRender(frames));
}

void concurrent()
{
  Probe p;
  std::atomic<bool> done{false};
  std::thread audio([&] {
    double input=.25,output=0.;
    for(int i=0;i<200000;++i) p.render(&input,&output,1,(i/103)%2!=0);
    done.store(true);
  });
  int i=0;
  while(!done.load())
  {
    p.request.store((i++%4)*32);
    p.ServiceLatencyUpdates();
  }
  audio.join();
  p.request.store(32);
  double input=.25,output=0.;
  for(int n=0;n<2000;++n) { p.render(&input,&output,1,true); p.ServiceLatencyUpdates(); }
  assert(p.GetLatency()==32 && output==input);
}

void delayReadiness()
{
  using Access = IPlugLatencyTestAccess;
  for(double rate : {44100.,48000.,88200.,96000.,176400.,192000.})
    for(int frames : {1,2,4,8,32,64,128})
    {
      Probe p(rate);
      std::array<double,128> input{}, output{};
      p.request.store(32);
      p.render(input.data(),output.data(),frames,false);
      p.ServiceLatencyUpdates();
      p.render(input.data(),output.data(),frames,false);
      assert(Access::audioTarget(p)==32 && Access::delayRemaining(p)>0);
      Access::finishCoordinatorEarly(p);
      p.ServiceLatencyUpdates();
      unsigned requests=0;
      while(Access::delayRemaining(p))
      {
        const int next = requests++%3==1 ? 32 : 0;
        p.request.store(next);
        assert(p.RequestLatency(next));
        p.ServiceLatencyUpdates();
        p.render(input.data(),output.data(),frames,requests%2!=0);
        assert(Access::audioTarget(p)==32); // even on the last blocked callback
      }
      p.request.store(0);
      assert(p.RequestLatency(0));
      p.ServiceLatencyUpdates();
      p.render(input.data(),output.data(),frames,true);
      assert(Access::audioTarget(p)==0); // resumes at the first ready boundary
      for(int n=0;n<int(std::round(.010*rate))+128;n+=frames)
      { p.render(input.data(),output.data(),frames,true); p.ServiceLatencyUpdates(); }
      assert(p.GetLatency()==0 && Access::delayRemaining(p)==0);
    }
}

int main()
{
#ifdef AH_TRACK_REALTIME
  inAudio=true;
  void* (*volatile allocate)(size_t)=&std::malloc;
  void (*volatile release)(void*)=&std::free;
  void* trackingCheck=allocate(17);
  release(trackingCheck);
  inAudio=false;
  assert(rtViolations.load()>=2); // prove the instrumentation actually intercepts
  rtViolations.store(0);
#endif
  delayReadiness(); sequential(); lifecycle(); continuity(); partitions(); concurrent();
  assert(rtViolations.load()==0);
  std::cout << "PASS: real IPlugProcessor normal/bypass, 6 rates x 7 blocks, exact settled taps, "
               "warm history, in-place, model+pedal totals, synchronous queries, restart lifecycle, concurrency, "
               "delay-readiness fault injection and per-render target agreement\n";
}
