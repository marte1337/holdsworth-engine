#pragma once

#include "../dsp/ChromaticTuner.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace holdsworth::integration
{
enum class TunerDisplayStatus { off, listening, noSignal, unstable, stable, unsupportedRate };
enum class TunerDirection { none, flat, inTune, sharp };

struct TunerDisplaySnapshot
{
  TunerDisplayStatus status = TunerDisplayStatus::off;
  TunerDirection direction = TunerDirection::none;
  int midiNote = -1;
  int octave = 0;
  int wholeCents = 0;
  double cents = 0.;
  // Held readings are historical and must not receive an in-tune highlight.
  bool held = false;
};

// Control/UI-thread-owned presentation state; no host input or DSP processing.
// Every accept() must represent fresh analysis evidence. Repeated timestamps
// neither confirm a new note nor keep an old reading alive.
class TunerDisplayState final
{
public:
  static constexpr double kSmoothingSeconds = .100;
  static constexpr double kUncertainSeconds = .150;
  static constexpr double kClearSeconds = .250;

  void reset(bool enabled) noexcept;
  void accept(const dsp::TunerPitchEstimate& estimate, double evidenceSeconds) noexcept;
  void tick(double nowSeconds) noexcept;
  [[nodiscard]] const TunerDisplaySnapshot& snapshot() const noexcept { return mSnapshot; }

  // Call every OnIdle. Only a true isNewEstimate submits pitch evidence;
  // unsupported-rate status and lifecycle resets are handled independently.
  void update(bool enabled, const dsp::TunerPitchEstimate& estimate, bool isNewEstimate,
              std::uint64_t historyRevision, double evidenceSeconds, double nowSeconds) noexcept;

private:
  void clearPitch(TunerDisplayStatus status) noexcept;
  void holdPitch() noexcept;
  void publish(double logFrequency, bool fresh) noexcept;
  void addHistory(double logFrequency) noexcept;
  [[nodiscard]] double medianHistory() const noexcept;

  TunerDisplaySnapshot mSnapshot{};
  std::array<double, 3> mHistory{};
  std::array<double, 2> mPendingLog{};
  std::size_t mHistoryCount = 0, mHistoryWrite = 0, mPendingCount = 0;
  int mPendingNote = -1;
  double mFilteredLog = 0., mFilterTime = 0., mEvidenceTime = 0.;
  double mObservationTime = 0., mClockTime = 0.;
  std::uint64_t mRevision = 0;
  dsp::TunerPitchStatus mLastRejection = dsp::TunerPitchStatus::noSignal;
  TunerDirection mDirection = TunerDirection::none;
  bool mEnabled = false, mHaveLock = false, mHaveEvidence = false;
  bool mHaveObservation = false, mHaveClock = false, mHaveRevision = false;
};
} // namespace holdsworth::integration
