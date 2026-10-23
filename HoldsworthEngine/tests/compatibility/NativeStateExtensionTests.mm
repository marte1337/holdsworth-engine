// M1a: exercise actual native state entry points on production Release products.
// Wire construction below is independent of the production extension codec.
#define main m1aUnusedAcceptedHostMain
#if defined(M1A_HOST_APP)
#include "../../references/tuner-v1/app-host.mm"
#elif defined(M1A_HOST_VST3)
#include "../../references/tuner-v1/vst3-host.mm"
#elif defined(M1A_HOST_AU)
#include "../../references/tuner-v1/au-host.mm"
#else
#error Select M1A_HOST_APP, M1A_HOST_VST3 or M1A_HOST_AU
#endif
#undef main
#include "../../control/ParameterRegistry.h"
#include "../../control/LegacyStateCodec.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#ifdef NDEBUG
#error Native state assertions must remain active.
#endif

namespace
{
namespace control = holdsworth::control;
#if defined(M1A_SANITIZED_STATE)
constexpr bool rtAuditEnabled = false;
#else
constexpr bool rtAuditEnabled = true;
#endif
using Bytes = std::vector<std::uint8_t>;
std::filesystem::path fixtureRoot, captureRoot;
std::size_t normalizationPoints = 0, successfulLoads = 0, rejectedLoads = 0, directOffsetChecks = 0;
std::size_t historicalLoads = 0, nativeCaptureLoads = 0, renderedFrames = 0;
std::size_t legacyFirstUseCalls = 0, extensionFirstUseCalls = 0;
double maximumNormalizationError = 0.;

Bytes readBytes(const std::filesystem::path& path)
{
  std::ifstream stream(path, std::ios::binary); assert(stream);
  return Bytes(std::istreambuf_iterator<char>{stream}, {});
}
void saveBytes(const std::filesystem::path& path, const Bytes& bytes)
{
  std::ofstream stream(path, std::ios::binary); assert(stream);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); assert(stream);
}
void u32(Bytes& bytes, std::uint32_t value)
{ for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift)); }
void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{ assert(offset + 4 <= bytes.size()); for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i)); }
void f64(Bytes& bytes, double value)
{ const auto bits = std::bit_cast<std::uint64_t>(value); for (unsigned shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(bits >> shift)); }
void stringBytes(Bytes& bytes, const std::string& value)
{ u32(bytes, static_cast<std::uint32_t>(value.size())); bytes.insert(bytes.end(), value.begin(), value.end()); }
std::uint32_t at32(const Bytes& bytes, std::size_t offset)
{ assert(offset + 4 <= bytes.size()); std::uint32_t result = 0; for (unsigned i = 0; i < 4; ++i) result |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i); return result; }
struct Record { std::uint32_t id, type; Bytes payload; };
Bytes doublePayload(double value) { Bytes result; f64(result, value); return result; }
const std::array<double, 13> defaults{0., -80., 5., 5., 5., 0., 1., 1., 1., 0., 12., 1., 0.};
const std::array<double, 13> customValues{2.125, -63.75, 1.125, 7.875, 4.375, -5.125, 0., 1., 0., 1., 15.125, 2., .375};
Bytes newState(const std::array<double, 13>& values, const std::vector<Record>& records = {},
               const std::string& model = {}, const std::string& ir = {})
{
  Bytes extension; u32(extension, static_cast<std::uint32_t>(records.size()));
  for (const auto& record : records)
  {
    u32(extension, record.id); u32(extension, record.type); u32(extension, static_cast<std::uint32_t>(record.payload.size()));
    extension.insert(extension.end(), record.payload.begin(), record.payload.end());
  }
  Bytes body; stringBytes(body, model); stringBytes(body, ir);
  for (double value : values) f64(body, value);
  u32(body, static_cast<std::uint32_t>(extension.size())); body.insert(body.end(), extension.begin(), extension.end());
  Bytes result{'H','E','N','A','M','S','T','1'}; u32(result, 1); u32(result, static_cast<std::uint32_t>(body.size()));
  result.insert(result.end(), body.begin(), body.end()); return result;
}
Bytes pluginBytes(iplug::IPlugAPIBase& plugin)
{
  iplug::IByteChunk chunk; assert(plugin.SerializeState(chunk));
  return {chunk.GetData(), chunk.GetData() + chunk.Size()};
}
std::size_t newEnd(const Bytes& bytes)
{
  constexpr std::array<std::uint8_t, 8> magic{'H','E','N','A','M','S','T','1'};
  assert(bytes.size() >= 16 && std::equal(magic.begin(), magic.end(), bytes.begin()));
  assert(at32(bytes, 8) == 1); const auto end = 16 + static_cast<std::size_t>(at32(bytes, 12)); assert(end <= bytes.size()); return end;
}

