#include "../dsp/TCBLDCleanBoostProcessor.h"
#include "../integration/DevelopmentTCBLDControlMapping.h"
#include "../integration/DevelopmentControlDefaults.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>

namespace holdsworth::test
{
namespace
{

using dsp::TCBLDCleanBoostControls;
using dsp::TCBLDCleanBoostProcessor;
using integration::tcBldControlsFromDevelopmentUI;

struct GoldenResponse final
{
  double gainDb;
  double phaseDegrees;
};

struct GoldenCurve final
{
  std::string_view id;
  TCBLDCleanBoostControls controls;
  std::array<GoldenResponse, 6> response;
};

// Direct transcription of representative points in M1
// golden/engaged-clean-boost-primary.json, results digest
// d5a8fa92b43650d559538f976b04e4fdc30023812bbebb6c4f03292ec54c2e15.
constexpr std::array<double, 6> kGoldenFrequenciesHz{{
  22.0863781529,
  97.5616199823,
  707.106781187,
  5124.96615053,
  10771.3032004,
  17673.3855069,
}};

constexpr std::array<GoldenCurve, 14> kGoldenCurves{{
  {"p1_0.1", {0.1, 0.5, 0.5}, {{{-12.3644310752, 48.4274983925},
                                  {-10.6295321708, 18.1646127117},
                                  {-8.55556297826, 5.01570741138},
                                  {-8.28666461659, -17.7466944079},
                                  {-8.96309012161, -38.1358584822},
                                  {-10.1818027234, -59.5793065964}}}},
  {"p1_terminal_unity", {0.13243092421, 0.5, 0.5}, {{{-4.2596805297, 49.0972934913},
                                                       {-2.50964895368, 18.6704768499},
                                                       {-0.289862657872, 6.95476540499},
                                                       {0.463385334945, -15.7884762695},
                                                       {-0.134869596262, -35.7449122395},
                                                       {-1.25323171285, -56.2601042615}}}},
  {"p1_0.25", {0.25, 0.5, 0.5}, {{{14.6750983138, 53.3809410202},
                                    {16.6851740313, 23.7344616143},
                                    {19.7894163889, 35.7018844506},
                                    {28.2722420372, 70.0650882548},
                                    {27.9815145141, 66.0627601087},
                                    {24.9343478188, 57.041220317}}}},
  {"p1_0.5", {0.5, 0.5, 0.5}, {{{36.3498271261, 125.293044267},
                                  {36.7757666846, 117.674769908},
                                  {24.1759320383, 123.595665114},
                                  {18.2380133759, 143.8420569},
                                  {16.9607537096, 126.832706572},
                                  {15.0248090208, 105.413702666}}}},
  {"p1_0.75", {0.75, 0.5, 0.5}, {{{34.6034151899, 176.133900972},
                                    {33.7969196893, 153.727088463},
                                    {23.4609219475, 130.802375294},
                                    {17.607579128, 145.170169834},
                                    {16.364116502, 127.917093921},
                                    {14.4787163861, 106.477861811}}}},
  {"p1_1", {1.0, 0.5, 0.5}, {{{33.6730256362, -176.787304611},
                                {33.001339822, 158.455350514},
                                {23.297684064, 132.207642471},
                                {17.4764901303, 145.433338455},
                                {16.2399262058, 128.133037477},
                                {14.3646405016, 106.69131007}}}},
  {"p2_0", {0.13243092421, 0.0, 0.5}, {{{2.71352795346, 51.518928264},
                                          {3.97700634646, 8.88263863377},
                                          {2.30778002968, -12.4365146491},
                                          {0.560693360754, -20.6649869224},
                                          {-0.111946926268, -38.1014791932},
                                          {-1.24442441664, -57.7036624876}}}},
  {"p2_1", {0.13243092421, 1.0, 0.5}, {{{-21.6047844662, 64.1495718295},
                                          {-14.9457630069, 62.7011627749},
                                          {-2.0034316085, 37.5929759739},
                                          {0.387227206488, -11.3215126755},
                                          {-0.198708340632, -33.6505486946},
                                          {-1.3146740428, -55.0099793826}}}},
  {"p3_0", {0.13243092421, 0.5, 0.0}, {{{-4.2662570777, 49.9361231418},
                                          {-2.41853779828, 22.0377059894},
                                          {0.722912918276, 25.2988556111},
                                          {8.53033972403, 2.18304052788},
                                          {8.66343686256, -31.3295178332},
                                          {7.27618445577, -59.1703341401}}}},
  {"p3_1", {0.13243092421, 0.5, 1.0}, {{{-4.26756562295, 48.2369519752},
                                          {-2.64223983624, 15.3641143383},
                                          {-1.27049666766, -6.43440890815},
                                          {-5.97866793578, -65.4511165083},
                                          {-15.070628114, -85.1435517456},
                                          {-19.4501262361, -41.5616982386}}}},
  {"p2_0_p3_0", {0.13243092421, 0.0, 0.0}, {{{2.70604634671, 51.825164858},
                                               {3.96036904613, 10.2992420136},
                                               {2.07641443256, 1.68615471321},
                                               {8.42088778715, 0.802848460347},
                                               {8.62868221983, -31.9205354997},
                                               {7.25557740894, -59.5316453942}}}},
  {"p2_0_p3_1", {0.13243092421, 0.0, 1.0}, {{{2.70893822374, 51.19071625},
                                               {3.98152413138, 7.46509951943},
                                               {2.25973153206, -25.3460263959},
                                               {-5.80056699308, -73.4287322079},
                                               {-15.297758537, -91.1740709203},
                                               {-20.0487720431, -41.8229933666}}}},
  {"p2_1_p3_0", {0.13243092421, 1.0, 0.0}, {{{-21.345730097, 69.0476442019},
                                               {-13.5743109073, 69.9965317846},
                                               {0.768839058303, 54.2601533645},
                                               {8.71046643826, 2.69079642372},
                                               {8.69367154844, -31.2697705602},
                                               {7.27555349505, -59.1621066294}}}},
  {"p2_1_p3_1", {0.13243092421, 1.0, 1.0}, {{{-21.8542111547, 58.9243239051},
                                               {-16.6325622908, 54.7597624358},
                                               {-4.18688944361, 29.1433747934},
                                               {-6.39553364607, -58.6994520264},
                                               {-15.4001384951, -79.5519203634},
                                               {-19.1360971918, -38.3505303196}}}},
}};

[[nodiscard]] double magnitudeDb(const std::complex<double> value) noexcept
{
  return 20.0 * std::log10(std::max(std::abs(value), 1.0e-300));
}

[[nodiscard]] double phaseDegrees(const std::complex<double> value) noexcept
{
  return std::arg(value) * 180.0 / std::numbers::pi;
}

[[nodiscard]] double wrappedPhaseError(const double actual,
                                       const double expected) noexcept
{
  return std::abs(std::remainder(actual - expected, 360.0));
}

bool compareGoldenAtSampleRate(const std::string_view testName,
                               const double sampleRate,
                               const std::size_t frequencyCount,
                               const double magnitudeToleranceDb,
                               const double phaseToleranceDegrees)
{
  TCBLDCleanBoostProcessor processor;
  processor.prepare(sampleRate, 64);
  double maximumMagnitudeError = 0.0;
  double maximumPhaseError = 0.0;
  double maximumReviewedUnstablePhaseDeparture = 0.0;
  std::string_view worstMagnitudeCurve;
  std::string_view worstPhaseCurve;
  double worstMagnitudeFrequency = 0.0;
  double worstPhaseFrequency = 0.0;

  for (const GoldenCurve& curve : kGoldenCurves)
  {
    // The M1 equations have right-half-plane poles at these reviewed Gain
    // positions. M2 preserves their magnitude through pole reflection, but a
    // causal stable processor cannot also preserve their phase. Complex phase
    // tolerances therefore apply to the stable Gain/tone configurations.
    const bool phaseComparable =
      curve.id != "p1_0.25" && curve.id != "p1_0.5"
      && curve.id != "p1_0.75" && curve.id != "p1_1";
    if (!processor.setControls(curve.controls))
      return false;
    for (std::size_t frequencyIndex = 0; frequencyIndex < frequencyCount; ++frequencyIndex)
    {
      const double frequency = kGoldenFrequenciesHz[frequencyIndex];
      const std::complex<double> response = processor.frequencyResponse(frequency);
      const double magnitudeError =
        std::abs(magnitudeDb(response) - curve.response[frequencyIndex].gainDb);
      const double phaseError = wrappedPhaseError(
        phaseDegrees(response), curve.response[frequencyIndex].phaseDegrees);
      if (magnitudeError > maximumMagnitudeError)
      {
        maximumMagnitudeError = magnitudeError;
        worstMagnitudeCurve = curve.id;
        worstMagnitudeFrequency = frequency;
      }
      if (phaseComparable && phaseError > maximumPhaseError)
      {
        maximumPhaseError = phaseError;
        worstPhaseCurve = curve.id;
        worstPhaseFrequency = frequency;
      }
      if (!phaseComparable)
        maximumReviewedUnstablePhaseDeparture =
          std::max(maximumReviewedUnstablePhaseDeparture, phaseError);
    }
  }

  std::cout << testName << ": maximum magnitude error " << maximumMagnitudeError
            << " dB at " << worstMagnitudeCurve << '/' << worstMagnitudeFrequency
            << " Hz; maximum phase error " << maximumPhaseError << " deg at "
            << worstPhaseCurve << '/' << worstPhaseFrequency
            << " Hz; reviewed unstable-Gain phase departure "
            << maximumReviewedUnstablePhaseDeparture << " deg\n";

  if (maximumMagnitudeError <= magnitudeToleranceDb
      && maximumPhaseError <= phaseToleranceDegrees)
    return true;
  return false;
}

bool testM1GoldenResponseAt192k()
{
  // At 192 kHz, the trapezoidal frequency mapping stays close across all six
  // representative points (22.1 Hz through 17.7 kHz), including four coupled
  // Bass/Treble corners and the reviewed non-monotonic Gain region.
  return compareGoldenAtSampleRate(
    "M1 192 kHz response", 192000.0, kGoldenFrequenciesHz.size(), 0.35, 2.3);
}

bool testM1GoldenAuditionBandAt48k()
{
  // This lower-rate criterion covers 22.1 Hz through 5.12 kHz, where most
  // guitar energy lies. Higher-frequency bilinear warping is reported
  // separately rather than hidden behind forced manual-EQ tuning.
  return compareGoldenAtSampleRate("M1 48 kHz audition band", 48000.0, 4, 0.35, 1.9);
}

bool testGainRetainsReviewedNonMonotonicOracleBehavior()
{
  TCBLDCleanBoostProcessor processor;
  processor.prepare(192000.0, 16);
  if (!processor.setControls({0.5, 0.5, 0.5}))
    return false;
  const double middleGainDb =
    magnitudeDb(processor.frequencyResponse(707.106781187));
  if (!processor.setControls({1.0, 0.5, 0.5}))
    return false;
  const double maximumShaftGainDb =
    magnitudeDb(processor.frequencyResponse(707.106781187));
  return middleGainDb > maximumShaftGainDb + 0.5;
}

bool testCoupledToneNetworkMovesMidband()
{
  TCBLDCleanBoostProcessor processor;
  processor.prepare(192000.0, 16);
  constexpr double frequencyHz = 707.106781187;
  std::array<double, 4> cornerGainDb{};
  std::size_t index = 0;
  for (const double bass : {0.0, 1.0})
  {
    for (const double treble : {0.0, 1.0})
    {
      if (!processor.setControls(
            {TCBLDCleanBoostProcessor::kTerminalUnityGainPosition, bass, treble}))
        return false;
      cornerGainDb[index++] = magnitudeDb(processor.frequencyResponse(frequencyHz));
    }
  }

  // If Bass and Treble were independent additive shelves, the mixed finite
  // difference H11-H10-H01+H00 would be zero. M1's shared passive network has
  // more than 5 dB of interaction here.
  const double interactionDb =
    cornerGainDb[3] - cornerGainDb[2] - cornerGainDb[1] + cornerGainDb[0];
  return std::abs(interactionDb) > 4.0;
}

bool testBlockPartitionAndInPlaceProcessingAreExact()
{
  constexpr std::size_t sampleCount = 511;
  std::array<double, sampleCount> input{};
  for (std::size_t index = 0; index < input.size(); ++index)
  {
    input[index] = 0.2 * std::sin(0.031 * static_cast<double>(index))
                   + 0.07 * std::cos(0.173 * static_cast<double>(index));
  }

  TCBLDCleanBoostProcessor whole;
  TCBLDCleanBoostProcessor partitioned;
  TCBLDCleanBoostProcessor inPlace;
  whole.prepare(48000.0, sampleCount);
  partitioned.prepare(48000.0, sampleCount);
  inPlace.prepare(48000.0, sampleCount);
  const TCBLDCleanBoostControls controls{0.25, 0.23, 0.81};
  if (!whole.setControls(controls) || !partitioned.setControls(controls)
      || !inPlace.setControls(controls))
    return false;

  std::array<double, sampleCount> wholeOutput{};
  std::array<double, sampleCount> partitionedOutput{};
  std::array<double, sampleCount> inPlaceOutput = input;
  whole.processBlock(input, wholeOutput);

  constexpr std::array<std::size_t, 7> partitions{{1, 7, 64, 3, 129, 11, 296}};
  std::size_t offset = 0;
  for (const std::size_t length : partitions)
  {
    partitioned.processBlock(
      std::span<const double>{input}.subspan(offset, length),
      std::span<double>{partitionedOutput}.subspan(offset, length));
    offset += length;
  }
  inPlace.processBlock(inPlaceOutput, inPlaceOutput);

  return offset == sampleCount
         && expectSamplesBitExact("TC BLD block partition", partitionedOutput, wholeOutput)
         && expectSamplesBitExact("TC BLD in-place", inPlaceOutput, wholeOutput);
}

bool testRealtimeCallsDoNotAllocate()
{
  TCBLDCleanBoostProcessor processor;
  processor.prepare(48000.0, 64);
  std::array<double, 64> samples{};

  beginAllocationTracking();
  const bool controlsApplied =
    processor.setControls({0.25, 0.0, 1.0})
    && processor.setControls({0.5, 1.0, 0.0})
    && processor.setControls({0.75, 0.0, 1.0});
  processor.processBlock(samples, samples);
  processor.reset();
  const std::size_t allocationCount = endAllocationTracking();
  return controlsApplied && allocationCount == 0;
}

bool testLatestStagedControlSetWinsAtBlockBoundary()
{
  TCBLDCleanBoostProcessor staged;
  TCBLDCleanBoostProcessor reference;
  staged.prepare(48000.0, 64);
  reference.prepare(48000.0, 64);
  constexpr std::array<TCBLDCleanBoostControls, 7> changes{{
    {0.25, 0.1, 0.9},
    {0.50, 0.8, 0.2},
    {0.75, 0.3, 0.7},
    {1.00, 1.0, 0.0},
    {0.10, 0.0, 1.0},
    {0.90, 0.6, 0.4},
    {0.40, 0.2, 0.8},
  }};
  for (const auto& controls : changes)
  {
    if (!staged.setControls(controls))
      return false;
  }
  if (!reference.setControls(changes.back()))
    return false;

  std::array<double, 64> input{};
  input.front() = 0.1;
  std::array<double, 64> stagedOutput{};
  std::array<double, 64> referenceOutput{};
  staged.processBlock(input, stagedOutput);
  reference.processBlock(input, referenceOutput);
  return expectSamplesBitExact(
    "TC BLD latest staged controls", stagedOutput, referenceOutput);
}

bool testCommonRatesAndControlExtremesRemainStable()
{
  constexpr std::array<double, 6> sampleRates{{
    44100.0,
    48000.0,
    88200.0,
    96000.0,
    176400.0,
    192000.0,
  }};
  constexpr std::array<TCBLDCleanBoostControls, 9> controls{{
    {0.0, 0.5, 0.5},
    {0.25, 0.5, 0.5},
    {0.5, 0.5, 0.5},
    {1.0, 0.5, 0.5},
    {TCBLDCleanBoostProcessor::kTerminalUnityGainPosition, 0.0, 0.0},
    {TCBLDCleanBoostProcessor::kTerminalUnityGainPosition, 0.0, 1.0},
    {TCBLDCleanBoostProcessor::kTerminalUnityGainPosition, 1.0, 0.0},
    {TCBLDCleanBoostProcessor::kTerminalUnityGainPosition, 1.0, 1.0},
    {},
  }};

  constexpr std::size_t blockSize = 257;
  constexpr std::size_t blockCount = 16;
  std::array<double, blockSize> input{};
  std::array<double, blockSize> output{};
  TCBLDCleanBoostProcessor processor;
  for (const double sampleRate : sampleRates)
  {
    processor.prepare(sampleRate, blockSize);
    for (const auto& currentControls : controls)
    {
      if (!processor.setControls(currentControls))
        return false;
      processor.reset();
      input.fill(0.0);
      input.front() = 0.1;
      for (std::size_t block = 0; block < blockCount; ++block)
      {
        processor.processBlock(input, output);
        input.fill(0.0);
        for (const double value : output)
        {
          if (!std::isfinite(value) || std::abs(value) > 1.0e3)
          {
            std::cerr << "unstable TC BLD render at " << sampleRate << " Hz, Gain="
                      << currentControls.gain << ", Bass=" << currentControls.bass
                      << ", Treble=" << currentControls.treble << ", block=" << block
                      << ", sample=" << value << '\n';
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool testControlSanitizationAndFiniteInputPolicy()
{
  TCBLDCleanBoostProcessor processor;
  if (!processor.setControls(
        {-1.0, 2.0, std::numeric_limits<double>::quiet_NaN()}))
    return false;
  const TCBLDCleanBoostControls controls = processor.controls();
  if (controls.gain != 0.0 || controls.bass != 1.0 || controls.treble != 0.5)
    return false;

  processor.prepare(48000.0, 3);
  const std::array<double, 3> input{{
    0.0,
    std::numeric_limits<double>::quiet_NaN(),
    std::numeric_limits<double>::infinity(),
  }};
  std::array<double, 3> output{};
  processor.processBlock(input, output);
  return std::all_of(output.begin(), output.end(), [](const double value) {
    return std::isfinite(value);
  });
}

bool testVoltsDomainBridgeAndFallbackIdentity()
{
  constexpr double sample = 0.375;
  for (const double calibrationDbu : {-60.0, -12.0, 0.0, 12.0, 24.0})
  {
    const double volts =
      TCBLDCleanBoostProcessor::normalizedSampleToVolts(sample, calibrationDbu);
    const double roundTrip =
      TCBLDCleanBoostProcessor::voltsToNormalizedSample(volts, calibrationDbu);
    if (!nearlyEqual(roundTrip, sample, 1.0e-14))
      return false;
  }

  constexpr double hostDbu = 12.0;
  constexpr double namDbu = -4.0;
  const double splitBridge =
    TCBLDCleanBoostProcessor::fullScalePeakVolts(hostDbu)
    / TCBLDCleanBoostProcessor::fullScalePeakVolts(namDbu);
  const double legacyCombinedGain = std::pow(10.0, (hostDbu - namDbu) / 20.0);
  return nearlyEqual(splitBridge, legacyCombinedGain, 1.0e-13)
         && TCBLDCleanBoostProcessor::fullScalePeakVolts(
              std::numeric_limits<double>::quiet_NaN()) == 1.0;
}

bool testDevelopmentUiToneEndpointsAreCutToBoost()
{
  constexpr double gain = TCBLDCleanBoostProcessor::kTerminalUnityGainPosition;
  constexpr auto bassMinimum = tcBldControlsFromDevelopmentUI(gain, 0.0, 0.5);
  constexpr auto bassMaximum = tcBldControlsFromDevelopmentUI(gain, 1.0, 0.5);
  constexpr auto trebleMinimum = tcBldControlsFromDevelopmentUI(gain, 0.5, 0.0);
  constexpr auto trebleMaximum = tcBldControlsFromDevelopmentUI(gain, 0.5, 1.0);
  static_assert(bassMinimum.bass == 1.0 && bassMaximum.bass == 0.0);
  static_assert(trebleMinimum.treble == 1.0 && trebleMaximum.treble == 0.0);

  TCBLDCleanBoostProcessor processor;
  processor.prepare(192000.0, 16);
  const auto responseDb = [&processor](const TCBLDCleanBoostControls& controls,
                                       const double frequencyHz) {
    if (!processor.setControls(controls))
      return std::numeric_limits<double>::quiet_NaN();
    return magnitudeDb(processor.frequencyResponse(frequencyHz));
  };

  // M1 P2=1/0 at 97.56 Hz and P3=1/0 at 10.77 kHz respectively.
  // These assertions verify both the wrapper reversal and the named oracle
  // endpoints without changing the processor's electrical coordinates.
  const double bassMinimumDb = responseDb(bassMinimum, 97.5616199823);
  const double bassMaximumDb = responseDb(bassMaximum, 97.5616199823);
  const double trebleMinimumDb = responseDb(trebleMinimum, 10771.3032004);
  const double trebleMaximumDb = responseDb(trebleMaximum, 10771.3032004);
  constexpr double toleranceDb = 0.35;
  return nearlyEqual(bassMinimumDb, -14.9457630069, toleranceDb)
         && nearlyEqual(bassMaximumDb, 3.97700634646, toleranceDb)
         && nearlyEqual(trebleMinimumDb, -15.070628114, toleranceDb)
         && nearlyEqual(trebleMaximumDb, 8.66343686256, toleranceDb)
         && bassMinimumDb < bassMaximumDb && trebleMinimumDb < trebleMaximumDb;
}

bool testDevelopmentResetDefaults()
{
  using namespace integration;
  DevelopmentGainRange range;
  range.setFocus(false);
  range.setPosition(0.9);
  range.reset();
  return range.focus && range.value == 0.132430924210101
         && kDevelopmentToneDefault == 0.5
         && kDevelopmentWetDefaultEncoded == 100'000 && kDevelopmentWetDefault == 0.1
         && tcBldControlsFromDevelopmentUI(range.value, 0.5, 0.5).gain == range.value;
}

bool testDevelopmentGainRangeMappingAndPreservation()
{
  using namespace integration;
  DevelopmentGainRange range;
  for (const double position : {0.0, 0.25, 0.5, 0.75, 1.0})
  {
    range.setPosition(position);
    if (!nearlyEqual(range.value, 0.10 + 0.20 * position)
        || !nearlyEqual(range.position(), position))
      return false;
  }
  for (const double value : {0.10, kDevelopmentGainDefault, 0.15, 0.23, 0.30})
  {
    range.value = value;
    for (int iteration = 0; iteration < 20; ++iteration)
    {
      if (range.setFocus(false) || range.value != value || range.position() != value)
        return false;
      if (range.setFocus(true) || range.value != value)
        return false;
    }
  }
  range.setFocus(false);
  range.setPosition(0.0);
  if (range.value != 0.0)
    return false;
  range.setPosition(1.0);
  return range.value == 1.0;
}

bool testDevelopmentGainFocusClampsOnlyOutsideRange()
{
  integration::DevelopmentGainRange range;
  range.setFocus(false);
  range.setPosition(0.0);
  if (!range.setFocus(true) || range.value != 0.10)
    return false;
  range.setFocus(false);
  range.setPosition(1.0);
  if (!range.setFocus(true) || range.value != 0.30)
    return false;
  range.setFocus(false);
  range.setPosition(0.23);
  return !range.setFocus(true) && range.value == 0.23;
}

constexpr std::array<TestCase, 15> kTests{{
  {"Development controls: exact reset defaults", testDevelopmentResetDefaults},
  {"Development Gain: Focus/Full mapping and exact preservation", testDevelopmentGainRangeMappingAndPreservation},
  {"Development Gain: explicit Focus selection clamps only outside range", testDevelopmentGainFocusClampsOnlyOutsideRange},
  {"TC BLD M1 golden response at 192 kHz", testM1GoldenResponseAt192k},
  {"TC BLD M1 golden 48 kHz guitar audition band", testM1GoldenAuditionBandAt48k},
  {"TC BLD reviewed Gain behavior is not made monotonic", testGainRetainsReviewedNonMonotonicOracleBehavior},
  {"TC BLD Bass/Treble network remains coupled", testCoupledToneNetworkMovesMidband},
  {"TC BLD block partition and in-place processing", testBlockPartitionAndInPlaceProcessingAreExact},
  {"TC BLD realtime calls allocate nothing", testRealtimeCallsDoNotAllocate},
  {"TC BLD latest staged controls win at a block boundary", testLatestStagedControlSetWinsAtBlockBoundary},
  {"TC BLD common rates and control extremes remain stable", testCommonRatesAndControlExtremesRemainStable},
  {"TC BLD controls and non-finite samples are sanitized", testControlSanitizationAndFiniteInputPolicy},
  {"TC BLD volts bridge preserves ideal-wire identity", testVoltsDomainBridgeAndFallbackIdentity},
  {"TC BLD development tone controls run from cut to boost", testDevelopmentUiToneEndpointsAreCutToBoost},
  {"TC BLD M1 identity is embedded", [] {
     return TCBLDCleanBoostProcessor::kOracleId == "TC-BLD-M1-OFFLINE-MNA-CLEAN-BOOST-V1"
            && TCBLDCleanBoostProcessor::kConfigurationId
                 == "STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY";
   }},
}};

} // namespace

TestSuite tcBldCleanBoostProcessorTests() noexcept
{
  return kTests;
}

} // namespace holdsworth::test
