#include "TunerDisplayState.h"

#include <algorithm>
#include <cmath>

namespace holdsworth::integration
{
namespace
{
// Avoid changing a direction because pitch conversion rounded an exact cents
// threshold by a few floating-point ulps.
constexpr double kCentsThresholdSlack = 1.e-9;
}
void TunerDisplayState::reset(bool enabled) noexcept
{
  mEnabled = enabled;
  mHaveClock = mHaveObservation = mHaveRevision = false;
  mLastRejection = dsp::TunerPitchStatus::noSignal;
  clearPitch(enabled ? TunerDisplayStatus::listening : TunerDisplayStatus::off);
}

void TunerDisplayState::clearPitch(TunerDisplayStatus status) noexcept
{
  mSnapshot = {};
  mSnapshot.status = status;
  mHaveLock = mHaveEvidence = false;
  mDirection = TunerDirection::none;
  mHistoryCount = mHistoryWrite = mPendingCount = 0;
  mPendingNote = -1;
}

void TunerDisplayState::holdPitch() noexcept
{
  if (mHaveLock)
  {
    mSnapshot.status = TunerDisplayStatus::unstable;
    mSnapshot.direction = TunerDirection::none;
    mSnapshot.held = true;
  }
}

void TunerDisplayState::addHistory(double logFrequency) noexcept
{
  mHistory[mHistoryWrite] = logFrequency;
  mHistoryWrite = (mHistoryWrite + 1) % mHistory.size();
  mHistoryCount = std::min(mHistoryCount + 1, mHistory.size());
}

double TunerDisplayState::medianHistory() const noexcept
{
  auto ordered = mHistory;
  std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(mHistoryCount));
  return mHistoryCount == 2 ? .5 * (ordered[0] + ordered[1]) : ordered[mHistoryCount / 2];
}

void TunerDisplayState::publish(double logFrequency, bool fresh) noexcept
{
  const auto pitch = dsp::ChromaticTuner::frequencyToPitch(std::exp2(logFrequency));
  mSnapshot.status = fresh ? TunerDisplayStatus::stable : TunerDisplayStatus::unstable;
  mSnapshot.midiNote = pitch.midiNote;
  mSnapshot.octave = pitch.octave;
  mSnapshot.cents = pitch.cents;
  mSnapshot.wholeCents = static_cast<int>(std::lround(pitch.cents));
  mSnapshot.held = !fresh;
  if (!fresh)
    mSnapshot.direction = TunerDirection::none;
  else if (mDirection == TunerDirection::inTune && std::abs(pitch.cents) < 3. - kCentsThresholdSlack)
    mDirection = TunerDirection::inTune;
  else if (std::abs(pitch.cents) <= 2. + kCentsThresholdSlack)
    mDirection = TunerDirection::inTune;
  else
    mDirection = pitch.cents < 0. ? TunerDirection::flat : TunerDirection::sharp;
  if (fresh) mSnapshot.direction = mDirection;
}

