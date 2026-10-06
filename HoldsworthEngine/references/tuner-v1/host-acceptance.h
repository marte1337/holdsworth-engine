#pragma once
#include "host-common.h"

inline void developmentControl(iplug::IPlugAPIBase& plugin, const int message, const int control, double value)
{
  assert(!inAudio);
  assert(plugin.OnMessage(message, control, sizeof(value), &value));
}

inline void enableTuner(iplug::IPlugAPIBase& plugin, const bool enabled)
{ developmentControl(plugin, kMsgTagTunerEnabled, kCtrlTagTunerEnabled, enabled ? 1. : 0.); }

inline void dryControls(iplug::IPlugAPIBase& plugin)
{
  for (const int parameter : {kNoiseGateActive, kEQActive, kCalibrateInput})
  { plugin.GetParam(parameter)->Set(0.); plugin.OnParamChange(parameter); }
  developmentControl(plugin, kMsgTagHoldsworthDelayEnabled, kCtrlTagHoldsworthDelayEnabled, 0.);
  developmentControl(plugin, kMsgTagPreNAMProcessor, kCtrlTagPreNAMProcessor, 0.);
  enableTuner(plugin, false);
  plugin.OnUIOpen(); // Exercise real lifecycle without creating or inspecting UI.
}

inline const holdsworth::dsp::TunerPitchEstimate& estimate(iplug::IPlugAPIBase& plugin)
{ return static_cast<NeuralAmpModeler&>(plugin).DevelopmentTunerEstimate(); }
inline std::uint64_t analysisCount(iplug::IPlugAPIBase& plugin)
{ return static_cast<NeuralAmpModeler&>(plugin).DevelopmentTunerAnalysisCount(); }
inline const auto& display(iplug::IPlugAPIBase& plugin)
{ return static_cast<NeuralAmpModeler&>(plugin).DevelopmentTunerResult(); }

inline void stageFixturePaths(iplug::IPlugAPIBase& plugin, const char* model, const char* ir)
{
  assert(!inAudio);
  iplug::IByteChunk oldState, replacement;
  assert(plugin.SerializeState(oldState));
  WDL_String header, version, oldModel, oldIR;
  int pos = oldState.GetStr(header, 0);
  pos = oldState.GetStr(version, pos);
  pos = oldState.GetStr(oldModel, pos);
  pos = oldState.GetStr(oldIR, pos);
  assert(pos >= 0 && pos <= oldState.Size());
  replacement.PutStr(header.Get()); replacement.PutStr(version.Get());
  replacement.PutStr(model); replacement.PutStr(ir);
  replacement.PutBytes(oldState.GetData() + pos, oldState.Size() - pos);
  assert(plugin.UnserializeState(replacement, 0) == replacement.Size());
}

template <typename Instance>
void pumpTone(Instance& host, const double rate, const double frequency, const double seconds,
              const bool bypass = false)
{
  std::array<double, tunerMaximumBlock> input{}, left{}, right{};
  std::uint64_t at = 0;
  const auto total = static_cast<std::uint64_t>(std::ceil(seconds * rate));
  const auto hop = static_cast<std::uint64_t>(std::round(.025 * rate));
  while (at < total)
  {
    const auto end = std::min(total, at + hop);
    while (at < end)
    {
      const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(tunerMaximumBlock, end - at));
      for (std::size_t i = 0; i < frames; ++i)
        input[i] = .15 * std::sin(2. * std::numbers::pi * frequency * static_cast<double>(at + i) / rate);
      host.render(std::span{input}.first(frames), std::span{left}.first(frames), std::span{right}.first(frames), bypass);
      at += frames;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    host.plugin().OnIdle();
  }
}