void checkParameters(iplug::IPlugAPIBase& plugin)
{
  assert(plugin.NParams() == 13 && kNumParams == 13);
  for (std::size_t index = 0; index < control::kLegacyParameterCount; ++index)
  {
    const auto& descriptor = control::kParameterRegistry[index]; const auto* p = plugin.GetParam(static_cast<int>(index));
    assert(descriptor.hostIndex == index && descriptor.name == p->GetName());
    const auto type = descriptor.kind == control::ParameterKind::continuous ? iplug::IParam::kTypeDouble
                    : descriptor.kind == control::ParameterKind::boolean ? iplug::IParam::kTypeBool : iplug::IParam::kTypeEnum;
    assert(p->Type() == type && p->GetMin() == descriptor.minimum && p->GetMax() == descriptor.maximum);
    assert(std::bit_cast<std::uint64_t>(p->GetDefault()) == std::bit_cast<std::uint64_t>(descriptor.defaultValue));
    assert(p->GetStep() == descriptor.displayStep && descriptor.unit == p->GetLabel());
    assert(p->GetCanAutomate() && p->GetStepped() == (descriptor.kind != control::ParameterKind::continuous));
    if (descriptor.kind != control::ParameterKind::continuous)
      for (std::size_t i = 0; i < descriptor.enumLabels.size(); ++i)
        assert(descriptor.enumLabels[i] == p->GetDisplayTextAtIdx(static_cast<int>(i)));
    for (int step = 0; step <= 1024; ++step)
    {
      const double normalized = static_cast<double>(step) / 1024.;
      const auto actual = p->FromNormalized(normalized);
      const auto error = std::max(std::abs(actual - control::fromNormalized(descriptor, normalized)),
        std::abs(p->ToNormalized(actual) - control::toNormalized(descriptor, actual)));
      maximumNormalizationError = std::max(maximumNormalizationError, error); assert(error <= 1.e-12); ++normalizationPoints;
    }
  }
}
void checkValues(iplug::IPlugAPIBase& plugin, const std::array<double, 13>& expected)
{ for (int i = 0; i < 13; ++i) assert(plugin.GetParam(i)->Value() == plugin.GetParam(i)->Constrain(expected[static_cast<std::size_t>(i)])); }

