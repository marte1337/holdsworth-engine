#include "LegacyStateCodec.h"

#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace holdsworth::control
{
namespace
{

static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);

class Reader
{
public:
  Reader(const std::span<const std::uint8_t> bytes,
         const std::size_t position,
         const LegacyStateLimits limits)
  : bytes_(bytes), position_(position), limits_(limits) {}

  [[nodiscard]] LegacyStateStatus string(std::string& value)
  {
    if (!available(sizeof(std::uint32_t)))
      return LegacyStateStatus::truncated;

    const auto length = littleEndian<std::uint32_t>();
    // The serialized length is signed int32. Test its sign before conversion
    // to a byte count, avoiding the historical GetStr negative-length hazard.
    if ((length & 0x80000000U) != 0U || length > limits_.maxStringBytes)
      return LegacyStateStatus::invalidLength;
    if (!available(length))
      return LegacyStateStatus::truncated;
    for (std::size_t i = 0; i < length; ++i)
      if (bytes_[position_ + i] == 0U)
        return LegacyStateStatus::embeddedNull;

    value.assign(reinterpret_cast<const char*>(bytes_.data() + position_), length);
    position_ += length;
    return LegacyStateStatus::success;
  }

  [[nodiscard]] LegacyStateStatus number(double& value)
  {
    if (!available(sizeof(std::uint64_t)))
      return LegacyStateStatus::truncated;
    value = std::bit_cast<double>(littleEndian<std::uint64_t>());
    return std::isfinite(value) ? LegacyStateStatus::success : LegacyStateStatus::nonFiniteParameter;
  }

  [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
  [[nodiscard]] bool available(const std::size_t count) const noexcept
  {
    return position_ <= bytes_.size() && count <= bytes_.size() - position_;
  }

  template <typename UInt>
  [[nodiscard]] UInt littleEndian() noexcept
  {
    UInt result{};
    for (std::size_t i = 0; i < sizeof(UInt); ++i)
      result |= static_cast<UInt>(bytes_[position_++]) << (8U * i);
    return result;
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t position_;
  LegacyStateLimits limits_;
};

[[nodiscard]] bool parseVersion(const std::string_view text, LegacyStateVersion& version) noexcept
{
  std::array<unsigned int, 3> values{};
  std::size_t position{};
  for (std::size_t component = 0; component < values.size(); ++component)
  {
    const std::size_t begin = position;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9')
    {
      const auto digit = static_cast<unsigned int>(text[position] - '0');
      if (values[component] > (std::numeric_limits<unsigned int>::max() - digit) / 10U)
        return false;
      values[component] = values[component] * 10U + digit;
      ++position;
    }
    if (begin == position)
      return false;
    if (component + 1 < values.size())
    {
      if (position >= text.size() || text[position++] != '.')
        return false;
    }
  }
  if (position != text.size())
    return false;
  version = {values[0], values[1], values[2], true};
  return true;
}

[[nodiscard]] LegacyStateStatus classify(const LegacyStateVersion& version,
                                        LegacyStateSchema& schema) noexcept
{
  // 0.8.0 is the current accepted writer. Newer schemas require an explicit
  // codec version/extension contract, especially before VST3 bypass seeking.
  if (version.major != 0U || version.minor > 8U ||
      (version.minor == 8U && version.patch != 0U) ||
      version.minor < 7U || (version.minor == 7U && version.patch < 9U))
    return LegacyStateStatus::unsupportedVersion;
  if (version.minor == 8U || version.patch >= 14U)
    schema = LegacyStateSchema::version0714;
  else if (version.patch >= 12U)
    schema = LegacyStateSchema::version0712;
  else if (version.patch >= 10U)
    schema = LegacyStateSchema::version0710;
  else
    schema = LegacyStateSchema::version079;
  return LegacyStateStatus::success;
}

} // namespace

LegacyStateStatus decodeLegacyState(const std::span<const std::uint8_t> bytes,
                                   const std::size_t startOffset,
                                   LegacyState& destination,
                                   std::size_t& nextOffset,
                                   const LegacyStateLimits limits)
{
  Reader reader(bytes, startOffset, limits);
  LegacyState candidate;
  std::string first;
  auto status = reader.string(first);
  if (status != LegacyStateStatus::success)
    return status;

  if (first == legacyStateHeader)
  {
    std::string version;
    status = reader.string(version);
    if (status != LegacyStateStatus::success)
      return status;
    if (!parseVersion(version, candidate.version))
      return LegacyStateStatus::invalidVersion;
    status = classify(candidate.version, candidate.schema);
    if (status != LegacyStateStatus::success)
      return status;
    status = reader.string(candidate.modelPath);
    if (status != LegacyStateStatus::success)
      return status;
  }
  else
    candidate.modelPath = std::move(first);

  status = reader.string(candidate.irPath);
  if (status != LegacyStateStatus::success)
    return status;

  const bool tenValues = candidate.schema == LegacyStateSchema::headerlessPre0710 ||
                         candidate.schema == LegacyStateSchema::version079 ||
                         candidate.schema == LegacyStateSchema::version0710;
  const std::size_t storedCount = tenValues ? 10U :
    (candidate.schema == LegacyStateSchema::version0712 ? 12U : 13U);
  std::array<double, legacyStateParameterCount> stored{};
  for (std::size_t i = 0; i < storedCount; ++i)
  {
    status = reader.number(stored[i]);
    if (status != LegacyStateStatus::success)
      return status;
  }

  if (tenValues)
  {
    // Old fields 8/9 were OutNorm/IRToggle. Rename OutNorm numerically:
    // true -> OutputMode 1 (Normalized), despite the old comment's wording.
    for (std::size_t i = 0; i < 8U; ++i)
      candidate.parameters[i] = stored[i];
    candidate.parameters[8] = stored[9];
    candidate.parameters[9] = 0.0;
    candidate.parameters[10] = 12.0;
    candidate.parameters[11] = stored[8];
    candidate.parameters[12] = 1.0;
  }
  else
  {
    candidate.parameters = stored;
    if (storedCount == 12U)
      candidate.parameters[12] = 1.0;
  }

  destination = std::move(candidate);
  nextOffset = reader.position();
  return LegacyStateStatus::success;
}

std::string_view legacyStateStatusName(const LegacyStateStatus status) noexcept
{
  switch (status)
  {
    case LegacyStateStatus::success: return "success";
    case LegacyStateStatus::truncated: return "truncated";
    case LegacyStateStatus::invalidLength: return "invalidLength";
    case LegacyStateStatus::embeddedNull: return "embeddedNull";
    case LegacyStateStatus::invalidVersion: return "invalidVersion";
    case LegacyStateStatus::unsupportedVersion: return "unsupportedVersion";
    case LegacyStateStatus::nonFiniteParameter: return "nonFiniteParameter";
  }
  return "unknown";
}

} // namespace holdsworth::control
