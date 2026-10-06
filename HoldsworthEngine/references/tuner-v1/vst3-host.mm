// Loads the real Release VST3 bundle and calls its actual host process API.
#include "host-common.h"
#include "host-acceptance.h"
#include <CoreFoundation/CoreFoundation.h>
#include "pluginterfaces/vst/ivsthostapplication.h"

thread_local bool inAudio = false;
std::atomic<unsigned> rtViolations{0};

using namespace Steinberg;
using namespace Steinberg::Vst;

struct HostContext final : IHostApplication, IComponentHandler
{
  uint32 refs = 1;
  unsigned restarts = 0;
  tresult PLUGIN_API queryInterface(const TUID iid, void** result) override
  {
    *result = nullptr;
    if (!std::memcmp(iid, IHostApplication_iid, 16) || !std::memcmp(iid, FUnknown_iid, 16))
      *result = static_cast<IHostApplication*>(this);
    else if (!std::memcmp(iid, IComponentHandler_iid, 16))
      *result = static_cast<IComponentHandler*>(this);
    if (*result) { addRef(); return kResultOk; }
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return ++refs; }
  uint32 PLUGIN_API release() override { return --refs; }
  tresult PLUGIN_API getName(String128 name) override
  {
    constexpr char text[] = "Tuner v1 actual VST3 acceptance";
    for (std::size_t i = 0; i < sizeof(text); ++i) name[i] = text[i];
    return kResultOk;
  }
  tresult PLUGIN_API createInstance(TUID, TUID, void** result) override
  { *result = nullptr; return kNoInterface; }
  tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
  tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API restartComponent(int32) override
  { assert(!inAudio); ++restarts; return kResultOk; }
};

struct VST3Instance
{
  explicit VST3Instance(IPluginFactory* factory, const TUID cid, const double rate)
  {
    assert(factory->createInstance(cid, IComponent_iid, reinterpret_cast<void**>(&component)) == kResultOk);
    assert(component->queryInterface(IAudioProcessor_iid, reinterpret_cast<void**>(&processor)) == kResultOk);
    assert(processor->getLatencySamples() == 32);
    assert(component->initialize(static_cast<IHostApplication*>(&context)) == kResultOk);
    assert(component->queryInterface(IEditController_iid, reinterpret_cast<void**>(&controller)) == kResultOk);
    assert(controller->setComponentHandler(&context) == kResultOk);
    SpeakerArrangement input = SpeakerArr::kMono, output = SpeakerArr::kStereo;
    assert(processor->setBusArrangements(&input, 1, &output, 1) == kResultOk);
    assert(component->activateBus(kAudio, kInput, 0, true) == kResultOk);
    assert(component->activateBus(kAudio, kOutput, 0, true) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample64;
    setup.maxSamplesPerBlock = tunerMaximumBlock;
    setup.sampleRate = rate;
    assert(processor->setupProcessing(setup) == kResultOk);
    assert(component->setActive(true) == kResultOk);
    assert(processor->setProcessing(true) == kResultOk);
    assert(latency() == 32 && context.restarts == 0);
  }
  ~VST3Instance()
  {
    plugin().OnUIClose();
    assert(processor->setProcessing(false) == kResultOk);
    assert(component->setActive(false) == kResultOk);
    assert(component->terminate() == kResultOk);
    controller->release(); processor->release(); component->release();
  }
  iplug::IPlugAPIBase& plugin()
  {
    auto* effect = static_cast<SingleComponentEffect*>(component);
    return *static_cast<iplug::IPlugVST3*>(effect);
  }
  unsigned latency() const { return processor->getLatencySamples(); }
  void render(std::span<double> input, std::span<double> left, std::span<double> right, const bool bypass)
  {
    auto& wrapper = static_cast<iplug::IPlugVST3&>(plugin());
    iplug::IPlugLatencyTestAccess::bypass(wrapper, bypass);
    double* in[] = {input.data()};
    double* out[] = {left.data(), right.data()};
    AudioBusBuffers inBus{}; inBus.numChannels = 1; inBus.channelBuffers64 = in;
    AudioBusBuffers outBus{}; outBus.numChannels = 2; outBus.channelBuffers64 = out;
    ProcessData data{};
    data.processMode = kRealtime; data.symbolicSampleSize = kSample64;
    data.numSamples = static_cast<int32>(input.size());
    data.numInputs = 1; data.numOutputs = 1; data.inputs = &inBus; data.outputs = &outBus;
    AudioScope audio;
    assert(processor->process(data) == kResultOk);
  }
  HostContext context;
  IComponent* component = nullptr;
  IAudioProcessor* processor = nullptr;
  IEditController* controller = nullptr;
};

int main(int argc, char** argv)
{
  assert(argc >= 2);
  auditSelfTest();
  const auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
    reinterpret_cast<const UInt8*>(argv[1]), std::strlen(argv[1]), true);
  const auto bundle = CFBundleCreate(nullptr, url); assert(bundle); CFRelease(url);
  assert(CFBundleLoadExecutable(bundle));
  const auto enter = reinterpret_cast<bool(*)(CFBundleRef)>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
  const auto leave = reinterpret_cast<bool(*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
  const auto getFactory = reinterpret_cast<IPluginFactory*(*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
  assert(enter && leave && getFactory && enter(bundle));
  auto* factory = getFactory(); assert(factory);
  bool exercised = false;
  for (int32 i = 0; i < factory->countClasses(); ++i)
  {
    PClassInfo info{}; assert(factory->getClassInfo(i, &info) == kResultOk);
    if (std::strcmp(info.category, kVstAudioEffectClass)) continue;
    runHostAcceptance("VST3", [&] (double rate) { return std::make_unique<VST3Instance>(factory, info.cid, rate); }, argc > 2);
    exercised = true;
  }
  factory->release(); assert(leave()); CFRelease(bundle);
  assert(exercised && rtViolations.load() == 0);
  if (argc <= 2) std::puts("PASS: actual Release VST3 tuner acceptance");
}
