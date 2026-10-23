#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace holdsworth::control
{

inline constexpr std::size_t kProductionStateParameterCount = 13;
inline constexpr std::array<std::uint8_t, 8> kProductionStateMagic{
  'H', 'E', 'N', 'A', 'M', 'S', 'T', '1'};
inline constexpr std::uint32_t kProductionStateVersion = 1;
inline constexpr std::uint32_t kStateExtensionFloat64 = 1;
inline constexpr std::size_t kProductionStateHeaderBytes = 16;

// All records are inert in M1a: even recognized proposed IDs are retained, not
// applied. A later milestone must explicitly assign ownership and validation
// before activating an ID. Payload and order survive load/save unchanged.
// IDs follow M0's high semantic namespace (>= 0x10000000); the original 13
// legacy semantic IDs are forbidden here because their values live in the core.
struct StateExtensionRecord
{
  std::uint32_t semanticId{};
  std::uint32_t wireType{};
  std::vector<std::uint8_t> payload;

  bool operator==(const StateExtensionRecord&) const = default;
};

struct ProductionState
{
  // Physical values in the unchanged legacy host-parameter order. Values are
  // finite but deliberately not clamped here: IParam retains that ownership.
  std::array<double, kProductionStateParameterCount> parameters{};
  std::string modelPath;
  std::string irPath;
  std::vector<StateExtensionRecord> extension;
};

struct StateCodecLimits
{
  std::size_t maxStringBytes{1024 * 1024};
  std::size_t maxExtensionBytes{256 * 1024};
  std::size_t maxExtensionRecords{1024};
  // Includes header, paths, parameters and extension. Allows the default
  // independent path and extension bounds plus fixed format overhead.
  std::size_t maxStateBytes{2 * 1024 * 1024 + 256 * 1024 + 256};
};

enum class StateCodecStatus
{
  success,
  truncated,
  invalidLength,
  oversized,
  embeddedNull,
  invalidVersion,
  unsupportedVersion,
  nonFiniteParameter,
  invalidExtension,
  duplicateExtensionId,
  reservedExtensionId,
  nonFiniteExtensionValue,
  trailingStateBytes
};

// Off-audio only. Strings/vectors may allocate. Complete validation precedes
// replacing destination; failure leaves destination and nextOffset unchanged.
// nextOffset stops immediately after plugin-owned bytes, before VST3 bypass.
// Legacy headers/layouts are read and migrated; only schema 1 is written.
[[nodiscard]] StateCodecStatus decodeProductionState(
  std::span<const std::uint8_t> bytes,
  std::size_t startOffset,
  ProductionState& destination,
  std::size_t& nextOffset,
  StateCodecLimits limits = {});

// Replaces destination only on success. Does not append framework bypass data.
[[nodiscard]] StateCodecStatus encodeProductionState(
  const ProductionState& state,
  std::vector<std::uint8_t>& destination,
  StateCodecLimits limits = {});

[[nodiscard]] std::string_view stateCodecStatusName(StateCodecStatus status) noexcept;

} // namespace holdsworth::control
