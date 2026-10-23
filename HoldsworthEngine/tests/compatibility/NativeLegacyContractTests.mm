// M0 only: actual accepted APP objects / VST3 and AU APIs, no production hooks.
#define main m0UnusedAcceptedHostMain
#if defined(M0_HOST_APP)
#include "../../references/tuner-v1/app-host.mm"
#elif defined(M0_HOST_VST3)
#include "../../references/tuner-v1/vst3-host.mm"
#elif defined(M0_HOST_AU)
#include "../../references/tuner-v1/au-host.mm"
#else
#error Select an M0 host format
#endif
#undef main

#include "../../control/ParameterRegistry.h"
#include "../../control/LegacyStateCodec.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef NDEBUG
#error Native compatibility assertions must remain active.
#endif

namespace
{
namespace control = holdsworth::control;
using Bytes = std::vector<std::uint8_t>;
std::filesystem::path fixtureRoot, captureRoot;
bool capturing = false;
std::size_t comparisons = 0;
double maximumConversionError = 0.;

Bytes readBytes(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  assert(input && "Frozen fixture is missing");
  return Bytes(std::istreambuf_iterator<char>{input}, {});
}

void retain(const std::string& name, std::span<const std::uint8_t> bytes)
{
  if (!capturing)
  {
    const auto frozen = readBytes(fixtureRoot / "native-current" / name);
    assert(frozen.size() == bytes.size());
    assert(std::equal(frozen.begin(), frozen.end(), bytes.begin()));
  }
  std::ofstream output(captureRoot / name, std::ios::binary);
  assert(output);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  assert(output);
}

Bytes stateBytes(iplug::IPlugAPIBase& plugin)
{
  iplug::IByteChunk chunk;
  assert(plugin.SerializeState(chunk));
  return {chunk.GetData(), chunk.GetData() + chunk.Size()};
}

control::LegacyState decode(std::span<const std::uint8_t> bytes, std::size_t& end)
{
  control::LegacyState state;
  assert(control::decodeLegacyState(bytes, 0, state, end) == control::LegacyStateStatus::success);
  return state;
}

void checkParameters(iplug::IPlugAPIBase& plugin)
{
  assert(plugin.NParams() == 13 && kNumParams == 13); // M0 must not expose anything.
  for (std::size_t index = 0; index < control::kLegacyParameterCount; ++index)
  {
    const auto& descriptor = control::kParameterRegistry[index];
    const auto* parameter = plugin.GetParam(static_cast<int>(index));
    assert(descriptor.hostIndex == index && descriptor.name == parameter->GetName());
    const auto type = descriptor.kind == control::ParameterKind::continuous ? iplug::IParam::kTypeDouble
                    : descriptor.kind == control::ParameterKind::boolean ? iplug::IParam::kTypeBool
                    : iplug::IParam::kTypeEnum;
    assert(parameter->Type() == type);
    assert(parameter->GetMin() == descriptor.minimum && parameter->GetMax() == descriptor.maximum);
    assert(std::bit_cast<std::uint64_t>(parameter->GetDefault()) ==
           std::bit_cast<std::uint64_t>(descriptor.defaultValue));
    assert(parameter->GetStep() == descriptor.displayStep);
    assert(descriptor.unit == parameter->GetLabel());
    assert(parameter->GetCanAutomate() && descriptor.isCurrentlyHostAutomatable);
    assert(parameter->GetStepped() == (descriptor.kind != control::ParameterKind::continuous));
    if (descriptor.kind != control::ParameterKind::continuous)
    {
      assert(static_cast<std::size_t>(parameter->NDisplayTexts()) == descriptor.enumLabels.size());
      for (std::size_t label = 0; label < descriptor.enumLabels.size(); ++label)
        assert(descriptor.enumLabels[label] == parameter->GetDisplayTextAtIdx(static_cast<int>(label)));
    }
    for (int step = 0; step <= 1024; ++step)
    {
      const double normalized = static_cast<double>(step) / 1024.;
      const double expected = control::fromNormalized(descriptor, normalized);
      const double actual = parameter->FromNormalized(normalized);
      const double error = std::abs(actual - expected);
      maximumConversionError = std::max(maximumConversionError, error);
      assert(error <= 1.e-12);
      const double inverseError = std::abs(parameter->ToNormalized(actual) -
                                            control::toNormalized(descriptor, actual));
      maximumConversionError = std::max(maximumConversionError, inverseError);
      assert(inverseError <= 1.e-12);
      ++comparisons;
    }
  }
}

void selectState(iplug::IPlugAPIBase& plugin, bool custom)
{
  constexpr std::array<double, 13> values{2.125, -63.75, 1.125, 7.875, 4.375, -5.125,
                                        0., 1., 0., 1., 15.125, 2., .375};
  for (int index = 0; index < 13; ++index)
  {
    auto* p = plugin.GetParam(index);
    p->Set(custom ? values[static_cast<std::size_t>(index)] : p->GetDefault());
  }
  plugin.OnParamReset(iplug::kPresetRecall);
  // Deliberately nonexistent, controlled paths: exercise actual legacy path
  // storage without loading user assets or depending on a workstation's files.
  stageFixturePaths(plugin, custom ? "/__Holdsworth_M0_missing__/Amps/\xc3\x89tude Amp.nam" : "",
                            custom ? "/__Holdsworth_M0_missing__/Cabs/Room cab.wav" : "");
}

void compareDecoded(iplug::IPlugAPIBase& plugin, const control::LegacyState& state)
{
  for (std::size_t index = 0; index < state.parameters.size(); ++index)
  {
    const auto* parameter = plugin.GetParam(static_cast<int>(index));
    assert(parameter->Value() == parameter->Constrain(state.parameters[index]));
  }
}

template<class Instance>
void verifyHistoricalReaders(Instance& host)
{
  std::size_t checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(fixtureRoot))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".bin") continue;
    const auto bytes = readBytes(entry.path());
    std::size_t end = 0;
    control::LegacyState state;
    assert(control::decodeLegacyState(bytes, 0, state, end) == control::LegacyStateStatus::success);
    iplug::IByteChunk chunk;
    chunk.PutBytes(bytes.data(), static_cast<int>(bytes.size()));
    assert(host.plugin().UnserializeState(chunk, 0) == static_cast<int>(end));
    compareDecoded(host.plugin(), state);
    const auto rewritten = stateBytes(host.plugin());
    std::size_t rewrittenEnd = 0;
    const auto migrated = decode(rewritten, rewrittenEnd);
    assert(migrated.modelPath == state.modelPath && migrated.irPath == state.irPath);
    assert(rewrittenEnd == rewritten.size());
    ++checked;
  }
  assert(checked == 13);
  std::printf("METRIC native historical production-reader comparisons=%zu\n", checked);
}

