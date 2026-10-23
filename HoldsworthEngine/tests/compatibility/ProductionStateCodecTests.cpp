#include "../../control/ProductionStateCodec.h"
#include "../../control/LegacyStateCodec.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace holdsworth::control;
std::size_t checks{};
std::size_t tests{};
std::size_t truncatedPrefixes{};

void require(const bool condition, const char* message)
{
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}

template <typename UInt>
void append(std::vector<std::uint8_t>& bytes, const UInt value)
{
  for (std::size_t i = 0; i < sizeof(UInt); ++i)
    bytes.push_back(static_cast<std::uint8_t>(value >> (8U * i)));
}

void write32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
{
  require(offset + 4U <= bytes.size(), "test patch in bounds");
  for (std::size_t i = 0; i < 4U; ++i)
    bytes[offset + i] = static_cast<std::uint8_t>(value >> (8U * i));
}

void appendString(std::vector<std::uint8_t>& bytes, const std::string& value)
{
  append(bytes, static_cast<std::uint32_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> scalar(const double value)
{
  std::vector<std::uint8_t> bytes;
  append(bytes, std::bit_cast<std::uint64_t>(value));
  return bytes;
}

ProductionState sample()
{
  ProductionState state;
  state.parameters = {2.125, -63.75, 1.125, 7.875, 4.375, -5.125, 0, 1, 0, 1, 15.125, 2, .375};
  state.modelPath = "/models/\xc3\x89tude Amp.nam";
  state.irPath = "/cabs/Room cab.wav";
  return state;
}

bool same(const ProductionState& a, const ProductionState& b)
{
  if (a.modelPath != b.modelPath || a.irPath != b.irPath || a.extension != b.extension)
    return false;
  for (std::size_t i = 0; i < a.parameters.size(); ++i)
    if (std::bit_cast<std::uint64_t>(a.parameters[i]) != std::bit_cast<std::uint64_t>(b.parameters[i]))
      return false;
  return true;
}

// Independent format construction: neither production encoder nor M0 reader
// participates in the reference new-format bytes.
std::vector<std::uint8_t> referenceBytes(const ProductionState& state)
{
  std::vector<std::uint8_t> body;
  appendString(body, state.modelPath);
  appendString(body, state.irPath);
  for (const auto value : state.parameters)
    append(body, std::bit_cast<std::uint64_t>(value));
  std::vector<std::uint8_t> extension;
  append(extension, static_cast<std::uint32_t>(state.extension.size()));
  for (const auto& record : state.extension)
  {
    append(extension, record.semanticId);
    append(extension, record.wireType);
    append(extension, static_cast<std::uint32_t>(record.payload.size()));
    extension.insert(extension.end(), record.payload.begin(), record.payload.end());
  }
  append(body, static_cast<std::uint32_t>(extension.size()));
  body.insert(body.end(), extension.begin(), extension.end());
  std::vector<std::uint8_t> bytes{'H', 'E', 'N', 'A', 'M', 'S', 'T', '1'};
  append(bytes, std::uint32_t{1});
  append(bytes, static_cast<std::uint32_t>(body.size()));
  bytes.insert(bytes.end(), body.begin(), body.end());
  return bytes;
}

void rejected(const std::span<const std::uint8_t> bytes,
              const StateCodecLimits limits = {},
              const std::size_t startOffset = 0)
{
  auto destination = sample();
  destination.extension.push_back({0xf0000001U, 99, {9, 8, 7}});
  const auto before = destination;
  std::size_t end = 0x123456;
  require(decodeProductionState(bytes, startOffset, destination, end, limits) != StateCodecStatus::success,
          "malformed input rejected");
  require(same(before, destination), "failed decode leaves full destination unchanged");
  require(end == 0x123456U, "failed decode leaves end offset unchanged");
}

void roundTrip(const ProductionState& state, const StateCodecLimits limits = {})
{
  const auto expected = referenceBytes(state);
  std::vector<std::uint8_t> bytes{7, 8, 9};
  require(encodeProductionState(state, bytes, limits) == StateCodecStatus::success, "encode success");
  require(bytes == expected, "writer matches independent byte construction");
  ProductionState actual;
  std::size_t end{};
  require(decodeProductionState(expected, 0, actual, end, limits) == StateCodecStatus::success,
          "independent format decoded");
  require(same(state, actual), "complete round-trip bit exact");
  require(end == expected.size(), "round-trip exact boundary");
}

std::vector<std::uint8_t> legacyBytes(const std::string& version,
                                    const std::size_t count,
                                    const bool header = true)
{
  const auto state = sample();
  std::vector<std::uint8_t> bytes;
  if (header)
  {
    appendString(bytes, "###NeuralAmpModeler###");
    appendString(bytes, version);
  }
  appendString(bytes, state.modelPath);
  appendString(bytes, state.irPath);
  for (std::size_t i = 0; i < count; ++i)
    append(bytes, std::bit_cast<std::uint64_t>(state.parameters[i]));
  return bytes;
}

void testBasic()
{
  ProductionState empty;
  roundTrip(empty);
  require(referenceBytes(empty).size() == 136U, "minimal state has independent 136-byte length");
  auto state = sample();
  state.parameters[0] = -0.0;
  state.parameters[1] = 1.2345678901234567;
  state.modelPath.append(1, static_cast<char>(0xff));
  roundTrip(state);
  state.extension = {{0x30000004U, 1, scalar(-0.0)},
                     {0xf0000001U, 0xf1234567U, {0, 1, 127, 128, 255}},
                     {0xffffffffU, 2, {}}};
  roundTrip(state);
  // Future physical ranges/owners are deliberately not invented in M1a.
  state.extension[0].payload = scalar(1e200);
  roundTrip(state);
  ++tests;
}

void testBoundaries()
{
  auto state = sample();
  state.extension = {{0xf0000001U, 1, scalar(3.75)}};
  const auto bytes = referenceBytes(state);
  for (const std::size_t prefix : {0U, 1U, 7U, 64U})
  {
    for (const std::uint32_t bypass : {0U, 1U})
    {
      std::vector<std::uint8_t> framed(prefix, 0x5a);
      framed.insert(framed.end(), bytes.begin(), bytes.end());
      append(framed, bypass);
      framed.insert(framed.end(), {0xde, 0xad, 0xbe, 0xef});
      ProductionState decoded;
      std::size_t end{};
      require(decodeProductionState(framed, prefix, decoded, end) == StateCodecStatus::success,
              "framed decode");
      require(same(state, decoded), "framed content exact");
      require(end == prefix + bytes.size(), "never consume bypass or suffix");
      require(framed[end] == bypass && framed[end + 4] == 0xde, "framework suffix unchanged");
    }
  }
  for (std::size_t length = 0; length < bytes.size(); ++length)
  {
    rejected(std::span(bytes).first(length));
    ++truncatedPrefixes;
  }
  rejected(bytes, {}, bytes.size() + 1U);
  ++tests;
}

void testLegacyVersions()
{
  for (const std::string version : {"0.7.14", "0.7.99", "0.8.0", "0.9.0", "1.0.0",
                                    "0.8.0-beta", " +0. 8.0", "0.8.0.", "2147483647.0.0"})
  {
    const auto bytes = legacyBytes(version, 13);
    ProductionState state;
    std::size_t end{};
    require(decodeProductionState(bytes, 0, state, end) == StateCodecStatus::success,
            "production-supported legacy numeric version accepted");
    require(same(state, sample()), "legacy 13 values exact");
    require(end == bytes.size(), "legacy current offset exact");
    roundTrip(state);
  }
  for (const std::string version : {"0.7.9", "0.7.10", "0.7.11"})
  {
    const auto bytes = legacyBytes(version, 10);
    ProductionState state;
    std::size_t end{};
    require(decodeProductionState(bytes, 0, state, end) == StateCodecStatus::success, "old ten load");
    require(state.parameters[8] == sample().parameters[9], "old IR position migration");
    require(state.parameters[11] == sample().parameters[8], "OutNorm numeric output mode migration");
    require(state.parameters[9] == 0 && state.parameters[10] == 12 && state.parameters[12] == 1,
            "ten-value migration defaults unchanged");
    require(end == bytes.size(), "ten-value exact offset");
  }
  for (const std::string version : {"0.7.12", "0.7.13"})
  {
    ProductionState state;
    std::size_t end{};
    const auto bytes = legacyBytes(version, 12);
    require(decodeProductionState(bytes, 0, state, end) == StateCodecStatus::success, "old twelve load");
    require(state.parameters[12] == 1, "twelve-value Slim migration");
    require(end == bytes.size(), "twelve-value exact offset");
  }
  ProductionState state;
  std::size_t end{};
  const auto headerless = legacyBytes("", 10, false);
  require(decodeProductionState(headerless, 0, state, end) == StateCodecStatus::success, "headerless load");
  require(state.parameters[12] == 1 && end == headerless.size(), "headerless migrated end");
  for (const std::string version : {"0.7.8", "-1.8.0", "0.8", "0.8.0.0", "x.8.0",
                                    "2147483648.0.0", "0.8.-2147483649", "0..8", "0.8."})
    rejected(legacyBytes(version, 13));
  ++tests;
}

void testFrozenFixtures(const std::filesystem::path& directory)
{
  std::size_t fixtures{};
  for (const auto& item : std::filesystem::directory_iterator(directory))
  {
    if (item.path().extension() != ".bin")
      continue;
    std::ifstream stream(item.path(), std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), {}};
    LegacyState legacy;
    std::size_t legacyEnd{};
    require(decodeLegacyState(bytes, 0, legacy, legacyEnd) == LegacyStateStatus::success,
            "frozen M0 fixture decodes");
    ProductionState production;
    std::size_t end{};
    require(decodeProductionState(bytes, 0, production, end) == StateCodecStatus::success,
            "production decoder accepts frozen fixture");
    ProductionState expected{legacy.parameters, legacy.modelPath, legacy.irPath, {}};
    require(same(production, expected), "frozen historical migrated values exact");
    require(end == legacyEnd, "frozen bypass boundary exact");
    roundTrip(production);
    for (std::size_t length = 0; length < legacyEnd; ++length)
    {
      rejected(std::span(bytes).first(length));
      ++truncatedPrefixes;
    }
    ++fixtures;
  }
  require(fixtures == 13U, "all thirteen independent M0 binary fixtures covered");
  ++tests;
}

void testMalformed()
{
  ProductionState empty;
  auto bytes = referenceBytes(empty);
  const auto original = bytes;
  write32(bytes, 8, 2);
  rejected(bytes);
  bytes = original;
  write32(bytes, 12, std::numeric_limits<std::uint32_t>::max());
  rejected(bytes);
  bytes = original;
  write32(bytes, 16, 0xffffffffU);
  rejected(bytes);
  bytes = original;
  write32(bytes, 16, 1024 * 1024 + 1);
  rejected(bytes);
  for (const auto invalid : {std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()})
  {
    for (std::size_t parameter = 0; parameter < 13; ++parameter)
    {
      bytes = original;
      const auto value = scalar(invalid);
      std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(24 + parameter * 8));
      rejected(bytes);
    }
  }
  for (const std::uint32_t extensionLength : {0U, 3U, 5U, 256U * 1024U + 1U})
  {
    bytes = original;
    write32(bytes, 128, extensionLength);
    rejected(bytes);
  }
  for (const std::uint32_t count : {1U, 1025U, 0xffffffffU})
  {
    bytes = original;
    write32(bytes, 132, count);
    rejected(bytes);
  }
  bytes = original;
  bytes.push_back(0x5a);
  write32(bytes, 12, 121);
  rejected(bytes);
  bytes = original;
  bytes.push_back(0x5a);
  write32(bytes, 12, 121);
  write32(bytes, 128, 5);
  rejected(bytes);
  auto pathState = sample();
  pathState.modelPath = std::string("x\0y", 3);
  rejected(referenceBytes(pathState));
  ++tests;
}

