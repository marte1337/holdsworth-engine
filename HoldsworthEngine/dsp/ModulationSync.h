#pragma once

#include "DelayBandIdentity.h"

#include <array>
#include <optional>

namespace holdsworth::dsp
{

// Physical oscillator-phase displacement measured in normalized cycles. The
// raw value is retained here so a whole synchronization request can reject a
// non-finite value transactionally. Accepted finite values are canonicalized
// modulo one into [0, 1) before becoming engine state.
struct ModulationPhaseOffsetCycles final
{
  explicit constexpr ModulationPhaseOffsetCycles(const double cycles = 0.0) noexcept
  : value(cycles)
  {
  }

  double value = 0.0;
};

// The slave is identified by this relationship's array position. The master
// uses the explicit one-based DSP identifier above.
struct SynchronizedModulationRelationship final
{
  DelayBandId masterBand = DelayBandId::band1;
  ModulationPhaseOffsetCycles phaseOffset{};
};

// relationships[0] describes Band 1 as a slave, relationships[1] Band 2, and
// so on. std::nullopt means that band owns an independent oscillator clock.
// Version one accepts direct master/slave stars and fan-out, but rejects chains
// and cycles when the configuration is applied.
struct ModulationSyncConfiguration final
{
  std::array<std::optional<SynchronizedModulationRelationship>,
             kHoldsworthDelayBandCount>
    relationships{};
};

enum class ModulationSyncApplyResult
{
  applied,
  invalidMasterReference,
  invalidPhaseOffset,
  selfReference,
  cycleDetected,
  unsupportedChain
};

} // namespace holdsworth::dsp
