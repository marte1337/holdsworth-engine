#pragma once

#include "../dsp/MC402BoostOverdriveProcessor.h"

namespace holdsworth::test
{
// Test/offline diagnostics only. No runtime quality or model-shape controls.
struct MC402TestAccess
{
  using Processor = dsp::MC402BoostOverdriveProcessor;
  static void prepare(Processor& p, double rate, std::size_t blockSize, unsigned factor)
  {
    p.prepareWithFactor(rate, blockSize, factor);
  }
  static double wireSample(Processor& p, double sample) noexcept { return p.rateStage<true>(sample, 0); }
  static double saturate(double sample, double swing) noexcept { return Processor::saturate(sample, swing); }
  static double mix(const Processor& p) noexcept { return p.mOverdriveMix.value; }
  static bool running(const Processor& p) noexcept { return p.mOverdriveRunning; }
  static auto audioControls(const Processor& p) noexcept { return p.mAudioTargets.controls; }
};
} // namespace holdsworth::test
