#include "../dsp/ChromaticTuner.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <limits>
#include <numbers>
#include <vector>

namespace holdsworth::test
{
namespace
{
using Tuner = dsp::ChromaticTuner;
using Status = dsp::TunerPitchStatus;
constexpr std::array rates{44100., 48000., 88200., 96000., 176400., 192000.};
constexpr std::array offsets{-25., -10., -5., 0., 5., 10., 25.};
constexpr std::array phases{0., .731, 2.217};
constexpr double pi = std::numbers::pi;

double frequency(int midi, double cents = 0.)
{
  return 440. * std::exp2((static_cast<double>(midi - 69) + cents / 100.) / 12.);
}

double errorCents(const dsp::TunerPitchEstimate& result, double expected)
{
  return 1200. * std::log2(result.frequencyHz / expected);
}

double noise(std::uint32_t& state)
{
  state = state * 1664525U + 1013904223U;
  return 2. * static_cast<double>(state) / 4294967295. - 1.;
}

std::vector<float> sine(double fs, double hz, double phase = 0., double rmsDb = -18., double seconds = .13)
{
  std::vector<float> data(static_cast<std::size_t>(std::ceil(fs * seconds)));
  const double amplitude = std::sqrt(2.) * std::pow(10., rmsDb / 20.);
  for (std::size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<float>(amplitude * std::sin(2. * pi * hz * static_cast<double>(i) / fs + phase));
  return data;
}

bool checkPitch(const dsp::TunerPitchEstimate& result, double hz, int midi, double tolerance,
                double fs, double& maximum, bool requireHigh = true)
{
  const double error = result.frequencyHz > 0. ? std::abs(errorCents(result, hz)) : 1.e9;
  maximum = std::max(maximum, error);
  const double expectedCents = 1200. * std::log2(hz / frequency(midi));
  if (result.status == Status::valid && (!requireHigh || result.highConfidence)
      && result.midiNote == midi && result.octave == midi / 12 - 1
      && std::isfinite(result.cents) && std::abs(result.cents - expectedCents) <= tolerance && error <= tolerance)
    return true;
  std::cerr << "Tuner fs=" << fs << " expected=" << hz << " MIDI=" << midi
            << " got=" << result.frequencyHz << " MIDI=" << result.midiNote
            << " error=" << error << " score=" << result.periodicity
            << " rms=" << result.rmsDbFS << " status=" << static_cast<int>(result.status) << '\n';
  return false;
}

bool chromaticAccuracy()
{
  double maximum = 0.;
  std::size_t count = 0;
  for (double fs : rates)
  {
    Tuner tuner;
    if (!tuner.prepare(fs)) return false;
    for (int midi = 28; midi <= 88; ++midi)
      for (double cents : offsets)
        for (double phase : phases)
        {
          const double hz = frequency(midi, cents);
          tuner.reset();
          tuner.pushSamples(sine(fs, hz, phase));
          if (!checkPitch(tuner.analyze(), hz, midi, 1., fs, maximum)) return false;
          ++count;
        }
  }
  std::cout << "METRIC tuner clean_sine cases=" << count << " max_abs_cents=" << maximum << '\n';
  return true;
}

bool amplitudeAndDC()
{
  double maximum = 0.;
  for (double fs : rates)
    for (int midi : {28, 40, 45, 69, 88})
      for (double level : {-12., -36., -54., -60.})
      {
        Tuner tuner;
        if (!tuner.prepare(fs)) return false;
        auto data = sine(fs, frequency(midi, 10.), .731, level);
        // A modest interface DC offset must not turn a quiet guitar into silence
        // or change periodicity. Float capture precision is part of this test.
        for (auto& value : data) value += .025F;
        tuner.pushSamples(data);
        if (!checkPitch(tuner.analyze(), frequency(midi, 10.), midi, 1., fs, maximum)) return false;
      }
  for (double fs : rates)
  {
    Tuner tuner;
    if (!tuner.prepare(fs)) return false;
    for (double level : {-80., -100.})
    {
      tuner.reset();
      tuner.pushSamples(sine(fs, 110., 0., level));
      const auto result = tuner.analyze();
      if (result.status != Status::noSignal || result.highConfidence || result.midiNote != -1) return false;
    }
  }
  std::cout << "METRIC tuner amplitude_dc max_abs_cents=" << maximum << '\n';
  return true;
}

bool rejection()
{
  for (double fs : rates)
  {
    Tuner tuner;
    if (!tuner.prepare(fs) || tuner.analyze().status != Status::collecting) return false;
    std::vector<float> data(static_cast<std::size_t>(fs * .15));
    for (float value : {0.F, .3F, -.4F})
    {
      tuner.reset();
      std::fill(data.begin(), data.end(), value);
      tuner.pushSamples(data);
      const auto result = tuner.analyze();
      if (result.status != Status::noSignal || result.midiNote != -1) return false;
    }
    for (double hz : {20., 30., 34., 1401., 1500., 2000.})
    {
      tuner.reset();
      tuner.pushSamples(sine(fs, hz));
      if (tuner.analyze().status == Status::valid) return false;
    }
    for (double hz : {35.1, 1399.})
    {
      tuner.reset();
      tuner.pushSamples(sine(fs, hz, .731));
      double maximum = 0.;
      const int midi = static_cast<int>(std::lround(69. + 12. * std::log2(hz / 440.)));
      if (!checkPitch(tuner.analyze(), hz, midi, 1., fs, maximum)) return false;
    }
    // Without anti-alias filtering this native tone folds exactly to A4.
    tuner.reset();
    tuner.pushSamples(sine(fs, tuner.analysisSampleRate() - 440., 0., -12.));
    if (tuner.analyze().status == Status::valid) return false;
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
      tuner.reset();
      tuner.pushSamples(sine(fs, 110.));
      tuner.pushSamples(std::span<const float>{&bad, 1});
      if (tuner.analyze().status == Status::valid) return false;
      tuner.pushSamples(sine(fs, 110.));
      double maximum = 0.;
      if (!checkPitch(tuner.analyze(), 110., 45, 1., fs, maximum)) return false;
    }
  }
  Tuner tuner;
  for (double fs : {0., -1., 22050., 12345., std::numeric_limits<double>::quiet_NaN()})
    if (tuner.prepare(fs) || tuner.analyze().status != Status::unsupportedRate) return false;
  return true;
}

bool broadbandNoise()
{
  std::uint32_t seed = 0x71a912bcU;
  for (double fs : rates)
    for (bool pink : {false, true})
    {
      Tuner tuner;
      if (!tuner.prepare(fs)) return false;
      const int ticks = fs == 48000. ? 200 : 20; // ten-second reference fixtures
      std::vector<float> block(static_cast<std::size_t>(fs / 20.));
      double a = 0., b = 0., c = 0.;
      for (int tick = 0; tick < ticks; ++tick)
      {
        for (auto& value : block)
        {
          const double white = noise(seed);
          a = .99886 * a + .0555179 * white;
          b = .99332 * b + .0750759 * white;
          c = .969 * c + .153852 * white;
          value = static_cast<float>(pink ? .06 * (a + b + c + .5362 * white) : .2 * white);
        }
        tuner.pushSamples(block);
        if (tuner.analyze().highConfidence) return false;
      }
    }
  return true;
}

std::vector<float> guitar(double fs, double hz, bool weak, bool noisy, int phasePattern = 0)
{
  const std::array<double, 8> weights = weak
    ? std::array<double, 8>{.2, 1., .45, .3, .18, .1, .07, .04}
    : std::array<double, 8>{1., .7, .5, .35, .22, .13, .08, .05};
  std::vector<float> data(static_cast<std::size_t>(std::ceil(fs * .2)));
  std::uint32_t seed = 0x12ae2341U;
  double energy = 0.;
  for (std::size_t i = 0; i < data.size(); ++i)
  {
    const double time = static_cast<double>(i) / fs;
    double value = 0.;
    for (std::size_t h = 0; h < weights.size(); ++h)
    {
      const double index = static_cast<double>(h);
      const double phase = phasePattern == 0 ? .371 * index : phasePattern == 1 ? 0.
        : phasePattern == 2 ? .11 * (index + 1.) * (index + 1.) : .83 * (index + 1.) + .24 * index * index;
      value += weights[h] * std::sin(2. * pi * hz * static_cast<double>(h + 1) * time + phase);
    }
    value *= .1 * std::exp(-time / 1.5);
    data[i] = static_cast<float>(value);
    energy += value * value;
  }
  // Native-rate broadband noise, exactly specified at 25 dB nominal SNR.
  const double noiseScale = std::sqrt(3. * energy / static_cast<double>(data.size())) * std::pow(10., -25. / 20.);
  if (noisy) for (auto& value : data) value += static_cast<float>(noiseScale * noise(seed));
  return data;
}

bool harmonicAccuracy()
{
  double cleanMaximum = 0., noisyMaximum = 0.;
  std::size_t count = 0;
  for (double fs : rates)
  {
    Tuner tuner;
    if (!tuner.prepare(fs)) return false;
    for (int midi = 28; midi <= 88; ++midi)
      for (bool weak : {false, true})
        for (bool noisy : {false, true})
        {
          const double hz = frequency(midi, (midi % 3 - 1) * 10.);
          tuner.reset();
          tuner.pushSamples(guitar(fs, hz, weak, noisy));
          if (!checkPitch(tuner.analyze(), hz, midi, noisy ? 5. : 3., fs,
                          noisy ? noisyMaximum : cleanMaximum)) return false;
          ++count;
        }
  }
  std::cout << "METRIC tuner guitar cases=" << count << " clean_max_abs_cents=" << cleanMaximum
            << " snr25_max_abs_cents=" << noisyMaximum << '\n';
  return true;
}

bool harmonicBoundaryAccuracy()
{
  double maximum = 0.;
  std::size_t count = 0;
  for (double fs : rates)
  {
    Tuner tuner;
    if (!tuner.prepare(fs)) return false;
    for (int midi = 76; midi <= 88; ++midi)
      for (double cents : {-49., -25., 25., 49.})
        for (bool weak : {false, true})
          for (int phasePattern = 0; phasePattern != 4; ++phasePattern)
          {
            const double hz = frequency(midi, cents);
            tuner.reset();
            tuner.pushSamples(guitar(fs, hz, weak, false, phasePattern));
            const auto result = tuner.analyze();
            const double error = result.frequencyHz > 0. ? std::abs(errorCents(result, hz)) : 1.e9;
            maximum = std::max(maximum, error);
            // A small allowed measurement error can cross the nearest-note
            // boundary at +/-49 cents. Assert the measured conversion remains
            // coherent rather than requiring the source's nominal note name.
            const auto converted = Tuner::frequencyToPitch(result.frequencyHz);
            if (result.status != Status::valid || !result.highConfidence || error > 3.
                || result.midiNote != converted.midiNote || result.octave != converted.octave
                || !expectNear("Tuner rich boundary coherent cents", result.cents, converted.cents, 1.e-9))
            {
              std::cerr << "Tuner rich boundary fs=" << fs << " MIDI=" << midi << " cents=" << cents
                        << " weak=" << weak << " phase=" << phasePattern << " error=" << error
                        << " status=" << static_cast<int>(result.status) << '\n';
              return false;
            }
            ++count;
          }
  }
  std::cout << "METRIC tuner rich_boundary cases=" << count << " max_abs_cents=" << maximum << '\n';
  return true;
}

bool conversion()
{
  constexpr std::array names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  for (std::size_t i = 0; i < names.size(); ++i)
    if (std::string_view(Tuner::noteName(60 + static_cast<int>(i))) != names[i]) return false;
  for (int midi = 28; midi <= 88; ++midi)
    for (double cents : {-50.001, -49.999, -25., 0., 25., 49.999, 50.001})
    {
      const auto result = Tuner::frequencyToPitch(frequency(midi, cents));
      const int expectedMidi = midi + (cents > 50. ? 1 : cents < -50. ? -1 : 0);
      if (result.midiNote != expectedMidi || result.octave != expectedMidi / 12 - 1
          || !expectNear("Tuner cents conversion", result.cents,
                         cents - static_cast<double>(expectedMidi - midi) * 100., 1.e-9)) return false;
    }
  for (double invalid : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    if (Tuner::frequencyToPitch(invalid).midiNote != -1) return false;
  return true;
}

bool streamingAndAcquisition()
{
  double slowest = 0.;
  for (double fs : rates)
    for (int midi : {28, 40, 69, 88})
    {
      auto data = sine(fs, frequency(midi), .731, -18., .25);
      Tuner whole, split;
      if (!whole.prepare(fs) || !split.prepare(fs)) return false;
      whole.pushSamples(data);
      const std::array<std::size_t, 12> sizes{1, 2, 4, 8, 32, 64, 128, 256, 512, 1024, 7, 301};
      for (std::size_t i = 0, block = 0; i < data.size(); ++block)
      {
        const auto n = std::min(sizes[block % sizes.size()], data.size() - i);
        split.pushSamples(std::span<const float>{data}.subspan(i, n));
        i += n;
      }
      const auto a = whole.analyze(), b = split.analyze();
      if (a.frequencyHz != b.frequencyHz || a.periodicity != b.periodicity || a.cents != b.cents) return false;
      split.reset();
      const auto hop = static_cast<std::size_t>(fs * .01);
      bool acquired = false;
      for (std::size_t i = 0; i + hop <= data.size(); i += hop)
      {
        split.pushSamples(std::span<const float>{data}.subspan(i, hop));
        const auto result = split.analyze();
        if (result.highConfidence)
        {
          const double time = static_cast<double>(i + hop) / fs;
          slowest = std::max(slowest, time);
          if (result.midiNote != midi || std::abs(errorCents(result, frequency(midi))) > 1. || time > .15) return false;
          acquired = true;
          break;
        }
      }
      if (!acquired) return false;
      // Exercise history replacement without resetting the detector.
      split.pushSamples(sine(fs, frequency(57), .3));
      if (split.analyze().midiNote != 57) return false;
      std::vector<float> silence(static_cast<std::size_t>(fs * .15));
      split.pushSamples(silence);
      if (split.analyze().status != Status::noSignal) return false;
    }
  std::cout << "METRIC tuner detector_acquisition_max_ms=" << slowest * 1000. << '\n';
  return true;
}

bool fixedStorage()
{
  Tuner tuner;
  if (!tuner.prepare(192000.)) return false;
  const auto data = sine(192000., 110.);
  beginAllocationTracking();
  tuner.reset();
  tuner.pushSamples(data);
  const auto result = tuner.analyze();
  const auto allocations = endAllocationTracking();
  return allocations == 0 && result.highConfidence;
}
} // namespace

TestSuite chromaticTunerTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Tuner chromatic sine accuracy all rates phases cents", chromaticAccuracy},
    TestCase{"Tuner amplitude and DC accuracy", amplitudeAndDC},
    TestCase{"Tuner rejection and recovery", rejection},
    TestCase{"Tuner broadband noise rejection", broadbandNoise},
    TestCase{"Tuner harmonic weak fundamental and noise accuracy", harmonicAccuracy},
    TestCase{"Tuner harmonic boundary phase and offset accuracy", harmonicBoundaryAccuracy},
    TestCase{"Tuner note octave cents boundaries", conversion},
    TestCase{"Tuner partitions and detector acquisition", streamingAndAcquisition},
    TestCase{"Tuner fixed detector storage", fixedStorage}};
  return tests;
}
} // namespace holdsworth::test