#if defined(M1A_HOST_VST3)
class MemoryStream final : public Steinberg::IBStream
{
public:
  Bytes bytes;
  std::size_t position = 0, lastSeek = 0;
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** out) override { *out = nullptr; return Steinberg::kNoInterface; }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
  Steinberg::tresult PLUGIN_API read(void* data, Steinberg::int32 count, Steinberg::int32* actual = nullptr) override
  {
    if (count < 0 || position > bytes.size()) return Steinberg::kInvalidArgument;
    const auto n = std::min(static_cast<std::size_t>(count), bytes.size() - position);
    if (n) std::memcpy(data, bytes.data() + position, n); position += n;
    if (actual) *actual = static_cast<Steinberg::int32>(n); return n ? Steinberg::kResultOk : Steinberg::kResultFalse;
  }
  Steinberg::tresult PLUGIN_API write(void* data, Steinberg::int32 count, Steinberg::int32* actual = nullptr) override
  {
    if (count < 0) return Steinberg::kInvalidArgument; const auto n = static_cast<std::size_t>(count);
    bytes.resize(std::max(bytes.size(), position + n)); if (n) std::memcpy(bytes.data() + position, data, n); position += n;
    if (actual) *actual = count; return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API seek(Steinberg::int64 offset, Steinberg::int32 mode, Steinberg::int64* result = nullptr) override
  {
    const auto base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? position : bytes.size();
    const auto next = static_cast<Steinberg::int64>(base) + offset;
    if (next < 0 || static_cast<std::uint64_t>(next) > bytes.size()) return Steinberg::kInvalidArgument;
    position = static_cast<std::size_t>(next); lastSeek = position; if (result) *result = next; return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API tell(Steinberg::int64* result) override { *result = static_cast<Steinberg::int64>(position); return Steinberg::kResultOk; }
};
Bytes externalState(VST3Instance& host)
{ MemoryStream stream; assert(host.component->getState(&stream) == Steinberg::kResultOk); return stream.bytes; }
bool loadExternal(VST3Instance& host, const Bytes& bytes, bool bypass = false)
{
  MemoryStream stream; stream.bytes = bytes; u32(stream.bytes, bypass ? 1U : 0U);
  const bool ok = host.component->setState(&stream) == Steinberg::kResultOk;
  if (ok) assert(host.controller->getParamNormalized(iplug::kBypassParam) == (bypass ? 1. : 0.));
  return ok;
}
double externalBypass(VST3Instance& host) { return host.controller->getParamNormalized(iplug::kBypassParam); }
#elif defined(M1A_HOST_AU)
CFPropertyListRef auState(AUInstance& host)
{
  CFPropertyListRef state = nullptr; UInt32 size = sizeof(state);
  assert(AudioUnitGetProperty(host.unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size) == noErr);
  assert(state && CFGetTypeID(state) == CFDictionaryGetTypeID()); return state;
}
Bytes externalState(AUInstance& host)
{
  const auto state = auState(host); const auto dict = static_cast<CFDictionaryRef>(state);
  auto number = [&](CFStringRef key) { SInt32 value = 0; const auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, key)); assert(n && CFNumberGetValue(n, kCFNumberSInt32Type, &value)); return value; };
  assert(number(CFSTR("type")) == static_cast<SInt32>(kAudioUnitType_Effect));
  assert(number(CFSTR("subtype")) == 0x3159456f && number(CFSTR("manufacturer")) == 0x53444161);
  const auto data = static_cast<CFDataRef>(CFDictionaryGetValue(dict, CFSTR("data"))); assert(data);
  Bytes result(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data)); CFRelease(state); return result;
}
bool loadExternal(AUInstance& host, const Bytes& bytes, bool = false)
{
  const auto old = auState(host); const auto state = CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(old));
  const auto data = CFDataCreate(nullptr, bytes.data(), static_cast<CFIndex>(bytes.size()));
  CFDictionarySetValue(state, CFSTR("data"), data); CFRelease(data); CFRelease(old);
  const auto status = AudioUnitSetProperty(host.unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof(state));
  CFRelease(state); return status == noErr;
}
double externalBypass(AUInstance& host)
{ UInt32 value = 0, size = sizeof(value); assert(AudioUnitGetProperty(host.unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &value, &size) == noErr); return value; }
void verifyAUDictionaries(AUInstance& host)
{
  auto reject = [&](CFPropertyListRef state)
  {
    const auto before=pluginBytes(host.plugin()); const auto bypass=externalBypass(host);
    assert(AudioUnitSetProperty(host.unit,kAudioUnitProperty_ClassInfo,kAudioUnitScope_Global,0,&state,sizeof(state))==kAudioUnitErr_InvalidPropertyValue);
    assert(pluginBytes(host.plugin())==before && externalBypass(host)==bypass); ++rejectedLoads;
  };
  reject(nullptr); reject(CFSTR("not a dictionary"));
  for (CFStringRef key : {CFSTR("version"),CFSTR("type"),CFSTR("subtype"),CFSTR("manufacturer"),CFSTR("name"),CFSTR("data")})
  {
    const auto original=auState(host); const auto state=CFDictionaryCreateMutableCopy(nullptr,0,static_cast<CFDictionaryRef>(original)); CFRelease(original);
    CFDictionaryRemoveValue(state,key); reject(state);
    CFDictionarySetValue(state,key,kCFBooleanTrue); reject(state); CFRelease(state);
  }
  for (const auto& name : {std::string(64,'a'),std::string(1024,'a'),std::string(63,'a')+"é"})
  {
    const auto original=auState(host); const auto state=CFDictionaryCreateMutableCopy(nullptr,0,static_cast<CFDictionaryRef>(original)); CFRelease(original);
    const auto value=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(name.data()),static_cast<CFIndex>(name.size()),kCFStringEncodingUTF8,false); assert(value);
    CFDictionarySetValue(state,CFSTR("name"),value); CFRelease(value); reject(state); CFRelease(state);
  }
  std::puts("PASS actual AU ClassInfo invalid dictionaries, missing/wrong CF types and oversized UTF-8 names reject without mutation");
}
#else
Bytes externalState(APPInstance& host) { return pluginBytes(host.plugin()); }
bool loadExternal(APPInstance& host, const Bytes& bytes, bool = false)
{
  iplug::IByteChunk chunk; chunk.PutBytes(bytes.data(), static_cast<int>(bytes.size()));
  return host.plugin().UnserializeState(chunk, 0) > 0;
}
double externalBypass(APPInstance&) { return 0.; }
#endif

