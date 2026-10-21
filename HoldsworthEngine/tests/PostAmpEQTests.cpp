#include "../dsp/PostAmpEQProcessor.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <thread>
#include <vector>

namespace holdsworth::dsp
{
struct PostAmpEQTestAccess
{
  struct RampSnapshot { double current, target, increment; std::size_t remaining; };
  static auto ramp(const PostAmpEQProcessor::Ramp& r) { return RampSnapshot{r.current, r.target, r.increment, r.remaining}; }
  static auto band(const PostAmpEQProcessor& p, std::size_t i)
  { return std::array{ramp(p.mBands[i].g), ramp(p.mBands[i].k), ramp(p.mBands[i].m)}; }
  static auto highPass(const PostAmpEQProcessor& p) { return ramp(p.mHighPass.g); }
  static auto lowPass(const PostAmpEQProcessor& p) { return ramp(p.mLowPass.g); }
  static auto enabled(const PostAmpEQProcessor& p) { return ramp(p.mEnabled); }
  static auto highPassEnabled(const PostAmpEQProcessor& p) { return ramp(p.mHighPassEnabled); }
  static auto lowPassEnabled(const PostAmpEQProcessor& p) { return ramp(p.mLowPassEnabled); }
  static auto trim(const PostAmpEQProcessor& p) { return ramp(p.mTrim); }
  static auto controls(const PostAmpEQProcessor& p) { return p.mControls; }
};
}

