#pragma once
#include "../tuner-v1/host-acceptance.h"
#include <complex>
#include <limits>

inline void eqControls(iplug::IPlugAPIBase& plugin, const holdsworth::dsp::PostAmpEQControls& controls)
{
  using namespace holdsworth::dsp;
  for (std::size_t i = 0; i < kPostAmpEQParameterCount; ++i)
  {
    const auto id = static_cast<PostAmpEQParameter>(i);
    developmentControl(plugin, kMsgTagPostAmpEQ, kCtrlTagEQEnable + static_cast<int>(i),
      postAmpEQParameterToNormalized(id, postAmpEQParameterValue(controls, id)));
  }
}

inline double eqBellOracleDb(const double rate, const double frequency)
{
  // Independent direct-form RBJ oracle for the active integration fixture:
  // one +6 dB, Q .7 bell at 1 kHz. No production SVF helpers are used here.
  const double omega = 2. * std::numbers::pi * 1000. / rate;
  const double a = std::pow(10., 6. / 40.);
  const double alpha = std::sin(omega) / 1.4;
  const double cosine = std::cos(omega);
  const auto z = std::polar(1., -2. * std::numbers::pi * frequency / rate);
  const auto numerator = (1. + alpha * a) - 2. * cosine * z + (1. - alpha * a) * z * z;
  const auto denominator = (1. + alpha / a) - 2. * cosine * z + (1. - alpha / a) * z * z;
  return 20. * std::log10(std::abs(numerator / denominator));
}

