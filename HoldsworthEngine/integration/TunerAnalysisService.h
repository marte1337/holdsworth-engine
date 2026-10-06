#pragma once

#include "TunerCaptureBuffer.h"
#include "../dsp/ChromaticTuner.h"

#include <cstdint>

namespace holdsworth::integration
{
// Audio only calls capture().prepare/captureBlock. The control thread alone
// owns service() and all detector state. No control operation resets the FIFO.
class TunerAnalysisService final
{
public:
  [[nodiscard]] TunerCaptureBuffer& capture() noexcept { return mCapture; }
  [[nodiscard]] const TunerCaptureBuffer& capture() const noexcept { return mCapture; }

  // Monotonic seconds supplied by OnIdle, not read from the audio callback.
  // Returns true only for a new analysis. It never replays missed analyses.
  // First idle and long idle gaps discard queued data with unknown wall age;
  // the next estimate requires a complete subsequently captured window.
  [[nodiscard]] bool service(double nowSeconds) noexcept;
  [[nodiscard]] const dsp::TunerPitchEstimate& estimate() const noexcept { return mEstimate; }
  [[nodiscard]] double evidenceTime() const noexcept { return mEvidenceTime; }
  [[nodiscard]] std::uint64_t analysisCount() const noexcept { return mAnalysisCount; }
  [[nodiscard]] std::uint64_t historyRevision() const noexcept { return mHistoryRevision; }
  [[nodiscard]] bool active() const noexcept;

private:
  void invalidate(dsp::TunerPitchStatus status = dsp::TunerPitchStatus::collecting) noexcept;

  TunerCaptureBuffer mCapture;
  dsp::ChromaticTuner mDetector;
  dsp::TunerPitchEstimate mEstimate{};
  std::uint64_t mCursor = 0, mAnalysisCount = 0, mHistoryRevision = 0;
  std::uint32_t mToken = 0, mEpoch = 0, mLastSequence = 0, mAnalyzedSequence = 0;
  double mPreparedRate = 0., mLastIdle = 0., mLastProgress = 0.;
  double mNextAnalysis = 0., mEvidenceTime = 0.;
  bool mHaveIdle = false, mHaveSequence = false, mHaveAnalysis = false, mStalled = false;
};
} // namespace holdsworth::integration