template <typename Factory>
void runHostAcceptance(const char* format, Factory&& make, const bool benchmark)
{
  if (benchmark) std::puts("format,rate,block,mode,p50_us,p99_us,max_us,deadline_us");
  for (const double rate : tunerRates)
  {
    auto reference = make(rate), observed = make(rate);
    dryControls(reference->plugin()); dryControls(observed->plugin());
    std::array<double, tunerMaximumBlock> input{}, refLeft{}, refRight{}, left{}, right{};
    signal(input, 0, rate, 0);
    // Legacy NAM scratch/filter storage allocates on first use. Warm disabled
    // whole-plugin rendering before checking tuner enable and all later calls.
    const auto violationsBeforeWarmup = rtViolations.load();
    reference->render(input, refLeft, refRight, false);
    observed->render(input, left, right, false);
    rtViolations.store(violationsBeforeWarmup);

    if (benchmark)
    {
      for (const int block : tunerBlocks)
      {
        for (const bool enabled : {false, true})
        {
          enableTuner(observed->plugin(), enabled);
          auto render = [&] {
            observed->render(std::span{input}.first(block), std::span{left}.first(block),
                             std::span{right}.first(block), false);
          };
          // Full-plugin measurements include callbacks under control-thread
          // servicing. A separate direct-capture benchmark drains every call.
          measureCallbacks(format, rate, block, enabled ? "enabled" : "disabled", render,
                           [&] { observed->plugin().OnIdle(); });
        }
      }
      continue;
    }

    std::uint64_t sample = 0;
    for (const int block : tunerBlocks)
    {
      for (const bool bypass : {false, true})
      {
        for (int fixture = 0; fixture < 3; ++fixture)
        {
          for (int callback = 0; callback < 32; ++callback)
          {
            enableTuner(observed->plugin(), callback % 7 != 0 || (fixture == 1 && callback == 0));
            const auto fixtureSample = fixture == 1
              ? static_cast<std::uint64_t>(callback) * static_cast<std::uint64_t>(block) : sample;
            signal(std::span{input}.first(block), fixtureSample, rate, fixture);
            reference->render(std::span{input}.first(block), std::span{refLeft}.first(block),
                              std::span{refRight}.first(block), bypass);
            observed->render(std::span{input}.first(block), std::span{left}.first(block),
                             std::span{right}.first(block), bypass);
            expectBitExact(std::span{left}.first(block), std::span{refLeft}.first(block));
            expectBitExact(std::span{right}.first(block), std::span{refRight}.first(block));
            assert(reference->latency() == 32 && observed->latency() == 32);
            sample += block;
          }
        }
      }
    }
    if (rate == 48000.)
    {
      for (int route = 0; route < 4; ++route)
      {
        for (auto* host : {reference.get(), observed.get()})
          developmentControl(host->plugin(), kMsgTagPreNAMProcessor, kCtrlTagPreNAMProcessor,
                             static_cast<double>(route) / 3.);
        for (int block : {64, 128, 1024}) for (bool bypass : {false, true})
          for (int callback = 0; callback < 24; ++callback)
          {
            enableTuner(observed->plugin(), callback % 7 != 0);
            signal(std::span{input}.first(block), sample, rate, 0);
            reference->render(std::span{input}.first(block), std::span{refLeft}.first(block),
                              std::span{refRight}.first(block), bypass);
            observed->render(std::span{input}.first(block), std::span{left}.first(block),
                             std::span{right}.first(block), bypass);
            expectBitExact(std::span{left}.first(block), std::span{refLeft}.first(block));
            expectBitExact(std::span{right}.first(block), std::span{refRight}.first(block));
            assert(reference->latency() == 32 && observed->latency() == 32);
            sample += block;
          }
      }
      std::printf("PASS %s selected-pedal on/off arithmetic: Off/TC/MC/AH, 48k, blocks64/128/1024, normal/bypass\n", format);
    }
    enableTuner(observed->plugin(), false);
    observed->plugin().OnIdle();
    enableTuner(observed->plugin(), true);
    observed->plugin().OnIdle();
    assert(estimate(observed->plugin()).status == holdsworth::dsp::TunerPitchStatus::collecting);
    const auto before = analysisCount(observed->plugin());
    pumpTone(*observed, rate, 110., .45);
    const auto pitch = estimate(observed->plugin());
    assert(analysisCount(observed->plugin()) > before);
    assert(pitch.status == holdsworth::dsp::TunerPitchStatus::valid && pitch.highConfidence);
    assert(pitch.midiNote == 45 && std::abs(pitch.cents) <= 2.);
    const auto shown = display(observed->plugin());
    assert(shown.status == decltype(shown.status)::stable && shown.midiNote == 45);
    assert(shown.octave == 2 && std::abs(shown.wholeCents) <= 3);

    // Raw input placement remains independent of trim and nonlinear pedal DSP.
    observed->plugin().GetParam(kInputLevel)->Set(18.);
    observed->plugin().OnParamChange(kInputLevel);
    developmentControl(observed->plugin(), kMsgTagPreNAMProcessor, kCtrlTagPreNAMProcessor, 1.);
    developmentControl(observed->plugin(), kMsgTagAHDriveEnabled, kCtrlTagAHDriveEnabled, 1.);
    developmentControl(observed->plugin(), kMsgTagAHDriveGain, kCtrlTagAHDriveGain, 1.);
    pumpTone(*observed, rate, 110., .15);
    const auto changed = estimate(observed->plugin());
    assert(changed.status == holdsworth::dsp::TunerPitchStatus::valid && changed.midiNote == 45);
    assert(std::abs(changed.cents) <= 2. && std::abs(changed.rmsDbFS - pitch.rmsDbFS) <= .5);
    assert(observed->latency() == 32);

    // Simulate a blocked control thread followed by stopped audio: queued
    // samples must not regain validity merely because OnIdle runs again.
    const auto stalledCount = analysisCount(observed->plugin());
    for (std::uint64_t at = 0, total = static_cast<std::uint64_t>(std::ceil(.2 * rate)); at < total;)
    {
      const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(input.size(), total - at));
      signal(std::span{input}.first(frames), at, rate, 0);
      observed->render(std::span{input}.first(frames), std::span{left}.first(frames),
                       std::span{right}.first(frames), false);
      at += frames;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    observed->plugin().OnIdle();
    assert(analysisCount(observed->plugin()) == stalledCount);
    assert(estimate(observed->plugin()).status == holdsworth::dsp::TunerPitchStatus::collecting);
    assert(display(observed->plugin()).status != decltype(shown.status)::stable);
    observed->plugin().OnIdle();
    assert(analysisCount(observed->plugin()) == stalledCount);
    pumpTone(*observed, rate, 110., .35);
    assert(estimate(observed->plugin()).status == holdsworth::dsp::TunerPitchStatus::valid);
    assert(display(observed->plugin()).status == decltype(shown.status)::stable);

    const char* model = std::getenv("TUNER_MODEL_FIXTURE");
    const char* ir = std::getenv("TUNER_IR_FIXTURE");
    if (rate == 48000. && model && ir)
    {
      auto fixtureReference = make(rate), fixtureObserved = make(rate);
      for (auto* host : {fixtureReference.get(), fixtureObserved.get()})
      {
        dryControls(host->plugin());
        stageFixturePaths(host->plugin(), model, ir);
      }
      // Existing model/IR staging has its own first-use storage; the loaded
      // pipeline is warmed separately, then all tuner comparisons stay audited.
      const auto beforeFixtureWarmup = rtViolations.load();
      signal(input, 0, rate, 0);
      fixtureReference->render(input, refLeft, refRight, false);
      fixtureObserved->render(input, left, right, false);
      rtViolations.store(beforeFixtureWarmup);
      for (int callback = 0; callback < 48; ++callback)
      {
        enableTuner(fixtureObserved->plugin(), callback % 7 != 0);
        signal(input, sample, rate, 0);
        fixtureReference->render(input, refLeft, refRight, false);
        fixtureObserved->render(input, left, right, false);
        expectBitExact(left, refLeft); expectBitExact(right, refRight);
        sample += input.size();
        fixtureReference->plugin().OnIdle(); fixtureObserved->plugin().OnIdle();
      }
      double squareSum = 0.;
      for (double value : left) squareSum += value * value;
      const double rms = std::sqrt(squareSum / left.size());
      // Half-gain NAM plus colored IR and the application's existing -18 dB
      // IR bridge produce about .0042 RMS. Neither fixture alone meets this.
      assert(rms > .003 && rms < .0055);
      enableTuner(fixtureObserved->plugin(), true);
      pumpTone(*fixtureObserved, rate, 110., .35);
      const auto loaded = estimate(fixtureObserved->plugin());
      assert(loaded.status == holdsworth::dsp::TunerPitchStatus::valid && loaded.midiNote == 45);
      assert(std::abs(loaded.cents) <= 2. && fixtureObserved->latency() == 32);
      std::printf("PASS %s real NAM/IR fixtures: stock state loading, loaded-pipeline bit-exact on/off, raw A2 unchanged\n", format);
    }

    observed->plugin().OnUIClose();
    observed->plugin().OnIdle();
    const auto closedCount = analysisCount(observed->plugin());
    pumpTone(*observed, rate, 110., .10);
    assert(analysisCount(observed->plugin()) == closedCount);
    assert(estimate(observed->plugin()).status == holdsworth::dsp::TunerPitchStatus::collecting);
    assert(display(observed->plugin()).status != decltype(shown.status)::stable);
    observed->plugin().OnUIOpen();
    observed->plugin().OnIdle();
    assert(estimate(observed->plugin()).status == holdsworth::dsp::TunerPitchStatus::collecting);
    assert(display(observed->plugin()).status != decltype(shown.status)::stable);
    pumpTone(*observed, rate, 110., .35, true);
    const auto bypassPitch = estimate(observed->plugin());
    assert(bypassPitch.status == holdsworth::dsp::TunerPitchStatus::valid && bypassPitch.midiNote == 45);
    assert(std::abs(bypassPitch.cents) <= 2. && observed->latency() == 32);
    assert(display(observed->plugin()).status == decltype(shown.status)::stable);
    enableTuner(observed->plugin(), false);
    observed->plugin().OnIdle();
    const auto disabledCount = analysisCount(observed->plugin());
    pumpTone(*observed, rate, 110., .075);
    assert(analysisCount(observed->plugin()) == disabledCount);
    assert(estimate(observed->plugin()).status == holdsworth::dsp::TunerPitchStatus::collecting);
    assert(display(observed->plugin()).status == decltype(shown.status)::off);
    assert(rtViolations.load() == 0);
    std::printf("PASS %s %.0f Hz: 11 blocks, normal/bypass bit-exact, raw pitch, trim/pedal independence, stale-backlog rejection, close/reopen/off, latency 32, RT violations 0\n",
                format, rate);
  }
}