namespace holdsworth::test
{
namespace
{
using EQ = dsp::PostAmpEQProcessor;
using Controls = dsp::PostAmpEQControls;
using Access = dsp::PostAmpEQTestAccess;
constexpr std::array rates{44100., 48000., 88200., 96000., 176400., 192000.};
constexpr double pi = std::numbers::pi;
constexpr std::size_t maximumBlock = 4096;
constexpr double responseToleranceDb = .01;

// Independent RBJ direct-form transfer-function oracle. This intentionally does
// not use the production trapezoidal SVF coefficients, integrators or mappings.
struct OracleBiquad
{
  std::array<double, 3> b{}, a{};
  std::complex<double> response(double hz, double fs) const
  {
    const auto z = std::polar(1., -2. * pi * hz / fs);
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z);
  }
};

OracleBiquad bellOracle(double fs, const dsp::PostAmpEQBandControls& band)
{
  const double omega = 2. * pi * band.frequencyHz / fs;
  const double cosine = std::cos(omega), alpha = std::sin(omega) / (2. * band.q);
  const double amplitude = std::pow(10., band.gainDb / 40.);
  return {{1. + alpha * amplitude, -2. * cosine, 1. - alpha * amplitude},
          {1. + alpha / amplitude, -2. * cosine, 1. - alpha / amplitude}};
}

OracleBiquad cutOracle(double fs, double hz, bool highPass)
{
  const double omega = 2. * pi * hz / fs, cosine = std::cos(omega);
  const double alpha = std::sin(omega) / std::sqrt(2.);
  const double b0 = (1. + (highPass ? cosine : -cosine)) / 2.;
  return {{b0, (highPass ? -2. : 2.) * b0, b0}, {1. + alpha, -2. * cosine, 1. - alpha}};
}

std::complex<double> oracle(const Controls& controls, double fs, double hz)
{
  std::complex<double> result{std::pow(10., controls.trimDb / 20.), 0.};
  if (controls.highPassEnabled) result *= cutOracle(fs, controls.highPassHz, true).response(hz, fs);
  for (const auto& band : controls.bands) result *= bellOracle(fs, band).response(hz, fs);
  if (controls.lowPassEnabled) result *= cutOracle(fs, controls.lowPassHz, false).response(hz, fs);
  return result;
}

void run(EQ& eq, std::span<const double> input, std::span<double> output, std::size_t block = maximumBlock)
{
  for (std::size_t i = 0; i < input.size(); i += block)
  {
    const auto count = std::min(block, input.size() - i);
    eq.processBlock(input.subspan(i, count), output.subspan(i, count));
  }
}

bool setup(EQ& eq, double fs, const Controls& controls)
{
  eq.prepare(fs, maximumBlock);
  eq.setControls(controls);
  eq.reset();
  return true;
}

std::vector<double> signal(std::size_t size)
{
  std::vector<double> result(size);
  std::uint32_t seed = 0x771928baU;
  for (std::size_t i = 0; i < size; ++i)
  {
    seed = 1664525U * seed + 1013904223U;
    result[i] = .1 * std::sin(.017 * static_cast<double>(i))
      + .02 * (2. * static_cast<double>(seed) / 4294967295. - 1.);
  }
  if (size > 1) { result[0] = 0.; result[1] = -0.; }
  return result;
}

Controls colored()
{
  Controls c;
  c.enabled = true;
  c.highPassEnabled = true; c.highPassHz = 120.;
  c.lowPassEnabled = true; c.lowPassHz = 5200.;
  c.bands = {{{180., 7., 1.2}, {1370., -9., 3.1}, {4300., 4., .35}}};
  c.trimDb = -4.;
  return c;
}

void fft(std::vector<std::complex<double>>& values)
{
  const auto size = values.size();
  for (std::size_t i = 1, j = 0; i < size; ++i)
  {
    auto bit = size >> 1;
    for (; (j & bit) != 0; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(values[i], values[j]);
  }
  for (std::size_t count = 2; count <= size; count <<= 1)
  {
    const auto step = std::polar(1., -2. * pi / static_cast<double>(count));
    for (std::size_t begin = 0; begin < size; begin += count)
    {
      std::complex<double> twiddle{1., 0.};
      for (std::size_t j = 0; j < count / 2; ++j)
      {
        const auto even = values[begin + j], odd = twiddle * values[begin + j + count / 2];
        values[begin + j] = even + odd;
        values[begin + j + count / 2] = even - odd;
        twiddle *= step;
      }
    }
  }
}

bool impulseAccuracy()
{
  double worst = 0., worstPhase = 0.;
  std::size_t comparisons = 0;
  for (double fs : rates)
  {
    std::size_t size = 1;
    while (static_cast<double>(size) < fs * 1.5) size <<= 1;
    std::vector<double> input(size), output(size);
    input[0] = 1.;
    std::array<Controls, 9> fixtures{};
    for (auto& c : fixtures) c.enabled = true;
    fixtures[0].bands[0] = {80., 12., 4.};
    fixtures[1].bands[1] = {8000., -12., 4.};
    fixtures[2].bands[2] = {1000., 6., .35};
    fixtures[3].highPassEnabled = true; fixtures[3].highPassHz = 20.;
    fixtures[4].highPassEnabled = true; fixtures[4].highPassHz = 200.;
    fixtures[5].lowPassEnabled = true; fixtures[5].lowPassHz = 3000.;
    fixtures[6].lowPassEnabled = true; fixtures[6].lowPassHz = 16000.;
    fixtures[7] = colored();
    fixtures[8].bands = {{{80., -12., .35}, {80., 12., .35}, {8000., 12., 4.}}};
    for (std::size_t fixture = 0; fixture < fixtures.size(); ++fixture)
    {
      EQ eq;
      if (!setup(eq, fs, fixtures[fixture])) return false;
      run(eq, input, output);
      std::vector<std::complex<double>> transform(output.begin(), output.end());
      fft(transform);
      for (std::size_t point = 0; point <= 360; ++point)
      {
        const double nominalHz = point == 0 ? 0. : point == 360 ? fs / 2.
          : 5. * std::pow((fs / 2. - 1.) / 5., static_cast<double>(point - 1) / 358.);
        const auto bin = static_cast<std::size_t>(std::llround(nominalHz * static_cast<double>(size) / fs));
        const double hz = static_cast<double>(bin) * fs / static_cast<double>(size);
        const auto expected = oracle(fixtures[fixture], fs, hz);
        const double magnitude = std::abs(expected);
        if (magnitude < .0001) continue; // Specified accuracy domain: response > -80 dB.
        const double error = std::abs(20. * std::log10(std::abs(transform[bin]) / magnitude));
        const double phaseError = std::abs(std::arg(transform[bin] / expected));
        worst = std::max(worst, error); worstPhase = std::max(worstPhase, phaseError);
        if (!std::isfinite(error) || error > responseToleranceDb)
        {
          std::cerr << "EQ impulse oracle fs=" << fs << " fixture=" << fixture << " hz=" << hz
                    << " error_db=" << error << '\n';
          return false;
        }
        ++comparisons;
      }
    }
  }
  std::cout << std::setprecision(17) << "METRIC eq impulse comparisons=" << comparisons << " max_abs_db=" << worst
            << " max_phase_radians=" << worstPhase << '\n';
  return true;
}

double sineMagnitude(double fs, double hz, const Controls& controls)
{
  EQ eq;
  if (!setup(eq, fs, controls)) return std::numeric_limits<double>::quiet_NaN();
  const auto settle = static_cast<std::size_t>(fs), measured = static_cast<std::size_t>(fs * .04);
  std::array<double, maximumBlock> input{}, output{};
  double ss = 0., cc = 0., sc = 0., ys = 0., yc = 0.;
  for (std::size_t begin = 0; begin < settle + measured; begin += maximumBlock)
  {
    const auto count = std::min(maximumBlock, settle + measured - begin);
    for (std::size_t j = 0; j < count; ++j)
      input[j] = .1 * std::sin(2. * pi * hz * static_cast<double>(begin + j) / fs);
    eq.processBlock(std::span<const double>{input}.first(count), std::span<double>{output}.first(count));
    for (std::size_t j = 0; j < count; ++j)
      if (begin + j >= settle)
      {
        const double phase = 2. * pi * hz * static_cast<double>(begin + j) / fs;
        const double s = std::sin(phase), c = std::cos(phase);
        ss += s * s; cc += c * c; sc += s * c;
        ys += output[j] * s; yc += output[j] * c;
      }
  }
  const double determinant = ss * cc - sc * sc;
  return std::hypot((ys * cc - yc * sc) / determinant, (yc * ss - ys * sc) / determinant) / .1;
}

bool bellGainQAndReciprocity()
{
  double worst = 0., reciprocal = 0.;
  std::size_t cases = 0;
  for (double fs : rates)
    for (double center : {80., 1000., 8000.})
      for (double q : {.35, .7, 4.})
      {
        std::array<double, 3> positive{};
        const double width = 1. / q, root = std::sqrt(4. + width * width);
        const std::array frequencies{
          fs / pi * std::atan(std::tan(pi * center / fs) * (root - width) / 2.),
          center,
          fs / pi * std::atan(std::tan(pi * center / fs) * (root + width) / 2.)};
        for (double gain : {12., -12.})
        {
          Controls c; c.enabled = true; c.bands[0] = {center, gain, q};
          for (std::size_t i = 0; i < frequencies.size(); ++i)
          {
            const double db = 20. * std::log10(sineMagnitude(fs, frequencies[i], c));
            const double expectedDb = i == 1 ? gain : gain / 2.;
            const double error = std::abs(db - expectedDb);
            worst = std::max(worst, error);
            if (!std::isfinite(error) || error > responseToleranceDb) return false;
            if (gain > 0.) positive[i] = db;
            else reciprocal = std::max(reciprocal, std::abs(db + positive[i]));
            ++cases;
          }
        }
      }
  std::cout << "METRIC eq sine_center_halfgain cases=" << cases << " max_abs_db=" << worst
            << " reciprocal_max_db=" << reciprocal << '\n';
  return reciprocal <= responseToleranceDb;
}

bool cutSemantics()
{
  double worst = 0., endpoint = 0.;
  for (double fs : rates)
    for (bool highPass : {false, true})
      for (double cutoff : highPass ? std::array{20., 200.} : std::array{3000., 16000.})
      {
        Controls c; c.enabled = true;
        c.highPassEnabled = highPass; c.highPassHz = highPass ? cutoff : 80.;
        c.lowPassEnabled = !highPass; c.lowPassHz = highPass ? 8000. : cutoff;
        for (double hz : {cutoff / 2., cutoff, std::min(fs * .49, cutoff * 2.)})
        {
          const double actual = sineMagnitude(fs, hz, c);
          const double expected = std::abs(oracle(c, fs, hz));
          const double error = std::abs(20. * std::log10(actual / expected));
          worst = std::max(worst, error);
          if (!std::isfinite(error) || error > responseToleranceDb) return false;
          if (hz == cutoff && !expectNear("EQ Butterworth cutoff", 20. * std::log10(actual),
                                         -3.010299956639812, responseToleranceDb)) return false;
        }
        EQ eq;
        if (!setup(eq, fs, c)) return false;
        std::vector<double> input(static_cast<std::size_t>(fs)), output(input.size());
        for (std::size_t i = 0; i < input.size(); ++i) input[i] = highPass ? 1. : (i % 2 == 0 ? 1. : -1.);
        run(eq, input, output);
        endpoint = std::max(endpoint, std::abs(output.back()));
      }
  std::cout << "METRIC eq hp_lp max_abs_db=" << worst << " rejected_endpoint_abs=" << endpoint << '\n';
  return endpoint < 1.e-10;
}

bool neutralAndBypass()
{
  for (double fs : rates)
  {
    EQ eq;
    auto data = signal(static_cast<std::size_t>(fs * .04));
    std::vector<double> output(data.size());
    if (!setup(eq, fs, Controls{})) return false;
    run(eq, data, output);
    if (!expectSamplesBitExact("EQ initially bypassed", output, data)) return false;
    auto c = colored(); eq.setControls(c);
    run(eq, data, output);
    c.enabled = false; eq.setControls(c);
    run(eq, data, output);
    run(eq, data, output);
    if (!expectSamplesBitExact("EQ stale-state settled bypass", output, data)) return false;
    c = Controls{}; c.enabled = true; eq.setControls(c);
    run(eq, data, output);
    run(eq, data, output);
    if (!expectSamplesBitExact("EQ stale-state neutral enabled", output, data)) return false;
    std::fill(data.begin(), data.end(), -0.);
    run(eq, data, output);
    if (!expectSamplesBitExact("EQ neutral preserves negative zero", output, data)) return false;
  }
  return true;
}

std::vector<double> renderPartitions(double fs, std::span<const double> input, std::span<const std::size_t> blocks)
{
  EQ eq;
  eq.prepare(fs, maximumBlock);
  std::vector<double> output(input.size());
  constexpr std::array<std::size_t, 10> events{0, 1, 17, 175, 499, 1192, 2431, 4097, 8193, 10000};
  std::size_t event = 0, block = 0;
  for (std::size_t position = 0; position < input.size(); ++block)
  {
    if (event < events.size() && events[event] == position)
    {
      auto c = colored();
      c.enabled = event % 3 != 1;
      c.highPassEnabled = event % 2 == 0; c.lowPassEnabled = event % 3 == 0;
      c.bands[event % 3].frequencyHz = event % 2 == 0 ? 80. : 8000.;
      c.bands[event % 3].q = event % 2 == 0 ? .35 : 4.;
      c.bands[event % 3].gainDb = event % 2 == 0 ? 12. : -12.;
      eq.setControls(c); ++event;
    }
    auto count = std::min(blocks[block % blocks.size()], input.size() - position);
    if (event < events.size()) count = std::min(count, events[event] - position);
    eq.processBlock(input.subspan(position, count), std::span<double>{output}.subspan(position, count));
    position += count;
  }
  return output;
}

bool partitions()
{
  constexpr std::array<std::size_t, 12> irregular{1, 7, 64, 127, 4096, 3, 19, 301, 2, 1024, 33, 511};
  const auto data = signal(18000);
  for (double fs : rates)
  {
    const std::array<std::size_t, 1> single{1};
    const auto expected = renderPartitions(fs, data, single);
    for (std::size_t size : {7U, 64U, 127U, 4096U})
    {
      const std::array block{size};
      if (!expectSamplesBitExact("EQ sample-timed fixed partitions", renderPartitions(fs, data, block), expected)) return false;
    }
    if (!expectSamplesBitExact("EQ sample-timed irregular partitions", renderPartitions(fs, data, irregular), expected)) return false;
  }
  return true;
}

bool lifecycleAndAliasing()
{
  const auto input = signal(8291);
  for (double fs : rates)
  {
    EQ separate, inplace;
    if (!setup(separate, fs, colored()) || !setup(inplace, fs, colored())) return false;
    if (!separate.isPrepared() || separate.maximumBlockSize() != maximumBlock || separate.latencySamples() != 0) return false;
    std::vector<double> output(input.size()), aliased = input;
    run(separate, input, output, 127);
    run(inplace, aliased, aliased, 127);
    if (!expectSamplesBitExact("EQ exact in-place alias", output, aliased)) return false;
    separate.reset(); inplace.reset();
    separate.processBlock({}, {});
    run(separate, input, output, 127);
    aliased = input; run(inplace, aliased, aliased, 127);
    if (!expectSamplesBitExact("EQ reset and empty equivalence", output, aliased)) return false;
    EQ reconfigured;
    if (!setup(reconfigured, 44100., colored())) return false;
    reconfigured.prepare(fs, maximumBlock);
    reconfigured.setControls(colored()); reconfigured.reset();
    run(reconfigured, input, aliased, 127);
    if (!expectSamplesBitExact("EQ prepare rate change resets history", output, aliased)) return false;
  }
  EQ unprepared;
  std::vector<double> output(input.size());
  unprepared.processBlock(input, output);
  if (!expectSamplesBitExact("EQ unprepared bypass", output, input)) return false;
  EQ small;
  small.prepare(48000., 8);
  small.setControls(colored()); small.reset();
  small.processBlock(input, output);
  if (!expectSamplesBitExact("EQ oversized guarded bypass", output, input)) return false;
  std::array<double, 3> mismatched{};
  small.processBlock(std::span<const double>{input}.first(4), mismatched);
  return expectSamplesBitExact("EQ mismatch guarded prefix", mismatched, std::span<const double>{input}.first(3));
}

bool rapidChangesAndDecay()
{
  double maximum = 0., maximumTail = 0.;
  std::uint32_t seed = 0x17a8382U;
  for (double fs : rates)
  {
    EQ eq;
    eq.prepare(fs, maximumBlock);
    std::array<double, 127> input{}, output{};
    for (std::size_t tick = 0; tick < 1500; ++tick)
    {
      auto c = colored();
      c.enabled = tick % 17 != 0;
      c.highPassEnabled = tick % 11 < 5; c.highPassHz = tick % 2 == 0 ? 20. : 200.;
      c.lowPassEnabled = tick % 7 < 3; c.lowPassHz = tick % 2 == 0 ? 3000. : 16000.;
      c.trimDb = tick % 2 == 0 ? -24. : 6.;
      for (std::size_t band = 0; band < c.bands.size(); ++band)
      {
        seed = 1664525U * seed + 1013904223U;
        c.bands[band] = {80. + 7920. * static_cast<double>(seed & 65535U) / 65535.,
                        tick % 2 == 0 ? -12. : 12., tick % 3 == 0 ? .35 : 4.};
      }
      eq.setControls(c);
      const auto count = tick % 5 == 0 ? 1U : input.size();
      for (std::size_t i = 0; i < count; ++i) input[i] = .02 * std::sin(.097 * static_cast<double>(tick * 127 + i));
      eq.processBlock(std::span<const double>{input}.first(count), std::span<double>{output}.first(count));
      for (std::size_t i = 0; i < count; ++i)
      {
        maximum = std::max(maximum, std::abs(output[i]));
        if (!std::isfinite(output[i]) || std::abs(output[i]) > 1000.) return false;
      }
    }
    auto c = colored(); c.bands[0] = {80., 12., 4.};
    eq.setControls(c); input.fill(0.);
    for (std::size_t n = 0; n < static_cast<std::size_t>(fs * 2.); n += input.size())
    {
      eq.processBlock(input, output);
      if (!std::all_of(output.begin(), output.end(), [](double value) { return std::isfinite(value); })) return false;
    }
    for (double value : output) maximumTail = std::max(maximumTail, std::abs(value));
  }
  std::cout << "METRIC eq rapid_changes peak_abs=" << maximum << " two_second_tail_abs=" << maximumTail << '\n';
  return maximumTail < 1.e-10;
}

bool fixedStorage()
{
  EQ eq;
  eq.prepare(192000., maximumBlock);
  std::array<double, maximumBlock> input{}, output{};
  input.fill(.001);
  auto c = colored();
  beginAllocationTracking();
  for (unsigned tick = 0; tick < 1000; ++tick)
  {
    c.bands[tick % 3].frequencyHz = tick % 2 == 0 ? 80. : 8000.;
    c.bands[tick % 3].q = tick % 2 == 0 ? .35 : 4.;
    c.trimDb = tick % 2 == 0 ? 6. : -24.;
    eq.setControls(c); eq.processBlock(input, output);
    if (tick % 17 == 0) eq.reset();
  }
  const auto allocations = endAllocationTracking();
  std::cout << "METRIC eq realtime_cpp_allocations=" << allocations << '\n';
  return allocations == 0;
}

bool exactRampsAndRetargeting()
{
  for (double fs : rates)
  {
    const auto parameterSamples = static_cast<std::size_t>(std::ceil(fs * .02));
    const auto mixSamples = static_cast<std::size_t>(std::ceil(fs * .01));
    EQ eq;
    Controls initial; initial.enabled = true;
    if (!setup(eq, fs, initial)) return false;
    std::array<double, 1> input{.1}, output{};
    const auto before = Access::band(eq, 0);
    auto c = colored(); c.enabled = false;
    eq.setControls(c); eq.processBlock(input, output);
    const auto first = Access::band(eq, 0);
    for (std::size_t i = 0; i < first.size(); ++i)
    {
      const auto& r = first[i];
      if (r.remaining != parameterSamples - 1
          || !expectNear("EQ 20 ms first primitive step", r.current,
                         before[i].current + (r.target - before[i].current) / static_cast<double>(parameterSamples), 1.e-14)) return false;
    }
    if (Access::enabled(eq).remaining != mixSamples - 1 || Access::trim(eq).remaining != mixSamples - 1
        || Access::highPassEnabled(eq).remaining != mixSamples - 1 || Access::lowPassEnabled(eq).remaining != mixSamples - 1
        || Access::highPass(eq).remaining != parameterSamples - 1 || Access::lowPass(eq).remaining != parameterSamples - 1) return false;
    for (std::size_t n = 1; n < mixSamples - 1; ++n) eq.processBlock(input, output);
    if (Access::enabled(eq).remaining != 1 || Access::trim(eq).remaining != 1
        || Access::highPassEnabled(eq).remaining != 1 || Access::lowPassEnabled(eq).remaining != 1) return false;
    eq.processBlock(input, output);
    for (const auto r : {Access::enabled(eq), Access::trim(eq), Access::highPassEnabled(eq), Access::lowPassEnabled(eq)})
      if (r.remaining != 0 || r.current != r.target) return false;
    for (std::size_t n = mixSamples; n < parameterSamples - 1; ++n) eq.processBlock(input, output);
    for (const auto& r : Access::band(eq, 0)) if (r.remaining != 1) return false;
    eq.processBlock(input, output);
    for (std::size_t band = 0; band < 3; ++band)
      for (const auto& r : Access::band(eq, band)) if (r.remaining != 0 || r.current != r.target) return false;
    for (const auto r : {Access::highPass(eq), Access::lowPass(eq)})
      if (r.remaining != 0 || r.current != r.target) return false;
    c.bands[0] = {8000., -12., 4.}; eq.setControls(c);
    for (unsigned n = 0; n < 47; ++n) eq.processBlock(input, output);
    const auto retargetFrom = Access::band(eq, 0);
    c.bands[0] = {80., 12., .35}; eq.setControls(c); eq.processBlock(input, output);
    const auto retargeted = Access::band(eq, 0);
    for (std::size_t i = 0; i < retargeted.size(); ++i)
      if (retargeted[i].remaining != parameterSamples - 1
          || !expectNear("EQ continuous ramp retarget", retargeted[i].current,
                         retargetFrom[i].current + (retargeted[i].target - retargetFrom[i].current)
                           / static_cast<double>(parameterSamples), 1.e-14)) return false;
    eq.setControls(c); eq.processBlock(input, output);
    for (const auto& r : Access::band(eq, 0)) if (r.remaining != parameterSamples - 2) return false;
  }
  return true;
}

bool trimAndBypassLinearFades()
{
  double worst = 0.;
  for (double fs : rates)
  {
    EQ eq;
    Controls c; c.enabled = true;
    if (!setup(eq, fs, c)) return false;
    std::array<double, 1> input{1.}, output{};
    const auto samples = static_cast<std::size_t>(std::ceil(fs * .01));
    c.trimDb = 6.; eq.setControls(c);
    const double amplitude = std::pow(10., 6. / 20.);
    for (std::size_t n = 1; n <= samples; ++n)
    {
      eq.processBlock(input, output);
      const double expected = 1. + (amplitude - 1.) * static_cast<double>(n) / static_cast<double>(samples);
      worst = std::max(worst, std::abs(output[0] - expected));
      if (!expectNear("EQ linear 10 ms trim", output[0], expected, 2.e-12)) return false;
    }
    if (output[0] != amplitude) return false;
    c.enabled = false; eq.setControls(c);
    for (std::size_t n = 1; n <= samples; ++n)
    {
      eq.processBlock(input, output);
      const double expected = amplitude + (1. - amplitude) * static_cast<double>(n) / static_cast<double>(samples);
      worst = std::max(worst, std::abs(output[0] - expected));
      if (!expectNear("EQ linear 10 ms global bypass", output[0], expected, 2.e-12)) return false;
    }
    if (std::bit_cast<std::uint64_t>(output[0]) != std::bit_cast<std::uint64_t>(input[0])) return false;
  }
  std::cout << "METRIC eq exact_linear_fade max_abs_error=" << worst << '\n';
  return true;
}

bool invalidSnapshotAndInputRecovery()
{
  const auto input = signal(1024);
  for (double fs : rates)
  {
    EQ eq, reference;
    const auto c = colored();
    if (!setup(eq, fs, c) || !setup(reference, fs, c)) return false;
    std::vector<double> output(input.size()), expected(input.size());
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()})
    {
      for (unsigned field = 0; field < 6; ++field)
      {
        auto invalid = c;
        switch (field)
        {
          case 0: invalid.bands[1].frequencyHz = bad; break;
          case 1: invalid.bands[1].gainDb = bad; break;
          case 2: invalid.bands[1].q = bad; break;
          case 3: invalid.highPassHz = bad; break;
          case 4: invalid.lowPassHz = bad; break;
          default: invalid.trimDb = bad; break;
        }
        eq.setControls(invalid); run(eq, input, output); run(reference, input, expected);
        if (!expectSamplesBitExact("EQ rejects complete invalid snapshot", output, expected)) return false;
      }
      const std::array<double, 1> invalidInput{bad}; std::array<double, 1> recovered{};
      eq.processBlock(invalidInput, recovered);
      if (!std::isfinite(recovered[0])) return false;
      reference.reset();
      const std::array<double, 1> zero{0.}; reference.processBlock(zero, recovered);
      run(eq, input, output); run(reference, input, expected);
      if (!expectSamplesBitExact("EQ nonfinite input clears histories", output, expected)) return false;
    }
    auto pending = c; pending.bands[0].gainDb = -12.;
    eq.setControls(pending); eq.processBlock({}, {});
    if (Access::controls(eq) != c) return false;
    eq.processBlock(std::span<const double>{input}.first(1), std::span<double>{output}.first(1));
    if (Access::controls(eq) != pending) return false;
    const std::array<double, 3> extremes{std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), 1.};
    std::array<double, 3> finite{}; eq.processBlock(extremes, finite);
    if (!std::all_of(finite.begin(), finite.end(), [](double v) { return std::isfinite(v); })) return false;
  }
  return true;
}