template <typename Factory>
void runActiveEQResponse(const char* format, Factory& make, const double rate,
                         const bool loadFixtures)
{
  using namespace holdsworth::dsp;
  auto reference = make(rate), observed = make(rate);
  for (auto* host : {reference.get(), observed.get()})
  {
    dryControls(host->plugin());
    if (loadFixtures)
    {
      const char* model = std::getenv("TUNER_MODEL_FIXTURE");
      const char* ir = std::getenv("TUNER_IR_FIXTURE");
      assert(model && ir);
      stageFixturePaths(host->plugin(), model, ir);
    }
  }
  std::array<double, tunerMaximumBlock> input{}, refLeft{}, refRight{}, left{}, right{};
  auto render = [&](const std::size_t frames) {
    const auto violations = rtViolations.load();
    reference->render(std::span{input}.first(frames), std::span{refLeft}.first(frames),
                      std::span{refRight}.first(frames), false);
    observed->render(std::span{input}.first(frames), std::span{left}.first(frames),
                     std::span{right}.first(frames), false);
    assert(rtViolations.load() == violations);
    assert(reference->latency() == 32 && observed->latency() == 32);
  };
  // Only the first callback warms stock NAM/IR/filter scratch. Every following
  // active parameter/control callback remains covered by RT interposition.
  for (auto* host : {reference.get(), observed.get()})
  {
    host->plugin().GetParam(kEQActive)->Set(1.);
    host->plugin().OnParamChange(kEQActive);
  }
  const auto warmupViolations = rtViolations.load();
  reference->render(input, refLeft, refRight, false);
  observed->render(input, left, right, false);
  rtViolations.store(warmupViolations);
  PostAmpEQControls controls;
  controls.enabled = true;
  controls.bands[1] = {1000., 6., .7};
  eqControls(observed->plugin(), controls);
  double worstDb = 0.;
  std::size_t cases = 0;
  const auto measure = [&](const double frequency) {
    const auto settle = static_cast<std::uint64_t>(rate);
    const auto measured = static_cast<std::uint64_t>(std::llround(rate * .2));
    double referenceSquares = 0., observedSquares = 0.;
    for (std::uint64_t at = 0; at < settle + measured;)
    {
      const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(
        input.size(), settle + measured - at));
      for (std::size_t i = 0; i < frames; ++i)
        input[i] = .03 * std::sin(2. * std::numbers::pi * frequency * static_cast<double>(at + i) / rate);
      render(frames);
      for (std::size_t i = 0; i < frames; ++i)
      {
        assert(std::isfinite(left[i]) && std::isfinite(right[i]));
        if (at + i >= settle)
        {
          referenceSquares += refLeft[i] * refLeft[i] + refRight[i] * refRight[i];
          observedSquares += left[i] * left[i] + right[i] * right[i];
        }
      }
      at += frames;
    }
    assert(referenceSquares > 1.e-12 && observedSquares > 1.e-12);
    const double actualDb = 10. * std::log10(observedSquares / referenceSquares);
    const double expectedDb = eqBellOracleDb(rate, frequency);
    const double error = std::abs(actualDb - expectedDb);
    worstDb = std::max(worstDb, error);
    assert(error <= .01); // Approved response tolerance, unchanged.
    ++cases;
  };
  for (const bool stockTone : {false, true})
  {
    for (auto* host : {reference.get(), observed.get()})
    {
      host->plugin().GetParam(kEQActive)->Set(stockTone ? 1. : 0.);
      host->plugin().OnParamChange(kEQActive);
      for (const auto& [parameter, value] : std::array<std::pair<int, double>, 3>{
             {{kToneBass, 4.}, {kToneMid, 6.}, {kToneTreble, 3.}}})
      {
        host->plugin().GetParam(parameter)->Set(value);
        host->plugin().OnParamChange(parameter);
      }
    }
    for (const bool cabinet : {false, true})
    {
      if (!loadFixtures && cabinet) continue;
      for (auto* host : {reference.get(), observed.get()})
      {
        host->plugin().GetParam(kIRToggle)->Set(cabinet ? 1. : 0.);
        host->plugin().OnParamChange(kIRToggle);
      }
      for (const double frequency : {250., 1000., 4000.}) measure(frequency);
    }
  }
  // Reopening the editor must retain active physical controls and must not
  // restart or bypass the processor. Verify the actual resulting audio.
  observed->plugin().OnUIClose();
  measure(1000.);
  observed->plugin().OnUIOpen();
  measure(1000.);
  // Exercise active updates with all native callback sizes, including one
  // sample. Maximum-size scratch is already warm; no further exclusions.
  std::uint64_t sample = 0;
  for (const int block : tunerBlocks)
    for (int callback = 0; callback < 12; ++callback)
    {
      controls.bands[0] = {callback % 2 ? 80. : 8000., callback % 2 ? -12. : 12., callback % 2 ? .35 : 4.};
      controls.highPassEnabled = callback % 2 != 0;
      controls.lowPassEnabled = !controls.highPassEnabled;
      controls.highPassHz = callback % 2 ? 20. : 200.;
      controls.lowPassHz = callback % 2 ? 3000. : 16000.;
      controls.trimDb = callback % 2 ? -24. : 6.;
      eqControls(observed->plugin(), controls);
      signal(std::span{input}.first(block), sample, rate, 2);
      render(static_cast<std::size_t>(block));
      for (int i = 0; i < block; ++i) assert(std::isfinite(left[i]) && std::isfinite(right[i]));
      sample += block;
    }
  std::printf("METRIC EQ %s active %.0f Hz fixtures=%d oracle_cases=%zu max_abs_db=%.12g; stock-tone/IR independence; close/reopen retained; 11 active block sizes; latency32; audited RT calls0\n",
              format, rate, loadFixtures ? 1 : 0, cases, worstDb);
}

