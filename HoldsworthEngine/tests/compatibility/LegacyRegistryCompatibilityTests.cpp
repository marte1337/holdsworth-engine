#include "../../control/LegacyStateCodec.h"
#include "../../control/ParameterRegistry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace control = holdsworth::control;
using LegacyValues = std::array<double, 13>;
using FutureValues = std::array<double, 46>;
std::size_t checks = 0;

void check(bool condition, const char* message)
{
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

bool sameBits(double left, double right)
{
  return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

// Test-only composition, not an application/coordinator or runtime migration API.
// Reproduce legacy physical IParam constraints directly, without a normalized
// round-trip. Decoding has already rejected nonfinite stored parameters.
FutureValues futureSnapshot(const LegacyValues& legacy)
{
  FutureValues values{};
  for (const auto& parameter : control::kParameterRegistry)
    values[parameter.hostIndex] = parameter.defaultValue;
  for (std::size_t i = 0; i < legacy.size(); ++i)
  {
    const auto& parameter = control::kParameterRegistry[i];
    const double physical = parameter.kind == control::ParameterKind::continuous
      ? legacy[i] : std::round(legacy[i]);
    values[i] = std::clamp(physical, parameter.minimum, parameter.maximum);
  }
  return values;
}

void verifySnapshot(const FutureValues& future, const LegacyValues& expected)
{
  for (std::size_t i = 0; i < expected.size(); ++i)
    check(sameBits(future[i], expected[i]), "legacy physical value changed");
  for (std::size_t i = 13; i < future.size(); ++i)
    check(sameBits(future[i], control::kParameterRegistry[i].defaultValue),
          "appended parameter did not retain its exact default");
  const holdsworth::dsp::PostAmpEQControls eqDefaults;
  for (std::size_t i = 0; i < holdsworth::dsp::kPostAmpEQParameterCount; ++i)
    check(sameBits(future[i + 27], holdsworth::dsp::postAmpEQParameterValue(
      eqDefaults, static_cast<holdsworth::dsp::PostAmpEQParameter>(i))),
      "legacy import colored the accepted neutral EQ");
  for (std::size_t i : {std::size_t{29}, std::size_t{32}, std::size_t{35}, std::size_t{41}})
    check(sameBits(future[i], 0.0), "neutral EQ gain or trim is not positive exact zero");
}

constexpr LegacyValues current{-3.25, -72.5, 2.25, 6.75, 8.5, 1.125,
  0, 1, 0, 1, 9.5, 2, .375};
constexpr LegacyValues historical10{-3.25, -72.5, 2.25, 6.75, 8.5, 1.125,
  0, 1, 0, 0, 12, 1, 1};
constexpr LegacyValues historical12{-3.25, -72.5, 2.25, 6.75, 8.5, 1.125,
  0, 1, 0, 1, 9.5, 2, 1};
constexpr LegacyValues outNormOff{-3.25, -72.5, 2.25, 6.75, 8.5, 1.125,
  0, 1, 1, 0, 12, 0, 1};
constexpr LegacyValues currentDefaults{0, -80, 5, 5, 5, 0, 1, 1, 1, 0, 12, 1, 0};

struct Fixture
{
  const char* file;
  std::size_t bytes, pluginEnd;
  LegacyValues expected;
  int bypass = -1;
};
constexpr std::array<Fixture, 13> fixtures{{
  {"headerless-pre-0710.plugin.bin", 147, 147, historical10},
  {"header-0.7.9.plugin.bin", 182, 182, historical10},
  {"header-0.7.10.plugin.bin", 183, 183, historical10},
  {"header-0.7.11.plugin.bin", 183, 183, historical10},
  {"header-0.7.12.plugin.bin", 199, 199, historical12},
  {"header-0.7.13.plugin.bin", 199, 199, historical12},
  {"header-0.7.14.plugin.bin", 207, 207, current},
  {"header-0.8.0.plugin.bin", 206, 206, current},
  {"header-0.7.10-outnorm-off.plugin.bin", 183, 183, outNormOff},
  {"current-bypass-0.vst3-state.bin", 210, 206, current, 0},
  {"current-bypass-1.vst3-state.bin", 210, 206, current, 1},
  {"current-bypass-1-tail.vst3-state.bin", 218, 206, current, 1},
  {"current-empty-paths.plugin.bin", 147, 147, currentDefaults}
}};

void verifyFixture(const std::filesystem::path& directory, const Fixture& fixture)
{
  std::ifstream file(directory / fixture.file, std::ios::binary);
  check(file.good(), "cannot open frozen fixture");
  const std::vector<char> raw{std::istreambuf_iterator<char>{file}, {}};
  std::vector<std::uint8_t> bytes;
  bytes.reserve(raw.size());
  for (char value : raw)
    bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
  check(bytes.size() == fixture.bytes, "frozen fixture size changed");
  control::LegacyState state;
  std::size_t end = 9999;
  check(control::decodeLegacyState(bytes, 0, state, end) == control::LegacyStateStatus::success,
        "frozen legacy state failed to decode");
  check(end == fixture.pluginEnd, "legacy plugin-owned byte boundary changed");
  verifySnapshot(futureSnapshot(state.parameters), fixture.expected);
  check(end == fixture.pluginEnd, "appended defaults consumed framework state");
  if (fixture.bypass >= 0)
  {
    std::uint32_t bypass = 0;
    for (std::size_t i = 0; i < 4; ++i)
      bypass |= static_cast<std::uint32_t>(bytes.at(end + i)) << (8U * i);
    check(bypass == static_cast<std::uint32_t>(fixture.bypass),
          "VST3 bypass no longer starts at the returned byte offset");
    if (fixture.bytes > end + 4)
    {
      constexpr std::array<std::uint8_t, 8> suffix{0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4};
      check(std::equal(suffix.begin(), suffix.end(), bytes.begin() + static_cast<std::ptrdiff_t>(end + 4)),
            "opaque framework suffix changed");
    }
  }
}

void physicalConstraints()
{
  const LegacyValues raw{.1, 500, 12.345, -4, 1.23456789, -0.0,
    .49, .5, -1, 3, 100, 1.5, 2};
  const LegacyValues expected{.1, 0, 10, 0, 1.23456789, -0.0,
    0, 1, 0, 1, 60, 2, 1};
  verifySnapshot(futureSnapshot(raw), expected);
  // A normalized round-trip loses this sign. Physical state application must
  // retain it rather than inserting an avoidable conversion at the boundary.
  check(sameBits(futureSnapshot(raw)[5], -0.0), "physical application lost signed zero");
}

void slimMigrationVersusStartup()
{
  check(sameBits(control::kParameterRegistry[12].defaultValue, 0), "new-instance Slim default changed");
  check(sameBits(futureSnapshot(historical10)[12], 1), "pre-0.7.12 Slim migration lost");
  check(sameBits(futureSnapshot(historical12)[12], 1), "pre-0.7.14 Slim migration lost");
  check(sameBits(futureSnapshot(currentDefaults)[12], 0), "current explicit Slim zero was overwritten");
}
} // namespace

int main(int argc, const char* const* argv)
{
  if (argc > 2)
  {
    std::cerr << "usage: legacy-registry-tests [fixture-directory]\n";
    return 2;
  }
  const std::filesystem::path directory = argc == 2 ? argv[1]
    : "HoldsworthEngine/references/midi-automation-v1/m0/fixtures";
  std::size_t passed = 0;
  try
  {
    for (const auto& fixture : fixtures)
    {
      verifyFixture(directory, fixture);
      ++passed;
    }
    physicalConstraints(); ++passed;
    slimMigrationVersusStartup(); ++passed;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
  }
  std::cout << "Legacy registry compatibility: " << passed << "/15 tests passed ("
            << checks << " checks; 13 frozen fixtures)\n";
  return passed == 15 ? 0 : 1;
}
