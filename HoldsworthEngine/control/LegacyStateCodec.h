#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace holdsworth::control
{

// M0 compatibility prototype only. This codec is not connected to production
// serialization, parameter application, asset loading, or the audio callback.
inline constexpr std::size_t legacyStateParameterCount = 13;
inline constexpr std::string_view legacyStateHeader = "###NeuralAmpModeler###";

enum class LegacyStateSchema
{
  headerlessPre0710,
  version079,
  version0710,
  version0712,
  version0714
};

struct LegacyStateVersion
{
  unsigned int major{};
  unsigned int minor{};
  unsigned int patch{};
  bool known{};
};

struct LegacyState
{
  // Physical values, in the frozen current host-parameter order 0..12.
  // Decoding deliberately does not clamp: the eventual IParam application
  // stage must reproduce the existing parameter constraints independently.
  std::array<double, legacyStateParameterCount> parameters{};
  std::string modelPath;
  std::string irPath;
  LegacyStateSchema schema{LegacyStateSchema::headerlessPre0710};
  LegacyStateVersion version;
};

struct LegacyStateLimits
{
  // A caller-configurable denial-of-service bound, not a historical IByteChunk
  // file-format limit. Supported macOS path strings are far smaller.
  std::size_t maxStringBytes{1024 * 1024};
};

enum class LegacyStateStatus
{
  success,
  truncated,
  invalidLength,
  embeddedNull,
  invalidVersion,
  unsupportedVersion,
  nonFiniteParameter
};

// Supported Intel/Apple Silicon macOS states use little-endian signed int32
// byte lengths and little-endian IEEE-754 binary64 physical parameter values.
// No terminator is serialized. Path bytes are preserved, without normalization.
//
// On success, nextOffset is the first byte AFTER plugin-owned state, leaving
// VST3's int32 bypass word and any subsequent framework bytes untouched. On any
// validation failure, destination and nextOffset remain unchanged. Allocation
// failures propagate normally; this function is strictly for non-audio use.
// Unknown future schemas fail closed instead of guessing their bypass offset.
[[nodiscard]] LegacyStateStatus decodeLegacyState(
  std::span<const std::uint8_t> bytes,
  std::size_t startOffset,
  LegacyState& destination,
  std::size_t& nextOffset,
  LegacyStateLimits limits = {});

[[nodiscard]] std::string_view legacyStateStatusName(LegacyStateStatus status) noexcept;

} // namespace holdsworth::control
