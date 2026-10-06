// Linked against the exact Release APP target objects, replacing its entry point.
#include "host-common.h"
#include "host-acceptance.h"
#include "IPlugSWELL.h"

thread_local bool inAudio = false;
std::atomic<unsigned> rtViolations{0};
HWND gHWND = nullptr;
INT_PTR SWELLAppMain(int, INT_PTR, INT_PTR) { return 0; }

struct APPInstance
{
  explicit APPInstance(const double rate)
  : wrapper(iplug::MakePlug(iplug::InstanceInfo{nullptr}))
  {
    assert(wrapper && wrapper->GetLatency() == 32);
    iplug::IPlugLatencyTestAccess::prepare(*wrapper, rate, tunerMaximumBlock);
    wrapper->OnReset();
  }
  ~APPInstance() { plugin().OnUIClose(); }
  iplug::IPlugAPIBase& plugin() { return *wrapper; }
  unsigned latency() const { return static_cast<unsigned>(wrapper->GetLatency()); }
  void render(std::span<double> input, std::span<double> left, std::span<double> right, const bool bypass)
  {
    // The real standalone callback uses its configured frame count.
    iplug::IPlugLatencyTestAccess::frames(*wrapper, static_cast<int>(input.size()));
    iplug::IPlugLatencyTestAccess::bypass(*wrapper, bypass);
    double* in[] = {input.data()};
    double* out[] = {left.data(), right.data()};
    AudioScope audio;
    if (bypass)
    {
      // APP has no external host bypass API. Exercise the real shared framework
      // bypass path directly; normal rendering always uses actual AppProcess.
      iplug::IPlugLatencyTestAccess::renderBypass(*wrapper, in, out, static_cast<int>(input.size()));
    }
    else wrapper->AppProcess(in, out, static_cast<int>(input.size()));
  }
  std::unique_ptr<iplug::IPlugAPP> wrapper;
};

int main(int argc, char**)
{
  auditSelfTest();
  runHostAcceptance("APP", [] (double rate) { return std::make_unique<APPInstance>(rate); }, argc > 1);
  assert(rtViolations.load() == 0);
  if (argc <= 1) std::puts("PASS: production Release APP-object tuner acceptance");
}
