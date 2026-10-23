#include "../../control/LegacyStateCodec.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace holdsworth::control;
using Bytes = std::vector<std::uint8_t>;

std::filesystem::path fixtureDirectory;
constexpr std::array<double, 13> current{
  -3.25, -72.5, 2.25, 6.75, 8.5, 1.125, 0.0, 1.0, 0.0, 1.0, 9.5, 2.0, 0.375};
const std::string model = "/Users/音楽/Models/Amp Étude.nam";
const std::string ir = "D:\\Cab IRs\\箱 4×12.wav";

void check(const bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

Bytes fixture(const char* name)
{
  std::ifstream file(fixtureDirectory / name, std::ios::binary);
  check(file.good(), "cannot open fixture");
  const std::vector<char> raw{std::istreambuf_iterator<char>{file}, {}};
  Bytes bytes;
  bytes.reserve(raw.size());
  for (const char value : raw)
    bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
  return bytes;
}

bool sameParameters(const std::array<double, 13>& left, const std::array<double, 13>& right)
{
  for (std::size_t i = 0; i < left.size(); ++i)
    if (std::bit_cast<std::uint64_t>(left[i]) != std::bit_cast<std::uint64_t>(right[i]))
      return false;
  return true;
}

LegacyState decoded(const std::span<const std::uint8_t> bytes, const std::size_t begin,
                    const std::size_t expectedEnd)
{
  LegacyState state;
  std::size_t end = 9999;
  check(decodeLegacyState(bytes, begin, state, end) == LegacyStateStatus::success, "decode failed");
  check(end == expectedEnd, "wrong consumed byte boundary");
  return state;
}

void failsTransactionally(const std::span<const std::uint8_t> bytes,
                          const LegacyStateStatus expected,
                          const std::size_t start = 0,
                          const LegacyStateLimits limits = {})
{
  LegacyState destination;
  destination.modelPath = "unchanged model";
  destination.irPath = "unchanged IR";
  destination.parameters.fill(-123.5);
  destination.schema = LegacyStateSchema::version0712;
  destination.version = {3, 4, 5, true};
  const auto before = destination;
  std::size_t end = 9999;
  const auto status = decodeLegacyState(bytes, start, destination, end, limits);
  check(status == expected, "wrong failure status");
  check(end == 9999, "failed decode changed end offset");
  check(destination.modelPath == before.modelPath && destination.irPath == before.irPath &&
        sameParameters(destination.parameters, before.parameters) && destination.schema == before.schema &&
        destination.version.major == before.version.major && destination.version.minor == before.version.minor &&
        destination.version.patch == before.version.patch && destination.version.known == before.version.known,
        "failed decode changed destination");
}

void set32(Bytes& bytes, const std::size_t offset, const std::uint32_t value)
{
  for (std::size_t i = 0; i < 4; ++i)
    bytes.at(offset + i) = static_cast<std::uint8_t>((value >> (8U * i)) & 0xffU);
}

void setDouble(Bytes& bytes, const std::size_t offset, const double value)
{
  const auto bits = std::bit_cast<std::uint64_t>(value);
  for (std::size_t i = 0; i < 8; ++i)
    bytes.at(offset + i) = static_cast<std::uint8_t>((bits >> (8U * i)) & 0xffU);
}

struct VersionFixture
{
  const char* file;
  LegacyStateSchema schema;
  unsigned int patch;
  std::size_t count;
};

constexpr std::array<VersionFixture, 8> versions{{
  {"headerless-pre-0710.plugin.bin", LegacyStateSchema::headerlessPre0710, 0, 10},
  {"header-0.7.9.plugin.bin", LegacyStateSchema::version079, 9, 10},
  {"header-0.7.10.plugin.bin", LegacyStateSchema::version0710, 10, 10},
  {"header-0.7.11.plugin.bin", LegacyStateSchema::version0710, 11, 10},
  {"header-0.7.12.plugin.bin", LegacyStateSchema::version0712, 12, 12},
  {"header-0.7.13.plugin.bin", LegacyStateSchema::version0712, 13, 12},
  {"header-0.7.14.plugin.bin", LegacyStateSchema::version0714, 14, 13},
  {"header-0.8.0.plugin.bin", LegacyStateSchema::version0714, 0, 13},
}};

void historicalSchemas()
{
  for (const auto& version : versions)
  {
    const auto bytes = fixture(version.file);
    const auto state = decoded(bytes, 0, bytes.size());
    auto expected = current;
    if (version.count == 10)
    {
      expected[8] = 0.0; expected[9] = 0.0; expected[10] = 12.0;
      expected[11] = 1.0; expected[12] = 1.0;
    }
    else if (version.count == 12)
      expected[12] = 1.0;
    check(sameParameters(state.parameters, expected), "legacy physical order/migration changed");
    check(state.modelPath == model && state.irPath == ir, "opaque UTF8/path bytes changed");
    check(state.schema == version.schema, "wrong schema");
    check(state.version.known == (version.schema != LegacyStateSchema::headerlessPre0710), "wrong version provenance");
    check(state.version.patch == version.patch, "wrong version patch");
  }
}

void outNormFalse()
{
  const auto bytes = fixture("header-0.7.10-outnorm-off.plugin.bin");
  const auto state = decoded(bytes, 0, bytes.size());
  check(state.parameters[8] == 1.0 && state.parameters[11] == 0.0,
        "OutNorm false/IR ordering changed");
  check(state.parameters[9] == 0.0 && state.parameters[10] == 12.0 && state.parameters[12] == 1.0,
        "migration defaults changed");
}

void currentDefaults()
{
  const auto bytes = fixture("current-empty-paths.plugin.bin");
  const auto state = decoded(bytes, 0, bytes.size());
  constexpr std::array<double, 13> expected{
    0.0, -80.0, 5.0, 5.0, 5.0, 0.0, 1.0, 1.0, 1.0, 0.0, 12.0, 1.0, 0.0};
  check(sameParameters(state.parameters, expected), "current defaults changed");
  check(state.modelPath.empty() && state.irPath.empty(), "empty paths changed");
}

void vst3BypassBoundary()
{
  const auto plugin = fixture("header-0.8.0.plugin.bin");
  for (unsigned int bypass = 0; bypass < 2; ++bypass)
  {
    const auto bytes = fixture(bypass == 0 ? "current-bypass-0.vst3-state.bin" : "current-bypass-1.vst3-state.bin");
    const auto state = decoded(bytes, 0, plugin.size());
    check(sameParameters(state.parameters, current), "framework bypass became a plugin parameter");
    check(bytes.size() == plugin.size() + 4 && bytes[plugin.size()] == bypass,
          "bypass is not exact int32 at consumed position");
  }
}

void opaqueSuffix()
{
  const auto plugin = fixture("header-0.8.0.plugin.bin");
  const auto bytes = fixture("current-bypass-1-tail.vst3-state.bin");
  static_cast<void>(decoded(bytes, 0, plugin.size()));
  constexpr std::array<std::uint8_t, 12> suffix{1, 0, 0, 0, 0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4};
  check(std::equal(suffix.begin(), suffix.end(), bytes.begin() + static_cast<std::ptrdiff_t>(plugin.size())),
        "framework suffix changed");
}

void nonzeroOffsets()
{
  for (const auto& version : versions)
  {
    const auto body = fixture(version.file);
    for (const std::size_t prefixSize : {std::size_t{1}, std::size_t{7}, std::size_t{64}})
    {
      Bytes bytes(prefixSize, 0xa5);
      bytes.insert(bytes.end(), body.begin(), body.end());
      bytes.insert(bytes.end(), {1, 0, 0, 0, 0xcc});
      const auto state = decoded(bytes, prefixSize, prefixSize + body.size());
      check(state.modelPath == model, "nonzero-offset path failed");
    }
  }
}

void everyTruncation()
{
  std::size_t tested{};
  for (const auto& version : versions)
  {
    const auto bytes = fixture(version.file);
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
      failsTransactionally(std::span<const std::uint8_t>{bytes}.first(length), LegacyStateStatus::truncated);
      ++tested;
    }
  }
  std::cout << "  truncated prefixes checked: " << tested << '\n';
}

