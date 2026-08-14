#pragma once

#include "../dsp/HoldsworthDelayEngine.h"

#include <algorithm>
#include <cassert>
#include <span>

namespace holdsworth::integration
{

// Framework-independent helpers for advancing the wet-only delay engine and
// combining its output with a mono dry signal.
//
// These functions own no storage and allocate nothing. The caller supplies the
// pre-zeroed silence and wet scratch buffers needed by bypass processing.
class MonoDryStereoWetMixer final
{
public:
  using Sample = double;

  // Always advances the delay engine. When delayEnabled is false, silence is
  // fed to the engine so existing feedback tails continue to decay without
  // capturing the live dry input. The resulting wet signal is then discarded
  // by clearing both scratch buffers. This does not change any engine or band
  // configuration.
  //
  // silenceInput must contain zeroes. All spans must have equal sizes, and the
  // wet scratch spans must be distinct and non-overlapping.
  static void advanceDelay(dsp::HoldsworthDelayEngine& engine, bool delayEnabled, std::span<const Sample> monoInput,
                           std::span<const Sample> silenceInput, std::span<Sample> wetLeftScratch,
                           std::span<Sample> wetRightScratch) noexcept
  {
    const bool outputsAreDistinct = monoInput.empty() || wetLeftScratch.data() != wetRightScratch.data();
    const bool validCall = monoInput.size() == silenceInput.size() && monoInput.size() == wetLeftScratch.size()
                           && monoInput.size() == wetRightScratch.size() && outputsAreDistinct;
    assert(validCall && "advanceDelay() spans must have matching sizes and distinct wet outputs");
    if (!validCall)
    {
      std::fill(wetLeftScratch.begin(), wetLeftScratch.end(), 0.0);
      std::fill(wetRightScratch.begin(), wetRightScratch.end(), 0.0);
      return;
    }

    const std::span<const Sample> delayInput = delayEnabled ? monoInput : silenceInput;
    engine.processBlock(delayInput, wetLeftScratch, wetRightScratch);

    if (!delayEnabled)
    {
      std::fill(wetLeftScratch.begin(), wetLeftScratch.end(), 0.0);
      std::fill(wetRightScratch.begin(), wetRightScratch.end(), 0.0);
    }
  }

  // Direct stereo equation:
  //   outputLeft  = monoDry + wetMixMultiplier * wetLeft
  //   outputRight = monoDry + wetMixMultiplier * wetRight
  //
  // No normalization, clipping, or gain sanitization is performed. The wet
  // multiplier is an integration-level control independent of the engine's
  // globalWetOutputLevel.
  static void mixStereo(std::span<const Sample> monoDry, std::span<const Sample> wetLeft,
                        std::span<const Sample> wetRight, Sample wetMixMultiplier, std::span<Sample> outputLeft,
                        std::span<Sample> outputRight) noexcept
  {
    const bool outputsAreDistinct = monoDry.empty() || outputLeft.data() != outputRight.data();
    const bool validCall = monoDry.size() == wetLeft.size() && monoDry.size() == wetRight.size()
                           && monoDry.size() == outputLeft.size() && monoDry.size() == outputRight.size()
                           && outputsAreDistinct;
    assert(validCall && "mixStereo() spans must have matching sizes and distinct outputs");
    if (!validCall)
    {
      std::fill(outputLeft.begin(), outputLeft.end(), 0.0);
      std::fill(outputRight.begin(), outputRight.end(), 0.0);
      return;
    }

    for (std::size_t frame = 0; frame < monoDry.size(); ++frame)
    {
      // Capture all inputs before either write so exact input/output aliases
      // remain safe.
      const Sample dry = monoDry[frame];
      const Sample left = wetLeft[frame];
      const Sample right = wetRight[frame];
      outputLeft[frame] = dry + wetMixMultiplier * left;
      outputRight[frame] = dry + wetMixMultiplier * right;
    }
  }

  // Explicit mono fold-down equation:
  //   output = monoDry + wetMixMultiplier * 0.5 * (wetLeft + wetRight)
  //
  // The 0.5 factor applies only to the stereo wet fold-down; the dry signal is
  // included once. No clipping or additional normalization is performed.
  static void mixMono(std::span<const Sample> monoDry, std::span<const Sample> wetLeft,
                      std::span<const Sample> wetRight, Sample wetMixMultiplier, std::span<Sample> outputMono) noexcept
  {
    const bool validCall =
      monoDry.size() == wetLeft.size() && monoDry.size() == wetRight.size() && monoDry.size() == outputMono.size();
    assert(validCall && "mixMono() spans must have matching sizes");
    if (!validCall)
    {
      std::fill(outputMono.begin(), outputMono.end(), 0.0);
      return;
    }

    for (std::size_t frame = 0; frame < monoDry.size(); ++frame)
    {
      const Sample dry = monoDry[frame];
      const Sample left = wetLeft[frame];
      const Sample right = wetRight[frame];
      outputMono[frame] = dry + wetMixMultiplier * 0.5 * (left + right);
    }
  }
};

} // namespace holdsworth::integration
