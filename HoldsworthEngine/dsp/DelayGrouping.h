#pragma once

#include "DelayBandIdentity.h"

#include <array>
#include <cstddef>
#include <optional>

namespace holdsworth::dsp
{

// Physical storage prepared for one shared GROUP delay history. This DSP
// capacity is deliberately distinct from Yamaha's documented maximum-delay
// facts and may be selected independently by an integration or test.
struct GroupedDelayPhysicalCapacityMs final
{
  explicit constexpr GroupedDelayPhysicalCapacityMs(const double milliseconds = 0.0) noexcept
  : value(milliseconds)
  {
  }

  double value = 0.0;
};

// Exact maximum-delay facts stated by Yamaha. No value is returned for group
// sizes whose maximum has not been documented; in particular, this seam must
// never interpolate or multiply the one-band maximum.
struct DocumentedYamahaGroupMaximumDelayMs final
{
  explicit constexpr DocumentedYamahaGroupMaximumDelayMs(const double milliseconds = 0.0) noexcept
  : value(milliseconds)
  {
  }

  double value = 0.0;
};

[[nodiscard]] constexpr std::optional<DocumentedYamahaGroupMaximumDelayMs> documentedYamahaGroupMaximumDelayMs(
  const std::size_t memberCount) noexcept
{
  switch (memberCount)
  {
    case 1: return DocumentedYamahaGroupMaximumDelayMs{696.0};
    case 2: return DocumentedYamahaGroupMaximumDelayMs{1430.0};
    case 8: return DocumentedYamahaGroupMaximumDelayMs{5890.0};
    default: return std::nullopt;
  }
}

// Replaceable v1 policy seam. Documented Yamaha limits constrain the group
// sizes for which they exist; all sizes are also constrained by the separately
// prepared physical history capacity.
[[nodiscard]] constexpr double groupedDelayMaximumPermittedTimeMs(
  const std::size_t memberCount, const GroupedDelayPhysicalCapacityMs physicalCapacity) noexcept
{
  const auto documentedMaximum = documentedYamahaGroupMaximumDelayMs(memberCount);
  if (documentedMaximum.has_value() && documentedMaximum->value < physicalCapacity.value)
    return documentedMaximum->value;
  return physicalCapacity.value;
}

// The array position identifies the GROUP head. endBand completes an ascending
// contiguous inclusive range. std::nullopt means that head is ungrouped.
struct GroupedDelayRange final
{
  DelayBandId endBand = DelayBandId::band1;
};

struct DelayGroupingConfiguration final
{
  std::array<std::optional<GroupedDelayRange>, kHoldsworthDelayBandCount> groupsByHead{};
};

enum class DelayGroupingApplyResult
{
  applied,
  invalidBandReference,
  singletonRange,
  descendingRange,
  overlappingMembership,
  delayTimeExceedsCapacity,
  nonHeadConnectedDestination,
  collapsedCycleDetected
};

enum class GroupAudioCompositionApplyResult
{
  applied,
  nonHeadConnectedDestination,
  collapsedCycleDetected
};

// Provisional GROUP-only timing interpretation pending Magicstomp
// measurement. Feedback always uses the unmodulated base delay; this helper
// controls only an output identity's modulated TAP observation time.
[[nodiscard]] double provisionalGroupedOutputTapDelayTimeMs(double groupBaseDelayTimeMs,
                                                            double outputModulationOffsetMs, double tapFraction,
                                                            double minimumDelayTimeMs,
                                                            double maximumDelayTimeMs) noexcept;

} // namespace holdsworth::dsp