void invalidStartOffsets()
{
  const auto bytes = fixture("header-0.8.0.plugin.bin");
  failsTransactionally(bytes, LegacyStateStatus::truncated, bytes.size());
  failsTransactionally(bytes, LegacyStateStatus::truncated, bytes.size() + 1);
  failsTransactionally(bytes, LegacyStateStatus::truncated, std::numeric_limits<std::size_t>::max());
}

std::array<std::size_t, 4> stringOffsets()
{
  const auto versionOffset = 4 + legacyStateHeader.size();
  const auto modelOffset = versionOffset + 4 + std::string_view{"0.8.0"}.size();
  return {0, versionOffset, modelOffset, modelOffset + 4 + model.size()};
}

void negativeLengths()
{
  for (const auto offset : stringOffsets())
  {
    auto bytes = fixture("header-0.8.0.plugin.bin");
    set32(bytes, offset, 0xffffffffU);
    failsTransactionally(bytes, LegacyStateStatus::invalidLength);
  }
}

void oversizedLengths()
{
  for (const auto offset : stringOffsets())
  {
    auto bytes = fixture("header-0.8.0.plugin.bin");
    set32(bytes, offset, 0x7fffffffU);
    failsTransactionally(bytes, LegacyStateStatus::invalidLength);
    set32(bytes, offset, 1024U);
    failsTransactionally(bytes, LegacyStateStatus::truncated);
  }
}