template <typename Factory>
void runEQDelaySendAcceptance(const char* format, Factory& make)
{
  using namespace holdsworth::dsp;
  constexpr double rate = 48000.;
  constexpr double scalarRmsTolerance = 1.e-5, scalarPeakTolerance = 2.e-5;
  const char* model = std::getenv("TUNER_MODEL_FIXTURE");
  const char* ir = std::getenv("TUNER_IR_FIXTURE");
  assert(model && ir);
  auto reference = make(rate), observed = make(rate);
  for (auto* host : {reference.get(), observed.get()})
  {
    dryControls(host->plugin());
    stageFixturePaths(host->plugin(), model, ir);
    developmentControl(host->plugin(), kMsgTagHoldsworthDelayEnabled, kCtrlTagHoldsworthDelayEnabled, 1.);
    developmentControl(host->plugin(), kMsgTagHoldsworthDelayWetLevel, kCtrlTagHoldsworthDelayWetLevel, .65);
  }
  std::array<double, tunerMaximumBlock> input{}, refLeft{}, refRight{}, left{}, right{};
  const auto warmupViolations = rtViolations.load();
  reference->render(input, refLeft, refRight, false);
  observed->render(input, left, right, false);
  rtViolations.store(warmupViolations);
  PostAmpEQControls controls;
  controls.enabled = true;
  controls.trimDb = 6.;
  eqControls(observed->plugin(), controls);
  const double scale = std::pow(10., 6. / 20.);
  double errorSquares = 0., expectedSquares = 0., peakError = 0., expectedPeak = 0.;
  double tailSquares = 0.;
  const std::uint64_t settle = 2048, driven = 48000, tail = 144000;
  for (std::uint64_t at = 0; at < settle + driven + tail;)
  {
    const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(
      input.size(), settle + driven + tail - at));
    for (std::size_t i = 0; i < frames; ++i)
    {
      const auto position = at + i;
      input[i] = position < settle || position >= settle + driven ? 0.
        : .03 * std::sin(2. * std::numbers::pi * 1000. * static_cast<double>(position - settle) / rate);
    }
    const auto violations = rtViolations.load();
    reference->render(std::span{input}.first(frames), std::span{refLeft}.first(frames),
                      std::span{refRight}.first(frames), false);
    observed->render(std::span{input}.first(frames), std::span{left}.first(frames),
                     std::span{right}.first(frames), false);
    assert(rtViolations.load() == violations);
    assert(reference->latency() == 32 && observed->latency() == 32);
    for (std::size_t i = 0; i < frames; ++i)
      if (at + i >= settle)
      {
        for (const auto& [actual, unscaled] : std::array<std::pair<double, double>, 2>{
               {{left[i], refLeft[i]}, {right[i], refRight[i]}}})
        {
          const double expected = unscaled * scale, error = actual - expected;
          assert(std::isfinite(actual));
          errorSquares += error * error;
          expectedSquares += expected * expected;
          peakError = std::max(peakError, std::abs(error));
          expectedPeak = std::max(expectedPeak, std::abs(expected));
          if (at + i >= settle + driven + 4800) tailSquares += expected * expected;
        }
      }
    at += frames;
  }
  assert(expectedSquares > 1.e-12 && expectedPeak > 1.e-8 && tailSquares > 1.e-8);
  const double relativeRms = std::sqrt(errorSquares / expectedSquares);
  const double relativePeak = peakError / expectedPeak;
  // Predetermined integration-only float32/IR rounding bounds; the separate
  // frequency-response criterion remains .01 dB. A changed delay send, routing
  // or preset cannot satisfy full dry+wet and three-second-tail linearity.
  assert(relativeRms <= scalarRmsTolerance && relativePeak <= scalarPeakTolerance);
  std::printf("METRIC EQ %s loaded NAM/IR dry+delay-send scalar linearity: relative_rms=%.12g relative_peak=%.12g tail_energy=%.12g; bounds=1e-5/2e-5; latency32; audited RT calls0\n",
              format, relativeRms, relativePeak, tailSquares);
}