template<class Factory>
void verifyUnchangedAudio(Factory make)
{
  auto reference = make(), observed = make();
  assert(reference->latency() == 32 && observed->latency() == 32);
  std::array<double, 127> input{}, refLeft{}, refRight{}, left{}, right{};
  std::uint64_t sample = 0;
  for (int callback = 0; callback < 16; ++callback)
  {
    signal(input, sample, 48000., 0);
    auto bytes = stateBytes(observed->plugin());
    std::size_t end = 0;
    const auto snapshot = decode(bytes, end);
    assert(end == bytes.size() && snapshot.parameters.size() == 13);
    reference->render(input, refLeft, refRight, false);
    observed->render(input, left, right, false);
    expectBitExact(refLeft, left); expectBitExact(refRight, right);
    sample += input.size();
  }
  std::puts("PASS native M0 decoder observation: stereo audio bit-exact; latency32");
}

#if defined(M0_HOST_VST3)
class MemoryStream final : public Steinberg::IBStream
{
public:
  Bytes bytes;
  std::size_t position = 0, lastSeek = 0;
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** result) override
  { *result = nullptr; return Steinberg::kNoInterface; }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API read(void* data, Steinberg::int32 count, Steinberg::int32* actual = nullptr) override
  {
    if (count < 0) return Steinberg::kInvalidArgument;
    const auto n = std::min(static_cast<std::size_t>(count), bytes.size() - position);
    if (n) std::memcpy(data, bytes.data() + position, n);
    position += n;
    if (actual) *actual = static_cast<Steinberg::int32>(n);
    return n ? Steinberg::kResultOk : Steinberg::kResultFalse;
  }
  Steinberg::tresult PLUGIN_API write(void* data, Steinberg::int32 count, Steinberg::int32* actual = nullptr) override
  {
    if (count < 0) return Steinberg::kInvalidArgument;
    const auto n = static_cast<std::size_t>(count);
    bytes.resize(std::max(bytes.size(), position + n));
    if (n) std::memcpy(bytes.data() + position, data, n);
    position += n;
    if (actual) *actual = count;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API seek(Steinberg::int64 offset, Steinberg::int32 mode, Steinberg::int64* result = nullptr) override
  {
    const auto base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? position : bytes.size();
    const auto next = static_cast<Steinberg::int64>(base) + offset;
    if (next < 0 || static_cast<std::uint64_t>(next) > bytes.size()) return Steinberg::kInvalidArgument;
    position = static_cast<std::size_t>(next); lastSeek = position;
    if (result) *result = next;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API tell(Steinberg::int64* result) override
  { *result = static_cast<Steinberg::int64>(position); return Steinberg::kResultOk; }
};

#endif
} // namespace