template<class Host>
void requireReject(Host& host, const Bytes& bytes)
{
  const auto before = pluginBytes(host.plugin()); const double bypass = externalBypass(host); const auto latency = host.latency();
  assert(!loadExternal(host, bytes, true));
  iplug::IByteChunk direct; direct.PutBytes(bytes.data(), static_cast<int>(bytes.size()));
  assert(host.plugin().UnserializeState(direct, 0) == 0);
  assert(pluginBytes(host.plugin()) == before && externalBypass(host) == bypass && host.latency() == latency);
  ++rejectedLoads;
}
template<class Host>
void requireLoad(Host& host, const Bytes& bytes, bool bypass = false)
{ assert(loadExternal(host, bytes, bypass)); assert(host.latency() == 32 && host.plugin().NParams() == 13); ++successfulLoads; }

template<class Host>
void verifyLegacy(Host& host)
{
  for (const auto& directory : {fixtureRoot, fixtureRoot / "native-current"})
  {
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
      if (!entry.is_regular_file() || entry.path().extension() != ".bin") continue;
      auto bytes = readBytes(entry.path()); std::size_t end = 0; control::LegacyState decoded;
      assert(control::decodeLegacyState(bytes, 0, decoded, end) == control::LegacyStateStatus::success);
      bool bypass = bytes.size() >= end + 4 && at32(bytes, end) == 1;
      bytes.resize(end); requireLoad(host, bytes, bypass); checkValues(host.plugin(), decoded.parameters);
      const auto expected = newState(decoded.parameters, {}, decoded.modelPath, decoded.irPath);
      assert(pluginBytes(host.plugin()) == expected);
      iplug::IByteChunk prefix; const std::array<std::uint8_t, 7> pad{1,2,3,4,5,6,7}; prefix.PutBytes(pad.data(), 7);
      prefix.PutBytes(bytes.data(), static_cast<int>(bytes.size())); const std::uint32_t suffix = 0x55331177; prefix.Put(&suffix);
      assert(host.plugin().UnserializeState(prefix, 7) == static_cast<int>(7 + end)); ++directOffsetChecks;
      if (directory == fixtureRoot) ++historicalLoads; else ++nativeCaptureLoads;
    }
  }
  assert(historicalLoads == 13 && nativeCaptureLoads == 8);
  std::puts("PASS native historical reconstructed fixtures=13 and actual accepted native payloads=8 (no saved DAW project claim)");
}

std::vector<Bytes> malformedStates(const Bytes& populated)
{
  std::vector<Bytes> cases;
  auto mutate = [&](std::size_t offset, std::uint32_t value) { auto copy = populated; put32(copy, offset, value); cases.push_back(std::move(copy)); };
  mutate(8, 0); mutate(8, 2); mutate(12, 0); mutate(12, 0xffffffffU);
  mutate(12, static_cast<std::uint32_t>(populated.size() - 17));
  mutate(12, static_cast<std::uint32_t>(populated.size() - 15));
  mutate(16, 0xffffffffU); mutate(16, 1048577); mutate(20, 0xffffffffU);
  // Empty-path new body: values start24, extensionBytes128, recordCount132,
  // first record id136/type140/length144/payload148.
  mutate(128, 0); mutate(128, 262145); mutate(132, 1025); mutate(132, 0);
  mutate(136, 0); mutate(136, 0x10000000U); mutate(140, 0); mutate(144, 7); mutate(144, 0xffffffffU);
  auto nonfinite = populated; const auto infinity = doublePayload(std::numeric_limits<double>::infinity());
  std::copy(infinity.begin(), infinity.end(), nonfinite.begin() + 24); cases.push_back(nonfinite);
  nonfinite = populated; std::copy(infinity.begin(), infinity.end(), nonfinite.begin() + 148); cases.push_back(nonfinite);
  cases.push_back(newState(customValues, {{0x30000000U,1,doublePayload(1.)},{0x30000000U,7,{1,2}}}));
  cases.push_back(newState(customValues, {}, std::string("bad\0path",8)));
  auto extra = populated; extra.push_back(0xff); put32(extra,12,static_cast<std::uint32_t>(extra.size()-16)); cases.push_back(extra);
  auto oversized = populated; oversized.resize(2U*1024U*1024U+256U*1024U+257U,0); put32(oversized,12,static_cast<std::uint32_t>(oversized.size()-16)); cases.push_back(std::move(oversized));
  return cases;
}