void embeddedNulls()
{
  for (const auto offset : stringOffsets())
  {
    auto bytes = fixture("header-0.8.0.plugin.bin");
    bytes.at(offset + 4) = 0;
    failsTransactionally(bytes, LegacyStateStatus::embeddedNull);
  }
}

void pathLimits()
{
  const auto bytes = fixture("header-0.8.0.plugin.bin");
  failsTransactionally(bytes, LegacyStateStatus::invalidLength, 0, {model.size() - 1});
  LegacyState state;
  std::size_t end{};
  check(decodeLegacyState(bytes, 0, state, end, {std::max(model.size(), ir.size())}) == LegacyStateStatus::success,
        "exact string limit rejected valid paths");
}

void opaqueNonUTF8Paths()
{
  auto bytes = fixture("header-0.8.0.plugin.bin");
  bytes.at(stringOffsets()[2] + 4) = 0xffU;
  const auto state = decoded(bytes, 0, bytes.size());
  check(state.modelPath.size() == model.size() &&
        static_cast<unsigned char>(state.modelPath.front()) == 0xffU && state.irPath == ir,
        "codec normalized or rejected opaque non-UTF8 path bytes");
}

void nonfiniteValues()
{
  const auto original = fixture("header-0.8.0.plugin.bin");
  const auto values = original.size() - current.size() * sizeof(double);
  for (std::size_t i = 0; i < current.size(); ++i)
    for (const double bad : {std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()})
    {
      auto bytes = original;
      setDouble(bytes, values + i * sizeof(double), bad);
      failsTransactionally(bytes, LegacyStateStatus::nonFiniteParameter);
    }
}

void finiteRawValues()
{
  auto bytes = fixture("header-0.8.0.plugin.bin");
  const auto values = bytes.size() - current.size() * sizeof(double);
  setDouble(bytes, values, 1000.125);
  setDouble(bytes, values + sizeof(double), -0.0);
  const auto state = decoded(bytes, 0, bytes.size());
  check(state.parameters[0] == 1000.125, "codec performed undocumented range clamping");
  check(std::bit_cast<std::uint64_t>(state.parameters[1]) == 0x8000000000000000ULL,
        "raw signed-zero bits changed");
}