void testInvalidRecordsAndWriter()
{
  auto state = sample();
  for (const std::uint32_t id : {0U, 1U, 65536U, 65538U, 0x0fffffffU, 0x10000000U, 0x1000000cU})
  {
    state.extension = {{id, 1, scalar(0)}};
    rejected(referenceBytes(state));
    std::vector<std::uint8_t> output{1, 2, 3};
    require(encodeProductionState(state, output) == StateCodecStatus::reservedExtensionId,
            "writer rejects reserved ID");
    require(output == std::vector<std::uint8_t>({1, 2, 3}), "failed writer unchanged");
  }
  for (const std::vector<StateExtensionRecord>& records : {
         std::vector<StateExtensionRecord>{{0xf0000001U, 0, {}}},
         std::vector<StateExtensionRecord>{{0xf0000001U, 1, {}}},
         std::vector<StateExtensionRecord>{{0xf0000001U, 1, {1, 2, 3}}},
         std::vector<StateExtensionRecord>{{0xf0000001U, 1, scalar(std::numeric_limits<double>::infinity())}},
         std::vector<StateExtensionRecord>{{0xf0000001U, 1, scalar(std::numeric_limits<double>::quiet_NaN())}},
         std::vector<StateExtensionRecord>{{0xf0000001U, 2, {}}, {0xf0000001U, 99, {1}}}})
  {
    state.extension = records;
    rejected(referenceBytes(state));
    std::vector<std::uint8_t> output{1, 2, 3};
    require(encodeProductionState(state, output) != StateCodecStatus::success, "writer validates record");
    require(output == std::vector<std::uint8_t>({1, 2, 3}), "failed record writer unchanged");
  }
  state = sample();
  state.parameters[0] = std::numeric_limits<double>::quiet_NaN();
  std::vector<std::uint8_t> output{1, 2, 3};
  require(encodeProductionState(state, output) == StateCodecStatus::nonFiniteParameter, "writer finite legacy");
  require(output == std::vector<std::uint8_t>({1, 2, 3}), "nonfinite writer unchanged");
  state = sample();
  state.irPath = std::string("x\0y", 3);
  require(encodeProductionState(state, output) == StateCodecStatus::embeddedNull, "writer validates NUL");
  ++tests;
}

