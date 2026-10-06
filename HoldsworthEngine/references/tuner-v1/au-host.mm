// Registers the supplied actual AU factory only inside this validation process.
#include "host-common.h"
#include "host-acceptance.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

thread_local bool inAudio = false;
std::atomic<unsigned> rtViolations{0};

struct AUInstance
{
  explicit AUInstance(const AudioComponent component, const double rate)
  : sampleRate(rate)
  {
    assert(AudioComponentInstanceNew(component, &unit) == noErr);
    UInt32 size = sizeof(api);
    assert(AudioUnitGetProperty(unit, iplug::kIPlugObjectPropertyID,
      kAudioUnitScope_Global, 0, &api, &size) == noErr && api);
    AudioStreamBasicDescription format{};
    format.mSampleRate = rate; format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerPacket = sizeof(float); format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float); format.mBitsPerChannel = 32;
    format.mChannelsPerFrame = 1;
    assert(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
      kAudioUnitScope_Input, 0, &format, sizeof(format)) == noErr);
    format.mChannelsPerFrame = 2;
    assert(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
      kAudioUnitScope_Output, 0, &format, sizeof(format)) == noErr);
    UInt32 maximum = tunerMaximumBlock;
    assert(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
      kAudioUnitScope_Global, 0, &maximum, sizeof(maximum)) == noErr);
    AURenderCallbackStruct callback{inputCallback, this};
    assert(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
      kAudioUnitScope_Input, 0, &callback, sizeof(callback)) == noErr);
    assert(AudioUnitInitialize(unit) == noErr);
    assert(latency() == 32);
  }
  ~AUInstance()
  {
    plugin().OnUIClose();
    assert(AudioUnitUninitialize(unit) == noErr);
    assert(AudioComponentInstanceDispose(unit) == noErr);
  }
  iplug::IPlugAPIBase& plugin() { return *api; }
  unsigned latency() const
  {
    Float64 seconds = 0.; UInt32 size = sizeof(seconds);
    assert(AudioUnitGetProperty(unit, kAudioUnitProperty_Latency,
      kAudioUnitScope_Global, 0, &seconds, &size) == noErr);
    return static_cast<unsigned>(std::llround(seconds * sampleRate));
  }
  static OSStatus inputCallback(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                               UInt32, const UInt32 frames, AudioBufferList* list)
  {
    auto& host = *static_cast<AUInstance*>(context);
    assert(inAudio && frames <= host.input.size() && list->mNumberBuffers == 1);
    std::memcpy(list->mBuffers[0].mData, host.input.data(), frames * sizeof(float));
    list->mBuffers[0].mDataByteSize = frames * sizeof(float);
    return noErr;
  }
  void render(std::span<double> source, std::span<double> left, std::span<double> right, const bool bypass)
  {
    UInt32 bypassValue = bypass ? 1 : 0;
    assert(AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect,
      kAudioUnitScope_Global, 0, &bypassValue, sizeof(bypassValue)) == noErr);
    for (std::size_t i = 0; i < source.size(); ++i) input[i] = static_cast<float>(source[i]);
    struct TwoBufferList { UInt32 number; AudioBuffer buffers[2]; } list{};
    list.number = 2;
    for (int i = 0; i < 2; ++i)
    {
      list.buffers[i].mNumberChannels = 1;
      list.buffers[i].mDataByteSize = static_cast<UInt32>(source.size() * sizeof(float));
      list.buffers[i].mData = output[i].data();
    }
    AudioTimeStamp stamp{};
    stamp.mFlags = kAudioTimeStampSampleTimeValid;
    stamp.mSampleTime = static_cast<Float64>(sampleTime);
    AudioUnitRenderActionFlags flags = 0;
    {
      AudioScope audio;
      assert(AudioUnitRender(unit, &flags, &stamp, 0, static_cast<UInt32>(source.size()),
        reinterpret_cast<AudioBufferList*>(&list)) == noErr);
    }
    sampleTime += source.size();
    for (std::size_t i = 0; i < source.size(); ++i)
    { left[i] = output[0][i]; right[i] = output[1][i]; }
  }
  AudioUnit unit = nullptr;
  iplug::IPlugAPIBase* api = nullptr;
  double sampleRate;
  std::uint64_t sampleTime = 0;
  std::array<float, tunerMaximumBlock> input{};
  std::array<std::array<float, tunerMaximumBlock>, 2> output{};
};

int main(int argc, char** argv)
{
  assert(argc >= 2);
  auditSelfTest();
  const auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
    reinterpret_cast<const UInt8*>(argv[1]), std::strlen(argv[1]), true);
  const auto bundle = CFBundleCreate(nullptr, url); assert(bundle); CFRelease(url);
  assert(CFBundleLoadExecutable(bundle));
  const auto factory = reinterpret_cast<AudioComponentFactoryFunction>(
    CFBundleGetFunctionPointerForName(bundle, CFSTR("NeuralAmpModeler_Factory")));
  assert(factory);
  AudioComponentDescription description{};
  description.componentType = kAudioUnitType_Effect;
  description.componentSubType = 0x3159456f; // PLUG_UNIQUE_ID: '1YEo'
  description.componentManufacturer = 0x53444161; // PLUG_MFR_ID: 'SDAa'
  // Match the real bundle's sandboxSafe declaration. AudioComponentRegister
  // explicitly requires this flag for direct loading in a sandboxed process.
  description.componentFlags = kAudioComponentFlag_SandboxSafe;
  const auto component = AudioComponentRegister(&description, CFSTR("Steven Atkinson: NeuralAmpModeler"),
    PLUG_VERSION_HEX, factory);
  assert(component);
  runHostAcceptance("AU", [&] (double rate) { return std::make_unique<AUInstance>(component, rate); }, argc > 2);
  assert(rtViolations.load() == 0);
  if (argc <= 2) std::puts("PASS: actual Release AU tuner acceptance");
  // The in-process registrar owns the factory pointer for the process lifetime.
  CFRelease(bundle);
}
