#pragma once

#include <cmath>
#include <cstdint>
#include <span>

namespace holdsworth::integration
{
enum class DevelopmentPreNAMProcessor : std::uint32_t { off = 0, tcBld = 1, mc402Boost = 2, jRockettAH = 3 };

inline DevelopmentPreNAMProcessor preNAMProcessorFromNormalized(double value) noexcept
{
  if (!std::isfinite(value) || value < 1.0 / 6.0 || value > 1.0)
    return DevelopmentPreNAMProcessor::off;
  return value < 0.5 ? DevelopmentPreNAMProcessor::tcBld
       : value < 5.0 / 6.0 ? DevelopmentPreNAMProcessor::mc402Boost : DevelopmentPreNAMProcessor::jRockettAH;
}

// Block-boundary selection: run at most one processor. Keep TC's original
// reset-on-engage/disengage semantics. An unavailable choice falls back to Off.
// Processor types are concrete at the call site; templates also allow routing
// tests to observe call counts without touching either DSP implementation.
class DevelopmentPreNAMSelector final
{
public:
  using Choice = DevelopmentPreNAMProcessor;
  void reset() noexcept { mApplied = Choice::off; mTCActive = mMCActive = mAHActive = false; }
  [[nodiscard]] Choice applied() const noexcept { return mApplied; }

  template<class TC, class MC, class AH>
  Choice beginBlock(Choice requested, std::size_t frames, TC& tc, MC& mc, AH& ah, bool ahSupported = true) noexcept
  {
    const bool tcReady = tc.isPrepared() && frames <= tc.maximumBlockSize();
    const bool mcReady = mc.isPrepared() && frames <= mc.maximumBlockSize();
    const bool ahReady = ahSupported && ah.isPrepared() && frames <= ah.maximumBlockSize();
    const auto next = requested == Choice::tcBld && tcReady ? Choice::tcBld
                      : requested == Choice::mc402Boost && mcReady ? Choice::mc402Boost
                      : requested == Choice::jRockettAH && ahReady ? Choice::jRockettAH : Choice::off;
    // Unavailable/oversized callbacks bypass without changing the historical
    // engagement state. The previous TC wrapper only adopted a switch when
    // that processor was ready; retain its behavior on the next valid block.
    if (tcReady && mTCActive != (next == Choice::tcBld))
    {
      tc.reset();
      mTCActive = next == Choice::tcBld;
    }
    if (mcReady && mMCActive != (next == Choice::mc402Boost))
    {
      mc.reset();
      mMCActive = next == Choice::mc402Boost;
    }
    if (ahReady && mAHActive != (next == Choice::jRockettAH))
    {
      ah.reset();
      mAHActive = next == Choice::jRockettAH;
    }
    mApplied = next;
    return next;
  }

  [[nodiscard]] double inputGain(double stockGain, double hostToVolts) const noexcept
  { return mApplied == Choice::off ? stockGain : hostToVolts; }

  template<class TC, class MC, class AH>
  void processSelected(std::span<double> monoVolts, double voltsToNam, TC& tc, MC& mc, AH& ah) noexcept
  {
    if (mApplied == Choice::off)
      return; // Preserve stock arithmetic, including bypass bit identity.
    if (mApplied == Choice::tcBld)
      tc.processBlock(monoVolts, monoVolts);
    else if (mApplied == Choice::mc402Boost)
      mc.processBlock(monoVolts, monoVolts);
    else
      ah.processBlock(monoVolts, monoVolts);
    for (auto& value : monoVolts)
      value *= voltsToNam;
  }

private:
  Choice mApplied = Choice::off;
  bool mTCActive = false, mMCActive = false, mAHActive = false;
};
} // namespace holdsworth::integration
