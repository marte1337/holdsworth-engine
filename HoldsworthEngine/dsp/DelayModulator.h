#pragma once

#include <cstddef>

namespace holdsworth::dsp
{

// Strong physical-unit types keep modulation DSP values distinct from future
// Yamaha control values, whose mappings have not yet been measured.
struct ModulationRateHz final
{
  explicit constexpr ModulationRateHz(const double value = 0.0) noexcept
  : value(value)
  {
  }

  double value = 0.0;
};

struct ModulationDepthMs final
{
  explicit constexpr ModulationDepthMs(const double value = 0.0) noexcept
  : value(value)
  {
  }

  double value = 0.0;
};

struct ModulationPhaseCycles final
{
  explicit constexpr ModulationPhaseCycles(const double value = 0.0) noexcept
  : value(value)
  {
  }

  double value = 0.0;
};

// A sine low-frequency oscillator that produces delay-time offsets in
// milliseconds. It owns no buffers and performs no allocation.
//
// mPhaseCycles is the sole authoritative time state. The sine/cosine pair is
// only a hot-path cache of that phase: state-changing calls rebuild it from
// mPhaseCycles, and a fixed sample-count interval periodically does the same.
// Recursive quadrature rotation never determines or corrects logical phase.
class DelayModulator final
{
public:
  using Sample = double;

  // This is only a discrete-time anti-aliasing guard. It is not a musical LFO
  // range and, in particular, does not imply any Yamaha SPEED-to-Hz mapping.
  static constexpr Sample kMaximumTechnicalCyclesPerSample = 0.5;

  // The cache is reconstructed from authoritative phase at this fixed sample
  // interval. Exposing the interval makes long-run synchronization testable;
  // it is not a user-facing parameter.
  static constexpr std::size_t kCacheResynchronizationInterval = 4096;

  // Establishes the sample rate, derives the technically bounded effective
  // rate, and restores the configured reset phase.
  void prepare(double sampleRate);

  // Restores the configured phase without changing rate or depth.
  void reset() noexcept;

  // Negative or non-finite values are treated as zero. Requested rate is kept
  // separately when a prepared oscillator applies its discrete-time limit.
  void setRate(ModulationRateHz rate) noexcept;
  void setDepth(ModulationDepthMs depth) noexcept;

  // Wraps a finite phase into [0, 1), immediately rephases the oscillator, and
  // establishes the phase subsequently restored by reset(). Non-finite values
  // are treated as zero.
  void setPhase(ModulationPhaseCycles phase) noexcept;

  [[nodiscard]] ModulationRateHz requestedRate() const noexcept { return mRequestedRate; }
  [[nodiscard]] ModulationRateHz effectiveRate() const noexcept { return mEffectiveRate; }
  [[nodiscard]] ModulationDepthMs depth() const noexcept { return mDepth; }
  [[nodiscard]] ModulationPhaseCycles resetPhase() const noexcept { return mResetPhase; }

  // Phase of the sample that the next nextOffsetMs() call will produce.
  [[nodiscard]] ModulationPhaseCycles currentPhase() const noexcept
  {
    return ModulationPhaseCycles{mPhaseCycles};
  }

  [[nodiscard]] bool isPrepared() const noexcept { return mPrepared; }

  // Returns depth * sin(2*pi*currentPhase), then advances authoritative phase
  // by one sample. The regular hot-path update uses quadrature rotation; only
  // the periodic cache resynchronization evaluates sine and cosine.
  [[nodiscard]] Sample nextOffsetMs() noexcept;

private:
  static Sample wrapPhase(Sample phaseCycles) noexcept;
  void updateRateDerivedState() noexcept;
  void synchronizeCacheFromAuthoritativePhase() noexcept;
  void advanceOneSample() noexcept;

  ModulationRateHz mRequestedRate{};
  ModulationRateHz mEffectiveRate{};
  ModulationDepthMs mDepth{};
  ModulationPhaseCycles mResetPhase{};
  Sample mSampleRate = 0.0;
  Sample mPhaseCycles = 0.0;
  Sample mPhaseIncrementCycles = 0.0;
  Sample mSine = 0.0;
  Sample mCosine = 1.0;
  Sample mSineIncrement = 0.0;
  Sample mCosineIncrement = 1.0;
  std::size_t mSamplesSinceCacheSynchronization = 0;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