void TunerDisplayState::accept(const dsp::TunerPitchEstimate& estimate, double evidence) noexcept
{
  if (!mEnabled)
    return;
  if (estimate.status == dsp::TunerPitchStatus::unsupportedRate)
  {
    clearPitch(TunerDisplayStatus::unsupportedRate);
    return;
  }
  if (!std::isfinite(evidence) || (mHaveObservation && evidence <= mObservationTime))
    return;
  if (mHaveObservation && evidence - mObservationTime >= kUncertainSeconds)
    mPendingCount = 0;
  mObservationTime = evidence;
  mHaveObservation = true;
  if (mHaveEvidence && evidence - mEvidenceTime >= kClearSeconds)
    clearPitch(TunerDisplayStatus::noSignal);

  const bool usable = estimate.status == dsp::TunerPitchStatus::valid
    && std::isfinite(estimate.frequencyHz)
    && estimate.frequencyHz >= dsp::ChromaticTuner::kMinimumFrequencyHz
    && estimate.frequencyHz <= dsp::ChromaticTuner::kMaximumFrequencyHz
    && std::isfinite(estimate.periodicity) && std::isfinite(estimate.rmsDbFS)
    && estimate.periodicity >= 1. - dsp::ChromaticTuner::kSustainDifferenceThreshold
    && estimate.rmsDbFS >= dsp::ChromaticTuner::kSustainRmsDbFS;
  if (!usable)
  {
    mLastRejection = estimate.status;
    mPendingCount = 0;
    if (mHaveLock)
      holdPitch();
    else
      clearPitch(estimate.status == dsp::TunerPitchStatus::noSignal ? TunerDisplayStatus::noSignal
        : estimate.status == dsp::TunerPitchStatus::collecting ? TunerDisplayStatus::listening
        : TunerDisplayStatus::unstable);
    return;
  }

  const auto pitch = dsp::ChromaticTuner::frequencyToPitch(estimate.frequencyHz);
  const double logFrequency = std::log2(estimate.frequencyHz);
  if (mHaveLock && pitch.midiNote == mSnapshot.midiNote)
  {
    mPendingCount = 0;
    addHistory(logFrequency);
    const double alpha = -std::expm1(-(evidence - mFilterTime) / kSmoothingSeconds);
    mFilteredLog += std::clamp(alpha, 0., 1.) * (medianHistory() - mFilteredLog);
    mFilterTime = mEvidenceTime = evidence;
    mHaveEvidence = true;
    mLastRejection = dsp::TunerPitchStatus::noSignal;
    publish(mFilteredLog, true);
    return;
  }

  mLastRejection = dsp::TunerPitchStatus::unstable;
  const bool highConfidence = estimate.highConfidence
    && estimate.periodicity >= 1. - dsp::ChromaticTuner::kAcquisitionDifferenceThreshold
    && estimate.rmsDbFS >= dsp::ChromaticTuner::kAcquisitionRmsDbFS;
  if (!highConfidence)
  {
    mPendingCount = 0;
    if (mHaveLock) holdPitch();
    else clearPitch(TunerDisplayStatus::unstable);
    return;
  }

  // Never feed a new note through the old note's EMA: doing so would display
  // phantom intermediate notes on large jumps and cents outside +/-50 near a
  // semitone boundary. Hold the entire previous snapshot while confirming.
  if (mPendingCount == 0 || mPendingNote != pitch.midiNote)
  {
    mPendingNote = pitch.midiNote;
    mPendingCount = 1;
    mPendingLog[0] = logFrequency;
    if (mHaveLock) holdPitch();
    else mSnapshot.status = TunerDisplayStatus::listening;
    return;
  }
  mPendingLog[1] = logFrequency;
  mHistoryCount = mHistoryWrite = 0;
  addHistory(mPendingLog[0]);
  addHistory(mPendingLog[1]);
  mFilteredLog = medianHistory();
  mFilterTime = mEvidenceTime = evidence;
  mHaveLock = mHaveEvidence = true;
  mPendingCount = 0;
  mDirection = TunerDirection::none;
  mLastRejection = dsp::TunerPitchStatus::noSignal;
  publish(mFilteredLog, true);
}

void TunerDisplayState::tick(double now) noexcept
{
  if (!std::isfinite(now) || (mHaveClock && now < mClockTime)
      || (mHaveEvidence && now < mEvidenceTime))
  {
    reset(mEnabled);
    return;
  }
  mClockTime = now;
  mHaveClock = true;
  if (mPendingCount != 0 && mHaveObservation && now - mObservationTime >= kUncertainSeconds)
    mPendingCount = 0;
  if (!mEnabled || !mHaveEvidence)
    return;
  const double age = now - mEvidenceTime;
  if (age >= kClearSeconds)
    clearPitch(mLastRejection == dsp::TunerPitchStatus::noSignal
      ? TunerDisplayStatus::noSignal : TunerDisplayStatus::unstable);
  else if (age >= kUncertainSeconds)
    holdPitch();
}

void TunerDisplayState::update(bool enabled, const dsp::TunerPitchEstimate& estimate, bool isNew,
                               std::uint64_t revision, double evidence, double now) noexcept
{
  if (!mHaveRevision || revision != mRevision || enabled != mEnabled)
  {
    reset(enabled);
    mRevision = revision;
    mHaveRevision = true;
  }
  if (enabled && (isNew || (estimate.status != dsp::TunerPitchStatus::valid
      && estimate.status != dsp::TunerPitchStatus::collecting)))
    accept(estimate, evidence);
  tick(now);
}
} // namespace holdsworth::integration