template <typename Factory>
void runPostAmpEQAcceptance(const char* format, Factory&& make, const bool benchmark)
{
  assert(!benchmark); // CPU sweeps are measured separately on the isolated DSP.
  using namespace holdsworth::dsp;
  for (const double rate : tunerRates)
  {
    auto reference = make(rate), observed = make(rate);
    dryControls(reference->plugin()); dryControls(observed->plugin());
    std::array<double, tunerMaximumBlock> input{}, refLeft{}, refRight{}, left{}, right{};
    std::uint64_t at = 0;
    auto render = [&](std::size_t frames, bool exact, bool warmup = false) {
      signal(std::span{input}.first(frames), at, rate, 2);
      const auto oldViolations = rtViolations.load();
      reference->render(std::span{input}.first(frames), std::span{refLeft}.first(frames),
                        std::span{refRight}.first(frames), false);
      observed->render(std::span{input}.first(frames), std::span{left}.first(frames),
                       std::span{right}.first(frames), false);
      // The stock wrapper lazily sizes scratch when callback sizes change.
      // Warm each size before auditing the following EQ changes/render calls.
      if (warmup) rtViolations.store(oldViolations);
      else assert(rtViolations.load() == oldViolations);
      if (exact)
      {
        expectBitExact(std::span{left}.first(frames), std::span{refLeft}.first(frames));
        expectBitExact(std::span{right}.first(frames), std::span{refRight}.first(frames));
      }
      assert(reference->latency() == 32 && observed->latency() == 32);
      at += frames;
    };
    PostAmpEQControls controls;
    controls.enabled = true;
    eqControls(observed->plugin(), controls);
    for (const int block : tunerBlocks)
    {
      render(static_cast<std::size_t>(block), false, true);
      for (int callback = 0; callback < 48; ++callback)
        render(static_cast<std::size_t>(block), true);
    }
    controls.enabled = false;
    eqControls(observed->plugin(), controls);
    for (int callback = 0; callback < 8; ++callback) render(tunerMaximumBlock, false);
    for (const int block : tunerBlocks)
    {
      render(static_cast<std::size_t>(block), false);
      for (int callback = 0; callback < 48; ++callback)
      {
        controls.bands[0] = {callback % 2 ? 80.0 : 8000.0, callback % 2 ? -12.0 : 12.0,
                             callback % 2 ? 0.35 : 4.0};
        controls.bands[1] = {1000.0, -controls.bands[0].gainDb, 0.7};
        controls.highPassEnabled = callback % 2 != 0;
        controls.lowPassEnabled = !controls.highPassEnabled;
        controls.trimDb = callback % 2 ? -24.0 : 6.0;
        eqControls(observed->plugin(), controls);
        render(static_cast<std::size_t>(block), true);
      }
    }
    // Validate malformed messages transactionally through the production API.
    double nan = std::numeric_limits<double>::quiet_NaN();
    assert(!observed->plugin().OnMessage(kMsgTagPostAmpEQ, kCtrlTagEQEnable, sizeof(nan), &nan));
    assert(!observed->plugin().OnMessage(kMsgTagPostAmpEQ, kCtrlTagEQTrim + 1, sizeof(nan), &nan));
    assert(!observed->plugin().OnMessage(kMsgTagPostAmpEQ, kCtrlTagEQTrim, 0, nullptr));
    std::printf("PASS EQ %s %.0f Hz: 11 blocks; neutral-on and bypassed sweeps bit-exact; latency32; audited RT calls0\n", format, rate);
    runActiveEQResponse(format, make, rate, false);
  }
  runActiveEQResponse(format, make, 48000., true);
  runEQDelaySendAcceptance(format, make);
#if defined(EQ_HOST_APP)
  // Existing off-domain host operation stays transparent. EQ's approved
  // frequency ranges are preserved, rather than clamped to a low Nyquist rate.
  for (const double rate : {32000., 384000.})
  {
    auto reference = make(rate), observed = make(rate);
    dryControls(reference->plugin()); dryControls(observed->plugin());
    PostAmpEQControls controls;
    controls.enabled = true;
    controls.bands[0] = {80., 12., 4.};
    controls.highPassEnabled = controls.lowPassEnabled = true;
    controls.trimDb = 6.;
    eqControls(observed->plugin(), controls);
    std::array<double, tunerMaximumBlock> input{}, refLeft{}, refRight{}, left{}, right{};
    const auto warmupViolations = rtViolations.load();
    reference->render(input, refLeft, refRight, false);
    observed->render(input, left, right, false);
    rtViolations.store(warmupViolations);
    for (int callback = 0; callback < 24; ++callback)
    {
      signal(input, static_cast<std::uint64_t>(callback) * input.size(), rate, 2);
      const auto violations = rtViolations.load();
      reference->render(input, refLeft, refRight, false);
      observed->render(input, left, right, false);
      assert(rtViolations.load() == violations);
      expectBitExact(left, refLeft); expectBitExact(right, refRight);
      assert(reference->latency() == 32 && observed->latency() == 32);
    }
    std::printf("PASS EQ APP %.0f Hz unsupported-domain fallback: active controls transparent; latency32; audited RT calls0\n", rate);
  }
#endif
  // Repeat the accepted tuner/pedal/model/IR native-host matrix with EQ default
  // bypassed. This retains the independent legacy regression coverage.
  runHostAcceptance(format, make, false);
}
