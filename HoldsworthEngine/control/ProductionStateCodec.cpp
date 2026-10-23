#include "ProductionStateCodec.h"

#include <algorithm>
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
constexpr std::string_view legacyHeader = "###NeuralAmpModeler###";
constexpr std::uint32_t firstLegacySemanticId = 0x10000000U;
constexpr std::uint32_t lastLegacySemanticId = 0x1000000cU;

template <typename UInt>
[[nodiscard]] UInt readLittleEndian(const std::uint8_t* data) noexcept
{
  UInt result{};
  for (std::size_t i = 0; i < sizeof(UInt); ++i)
    result |= static_cast<UInt>(data[i]) << (8U * i);
  return result;
}

template <typename UInt>
void appendLittleEndian(std::vector<std::uint8_t>& bytes, const UInt value)
{
  for (std::size_t i = 0; i < sizeof(UInt); ++i)
    bytes.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & static_cast<UInt>(0xffU)));
}

class Reader
{
public:
  Reader(const std::span<const std::uint8_t> bytes, const std::size_t maxBytes)
  : bytes_(bytes), maxBytes_(maxBytes) {}

  template <typename UInt>
  [[nodiscard]] StateCodecStatus integer(UInt& value)
  {
    const auto status = available(sizeof(UInt));
    if (status != StateCodecStatus::success)
      return status;
    value = readLittleEndian<UInt>(bytes_.data() + position_);
    position_ += sizeof(UInt);
    return StateCodecStatus::success;
  }

  [[nodiscard]] StateCodecStatus number(double& value)
  {
    std::uint64_t bits{};
    const auto status = integer(bits);
    if (status != StateCodecStatus::success)
      return status;
    value = std::bit_cast<double>(bits);
    return std::isfinite(value) ? StateCodecStatus::success : StateCodecStatus::nonFiniteParameter;
  }

  [[nodiscard]] StateCodecStatus string(std::string& value, const std::size_t maxLength)
  {
    std::uint32_t length{};
    auto status = integer(length);
    if (status != StateCodecStatus::success)
      return status;
    if ((length & 0x80000000U) != 0U)
      return StateCodecStatus::invalidLength;
    if (length > maxLength)
      return StateCodecStatus::oversized;
    status = available(length);
    if (status != StateCodecStatus::success)
      return status;
    const auto content = bytes_.subspan(position_, length);
    if (std::find(content.begin(), content.end(), 0U) != content.end())
      return StateCodecStatus::embeddedNull;
    value.assign(reinterpret_cast<const char*>(content.data()), content.size());
    position_ += length;
    return StateCodecStatus::success;
  }

  [[nodiscard]] StateCodecStatus payload(std::vector<std::uint8_t>& value, const std::size_t length)
  {
    const auto status = available(length);
    if (status != StateCodecStatus::success)
      return status;
    const auto content = bytes_.subspan(position_, length);
    value.assign(content.begin(), content.end());
    position_ += length;
    return StateCodecStatus::success;
  }

  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - position_; }

private:
  [[nodiscard]] StateCodecStatus available(const std::size_t count) const noexcept
  {
    if (position_ > maxBytes_ || count > maxBytes_ - position_)
      return StateCodecStatus::oversized;
    if (count > bytes_.size() - position_)
      return StateCodecStatus::truncated;
    return StateCodecStatus::success;
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t maxBytes_;
  std::size_t position_{};
};

[[nodiscard]] bool isSpace(const char value) noexcept
{
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\f' || value == '\v';
}

// The old _Version constructor used std::stoi on each dot-delimited segment.
// Preserve its valid signed/whitespace/integer-prefix behavior, without throws
// or narrowing overflow. For example 0.8.0-beta still dispatches as 0.8.0.
[[nodiscard]] bool parseLegacyInteger(const std::string_view text, std::int32_t& result) noexcept
{
  std::size_t position{};
  while (position < text.size() && isSpace(text[position]))
    ++position;
  bool negative{};
  if (position < text.size() && (text[position] == '-' || text[position] == '+'))
    negative = text[position++] == '-';
  const auto firstDigit = position;
  std::uint32_t value{};
  const std::uint32_t maximum = negative ? 0x80000000U : 0x7fffffffU;
  while (position < text.size() && text[position] >= '0' && text[position] <= '9')
  {
    const auto digit = static_cast<std::uint32_t>(text[position++] - '0');
    if (value > (maximum - digit) / 10U)
      return false;
    value = value * 10U + digit;
  }
  if (position == firstDigit)
    return false;
  if (negative)
    result = value == 0x80000000U ? std::numeric_limits<std::int32_t>::min() :
      -static_cast<std::int32_t>(value);
  else
    result = static_cast<std::int32_t>(value);
  return true;
}