int main(int argc, char** argv)
{
  assert(argc >= 3);
  fixtureRoot = argv[1]; captureRoot = argv[2];
  std::filesystem::create_directories(captureRoot);
  capturing = std::getenv("M0_CAPTURE") != nullptr;
#if defined(M0_HOST_APP)
  auto make = [] { return std::make_unique<APPInstance>(48000.); };
  auto host = make();
  checkParameters(host->plugin());
  for (bool custom : {false, true})
  {
    selectState(host->plugin(), custom);
    const auto bytes = stateBytes(host->plugin());
    std::size_t end = 0; const auto state = decode(bytes, end);
    assert(end == bytes.size()); compareDecoded(host->plugin(), state);
    retain(custom ? "app-custom.bin" : "app-default.bin", bytes);
  }
  verifyHistoricalReaders(*host); verifyUnchangedAudio(make);
#elif defined(M0_HOST_VST3)
  assert(argc >= 4);
  const auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
    reinterpret_cast<const UInt8*>(argv[3]), std::strlen(argv[3]), true);
  const auto bundle = CFBundleCreate(nullptr, url); assert(bundle); CFRelease(url);
  assert(CFBundleLoadExecutable(bundle));
  const auto enter = reinterpret_cast<bool(*)(CFBundleRef)>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
  const auto leave = reinterpret_cast<bool(*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
  const auto getFactory = reinterpret_cast<Steinberg::IPluginFactory*(*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
  assert(enter && leave && getFactory && enter(bundle));
  auto* factory = getFactory(); bool exercised = false;
  for (Steinberg::int32 index = 0; index < factory->countClasses(); ++index)
  {
    Steinberg::PClassInfo info{}; assert(factory->getClassInfo(index, &info) == Steinberg::kResultOk);
    if (std::strcmp(info.category, kVstAudioEffectClass)) continue;
    auto make = [&] { return std::make_unique<VST3Instance>(factory, info.cid, 48000.); };
    auto host = make(); checkParameters(host->plugin());
    for (bool custom : {false, true}) for (bool bypass : {false, true})
    {
      selectState(host->plugin(), custom);
      auto& wrapper = static_cast<iplug::IPlugVST3&>(host->plugin());
      iplug::IPlugLatencyTestAccess::bypass(wrapper, bypass);
      MemoryStream stream; assert(host->component->getState(&stream) == Steinberg::kResultOk);
      std::size_t end = 0; const auto state = decode(stream.bytes, end);
      assert(end + sizeof(Steinberg::int32) == stream.bytes.size());
      compareDecoded(host->plugin(), state);
      Steinberg::int32 saved = -1; std::memcpy(&saved, stream.bytes.data() + end, sizeof(saved));
      assert(saved == (bypass ? 1 : 0));
      const auto name = std::string("vst3-") + (custom ? "custom" : "default") + (bypass ? "-bypass1.bin" : "-bypass0.bin");
      retain(name, stream.bytes);
      // New reader's end must seek to the exact framework-owned int32. The
      // production SetState below independently verifies the same boundary
      // through the accepted wrapper and its existing production reader.
      assert(stream.seek(static_cast<Steinberg::int64>(end), MemoryStream::kIBSeekSet) == Steinberg::kResultOk);
      Steinberg::int32 readBypass = -1;
      assert(stream.read(&readBypass, sizeof(readBypass)) == Steinberg::kResultOk);
      assert(stream.lastSeek == end && stream.position == end + 4 && readBypass == saved);
      stream.position = 0; assert(host->component->setState(&stream) == Steinberg::kResultOk);
      assert(host->controller->getParamNormalized(iplug::kBypassParam) == (bypass ? 1. : 0.));
    }
    verifyHistoricalReaders(*host); verifyUnchangedAudio(make); exercised = true;
  }
  factory->release(); assert(leave()); CFRelease(bundle); assert(exercised);
#elif defined(M0_HOST_AU)
  assert(argc >= 4);
  const auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
    reinterpret_cast<const UInt8*>(argv[3]), std::strlen(argv[3]), true);
  const auto bundle = CFBundleCreate(nullptr, url); assert(bundle); CFRelease(url);
  assert(CFBundleLoadExecutable(bundle));
  const auto factory = reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle, CFSTR("NeuralAmpModeler_Factory")));
  AudioComponentDescription description{};
  description.componentType = kAudioUnitType_Effect;
  description.componentSubType = 0x3159456f;
  description.componentManufacturer = 0x53444161;
  description.componentFlags = kAudioComponentFlag_SandboxSafe;
  const auto component = AudioComponentRegister(&description, CFSTR("Steven Atkinson: NeuralAmpModeler"), PLUG_VERSION_HEX, factory);
  assert(component);
  auto make = [&] { return std::make_unique<AUInstance>(component, 48000.); };
  auto host = make(); checkParameters(host->plugin());
  for (bool custom : {false, true})
  {
    selectState(host->plugin(), custom);
    CFPropertyListRef state = nullptr; UInt32 size = sizeof(state);
    assert(AudioUnitGetProperty(host->unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size) == noErr);
    assert(state && CFGetTypeID(state) == CFDictionaryGetTypeID());
    const auto dict = static_cast<CFDictionaryRef>(state);
    auto number = [&] (CFStringRef key) { SInt32 value = 0; const auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, key)); assert(n && CFNumberGetValue(n, kCFNumberSInt32Type, &value)); return value; };
    assert(number(CFSTR("type")) == static_cast<SInt32>(kAudioUnitType_Effect));
    assert(number(CFSTR("subtype")) == 0x3159456f && number(CFSTR("manufacturer")) == 0x53444161);
    const auto data = static_cast<CFDataRef>(CFDictionaryGetValue(dict, CFSTR("data")));
    assert(data && CFGetTypeID(data) == CFDataGetTypeID());
    const std::span<const std::uint8_t> bytes{CFDataGetBytePtr(data), static_cast<std::size_t>(CFDataGetLength(data))};
    std::size_t end = 0; const auto decoded = decode(bytes, end);
    assert(end == bytes.size()); compareDecoded(host->plugin(), decoded);
    retain(custom ? "au-custom.bin" : "au-default.bin", bytes);
    const auto xml = CFPropertyListCreateData(nullptr, state, kCFPropertyListXMLFormat_v1_0, 0, nullptr); assert(xml);
    // plist key ordering is not a wire contract; runner canonicalizes XML keys.
    std::ofstream output(captureRoot / (custom ? "au-custom.plist" : "au-default.plist"), std::ios::binary);
    output.write(reinterpret_cast<const char*>(CFDataGetBytePtr(xml)), CFDataGetLength(xml)); output.close(); CFRelease(xml);
    assert(AudioUnitSetProperty(host->unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof(state)) == noErr);
    CFMutableDictionaryRef incompatible = CFDictionaryCreateMutableCopy(nullptr, 0, dict);
    SInt32 musicType = static_cast<SInt32>(kAudioUnitType_MusicEffect);
    const auto wrong = CFNumberCreate(nullptr, kCFNumberSInt32Type, &musicType);
    CFDictionarySetValue(incompatible, CFSTR("type"), wrong); CFRelease(wrong);
    assert(AudioUnitSetProperty(host->unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &incompatible, sizeof(incompatible)) == kAudioUnitErr_InvalidPropertyValue);
    CFRelease(incompatible); CFRelease(state);
  }
  verifyHistoricalReaders(*host); verifyUnchangedAudio(make); CFRelease(bundle);
#endif
  std::printf("PASS native M0 legacy parameter contract: conversions=%zu max_abs_error=%.17g\n", comparisons, maximumConversionError);
}