template<class Host>
void verifyExtension(Host& host)
{
  const auto empty = newState(defaults); assert(empty.size() == 136);
  requireLoad(host, empty); assert(pluginBytes(host.plugin()) == empty); checkValues(host.plugin(), defaults);
  // IDs already reserved by M0 and an unknown future ID are all opaque in M1a.
  // Their presence must not expose a parameter or change today's DSP controls.
  const std::vector<Record> records{{0x30000000U,1,doublePayload(1.)}, {0x30000007U,1,doublePayload(10.)}, {0xf0000001U,77,{0,255,72,69,0}}};
  const auto populated = newState(customValues, records);
  for (bool bypass : {false,true})
  {
    requireLoad(host,populated,bypass); checkValues(host.plugin(),customValues); assert(pluginBytes(host.plugin()) == populated);
    const auto native = externalState(host); assert(newEnd(native) == populated.size());
#if defined(M1A_HOST_VST3)
    // Native framework write includes processor bypass (host normal delivery is
    // independent of controller restore). Exercise both explicit framework states.
    auto& wrapper = static_cast<iplug::IPlugVST3&>(host.plugin()); iplug::IPlugLatencyTestAccess::bypass(wrapper,bypass);
    const auto actual = externalState(host); assert(actual.size() == populated.size()+4 && at32(actual,populated.size()) == (bypass?1U:0U));
    MemoryStream stream; stream.bytes=actual; stream.bytes.insert(stream.bytes.end(),{0xde,0xad,0xbe,0xef});
    assert(host.component->setState(&stream)==Steinberg::kResultOk);
    assert(host.controller->getParamNormalized(iplug::kBypassParam)==(bypass?1.:0.));
    saveBytes(captureRoot/(bypass?"new-populated-bypass1.bin":"new-populated-bypass0.bin"),actual);
#else
    assert(native==populated); saveBytes(captureRoot/"new-populated.bin",native);
#endif
  }
#if defined(M1A_HOST_VST3)
  // A complete plugin body without all four framework bypass bytes must fail
  // before changing parameters, paths, unknown records or controller bypass.
  for (std::size_t suffix=0;suffix<4;++suffix)
  {
    MemoryStream stream; stream.bytes=populated; stream.bytes.insert(stream.bytes.end(),suffix,0);
    const auto before=pluginBytes(host.plugin()); const auto bypass=externalBypass(host);
    assert(host.component->setState(&stream)!=Steinberg::kResultOk);
    assert(pluginBytes(host.plugin())==before && externalBypass(host)==bypass); ++rejectedLoads;
  }
  for (std::uint32_t value : {2U,0xffffffffU})
  {
    MemoryStream stream; stream.bytes=populated; u32(stream.bytes,value);
    const auto before=pluginBytes(host.plugin()); const auto bypass=externalBypass(host);
    assert(host.component->setState(&stream)!=Steinberg::kResultOk);
    assert(pluginBytes(host.plugin())==before && externalBypass(host)==bypass); ++rejectedLoads;
  }
#endif
  // Full validated state sits after arbitrary caller prefix and before suffix.
  for (int offset : {0,1,7,64})
  {
    Bytes bytes(static_cast<std::size_t>(offset),0xa5); bytes.insert(bytes.end(),populated.begin(),populated.end()); bytes.insert(bytes.end(),{1,0,0,0,9,8,7});
    iplug::IByteChunk chunk; chunk.PutBytes(bytes.data(),static_cast<int>(bytes.size()));
    assert(host.plugin().UnserializeState(chunk,offset)==static_cast<int>(offset+populated.size())); ++directOffsetChecks;
  }
  for (std::size_t count=0;count<populated.size();++count) requireReject(host,Bytes(populated.begin(),populated.begin()+static_cast<std::ptrdiff_t>(count)));
  for (const auto& malformed:malformedStates(populated)) requireReject(host,malformed);
  // Historical invalid/truncated data cannot reset an already-retained extension.
  for (const auto& name : {"headerless-pre-0710.plugin.bin","header-0.7.10.plugin.bin","header-0.7.12.plugin.bin","header-0.8.0.plugin.bin"})
  {
    const auto old=readBytes(fixtureRoot/name);
    for(std::size_t n=0;n<old.size();++n) requireReject(host,Bytes(old.begin(),old.begin()+static_cast<std::ptrdiff_t>(n)));
  }
  auto badLegacy=readBytes(fixtureRoot/"header-0.8.0.plugin.bin"); put32(badLegacy,0,0xffffffffU); requireReject(host,badLegacy);
  for (int offset : {-1,static_cast<int>(populated.size()+1)})
  {
    iplug::IByteChunk chunk; chunk.PutBytes(populated.data(),static_cast<int>(populated.size())); const auto before=pluginBytes(host.plugin());
    assert(host.plugin().UnserializeState(chunk,offset)==0 && pluginBytes(host.plugin())==before); ++rejectedLoads;
  }
  // A subsequent ordinary legacy recall deliberately removes unknown new data.
  requireLoad(host,readBytes(fixtureRoot/"native-current/app-default.bin")); assert(pluginBytes(host.plugin())==empty);
  saveBytes(captureRoot/"new-empty.bin",pluginBytes(host.plugin()));
  std::puts("PASS native new empty/populated/unknown extension roundtrips; transactional rejection and exact offsets");
}