[[nodiscard]] bool parseLegacyVersion(const std::string_view text,
                                    std::array<std::int32_t, 3>& version) noexcept
{
  std::size_t begin{};
  for (std::size_t component = 0; component < version.size(); ++component)
  {
    const auto dot = text.find('.', begin);
    const auto end = dot == std::string_view::npos ? text.size() : dot;
    if (!parseLegacyInteger(text.substr(begin, end - begin), version[component]))
      return false;
    if (component + 1U < version.size())
    {
      if (dot == std::string_view::npos)
        return false;
      begin = dot + 1U;
    }
    else if (dot != std::string_view::npos && dot + 1U != text.size())
      return false;
  }
  return true;
}

[[nodiscard]] StateCodecStatus validateExtension(const std::vector<StateExtensionRecord>& extension,
                                               const StateCodecLimits limits,
                                               std::size_t& encodedBytes)
{
  if (extension.size() > limits.maxExtensionRecords)
    return StateCodecStatus::oversized;
  encodedBytes = sizeof(std::uint32_t); // Record count, including for an empty section.
  if (encodedBytes > limits.maxExtensionBytes)
    return StateCodecStatus::oversized;
  std::vector<std::uint32_t> ids;
  ids.reserve(extension.size());
  for (const auto& record : extension)
  {
    if (record.semanticId < firstLegacySemanticId || (record.semanticId >= firstLegacySemanticId &&
                                   record.semanticId <= lastLegacySemanticId))
      return StateCodecStatus::reservedExtensionId;
    if (record.wireType == 0U)
      return StateCodecStatus::invalidExtension;
    if (record.payload.size() > std::numeric_limits<std::uint32_t>::max() ||
        12U > limits.maxExtensionBytes - encodedBytes ||
        record.payload.size() > limits.maxExtensionBytes - encodedBytes - 12U)
      return StateCodecStatus::oversized;
    encodedBytes += 12U + record.payload.size();
    if (record.wireType == kStateExtensionFloat64)
    {
      if (record.payload.size() != sizeof(double))
        return StateCodecStatus::invalidExtension;
      const auto value = std::bit_cast<double>(readLittleEndian<std::uint64_t>(record.payload.data()));
      if (!std::isfinite(value))
        return StateCodecStatus::nonFiniteExtensionValue;
    }
    ids.push_back(record.semanticId);
  }
  std::sort(ids.begin(), ids.end());
  if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
    return StateCodecStatus::duplicateExtensionId;
  return StateCodecStatus::success;
}