bool concurrentSnapshots()
{
  EQ eq;
  const auto a = colored();
  auto b = a; b.enabled = false; b.highPassEnabled = false; b.highPassHz = 20.;
  b.lowPassEnabled = false; b.lowPassHz = 16000.; b.trimDb = 6.;
  b.bands = {{{80., -12., 4.}, {8000., 12., .35}, {300., -3., 2.}}};
  if (!setup(eq, 192000., a)) return false;
  std::atomic<bool> start{false}, done{false};
  std::thread producer([&] {
    while (!start.load(std::memory_order_acquire)) {}
    for (unsigned tick = 0; tick < 30000; ++tick) eq.setControls(tick % 2 == 0 ? b : a);
    eq.setControls(b);
    done.store(true, std::memory_order_release);
  });
  std::array<double, 7> input{}, output{}; input.fill(.01);
  std::size_t processed = 0;
  bool coherent = true;
  start.store(true, std::memory_order_release);
  do
  {
    eq.processBlock(input, output);
    const auto adopted = Access::controls(eq);
    coherent &= (adopted == a || adopted == b)
      && std::all_of(output.begin(), output.end(), [](double value) { return std::isfinite(value); });
    ++processed;
  } while (!done.load(std::memory_order_acquire));
  producer.join();
  eq.processBlock(input, output);
  std::cout << "METRIC eq concurrent_snapshots published=30001 audio_blocks=" << processed << '\n';
  return coherent && Access::controls(eq) == b && processed != 0;
}
} // namespace