template<class Factory>
void verifyAudio(Factory make)
{
  const auto old = readBytes(fixtureRoot/"native-current/app-default.bin");
  const auto populated = newState(defaults,{{0x30000000U,1,doublePayload(1.)},{0x30000007U,1,doublePayload(10.)},{0xf0000001U,3,{1,2,3}}});
  for(double rate:tunerRates)
  {
    auto reference=make(rate), observed=make(rate); requireLoad(*reference,old); requireLoad(*observed,populated);
    assert(reference->latency()==32 && observed->latency()==32);
    std::array<double,1024> input{},refL{},refR{},outL{},outR{}; std::uint64_t sample=0;
    // Accepted NAM scratch/filter storage allocates on its first callback.
    // Measure both states separately, require equal first-use counts, and retain
    // them explicitly before auditing subsequent callbacks (as in tuner v1).
    const auto beforeWarm = rtViolations.load();
    reference->render(input,refL,refR,false);
    const auto referenceWarm = rtViolations.load() - beforeWarm;
    observed->render(input,outL,outR,false);
    const auto observedWarm = rtViolations.load() - beforeWarm - referenceWarm;
    assert(referenceWarm == observedWarm);
    legacyFirstUseCalls += referenceWarm; extensionFirstUseCalls += observedWarm;
    if (rtAuditEnabled) std::printf("METRIC first_use_rt rate=%.0f legacy=%u extension=%u\n",rate,referenceWarm,observedWarm);
    expectBitExact(refL,outL); expectBitExact(refR,outR);
    rtViolations.store(beforeWarm);
    for(int repeat=0;repeat<4;++repeat) for(int block:{1,2,37,127,1024})
    {
      const auto n=static_cast<std::size_t>(block); auto in=std::span(input).first(n);
      signal(in,sample,rate,repeat%3); const auto saved=externalState(*observed); assert(newEnd(saved)==populated.size());
      requireReject(*observed,Bytes(populated.begin(),populated.end()-1));
      reference->render(in,std::span(refL).first(n),std::span(refR).first(n),false);
      observed->render(in,std::span(outL).first(n),std::span(outR).first(n),false);
      expectBitExact(std::span(refL).first(n),std::span(outL).first(n)); expectBitExact(std::span(refR).first(n),std::span(outR).first(n));
      sample+=n; renderedFrames+=n;
    }
  }
  assert(rtViolations.load()==0);
  std::puts("PASS native six-rate irregular/one-sample stereo output bit-exact, opaque EQ IDs inactive, latency32");
  if (rtAuditEnabled) std::puts("PASS zero audited RT calls after matched legacy first use");
}