Bytes withVersion(const std::string_view version)
{
  const auto original = fixture("header-0.8.0.plugin.bin");
  const auto versionStart = stringOffsets()[1];
  const auto modelStart = stringOffsets()[2];
  Bytes bytes(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(versionStart));
  const auto lengthOffset = bytes.size();
  bytes.resize(bytes.size() + 4);
  set32(bytes, lengthOffset, static_cast<std::uint32_t>(version.size()));
  for (const char value : version)
    bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
  bytes.insert(bytes.end(), original.begin() + static_cast<std::ptrdiff_t>(modelStart), original.end());
  return bytes;
}

void invalidVersions()
{
  for (const auto version : {"", "0.8", "0.8.0.1", "0.8.0x", " 0.8.0", "-1.8.0", "0..0",
                             "4294967296.8.0", "0.4294967296.0", "0.8.4294967296"})
    failsTransactionally(withVersion(version), LegacyStateStatus::invalidVersion);
}

void unknownVersions()
{
  for (const auto version : {"0.8.1", "0.9.0", "1.0.0", "0.7.8", "0.6.99"})
    failsTransactionally(withVersion(version), LegacyStateStatus::unsupportedVersion);
}

void knownLaterPatch()
{
  const auto bytes = withVersion("0.7.99");
  const auto state = decoded(bytes, 0, bytes.size());
  check(sameParameters(state.parameters, current) && state.schema == LegacyStateSchema::version0714,
        "known 0.7.14..0.8.0 layout range changed");
}

void statusNames()
{
  for (const auto status : {LegacyStateStatus::success, LegacyStateStatus::truncated,
                            LegacyStateStatus::invalidLength, LegacyStateStatus::embeddedNull,
                            LegacyStateStatus::invalidVersion, LegacyStateStatus::unsupportedVersion,
                            LegacyStateStatus::nonFiniteParameter})
    check(legacyStateStatusName(status) != "unknown", "missing status diagnostic");
}

struct TestCase { const char* name; void (*run)(); };
constexpr std::array<TestCase, 19> tests{{
  {"historical physical fields/migrations", historicalSchemas},
  {"OutNorm false and IR ordering", outNormFalse},
  {"current default state and empty paths", currentDefaults},
  {"VST3 exact bypass boundary", vst3BypassBoundary},
  {"opaque framework suffix", opaqueSuffix},
  {"nonzero caller offsets", nonzeroOffsets},
  {"every historical truncated prefix", everyTruncation},
  {"out-of-range caller offsets", invalidStartOffsets},
  {"negative string lengths", negativeLengths},
  {"oversized and truncated string lengths", oversizedLengths},
  {"embedded NUL validation", embeddedNulls},
  {"configurable exact path-byte bound", pathLimits},
  {"opaque non-UTF8 path bytes", opaqueNonUTF8Paths},
  {"all parameter NaN/Inf cases", nonfiniteValues},
  {"finite raw values and signed zero", finiteRawValues},
  {"strict malformed versions", invalidVersions},
  {"unknown schemas fail closed", unknownVersions},
  {"known pre-current patch range", knownLaterPatch},
  {"status diagnostics", statusNames},
}};
} // namespace

int main(const int argc, const char* const* argv)
{
  if (argc != 2)
  {
    std::cerr << "usage: legacy-state-tests <synthetic-fixture-directory>\n";
    return 2;
  }
  fixtureDirectory = argv[1];
  std::size_t passed{};
  for (const auto& test : tests)
  {
    try
    {
      test.run();
      ++passed;
      std::cout << "PASS " << test.name << '\n';
    }
    catch (const std::exception& error)
    {
      std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
    }
  }
  std::cout << "Legacy state codec: " << passed << '/' << tests.size() << " tests passed\n";
  return passed == tests.size() ? 0 : 1;
}
