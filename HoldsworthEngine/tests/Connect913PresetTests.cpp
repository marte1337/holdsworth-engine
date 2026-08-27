#include "../dsp/HoldsworthDelayPresets.h"
#include "TestHarness.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <span>
#include <string_view>

namespace holdsworth::test
{
namespace
{

using dsp::DelayBandId;
using dsp::HoldsworthDelayConfiguration;
using dsp::HoldsworthDelayEngine;
using presets::YamahaConnectControlState;

bool expectDocumentedExerciseBand(const std::string_view testName,
                                  const dsp::DocumentedYamahaBandValues& band,
                                  const double delayTimeMs)
{
  const bool valid =
    !band.switchState.has_value()
    && band.connectControlValue.has_value()
    && band.connectControlValue->state() == YamahaConnectControlState::input
    && !band.connectControlValue->sourceBand().has_value()
    && !band.groupControlValue.has_value()
    && band.feedbackControlValue.has_value()
    && nearlyEqual(band.feedbackControlValue->value, 0.0)
    && !band.speedControlValue.has_value()
    && !band.depthControlValue.has_value()
    && band.delayTimeMs.has_value()
    && nearlyEqual(band.delayTimeMs->value, delayTimeMs)
    && !band.panControlValue.has_value()
    && !band.levelControlValue.has_value()
    && !band.lowCutControlValue.has_value()
    && !band.highCutControlValue.has_value()
    && !band.tapPercentValue.has_value()
    && !band.waveformControlValue.has_value()
    && !band.delaySignalPhaseControlValue.has_value()
    && !band.syncControlValue.has_value();

  if (!valid)
    std::cerr << testName << ": documented 9.13 band values differ or invent an unspecified control\n";
  return valid;
}

bool expectNoDocumentedBandValues(const std::string_view testName,
                                  const dsp::DocumentedYamahaBandValues& band)
{
  const bool valid =
    !band.switchState.has_value()
    && !band.connectControlValue.has_value()
    && !band.groupControlValue.has_value()
    && !band.feedbackControlValue.has_value()
    && !band.speedControlValue.has_value()
    && !band.depthControlValue.has_value()
    && !band.delayTimeMs.has_value()
    && !band.panControlValue.has_value()
    && !band.levelControlValue.has_value()
    && !band.lowCutControlValue.has_value()
    && !band.highCutControlValue.has_value()
    && !band.tapPercentValue.has_value()
    && !band.waveformControlValue.has_value()
    && !band.delaySignalPhaseControlValue.has_value()
    && !band.syncControlValue.has_value();

  if (!valid)
    std::cerr << testName << ": undocumented 9.13 band contains invented source values\n";
  return valid;
}

bool expectManualExerciseIdentity(const std::string_view testName,
                                  const dsp::HoldsworthDelayPresetDefinition& preset)
{
  if (preset.documentedYamahaPresetIdentity.has_value()
      || preset.documentedYamahaSyncAuditionReference.has_value()
      || !preset.documentedYamahaManualExerciseReference.has_value())
  {
    std::cerr << testName << ": manual exercise was confused with patch-list identity\n";
    return false;
  }

  const auto& reference = *preset.documentedYamahaManualExerciseReference;
  const bool valid =
    reference.documentTitle == "Yamaha UD-Stomp Owner's Manual"
    && reference.displayedPatch == "9.13"
    && reference.memoryArea == dsp::YamahaPatchMemoryArea::preset
    && reference.groupNumber == 9
    && reference.bankNumber == 1
    && reference.patchNumber == 3
    && reference.initialStateStep == 18
    && reference.modifiedStateStep == 19;
  if (!valid)
    std::cerr << testName << ": manual exercise identity mismatch\n";
  return valid;
}

bool expectExactDocumentedSource(const std::string_view testName,
                                 const dsp::HoldsworthDelayPresetDefinition& preset)
{
  if (!expectManualExerciseIdentity(testName, preset)
      || !expectDocumentedExerciseBand(testName, preset.documentedYamahaValues[0], 600.0)
      || !expectDocumentedExerciseBand(testName, preset.documentedYamahaValues[1], 80.0)
      || preset.documentedYamahaGlobalValues.effectLevel.has_value()
      || preset.documentedYamahaGlobalValues.directLevel.has_value()
      || preset.documentedYamahaGlobalValues.directPan.has_value())
    return false;

  for (std::size_t bandIndex = 2; bandIndex < preset.documentedYamahaValues.size(); ++bandIndex)
  {
    if (!expectNoDocumentedBandValues(testName, preset.documentedYamahaValues[bandIndex]))
      return false;
  }
  return true;
}

bool equalBandConfiguration(const dsp::DelayBandConfiguration& actual,
                            const dsp::DelayBandConfiguration& expected) noexcept
{
  return actual.delayTimeMs == expected.delayTimeMs
         && actual.feedback.value == expected.feedback.value
         && actual.outputLevel == expected.outputLevel
         && actual.pan == expected.pan
         && actual.enabled == expected.enabled
         && actual.modulationRate.value == expected.modulationRate.value
         && actual.modulationDepth.value == expected.modulationDepth.value
         && actual.modulationPhase.value == expected.modulationPhase.value
         && actual.loopFilter.lowCut.has_value() == expected.loopFilter.lowCut.has_value()
         && actual.loopFilter.highCut.has_value() == expected.loopFilter.highCut.has_value()
         && (!actual.loopFilter.lowCut.has_value()
             || actual.loopFilter.lowCut->value == expected.loopFilter.lowCut->value)
         && (!actual.loopFilter.highCut.has_value()
             || actual.loopFilter.highCut->value == expected.loopFilter.highCut->value)
         && actual.tapFraction.value == expected.tapFraction.value
         && actual.modulationWaveform == expected.modulationWaveform
         && actual.delaySignalPolarity == expected.delaySignalPolarity;
}

bool configurationsMatchExceptAudioRouting(const HoldsworthDelayConfiguration& actual,
                                           const HoldsworthDelayConfiguration& expected)
{
  if (actual.globalWetOutputLevel != expected.globalWetOutputLevel)
    return false;

  for (std::size_t bandIndex = 0; bandIndex < actual.bands.size(); ++bandIndex)
  {
    if (!equalBandConfiguration(actual.bands[bandIndex], expected.bands[bandIndex]))
      return false;

    const auto& actualSync = actual.modulationSync.relationships[bandIndex];
    const auto& expectedSync = expected.modulationSync.relationships[bandIndex];
    if (actualSync.has_value() != expectedSync.has_value()
        || (actualSync.has_value()
            && (actualSync->masterBand != expectedSync->masterBand
                || actualSync->phaseOffset.value != expectedSync->phaseOffset.value)))
      return false;
  }
  return true;
}

bool expectPhysicalDiagnosticBand(const std::string_view testName,
                                  const dsp::DelayBandConfiguration& band,
                                  const double delayTimeMs)
{
  const bool valid =
    band.enabled
    && nearlyEqual(band.delayTimeMs, delayTimeMs)
    && nearlyEqual(band.feedback.value, 0.0)
    && nearlyEqual(band.outputLevel, 1.0)
    && nearlyEqual(band.pan, 0.0)
    && nearlyEqual(band.modulationRate.value, 0.0)
    && nearlyEqual(band.modulationDepth.value, 0.0)
    && nearlyEqual(band.modulationPhase.value, 0.0)
    && !band.loopFilter.lowCut.has_value()
    && !band.loopFilter.highCut.has_value()
    && nearlyEqual(band.tapFraction.value, 1.0)
    && band.delaySignalPolarity == dsp::DelaySignalPolarity::normal;
  if (!valid)
    std::cerr << testName << ": physical diagnostic band mismatch\n";
  return valid;
}

template <std::size_t ImpulseCount>
bool expectOnlyTimedImpulses(const std::string_view testName,
                             const std::span<const double> samples,
                             const std::array<std::size_t, ImpulseCount>& impulseIndices,
                             const double impulseLevel)
{
  for (std::size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
  {
    bool isImpulse = false;
    for (const std::size_t expectedIndex : impulseIndices)
      isImpulse = isImpulse || sampleIndex == expectedIndex;
    const double expected = isImpulse ? impulseLevel : 0.0;
    if (!nearlyEqual(samples[sampleIndex], expected, 1.0e-12))
    {
      std::cerr << testName << ": sample " << sampleIndex << " was " << samples[sampleIndex]
                << ", expected " << expected << '\n';
      return false;
    }
  }
  return true;
}

bool testSourceIdentityAndOriginalParallelStateRemainExact()
{
  const auto& parallel = dsp::presets::connect913ParallelDiagnosticV1();
  const auto& serial = dsp::presets::connect913SerialDiagnosticV1();
  return parallel.id == "connect913-parallel-diagnostic-v1"
         && parallel.displayName == "Yamaha 9.13 CONNECT Parallel Diagnostic"
         && serial.id == "connect913-serial-diagnostic-v1"
         && serial.displayName == "Yamaha 9.13 CONNECT Serial Diagnostic"
         && expectExactDocumentedSource("9.13 parallel source", parallel)
         && expectExactDocumentedSource("9.13 serial source", serial);
}

bool testPhysicalDiagnosticsDifferOnlyByBand2Routing()
{
  const auto& parallel = dsp::presets::connect913ParallelDiagnosticV1();
  const auto& serial = dsp::presets::connect913SerialDiagnosticV1();
  if (!configurationsMatchExceptAudioRouting(parallel.dspConfiguration, serial.dspConfiguration)
      || !expectPhysicalDiagnosticBand("9.13 Band 1", parallel.dspConfiguration.bands[0], 600.0)
      || !expectPhysicalDiagnosticBand("9.13 Band 2", parallel.dspConfiguration.bands[1], 80.0)
      || !nearlyEqual(parallel.dspConfiguration.globalWetOutputLevel, 1.0)
      || !nearlyEqual(parallel.requiredMaximumDelayTimeMs, 600.0)
      || !nearlyEqual(serial.requiredMaximumDelayTimeMs, 600.0))
    return false;

  for (std::size_t bandIndex = 2; bandIndex < parallel.dspConfiguration.bands.size(); ++bandIndex)
  {
    if (parallel.dspConfiguration.bands[bandIndex].enabled
        || serial.dspConfiguration.bands[bandIndex].enabled)
    {
      std::cerr << "9.13 diagnostic unexpectedly enabled Band " << (bandIndex + 1) << '\n';
      return false;
    }
  }

  for (std::size_t bandIndex = 0; bandIndex < dsp::kHoldsworthDelayBandCount; ++bandIndex)
  {
    if (parallel.dspConfiguration.modulationSync.relationships[bandIndex].has_value()
        || parallel.dspConfiguration.audioRouting.inputs[bandIndex].has_value())
    {
      std::cerr << "9.13 parallel diagnostic contains an inter-band relationship\n";
      return false;
    }

    const auto& serialInput = serial.dspConfiguration.audioRouting.inputs[bandIndex];
    if (bandIndex == 1)
    {
      if (!serialInput.has_value() || serialInput->sourceBand != DelayBandId::band1)
      {
        std::cerr << "9.13 serial diagnostic does not route Band 2 from Band 1\n";
        return false;
      }
    }
    else if (serialInput.has_value())
    {
      std::cerr << "9.13 serial diagnostic contains an extra CONNECT relationship\n";
      return false;
    }
  }
  return true;
}

bool testParallelAndSerialImpulseTiming()
{
  constexpr std::size_t sampleCount = 700;
  constexpr std::array<std::size_t, 2> parallelTimes{80, 600};
  constexpr std::array<std::size_t, 3> serialTimes{80, 600, 680};
  constexpr double panAngle = std::numbers::pi_v<double> * 0.25;
  const double leftLevel = std::cos(panAngle);
  const double rightLevel = std::sin(panAngle);

  std::array<double, sampleCount> input{};
  input[0] = 1.0;
  std::array<double, sampleCount> parallelLeft{};
  std::array<double, sampleCount> parallelRight{};
  std::array<double, sampleCount> serialLeft{};
  std::array<double, sampleCount> serialRight{};

  HoldsworthDelayEngine parallelEngine(700.0);
  HoldsworthDelayEngine serialEngine(700.0);
  parallelEngine.prepare(1000.0, sampleCount);
  serialEngine.prepare(1000.0, sampleCount);
  if (!parallelEngine.applyConfiguration(
         dsp::presets::connect913ParallelDiagnosticV1().dspConfiguration)
         .wasApplied()
      || !serialEngine.applyConfiguration(
            dsp::presets::connect913SerialDiagnosticV1().dspConfiguration)
            .wasApplied())
    return false;

  parallelEngine.processBlock(input, parallelLeft, parallelRight);
  serialEngine.processBlock(input, serialLeft, serialRight);
  return expectOnlyTimedImpulses("9.13 parallel left", parallelLeft, parallelTimes, leftLevel)
         && expectOnlyTimedImpulses("9.13 parallel right", parallelRight, parallelTimes, rightLevel)
         && expectOnlyTimedImpulses("9.13 serial left", serialLeft, serialTimes, leftLevel)
         && expectOnlyTimedImpulses("9.13 serial right", serialRight, serialTimes, rightLevel)
         && serialLeft[0] == 0.0 && serialRight[0] == 0.0
         && parallelLeft[680] == 0.0 && parallelRight[680] == 0.0;
}

bool testDiagnosticConfigurationApplicationDoesNotAllocate()
{
  HoldsworthDelayEngine engine(700.0);
  engine.prepare(48000.0, 64);

  beginAllocationTracking();
  const auto parallelResult = engine.applyConfiguration(
    dsp::presets::connect913ParallelDiagnosticV1().dspConfiguration);
  const auto serialResult = engine.applyConfiguration(
    dsp::presets::connect913SerialDiagnosticV1().dspConfiguration);
  const std::size_t allocations = endAllocationTracking();

  if (allocations != 0)
    std::cerr << "9.13 diagnostic application made " << allocations << " allocation(s)\n";
  return parallelResult.wasApplied() && serialResult.wasApplied() && allocations == 0;
}

constexpr std::array kTests{
  TestCase{"Yamaha 9.13 CONNECT: manual identity and original parallel source state remain exact",
           testSourceIdentityAndOriginalParallelStateRemainExact},
  TestCase{"Yamaha 9.13 CONNECT: physical diagnostics differ only by Band 2 routing",
           testPhysicalDiagnosticsDifferOnlyByBand2Routing},
  TestCase{"Yamaha 9.13 CONNECT: parallel and serial impulse timings are exact",
           testParallelAndSerialImpulseTiming},
  TestCase{"Yamaha 9.13 CONNECT: diagnostic configuration application performs no allocations",
           testDiagnosticConfigurationApplicationDoesNotAllocate},
};

} // namespace

TestSuite connect913PresetTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