template<class Factory>
void verifyRealAssets(Factory make)
{
  const auto assets = fixtureRoot.parent_path().parent_path().parent_path()/"tuner-v1/fixtures";
  const auto model = std::filesystem::absolute(assets/"linear-half-48k.nam").lexically_normal().string();
  const auto ir = std::filesystem::absolute(assets/"colored-48k.wav").lexically_normal().string();
  assert(std::filesystem::is_regular_file(model) && std::filesystem::is_regular_file(ir));
  Bytes old; stringBytes(old,"###NeuralAmpModeler###"); stringBytes(old,"0.8.0");
  stringBytes(old,model); stringBytes(old,ir); for (double value:defaults) f64(old,value);
  const auto current=newState(defaults,{{0xf0000042U,77,{8,7,6}}},model,ir);
  auto reference=make(48000.), observed=make(48000.);
  requireLoad(*reference,old); requireLoad(*observed,current);
  assert(pluginBytes(observed->plugin())==current);
  std::array<double,1024> input{},refL{},refR{},outL{},outR{};
  const auto before=rtViolations.load();
  reference->render(input,refL,refR,false); const auto first=rtViolations.load()-before;
  observed->render(input,outL,outR,false); const auto second=rtViolations.load()-before-first;
  assert(first==second); rtViolations.store(before);
  if (rtAuditEnabled) std::printf("METRIC real_asset_first_use_rt legacy=%u extension=%u\n",first,second);
  for (bool bypass:{false,true}) for (int block:{1,37,127,1024})
  {
    const auto n=static_cast<std::size_t>(block); auto in=std::span(input).first(n); signal(in,renderedFrames,48000.,0);
    reference->render(in,std::span(refL).first(n),std::span(refR).first(n),bypass);
    observed->render(in,std::span(outL).first(n),std::span(outR).first(n),bypass);
    expectBitExact(std::span(refL).first(n),std::span(outL).first(n)); expectBitExact(std::span(refR).first(n),std::span(outR).first(n));
    assert(reference->latency()==32 && observed->latency()==32); renderedFrames+=n;
  }
  assert(rtViolations.load()==before);
  std::puts("PASS real NAM/IR paths retained, old/new state renders bit-exact through normal and host bypass, latency32");
}

template<class Host,class Factory>
void runTests(Host& host,Factory make)
{ checkParameters(host.plugin()); assert(pluginBytes(host.plugin())==newState(defaults)); verifyLegacy(host); verifyExtension(host); verifyAudio(make); verifyRealAssets(make); }

#if defined(M1A_HOST_APP)
void verifyConcurrentStateCallbacks()
{
  APPInstance host(48000.);
  const auto a=newState(defaults,{{0xf0000001U,2,{1,1,1}},{0xf0000002U,2,{1,1}}});
  const auto b=newState(customValues,{{0xf0000001U,2,{2,2,2}},{0xf0000002U,2,{2,2}}});
  assert(loadExternal(host,a));
  std::atomic<bool> ready{false};
  std::thread writer([&] {
    while(!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    for(int i=0;i<1000;++i) assert(loadExternal(host,(i%2)?a:b));
  });
  std::thread reader([&] {
    ready.store(true,std::memory_order_release);
    for(int i=0;i<1000;++i) { const auto saved=externalState(host); assert(saved==a || saved==b); }
  });
  writer.join(); reader.join();
  std::puts("PASS actual APP state-callback ownership: 1000 recalls / 1000 saves, whole snapshots; concurrent automation/path editing excluded");
}
#endif
} // namespace