void testLimits()
{
  ProductionState state;
  StateCodecLimits limits;
  limits.maxStringBytes = 4;
  state.modelPath = "four";
  state.irPath = "four";
  roundTrip(state, limits);
  state.modelPath += 'x';
  rejected(referenceBytes(state), limits);
  std::vector<std::uint8_t> output;
  require(encodeProductionState(state, output, limits) == StateCodecStatus::oversized, "writer path cap");
  state = {};
  limits = {};
  limits.maxStateBytes = 136;
  roundTrip(state, limits);
  limits.maxStateBytes = 135;
  rejected(referenceBytes(state), limits);
  require(encodeProductionState(state, output, limits) == StateCodecStatus::oversized, "writer total cap");
  limits = {};
  state.extension = {{0xf0000001U, 2, std::vector<std::uint8_t>(256 * 1024 - 16, 0xab)}};
  roundTrip(state);
  state.extension[0].payload.push_back(1);
  rejected(referenceBytes(state));
  require(encodeProductionState(state, output) == StateCodecStatus::oversized, "writer extension cap");
  state = {};
  for (std::uint32_t i = 0; i < 1024; ++i)
    state.extension.push_back({0xf0000000U + i, 2, {}});
  roundTrip(state);
  state.extension.push_back({0xf0000400U, 2, {}});
  rejected(referenceBytes(state));
  require(encodeProductionState(state, output) == StateCodecStatus::oversized, "writer record cap");
  state = {};
  limits.maxExtensionBytes = 3;
  rejected(referenceBytes(state), limits);
  require(encodeProductionState(state, output, limits) == StateCodecStatus::oversized, "count occupies four bytes");
  ++tests;
}