[[nodiscard]] StateCodecStatus decodeLegacy(const std::span<const std::uint8_t> bytes,
                                          ProductionState& candidate,
                                          std::size_t& consumed,
                                          const StateCodecLimits limits)
{
  Reader reader(bytes, limits.maxStateBytes);
  std::string first;
  auto status = reader.string(first, limits.maxStringBytes);
  if (status != StateCodecStatus::success)
    return status;
  std::size_t storedCount = 10;
  if (first == legacyHeader)
  {
    std::string versionText;
    status = reader.string(versionText, limits.maxStringBytes);
    if (status != StateCodecStatus::success)
      return status;
    std::array<std::int32_t, 3> version{};
    if (!parseLegacyVersion(versionText, version))
      return StateCodecStatus::invalidVersion;
    if (version >= std::array<std::int32_t, 3>{0, 7, 14})
      storedCount = 13;
    else if (version >= std::array<std::int32_t, 3>{0, 7, 12})
      storedCount = 12;
    else if (version < std::array<std::int32_t, 3>{0, 7, 9})
      return StateCodecStatus::unsupportedVersion;
    status = reader.string(candidate.modelPath, limits.maxStringBytes);
    if (status != StateCodecStatus::success)
      return status;
  }
  else
    candidate.modelPath = std::move(first);
  status = reader.string(candidate.irPath, limits.maxStringBytes);
  if (status != StateCodecStatus::success)
    return status;
  std::array<double, kProductionStateParameterCount> stored{};
  for (std::size_t i = 0; i < storedCount; ++i)
  {
    status = reader.number(stored[i]);
    if (status != StateCodecStatus::success)
      return status;
  }
  if (storedCount == 10U)
  {
    std::copy_n(stored.begin(), 8, candidate.parameters.begin());
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
  consumed = reader.position();
  return StateCodecStatus::success;
}

[[nodiscard]] StateCodecStatus decodeCurrent(const std::span<const std::uint8_t> bytes,
                                           ProductionState& candidate,
                                           std::size_t& consumed,
                                           const StateCodecLimits limits)
{
  if (bytes.size() < kProductionStateHeaderBytes)
    return StateCodecStatus::truncated;
  const auto version = readLittleEndian<std::uint32_t>(bytes.data() + 8);
  if (version != kProductionStateVersion)
    return StateCodecStatus::unsupportedVersion;
  const auto bodyBytes = readLittleEndian<std::uint32_t>(bytes.data() + 12);
  if (limits.maxStateBytes < kProductionStateHeaderBytes ||
      bodyBytes > limits.maxStateBytes - kProductionStateHeaderBytes)
    return StateCodecStatus::oversized;
  if (bodyBytes > bytes.size() - kProductionStateHeaderBytes)
    return StateCodecStatus::truncated;
  Reader reader(bytes.subspan(kProductionStateHeaderBytes, bodyBytes), bodyBytes);
  auto status = reader.string(candidate.modelPath, limits.maxStringBytes);
  if (status != StateCodecStatus::success)
    return status;
  status = reader.string(candidate.irPath, limits.maxStringBytes);
  if (status != StateCodecStatus::success)
    return status;
  for (auto& value : candidate.parameters)
  {
    status = reader.number(value);
    if (status != StateCodecStatus::success)
      return status;
  }
  std::uint32_t extensionBytes{};
  status = reader.integer(extensionBytes);
  if (status != StateCodecStatus::success)
    return status;
  if (extensionBytes > limits.maxExtensionBytes)
    return StateCodecStatus::oversized;
  if (extensionBytes < sizeof(std::uint32_t))
    return StateCodecStatus::invalidExtension;
  if (extensionBytes > reader.remaining())
    return StateCodecStatus::truncated;
  if (extensionBytes < reader.remaining())
    return StateCodecStatus::trailingStateBytes;
  std::uint32_t recordCount{};
  status = reader.integer(recordCount);
  if (status != StateCodecStatus::success)
    return status;
  if (recordCount > limits.maxExtensionRecords)
    return StateCodecStatus::oversized;
  if (recordCount > reader.remaining() / 12U)
    return StateCodecStatus::invalidExtension;
  candidate.extension.reserve(recordCount);
  for (std::uint32_t i = 0; i < recordCount; ++i)
  {
    StateExtensionRecord record;
    status = reader.integer(record.semanticId);
    if (status != StateCodecStatus::success)
      return status;
    status = reader.integer(record.wireType);
    if (status != StateCodecStatus::success)
      return status;
    std::uint32_t payloadBytes{};
    status = reader.integer(payloadBytes);
    if (status != StateCodecStatus::success)
      return status;
    status = reader.payload(record.payload, payloadBytes);
    if (status != StateCodecStatus::success)
      return status;
    candidate.extension.push_back(std::move(record));
  }
  if (reader.remaining() != 0U)
    return StateCodecStatus::trailingStateBytes;
  std::size_t validatedBytes{};
  status = validateExtension(candidate.extension, limits, validatedBytes);
  if (status != StateCodecStatus::success)
    return status;
  consumed = kProductionStateHeaderBytes + bodyBytes;
  return StateCodecStatus::success;
}

[[nodiscard]] StateCodecStatus validateString(const std::string& text,
                                            const StateCodecLimits limits) noexcept
{
  if (text.size() > limits.maxStringBytes || text.size() > 0x7fffffffU)
    return StateCodecStatus::oversized;
  return text.find('\0') == std::string::npos ? StateCodecStatus::success : StateCodecStatus::embeddedNull;
}

void appendString(std::vector<std::uint8_t>& bytes, const std::string& text)
{
  appendLittleEndian(bytes, static_cast<std::uint32_t>(text.size()));
  bytes.insert(bytes.end(), text.begin(), text.end());
}

} // namespace