int main(int argc,char** argv)
{
  assert(argc>=3); fixtureRoot=argv[1]; captureRoot=argv[2]; std::filesystem::create_directories(captureRoot);
  if (rtAuditEnabled) auditSelfTest();
  else std::puts("SANITIZER: functional state/audio tests active; RT interposer tested separately in Release, not combined with sanitizer interceptors");
#if defined(M1A_HOST_APP)
  auto make=[](double rate){return std::make_unique<APPInstance>(rate);}; auto host=make(48000.); runTests(*host,make); verifyConcurrentStateCallbacks();
#elif defined(M1A_HOST_VST3)
  assert(argc>=4); const auto url=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(argv[3]),std::strlen(argv[3]),true);
  const auto bundle=CFBundleCreate(nullptr,url); assert(bundle); CFRelease(url); assert(CFBundleLoadExecutable(bundle));
  const auto enter=reinterpret_cast<bool(*)(CFBundleRef)>(CFBundleGetFunctionPointerForName(bundle,CFSTR("bundleEntry")));
  const auto leave=reinterpret_cast<bool(*)()>(CFBundleGetFunctionPointerForName(bundle,CFSTR("bundleExit")));
  const auto getFactory=reinterpret_cast<Steinberg::IPluginFactory*(*)()>(CFBundleGetFunctionPointerForName(bundle,CFSTR("GetPluginFactory")));
  assert(enter&&leave&&getFactory&&enter(bundle)); auto* factory=getFactory(); bool exercised=false;
  for(Steinberg::int32 i=0;i<factory->countClasses();++i)
  {
    Steinberg::PClassInfo info{}; assert(factory->getClassInfo(i,&info)==Steinberg::kResultOk); if(std::strcmp(info.category,kVstAudioEffectClass))continue;
    auto make=[&](double rate){return std::make_unique<VST3Instance>(factory,info.cid,rate);}; auto host=make(48000.); runTests(*host,make); exercised=true;
  }
  factory->release(); assert(leave()); CFRelease(bundle); assert(exercised);
#elif defined(M1A_HOST_AU)
  assert(argc>=4); const auto url=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(argv[3]),std::strlen(argv[3]),true);
  const auto bundle=CFBundleCreate(nullptr,url); assert(bundle); CFRelease(url); assert(CFBundleLoadExecutable(bundle));
  const auto factory=reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle,CFSTR("NeuralAmpModeler_Factory"))); assert(factory);
  AudioComponentDescription description{}; description.componentType=kAudioUnitType_Effect; description.componentSubType=0x3159456f; description.componentManufacturer=0x53444161; description.componentFlags=kAudioComponentFlag_SandboxSafe;
  const auto component=AudioComponentRegister(&description,CFSTR("Steven Atkinson: NeuralAmpModeler"),PLUG_VERSION_HEX,factory); assert(component);
  auto make=[&](double rate){return std::make_unique<AUInstance>(component,rate);}; auto host=make(48000.); runTests(*host,make);
  verifyAUDictionaries(*host);
  // Real accepted AU ClassInfo plist captures retain their original component IDs.
  for(const auto* name:{"au-default.plist","au-custom.plist"})
  {
    const auto bytes=readBytes(fixtureRoot/"native-current"/name); const auto data=CFDataCreate(nullptr,bytes.data(),static_cast<CFIndex>(bytes.size()));
    const auto state=CFPropertyListCreateWithData(nullptr,data,kCFPropertyListImmutable,nullptr,nullptr); assert(state); CFRelease(data);
    assert(AudioUnitSetProperty(host->unit,kAudioUnitProperty_ClassInfo,kAudioUnitScope_Global,0,&state,sizeof(state))==noErr);
    checkValues(host->plugin(),std::strcmp(name,"au-default.plist")==0?defaults:customValues); CFRelease(state); ++nativeCaptureLoads;
  }
  const auto original=auState(*host); const auto wrong=CFDictionaryCreateMutableCopy(nullptr,0,static_cast<CFDictionaryRef>(original)); CFRelease(original);
  SInt32 aumf=static_cast<SInt32>(kAudioUnitType_MusicEffect); const auto type=CFNumberCreate(nullptr,kCFNumberSInt32Type,&aumf); CFDictionarySetValue(wrong,CFSTR("type"),type); CFRelease(type);
  const auto before=pluginBytes(host->plugin()); assert(AudioUnitSetProperty(host->unit,kAudioUnitProperty_ClassInfo,kAudioUnitScope_Global,0,&wrong,sizeof(wrong))==kAudioUnitErr_InvalidPropertyValue);
  assert(pluginBytes(host->plugin())==before); CFRelease(wrong); CFRelease(bundle);
#endif
  std::printf("PASS M1a native state: normalization_points=%zu max_abs_error=%.17g successful_loads=%zu transactional_rejections=%zu exact_direct_offsets=%zu historical_reconstructions=%zu native_capture_loads=%zu bitexact_stereo_frames=%zu latency=32 rt_audit_enabled=%d rt_violations=%u\n",normalizationPoints,maximumNormalizationError,successfulLoads,rejectedLoads,directOffsetChecks,historicalLoads,nativeCaptureLoads,renderedFrames,static_cast<int>(rtAuditEnabled),rtViolations.load());
}