TestSuite postAmpEQTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Post-amp EQ independent RBJ impulse response all rates", impulseAccuracy},
    TestCase{"Post-amp EQ sine center half-gain Q and reciprocal responses", bellGainQAndReciprocity},
    TestCase{"Post-amp EQ Butterworth cutoff and endpoint rejection", cutSemantics},
    TestCase{"Post-amp EQ stale neutral and bypass bit transparency", neutralAndBypass},
    TestCase{"Post-amp EQ sample-timed one irregular maximum partitions", partitions},
    TestCase{"Post-amp EQ lifecycle alias empty oversized contract", lifecycleAndAliasing},
    TestCase{"Post-amp EQ rapid extreme changes finite stability decay", rapidChangesAndDecay},
    TestCase{"Post-amp EQ exact 20 ms primitives 10 ms enable retarget", exactRampsAndRetargeting},
    TestCase{"Post-amp EQ exact linear trim and bypass fades", trimAndBypassLinearFades},
    TestCase{"Post-amp EQ invalid snapshots and bounded input recovery", invalidSnapshotAndInputRecovery},
    TestCase{"Post-amp EQ concurrent coherent latest SPSC snapshots", concurrentSnapshots},
    TestCase{"Post-amp EQ fixed realtime storage", fixedStorage}};
  return tests;
}
} // namespace holdsworth::test