StateCodecStatus decodeProductionState(const std::span<const std::uint8_t> bytes,
                                      const std::size_t startOffset,
                                      ProductionState& destination,
                                      std::size_t& nextOffset,
                                      const StateCodecLimits limits)
{
  if (startOffset > bytes.size())
    return StateCodecStatus::truncated;
  const auto stateBytes = bytes.subspan(startOffset);
  ProductionState candidate;
  std::size_t consumed{};
  const bool current = stateBytes.size() >= kProductionStateMagic.size() &&
    std::equal(kProductionStateMagic.begin(), kProductionStateMagic.end(), stateBytes.begin());
  const auto status = current ? decodeCurrent(stateBytes, candidate, consumed, limits) :
                               decodeLegacy(stateBytes, candidate, consumed, limits);
  if (status == StateCodecStatus::success)
  {
    destination = std::move(candidate);
    nextOffset = startOffset + consumed;
  }
  return status;
}

StateCodecStatus encodeProductionState(const ProductionState& state,
                                      std::vector<std::uint8_t>& destination,
                                      const StateCodecLimits limits)
{
  auto status = validateString(state.modelPath, limits);
  if (status != StateCodecStatus::success)
    return status;
  status = validateString(state.irPath, limits);
  if (status != StateCodecStatus::success)
    return status;
  for (const auto value : state.parameters)
    if (!std::isfinite(value))
      return StateCodecStatus::nonFiniteParameter;
  std::size_t extensionBytes{};
  status = validateExtension(state.extension, limits, extensionBytes);
  if (status != StateCodecStatus::success)
    return status;
  // String lengths are each <= INT32_MAX and extension <= UINT32_MAX after
  // the format bound below. Sum in uint64_t to avoid size_t wrap on 32-bit.
  const auto bodyBytes = std::uint64_t{8 + 13 * 8 + 4} +
    static_cast<std::uint64_t>(state.modelPath.size()) + state.irPath.size() + extensionBytes;
  if (extensionBytes > std::numeric_limits<std::uint32_t>::max() ||
      bodyBytes > std::numeric_limits<std::uint32_t>::max() ||
      bodyBytes + kProductionStateHeaderBytes > limits.maxStateBytes ||
      state.extension.size() > std::numeric_limits<std::uint32_t>::max())
    return StateCodecStatus::oversized;
  std::vector<std::uint8_t> candidate;
  candidate.reserve(static_cast<std::size_t>(bodyBytes + kProductionStateHeaderBytes));
  candidate.insert(candidate.end(), kProductionStateMagic.begin(), kProductionStateMagic.end());
  appendLittleEndian(candidate, kProductionStateVersion);
  appendLittleEndian(candidate, static_cast<std::uint32_t>(bodyBytes));
  appendString(candidate, state.modelPath);
  appendString(candidate, state.irPath);
  for (const auto value : state.parameters)
    appendLittleEndian(candidate, std::bit_cast<std::uint64_t>(value));
  appendLittleEndian(candidate, static_cast<std::uint32_t>(extensionBytes));
  appendLittleEndian(candidate, static_cast<std::uint32_t>(state.extension.size()));
  for (const auto& record : state.extension)
  {
    appendLittleEndian(candidate, record.semanticId);
    appendLittleEndian(candidate, record.wireType);
    appendLittleEndian(candidate, static_cast<std::uint32_t>(record.payload.size()));
    candidate.insert(candidate.end(), record.payload.begin(), record.payload.end());
  }
  destination = std::move(candidate);
  return StateCodecStatus::success;
}

std::string_view stateCodecStatusName(const StateCodecStatus status) noexcept
{
  switch (status)
  {
    case StateCodecStatus::success: return "success";
    case StateCodecStatus::truncated: return "truncated";
    case StateCodecStatus::invalidLength: return "invalidLength";
    case StateCodecStatus::oversized: return "oversized";
    case StateCodecStatus::embeddedNull: return "embeddedNull";
    case StateCodecStatus::invalidVersion: return "invalidVersion";
    case StateCodecStatus::unsupportedVersion: return "unsupportedVersion";
    case StateCodecStatus::nonFiniteParameter: return "nonFiniteParameter";
    case StateCodecStatus::invalidExtension: return "invalidExtension";
    case StateCodecStatus::duplicateExtensionId: return "duplicateExtensionId";
    case StateCodecStatus::reservedExtensionId: return "reservedExtensionId";
    case StateCodecStatus::nonFiniteExtensionValue: return "nonFiniteExtensionValue";
    case StateCodecStatus::trailingStateBytes: return "trailingStateBytes";
  }
  return "unknown";
}

} // namespace holdsworth::control
