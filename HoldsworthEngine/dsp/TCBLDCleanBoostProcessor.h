#pragma once

#include <array>
#include <atomic>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace holdsworth::dsp
{

// Mechanical/electrical positions from the frozen M1 profile. All three
// controls use the inclusive [0, 1] range. Gain is a mechanical shaft position
// mapped through TAPER-LOG-10PCT-MID-V1; Bass and Treble are the selected
// TAPER-LIN-IDEAL-V1 electrical positions.
struct TCBLDCleanBoostControls final
{
  double gain = 0.13243092421; // M1 full-circuit 1 kHz terminal-unity point.
  double bass = 0.5;
  double treble = 0.5;
};

// Realtime engaged CLEAN BOOST reduction of the frozen M1 documentary-nominal
// circuit. This is not a hardware-calibrated vintage-pedal model. It omits
// Distortion, the dynamic Noise Suppressor, bypass electronics, tolerances,
// clipping, and slew behavior exactly as declared by M1.
//
// prepare(), reset(), setControls(), and processBlock() allocate no memory.
// One control-producing thread may call setControls() concurrently with the
// audio thread; a bounded SPSC handoff applies the latest complete coefficient
// set at the next block. prepare()/reset() still require normal
// host lifecycle synchronization. processBlock() performs only a precomputed
// stable 24-state recurrence; it never solves MNA or performs the fit.
class TCBLDCleanBoostProcessor final
{
public:
  using Sample = double;

  static constexpr std::string_view kDocumentaryProfileId =
    "TC-BLD-DOC-NOMINAL__S1501-3__B1501-06__L1501-6__AS-M0A-001";
  static constexpr std::string_view kOracleId =
    "TC-BLD-M1-OFFLINE-MNA-CLEAN-BOOST-V1";
  static constexpr std::string_view kConfigurationId =
    "STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY";
  static constexpr std::string_view kGoldenResultsSha256 =
    "d5a8fa92b43650d559538f976b04e4fdc30023812bbebb6c4f03292ec54c2e15";
  static constexpr double kTerminalUnityGainPosition = 0.13243092421;

  TCBLDCleanBoostProcessor() = default;

  // Throws std::invalid_argument for an invalid sample rate/block contract and
  // std::runtime_error only if the fixed nominal circuit cannot be factored.
  // No exception can originate from processBlock().
  void prepare(double sampleRate, std::size_t maximumBlockSize);
  void reset() noexcept;

  // Non-finite values select that control's default; finite values are clamped
  // to [0, 1]. Returns false only if the fixed matrix could not be factored, in
  // which case the previous controls and coefficients remain active. A
  // successful change is published for the next processBlock(), which resets
  // the realization state because its fitted coordinates changed.
  [[nodiscard]] bool setControls(const TCBLDCleanBoostControls& controls) noexcept;
  [[nodiscard]] TCBLDCleanBoostControls controls() const noexcept { return mControls; }

  // Exact in-place processing is supported. Invalid calls assert in Debug and
  // fall back to a direct copy where possible in Release.
  void processBlock(std::span<const Sample> input, std::span<Sample> output) noexcept;

  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }
  [[nodiscard]] double sampleRate() const noexcept { return mSampleRate; }
  [[nodiscard]] std::size_t maximumBlockSize() const noexcept { return mMaximumBlockSize; }

  // Non-realtime diagnostic of the actual discrete recurrence. It is provided
  // so the processor can be compared directly with M1 magnitude and phase.
  // Call it only while setControls()/processBlock() are externally quiescent.
  [[nodiscard]] std::complex<double> frequencyResponse(double frequencyHz) const noexcept;

  // Explicit normalized-sample/volts bridge from the M1 handoff. These helpers
  // define voltage scale only; they do not reconstruct pickup/cable/interface
  // loading that already happened before the ADC.
  [[nodiscard]] static double fullScalePeakVolts(double calibrationDbu) noexcept;
  [[nodiscard]] static double normalizedSampleToVolts(double sample,
                                                       double calibrationDbu) noexcept;
  [[nodiscard]] static double voltsToNormalizedSample(double volts,
                                                       double calibrationDbu) noexcept;

private:
  static constexpr std::size_t kStateCount = 24;
  static constexpr std::size_t kCoefficientQueueCapacity = 4;
  static constexpr std::uint32_t kCoefficientSlotFree = 0;
  static constexpr std::uint32_t kCoefficientSlotWriting = 1;
  static constexpr std::uint32_t kCoefficientSlotReady = 2;
  static constexpr std::uint32_t kCoefficientSlotReading = 3;

  struct Coefficients final
  {
    std::array<std::array<double, kStateCount>, kStateCount> transition{};
    std::array<double, kStateCount> input{};
    std::array<double, kStateCount> output{};
    double direct = 0.0;
  };

  struct CoefficientSlot final
  {
    Coefficients coefficients{};
    std::atomic<std::uint32_t> sequence{0};
    std::atomic<std::uint32_t> status{kCoefficientSlotFree};
  };

  [[nodiscard]] bool buildCoefficients(const TCBLDCleanBoostControls& controls,
                                       Coefficients& destination) const noexcept;
  [[nodiscard]] bool buildStableApproximation(const Coefficients& oracleCoefficients,
                                              Coefficients& destination) const noexcept;
  [[nodiscard]] static TCBLDCleanBoostControls
  sanitizeControls(const TCBLDCleanBoostControls& controls) noexcept;

  Coefficients mCoefficients{};
  std::array<CoefficientSlot, kCoefficientQueueCapacity> mCoefficientQueue{};
  std::array<double, kStateCount> mState{};
  TCBLDCleanBoostControls mControls{};
  std::uint32_t mNextCoefficientSequence = 1;
  double mSampleRate = 0.0;
  std::size_t mMaximumBlockSize = 0;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
