#include "TunerAnalysisService.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace holdsworth::integration
{
namespace
{
constexpr double kMaximumSampleAge = .15;
constexpr double kMaximumProgressAge = .15;
constexpr double kAnalysisInterval = .05;

// A bounded modular distance; a just-published packet may precede publication
// of its cursor, in which case it is fresh rather than billions of chunks old.
std::uint32_t pastChunks(std::uint32_t newest, std::uint32_t packet) noexcept
{
  const std::uint32_t distance = newest - packet;
  return distance < 0x80000000U ? distance : 0U;
}
} // namespace

bool TunerAnalysisService::active() const noexcept
{
  constexpr auto mask = TunerCaptureBuffer::kEnabledMask | TunerCaptureBuffer::kEditorOpenMask;
  return (mCapture.requestToken() & mask) == mask;
}

void TunerAnalysisService::invalidate(dsp::TunerPitchStatus status) noexcept
{
  mDetector.reset();
  mEstimate = {};
  mEstimate.status = status == dsp::TunerPitchStatus::collecting && mPreparedRate > 0. && !mDetector.isPrepared()
    ? dsp::TunerPitchStatus::unsupportedRate : status;
  mHaveSequence = mHaveAnalysis = false;
  mNextAnalysis = 0.;
  ++mHistoryRevision;
}

bool TunerAnalysisService::service(double now) noexcept
{
  if (!std::isfinite(now)) return false;
  const auto serviceStart = std::chrono::steady_clock::now();
  const auto token = mCapture.requestToken();
  const auto cursor = mCapture.latestCursor();
  const auto epoch = TunerCaptureBuffer::cursorEpoch(cursor);
  const bool timeGap = mHaveIdle && (now < mLastIdle || now - mLastIdle > kMaximumProgressAge);
  // A sample cursor measures age only relative to other samples. After an idle
  // outage (or before the first idle), queued audio could have stopped long ago.
  // Discard the bounded backlog and collect a new window after this wall-time
  // boundary instead of assigning old samples the current evidence timestamp.
  const bool discardBacklog = !mHaveIdle || timeGap;
  if (token != mToken || epoch != mEpoch || discardBacklog)
    invalidate();
  mToken = token;
  mEpoch = epoch;
  mLastIdle = now;
  if (!mHaveIdle || cursor != mCursor)
  {
    mLastProgress = now;
    mStalled = false;
  }
  mHaveIdle = true;
  mCursor = cursor;

  const bool enabled = active();
  if (!enabled && (mHaveSequence || mEstimate.status != dsp::TunerPitchStatus::collecting))
    invalidate();
  if (enabled && now - mLastProgress > kMaximumProgressAge && !mStalled)
  {
    invalidate(dsp::TunerPitchStatus::noSignal);
    mStalled = true;
  }

  // A fixed snapshot bounds work even if the producer continues publishing.
  const auto count = std::min(mCapture.available(), TunerCaptureBuffer::kQueueCapacity);
  TunerCaptureBuffer::Chunk chunk;
  for (std::size_t i = 0; i < count; ++i)
  {
    if (!mCapture.pop(chunk)) break;
    if (discardBacklog || !enabled || mStalled || chunk.requestToken != token || chunk.epoch != epoch)
      continue;
    const auto ageChunks = pastChunks(TunerCaptureBuffer::cursorSequence(cursor), chunk.sequence);
    const double age = static_cast<double>(ageChunks) * static_cast<double>(TunerCaptureBuffer::kChunkSamples)
      / chunk.sampleRate;
    if (!std::isfinite(age) || age > kMaximumSampleAge)
    {
      if (mHaveSequence) invalidate();
      continue;
    }
    if (chunk.sampleRate != mPreparedRate)
    {
      invalidate();
      mPreparedRate = chunk.sampleRate;
      mEstimate.status = mDetector.prepare(chunk.sampleRate)
        ? dsp::TunerPitchStatus::collecting : dsp::TunerPitchStatus::unsupportedRate;
    }
    if (mHaveSequence && chunk.sequence != mLastSequence + 1U)
      invalidate();
    mDetector.pushSamples(chunk.samples);
    mLastSequence = chunk.sequence;
    mHaveSequence = true;
    mEvidenceTime = now - age;
  }

  // A control change or producer discontinuity during this drain invalidates
  // the entire candidate window. Never publish a previous generation briefly.
  const auto candidateCurrent = [&]() noexcept {
    const auto latest = mCapture.latestCursor();
    const double age = mHaveSequence && mPreparedRate > 0.
      ? static_cast<double>(pastChunks(TunerCaptureBuffer::cursorSequence(latest), mLastSequence))
          * static_cast<double>(TunerCaptureBuffer::kChunkSamples) / mPreparedRate : 0.;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - serviceStart).count();
    return token == mCapture.requestToken() && epoch == TunerCaptureBuffer::cursorEpoch(latest)
      && age <= kMaximumSampleAge && elapsed <= kMaximumProgressAge;
  };
  if (!candidateCurrent())
  {
    invalidate();
    return false;
  }
  if (discardBacklog || !enabled || mStalled || !mHaveSequence || mDetector.samplesAvailable() != dsp::ChromaticTuner::kWindowSize
      || (mHaveAnalysis && (mLastSequence == mAnalyzedSequence || now < mNextAnalysis)))
    return false;

  mEstimate = mDetector.analyze();
  ++mAnalysisCount;
  if (!mHaveAnalysis) mNextAnalysis = now + kAnalysisInterval;
  else mNextAnalysis += (std::floor((now - mNextAnalysis) / kAnalysisInterval) + 1.) * kAnalysisInterval;
  mAnalyzedSequence = mLastSequence;
  mHaveAnalysis = true;
  if (!candidateCurrent())
  {
    invalidate();
    return false;
  }
  return true;
}
} // namespace holdsworth::integration
