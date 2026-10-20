#pragma once
#include "../tuner-v1/host-acceptance.h"

inline auto legatoDiagnostics(iplug::IPlugAPIBase& plugin)
{ return static_cast<NeuralAmpModeler&>(plugin).DevelopmentLegatoDiagnostics(); }
inline void enableLegato(iplug::IPlugAPIBase& plugin, bool enabled)
{ developmentControl(plugin, kMsgTagLegatoEnabled, kCtrlTagLegatoEnabled, enabled ? 1. : 0.); }

template <typename Factory>
void runLegatoHostAcceptance(const char* format, Factory&& make, bool benchmark)
{
  if (benchmark) std::puts("format,rate,block,mode,p50_us,p99_us,max_us,deadline_us");
  for (double rate : tunerRates)
  {
    auto reference = make(rate), observed = make(rate), bypassed = make(rate);
    for (auto* host : {reference.get(), observed.get(), bypassed.get()}) dryControls(host->plugin());
    std::array<double, tunerMaximumBlock> input{}, rl{}, rr{}, left{}, right{}, bl{}, br{};
    signal(input, 0, rate, 0);
    auto before = rtViolations.load();
    reference->render(input, rl, rr, false);
    observed->render(input, left, right, false);
    bypassed->render(input, bl, br, true);
    rtViolations.store(before); // Existing downstream cold storage only.
    if (benchmark)
    {
      for (int block : tunerBlocks) for (bool enabled : {false, true})
      {
        enableLegato(observed->plugin(), enabled);
        measureCallbacks(format, rate, block, enabled ? "compressor-on" : "compressor-off", [&] {
          observed->render(std::span{input}.first(block), std::span{left}.first(block),
                           std::span{right}.first(block), false);
        }, [&] { observed->plugin().OnIdle(); });
      }
      continue;
    }
    // Toggle through real development messages; wait beyond 10 ms before
    // comparing fully bypassed audio against a never-enabled reference.
    enableLegato(observed->plugin(), true);
    for (int i = 0; i < 32; ++i)
    {
      reference->render(input, rl, rr, false); observed->render(input, left, right, false);
      observed->plugin().OnIdle(); assert(observed->latency() == 32);
      if constexpr (requires { observed->context.restarts; }) assert(observed->context.restarts == 0);
    }
    assert(legatoDiagnostics(observed->plugin()).reductionDb > 2.);
    enableLegato(observed->plugin(), false);
    // Existing downstream 5 Hz DC-blocker history must also settle after
    // feeding different audio while enabled. Compressor itself returns exact
    // dry immediately after its 10 ms transition (isolated test).
    for (int i = 0; i < static_cast<int>(std::ceil(2.*rate/1024.)); ++i)
    { reference->render(input, rl, rr, false); observed->render(input, left, right, false); }
    double residual = 0.;
    for (std::size_t i = 0; i < left.size(); ++i) residual = std::max(residual, std::abs(left[i]-rl[i]));
    assert(residual < 2.e-12 && legatoDiagnostics(observed->plugin()).enableMix == 0.);
    std::printf("MEASURE %s %.0f Hz post-toggle legacy DC history residual %.12g\n", format, rate, residual);
    // Bit comparisons require matching downstream filter histories. Distinct
    // earlier signals can leave roundoff-sized recursive-filter limit cycles.
    // The isolated processor checks exact dry after the fade with warm history.
    reference = make(rate); observed = make(rate);
    dryControls(reference->plugin()); dryControls(observed->plugin());
    before = rtViolations.load();
    reference->render(input, rl, rr, false); observed->render(input, left, right, false);
    rtViolations.store(before);
    std::uint64_t sample = 0;
    for (int block : tunerBlocks) for (int fixture = 0; fixture < 3; ++fixture)
      for (int callback = 0; callback < 16; ++callback)
      {
        signal(std::span{input}.first(block), sample, rate, fixture);
        reference->render(std::span{input}.first(block), std::span{rl}.first(block), std::span{rr}.first(block), false);
        observed->render(std::span{input}.first(block), std::span{left}.first(block), std::span{right}.first(block), false);
        expectBitExact(std::span{left}.first(block), std::span{rl}.first(block));
        expectBitExact(std::span{right}.first(block), std::span{rr}.first(block));
        assert(observed->latency() == 32); sample += block;
      }
    // Exact raw host bypass is independent of compressor enable. Match reset
    // detector histories with a normally rendered instance, sample for sample.
    reference = make(rate); observed = make(rate); bypassed = make(rate);
    for (auto* host : {reference.get(), observed.get(), bypassed.get()}) dryControls(host->plugin());
    enableLegato(observed->plugin(), true); enableLegato(bypassed->plugin(), true);
    before = rtViolations.load();
    reference->render(input, rl, rr, true); observed->render(input, left, right, false); bypassed->render(input, bl, br, true);
    rtViolations.store(before);
    // Existing framework bypass also fades for 10 ms. During that fade the
    // processed contribution intentionally differs; compare raw bypass only
    // once settled, with exactly matched input/detector histories.
    for (int warm = 0; warm < 3; ++warm)
    { reference->render(input, rl, rr, true); observed->render(input, left, right, false); bypassed->render(input, bl, br, true); }
    for (int block : tunerBlocks) for (int callback = 0; callback < 24; ++callback)
    {
      signal(std::span{input}.first(block), sample, rate, callback % 3);
      reference->render(std::span{input}.first(block), std::span{rl}.first(block), std::span{rr}.first(block), true);
      observed->render(std::span{input}.first(block), std::span{left}.first(block), std::span{right}.first(block), false);
      bypassed->render(std::span{input}.first(block), std::span{bl}.first(block), std::span{br}.first(block), true);
      expectBitExact(std::span{bl}.first(block), std::span{rl}.first(block));
      expectBitExact(std::span{br}.first(block), std::span{rr}.first(block));
      const auto a = legatoDiagnostics(observed->plugin()), b = legatoDiagnostics(bypassed->plugin());
      assert(a.power == b.power && a.reductionDb == b.reductionDb && a.appliedGain == b.appliedGain);
      assert(!a.calibrated && observed->latency() == 32 && bypassed->latency() == 32);
      sample += block;
    }
    if constexpr (requires { observed->context.restarts; }) assert(observed->context.restarts == 0);
    std::printf("PASS %s Legato %.0f Hz: 11 blocks, exact settled bypass, warm host-bypass history, latency32, no toggle PDC\n", format, rate);
  }
  if (benchmark) return;
  // Exercise all selected pedal domains with compression active in the real
  // plugin. Tuner remains before compressor, trim and nonlinear pedals.
  auto host = make(48000.);
  dryControls(host->plugin()); enableLegato(host->plugin(), true);
  std::array<double, tunerMaximumBlock> input{}, left{}, right{};
  signal(input, 0, 48000., 0);
  auto before = rtViolations.load(); host->render(input, left, right, false); rtViolations.store(before);
  for (int route = 0; route < 4; ++route)
  {
    developmentControl(host->plugin(), kMsgTagPreNAMProcessor, kCtrlTagPreNAMProcessor, static_cast<double>(route)/3.);
    for (int block : {1, 37, 64, 128, 1024}) for (int i = 0; i < 64; ++i)
    {
      signal(std::span{input}.first(block), i*block, 48000., 0);
      host->render(std::span{input}.first(block), std::span{left}.first(block), std::span{right}.first(block), i%2);
      for (double value : std::span{left}.first(block)) assert(std::isfinite(value));
      assert(host->latency() == 32);
    }
  }
  enableTuner(host->plugin(), true); pumpTone(*host, 48000., 110., .4);
  assert(estimate(host->plugin()).midiNote == 45 && std::abs(estimate(host->plugin()).cents) <= 2.);
  std::printf("PASS %s Legato active: Off/TC/MC/AH, normal/bypass, raw tuner A2\n", format);

  // SAME physical voltage, different host normalization, real calibrated NAM
  // state loading. AU float conversion is a separate, declared tolerance.
  const char* model = std::getenv("LEGATO_CALIBRATED_MODEL");
  const char* resampledModel = std::getenv("LEGATO_RESAMPLED_MODEL");
  assert(model && resampledModel);
  double maxLevel = 0., maxGain = 0.;
  for (int route = 0; route < 2; ++route)
  {
    std::vector<decltype(make(48000.))> instances;
    for (double calibration : {-6., 0., 6., 12., 18., 24.})
    {
      auto calibrated = make(48000.); dryControls(calibrated->plugin());
      stageFixturePaths(calibrated->plugin(), model, "");
      before = rtViolations.load(); calibrated->render(input, left, right, false); rtViolations.store(before);
      for (auto pair : {std::pair{kInputCalibrationLevel, calibration}, std::pair{kCalibrateInput, 1.}})
      { calibrated->plugin().GetParam(pair.first)->Set(pair.second); calibrated->plugin().OnParamChange(pair.first); }
      developmentControl(calibrated->plugin(), kMsgTagPreNAMProcessor, kCtrlTagPreNAMProcessor, route/3.);
      enableLegato(calibrated->plugin(), true);
      instances.push_back(std::move(calibrated));
    }
    for (int at = 0; at < 24000; at += 37)
    {
      holdsworth::dsp::LegatoCompressorDiagnostics first{};
      for (std::size_t j = 0; j < instances.size(); ++j)
      {
        const double calibration = -6.+6.*j;
        const double fullScale = std::sqrt(2.)*.775*std::pow(10., calibration/20.);
        for (int i = 0; i < 37; ++i) input[i] = .24*std::sin(2.*std::numbers::pi*110.*(at+i)/48000.)/fullScale;
        instances[j]->render(std::span{input}.first(37), std::span{left}.first(37), std::span{right}.first(37), false);
        const auto d = legatoDiagnostics(instances[j]->plugin()); assert(d.calibrated);
        if (j == 0) first = d;
        else { maxLevel = std::max(maxLevel, std::abs(d.levelDb-first.levelDb));
               maxGain = std::max(maxGain, std::abs(d.appliedGain-first.appliedGain)); }
      }
    }
  }
  const bool au = std::strcmp(format, "AU") == 0;
  assert(maxLevel < (au ? 2.e-6 : 2.e-11) && maxGain < (au ? 2.e-7 : 2.e-13));
  std::printf("PASS %s real calibrated equivalence: maxLevel=%.12g dB maxGain=%.12g\n", format, maxLevel, maxGain);
  // Actual NAM resampler latency, stable across compressor toggles. Return to
  // 48k NAM exercises the existing transition in the opposite direction.
  auto latencyHost = make(48000.); dryControls(latencyHost->plugin());
  for (auto fixture : {resampledModel, model})
  {
    stageFixturePaths(latencyHost->plugin(), fixture, "");
    before = rtViolations.load(); latencyHost->render(input, left, right, false); rtViolations.store(before);
    for (int i = 0; i < 96; ++i) { latencyHost->render(input, left, right, i%2); latencyHost->plugin().OnIdle(); }
    const auto expected = fixture == resampledModel ? 61U : 32U;
    assert(latencyHost->latency() == expected);
    unsigned beforeToggles = 0;
    if constexpr (requires { latencyHost->context.restarts; }) beforeToggles = latencyHost->context.restarts;
    for (int i = 0; i < 96; ++i)
    { enableLegato(latencyHost->plugin(), i%2); latencyHost->render(input, left, right, i%3==0); latencyHost->plugin().OnIdle(); assert(latencyHost->latency() == expected); }
    if constexpr (requires { latencyHost->context.restarts; }) assert(latencyHost->context.restarts == beforeToggles);
  }
  std::printf("PASS %s real NAM latency32->61->32, compressor toggles unchanged\n", format);
}

template <typename Factory>
void runAllHostAcceptance(const char* format, Factory&& make, bool benchmark)
{
  if (!benchmark) runHostAcceptance(format, make, false); // Complete accepted tuner regression.
  runLegatoHostAcceptance(format, make, benchmark);
}
