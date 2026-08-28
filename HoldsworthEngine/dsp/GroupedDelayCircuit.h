#pragma once

#include "DelayBand.h"
#include "DelayGrouping.h"
#include "DelayLoopFilter.h"
#include "FractionalDelayLine.h"

#include <array>
#include <cstddef>
#include <span>

namespace holdsworth::dsp
{

// One Yamaha-style GROUP runtime: a single delay history and feedback
// recurrence observed by several retained Effect Band output identities.
class GroupedDelayCircuit final
{
public:
  using Sample = double;

  struct OutputParameters final
  {
    Sample outputLevel = 0.0;
    Sample leftPanGain = 0.0;
    Sample rightPanGain = 0.0;
    DelaySignalPolarity polarity = DelaySignalPolarity::normal;
    bool enabled = false;
  };

  explicit GroupedDelayCircuit(GroupedDelayPhysicalCapacityMs physicalCapacity);

  GroupedDelayCircuit(const GroupedDelayCircuit&) = delete;
  GroupedDelayCircuit& operator=(const GroupedDelayCircuit&) = delete;
  GroupedDelayCircuit(GroupedDelayCircuit&&) noexcept = delete;
  GroupedDelayCircuit& operator=(GroupedDelayCircuit&&) noexcept = delete;

  void prepare(double sampleRate, std::size_t maximumBlockSize);
  void reset() noexcept;

  void setMembership(std::size_t headIndex, std::size_t endIndex) noexcept;
  void setBaseDelayTimeMs(Sample delayTimeMs) noexcept;
  void setFeedbackCoefficient(Sample coefficient) noexcept;
  void setLoopFilterConfiguration(const DelayLoopFilterConfiguration& configuration) noexcept;
  void setOutputTapFraction(std::size_t bandIndex, TapFraction tapFraction) noexcept;
  void setOutputObservationIsIndependent(std::size_t bandIndex, bool isIndependent) noexcept;

  [[nodiscard]] ModulationDepthMs effectiveOutputModulationDepth(ModulationDepthMs requestedDepth) const noexcept;

  [[nodiscard]] Sample requestedBaseDelayTimeMs() const noexcept { return mRequestedBaseDelayTimeMs; }
  [[nodiscard]] Sample baseDelayTimeMs() const noexcept { return mDelayLine.delayTimeMs(); }
  [[nodiscard]] Sample minimumDelayTimeMs() const noexcept { return mMinimumDelayTimeMs; }
  [[nodiscard]] Sample physicalCapacityMs() const noexcept { return mPhysicalCapacity.value; }
  [[nodiscard]] Sample permittedMaximumDelayTimeMs() const noexcept { return mPermittedMaximumDelayTimeMs; }
  [[nodiscard]] std::size_t headIndex() const noexcept { return mHeadIndex; }
  [[nodiscard]] std::size_t endIndex() const noexcept { return mEndIndex; }

  // Each modulation pointer addresses frameCount prepared offsets for the
  // corresponding global Band identity. Output buffers use bandStride between
  // identities. The shared history is read for every output before its single
  // write for that sample.
  void processBlock(std::span<const Sample> groupInput,
                    const std::array<OutputParameters, kHoldsworthDelayBandCount>& outputs,
                    const std::array<const Sample*, kHoldsworthDelayBandCount>& modulationOffsets,
                    Sample* routedOutputs, Sample* wetLeftOutputs, Sample* wetRightOutputs,
                    std::size_t bandStride) noexcept;

private:
  void applyEffectiveBaseDelayTime() noexcept;

  const GroupedDelayPhysicalCapacityMs mPhysicalCapacity;
  FractionalDelayLine mDelayLine;
  DelayLoopFilter mLoopFilter;
  std::array<DelayLoopFilter, kHoldsworthDelayBandCount> mObservationFilters;
  std::array<TapFraction, kHoldsworthDelayBandCount> mTapFractions{TapFraction{}, TapFraction{}, TapFraction{},
                                                                   TapFraction{}, TapFraction{}, TapFraction{},
                                                                   TapFraction{}, TapFraction{}};
  std::array<bool, kHoldsworthDelayBandCount> mIndependentObservationStates{};
  Sample mRequestedBaseDelayTimeMs = 0.0;
  Sample mMinimumDelayTimeMs = 0.0;
  Sample mPermittedMaximumDelayTimeMs = 0.0;
  Sample mFeedbackCoefficient = 0.0;
  std::size_t mHeadIndex = 0;
  std::size_t mEndIndex = 1;
  std::size_t mMaximumBlockSize = 0;
  bool mPrepared = false;
};

} // namespace holdsworth::dsp
