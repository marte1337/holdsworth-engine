#include "../integration/TunerDisplayState.h"
#include "../integration/TunerAnalysisService.h"
#include "TestHarness.h"

#include <array>
#include <limits>
#include <numbers>
#include <vector>

namespace holdsworth::test
{
namespace
{
using Display = integration::TunerDisplayState;
using Status = integration::TunerDisplayStatus;
using Direction = integration::TunerDirection;
using PitchStatus = dsp::TunerPitchStatus;

dsp::TunerPitchEstimate pitch(int midi, double cents = 0., bool high = true)
{
  const double hz = 440. * std::exp2((static_cast<double>(midi - 69) + cents / 100.) / 12.);
  auto estimate = dsp::ChromaticTuner::frequencyToPitch(hz);
  estimate.highConfidence = high;
  estimate.periodicity = high ? .99 : .87;
  estimate.rmsDbFS = high ? -18. : -67.;
  return estimate;
}

bool lock(Display& display, int midi, double cents = 0., double start = 0.)
{
  display.reset(true);
  display.accept(pitch(midi, cents), start);
  display.tick(start);
  if (display.snapshot().midiNote != -1) return false;
  display.accept(pitch(midi, cents), start + .05);
  display.tick(start + .05);
  return display.snapshot().status == Status::stable && display.snapshot().midiNote == midi;
}

bool acquisitionAndConfidence()
{
  Display display;
  display.reset(true);
  display.accept(pitch(69, 0., false), 0.);
  display.accept(pitch(69, 0., false), .05);
  if (display.snapshot().midiNote != -1 || display.snapshot().status != Status::unstable) return false;
  display.accept(pitch(69), .10);
  display.accept(pitch(69), .10); // A replay does not confirm a note.
  if (display.snapshot().midiNote != -1) return false;
  display.accept(pitch(69), .15);
  if (display.snapshot().status != Status::stable || display.snapshot().midiNote != 69) return false;
  display.accept(pitch(69, 0., false), .20);
  if (display.snapshot().status != Status::stable) return false;
  display.accept(pitch(71, 0., false), .25);
  if (display.snapshot().midiNote != 69 || !display.snapshot().held) return false;
  display.accept(pitch(71), .30);
  if (display.snapshot().midiNote != 69) return false;
  display.accept(pitch(71), .35);
  if (display.snapshot().midiNote != 71 || display.snapshot().status != Status::stable) return false;

  // Confirmation candidates expire, including before any lock exists.
  display.reset(true);
  display.accept(pitch(69), 0.);
  display.tick(.20);
  display.accept(pitch(69), 10.);
  if (display.snapshot().midiNote != -1) return false;
  display.accept(pitch(69), 10.05);
  if (display.snapshot().midiNote != 69) return false;
  display.reset(true);
  auto malformed = pitch(69);
  malformed.periodicity = .5;
  display.accept(malformed, 0.);
  display.accept(malformed, .05);
  if (display.snapshot().midiNote != -1) return false;
  malformed = pitch(69);
  malformed.rmsDbFS = -80.;
  display.accept(malformed, .10);
  display.accept(malformed, .15);
  return display.snapshot().midiNote == -1;
}

bool medianAndSmoothing()
{
  Display display;
  if (!lock(display, 69)) return false;
  display.accept(pitch(69, 10.), .10);
  if (!expectNear("Tuner display first step median", display.snapshot().cents, 0., 1.e-9)) return false;
  display.accept(pitch(69, 10.), .15);
  const double alpha = 1. - std::exp(-.05 / Display::kSmoothingSeconds);
  if (!expectNear("Tuner display 100 ms EMA", display.snapshot().cents, 10. * alpha, 1.e-9)) return false;
  display.accept(pitch(69, 10.), .20);
  if (!expectNear("Tuner display second EMA step", display.snapshot().cents, 10. * (1. - std::exp(-1.)), 1.e-9)) return false;
  if (!lock(display, 69)) return false;
  display.accept(pitch(69, 40.), .10);
  display.accept(pitch(69), .15);
  display.accept(pitch(69), .20);
  return expectNear("Tuner display median rejects one outlier", display.snapshot().cents, 0., 1.e-9);
}

bool noteBoundariesAndJumps()
{
  Display display;
  if (!lock(display, 69, 49.)) return false;
  const auto old = display.snapshot();
  display.accept(pitch(70, -49.), .10);
  if (display.snapshot().midiNote != old.midiNote || display.snapshot().cents != old.cents
      || !display.snapshot().held || display.snapshot().direction != Direction::none) return false;
  display.accept(pitch(70, -49.), .15);
  if (display.snapshot().midiNote != 70 || display.snapshot().held
      || !expectNear("Tuner boundary switches note/cents together", display.snapshot().cents, -49., 1.e-9)) return false;
  double time = .15;
  for (int i = 0; i < 30; ++i)
  {
    time += .05;
    display.accept(i % 2 == 0 ? pitch(69, 49.) : pitch(70, -49.), time);
    display.tick(time);
    if (display.snapshot().midiNote != 70 || std::abs(display.snapshot().cents) > 50.) return false;
  }
  if (!lock(display, 28)) return false;
  display.accept(pitch(88), .10);
  if (display.snapshot().midiNote != 28) return false;
  display.accept(pitch(88), .15);
  return display.snapshot().midiNote == 88 && display.snapshot().status == Status::stable;
}

bool directionHysteresis()
{
  Display display;
  if (!lock(display, 69) || display.snapshot().direction != Direction::inTune) return false;
  double time = .05;
  const auto settle = [&](double cents, Direction expected)
  {
    for (int i = 0; i < 30; ++i)
    {
      time += .10;
      display.accept(pitch(69, cents), time);
      display.tick(time);
    }
    return display.snapshot().direction == expected;
  };
  if (!settle(2.5, Direction::inTune) || !settle(3.5, Direction::sharp)
      || !settle(2.5, Direction::sharp) || !settle(1.5, Direction::inTune)
      || !settle(-3.5, Direction::flat) || !settle(-2.5, Direction::flat)
      || !settle(-1.5, Direction::inTune)) return false;
  for (double cents : {-2., 2.})
    if (!lock(display, 69, cents) || display.snapshot().direction != Direction::inTune) return false;
  for (double cents : {-3., 3.})
    if (!lock(display, 69, cents) || display.snapshot().direction == Direction::inTune) return false;
  return true;
}

bool evidenceAging()
{
  Display display;
  if (!lock(display, 69)) return false;
  display.tick(.199);
  if (display.snapshot().status != Status::stable) return false;
  display.tick(.201);
  if (display.snapshot().midiNote != 69 || !display.snapshot().held
      || display.snapshot().direction != Direction::none || display.snapshot().status != Status::unstable) return false;
  display.tick(.299);
  if (display.snapshot().midiNote != 69) return false;
  display.tick(.301);
  if (display.snapshot().midiNote != -1 || display.snapshot().status != Status::noSignal) return false;
  display.accept(pitch(69), .05); // Replay after clearing is still old evidence.
  if (display.snapshot().midiNote != -1) return false;
  display.accept(pitch(69), .35);
  display.accept(pitch(69), .40);
  dsp::TunerPitchEstimate bad;
  bad.status = PitchStatus::unstable;
  display.accept(bad, .45);
  if (!display.snapshot().held || display.snapshot().direction != Direction::none) return false;
  display.tick(.651);
  return display.snapshot().midiNote == -1 && display.snapshot().status == Status::unstable;
}

bool contextAndClocks()
{
  Display display;
  const auto good = pitch(69);
  display.update(true, good, true, 1, 0., 0.);
  display.update(true, good, true, 1, .05, .05);
  if (display.snapshot().status != Status::stable) return false;
  display.update(true, good, false, 1, .05, .201);
  if (!display.snapshot().held) return false;
  display.update(true, good, false, 2, .05, .21);
  if (display.snapshot().midiNote != -1 || display.snapshot().status != Status::listening) return false;
  display.update(true, good, true, 2, .25, .25);
  display.update(true, good, true, 2, .30, .30);
  display.update(false, good, false, 2, .30, .31);
  if (display.snapshot().status != Status::off || display.snapshot().midiNote != -1) return false;
  display.update(true, good, false, 2, .30, .32);
  if (display.snapshot().midiNote != -1) return false;
  dsp::TunerPitchEstimate unavailable;
  unavailable.status = PitchStatus::unsupportedRate;
  display.update(true, unavailable, false, 3, .32, .33);
  if (display.snapshot().status != Status::unsupportedRate) return false;
  unavailable.status = PitchStatus::noSignal;
  display.update(true, unavailable, false, 4, .33, .34);
  if (display.snapshot().status != Status::noSignal) return false;
  if (!lock(display, 69, 0., 1.)) return false;
  display.tick(.5);
  if (display.snapshot().midiNote != -1) return false;
  if (!lock(display, 69)) return false;
  display.tick(std::numeric_limits<double>::quiet_NaN());
  return display.snapshot().midiNote == -1 && display.snapshot().status == Status::listening;
}

bool pipelineAcquisition()
{
  constexpr std::array rates{44100., 48000., 88200., 96000., 176400., 192000.};
  double slowest = 0.;
  for (double fs : rates)
    for (int midi : {28, 40, 69, 88})
    {
      integration::TunerAnalysisService service;
      Display display;
      auto& capture = service.capture();
      capture.prepare(fs);
      capture.setEnabled(true);
      capture.setEditorOpen(true);
      const auto hop = static_cast<std::size_t>(fs / 100.);
      std::vector<double> input(hop);
      const double hz = pitch(midi).frequencyHz;
      bool acquired = false;
      for (std::size_t tick = 0; tick < 25; ++tick)
      {
        for (std::size_t i = 0; i < hop; ++i)
          input[i] = .2 * std::sin(2. * std::numbers::pi * hz * static_cast<double>(tick * hop + i) / fs);
        // A sequence of small and irregular host callbacks within each idle hop.
        constexpr std::array<std::size_t, 5> blocks{1, 64, 7, 128, 301};
        for (std::size_t offset = 0, block = 0; offset < hop; ++block)
        {
          const auto frames = std::min(blocks[block % blocks.size()], hop - offset);
          const double* pointer = input.data() + offset;
          capture.captureBlock(&pointer, frames, 1, true);
          offset += frames;
        }
        const double now = static_cast<double>((tick + 1) * hop) / fs;
        const bool analyzed = service.service(now);
        display.update(service.active(), service.estimate(), analyzed, service.historyRevision(), service.evidenceTime(), now);
        if (display.snapshot().status == Status::stable)
        {
          if (display.snapshot().midiNote != midi || std::abs(display.snapshot().cents) > 1.) return false;
          slowest = std::max(slowest, now);
          acquired = true;
          break;
        }
      }
      if (!acquired) return false;
    }
  std::cout << "METRIC tuner capture_to_display_acquisition_max_ms=" << slowest * 1000. << '\n';
  return slowest <= .250;
}

bool fixedStorage()
{
  Display display;
  const auto good = pitch(69);
  beginAllocationTracking();
  display.reset(true);
  display.accept(good, 0.);
  display.accept(good, .05);
  display.tick(.201);
  display.update(false, good, false, 1, .05, .30);
  const auto allocations = endAllocationTracking();
  return allocations == 0 && display.snapshot().status == Status::off;
}
} // namespace

TestSuite tunerDisplayStateTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Tuner display acquisition confidence and candidate expiry", acquisitionAndConfidence},
    TestCase{"Tuner display median log frequency and 100 ms EMA", medianAndSmoothing},
    TestCase{"Tuner display coherent boundaries and no phantom notes", noteBoundariesAndJumps},
    TestCase{"Tuner display flat in tune sharp hysteresis", directionHysteresis},
    TestCase{"Tuner display fresh evidence and 150 250 ms aging", evidenceAging},
    TestCase{"Tuner display context revision clock and unavailable status", contextAndClocks},
    TestCase{"Tuner display full pipeline acquisition all rates", pipelineAcquisition},
    TestCase{"Tuner display fixed storage", fixedStorage}};
  return tests;
}
} // namespace holdsworth::test