void testMutationSafety()
{
  auto state = sample();
  state.extension = {{0x30000004U, 1, scalar(1234.5)}, {0xffffffffU, 77, {0, 1, 2, 3}}};
  const auto base = referenceBytes(state);
  std::uint32_t random = 0x12345678U;
  for (std::size_t trial = 0; trial < 10000; ++trial)
  {
    auto bytes = base;
    random = random * 1664525U + 1013904223U;
    const auto position = static_cast<std::size_t>(random) % bytes.size();
    random = random * 1664525U + 1013904223U;
    bytes[position] ^= static_cast<std::uint8_t>((random >> 16U) | 1U);
    ProductionState decoded = state;
    std::size_t end = 0x123456;
    const auto status = decodeProductionState(bytes, 0, decoded, end);
    if (status == StateCodecStatus::success)
    {
      require(end <= bytes.size(), "mutation consumed in bounds");
      roundTrip(decoded);
    }
    else
    {
      require(same(decoded, state), "mutation rejection transactional");
      require(end == 0x123456U, "mutation rejection boundary transactional");
    }
  }
  ++tests;
}
} // namespace

int main(const int argc, const char* const* argv)
{
  try
  {
    const std::filesystem::path fixtures = argc > 1 ? argv[1] :
      "HoldsworthEngine/references/midi-automation-v1/m0/fixtures";
    testBasic();
    testBoundaries();
    testLegacyVersions();
    testFrozenFixtures(fixtures);
    testMalformed();
    testInvalidRecordsAndWriter();
    testLimits();
    testMutationSafety();
    std::cout << "Production state codec: " << tests << " tests, " << checks
              << " checks, " << truncatedPrefixes << " truncated prefixes, 10000 mutations; all passed\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
