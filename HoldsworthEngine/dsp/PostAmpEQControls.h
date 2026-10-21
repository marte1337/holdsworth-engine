#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace holdsworth::dsp
{
// Physical, unnormalized values are the processor/control contract. The same
// identifiers and mappings can later serve MIDI/host parameters without a
// separate development-UI interpretation. These are engineering defaults;
// they do not claim a measured or historical Holdsworth EQ curve.
struct PostAmpEQBandControls final
{
  double frequencyHz = 250.0;
  double gainDb = 0.0;
  double q = 0.7;
  bool operator==(const PostAmpEQBandControls&) const = default;
};

struct PostAmpEQControls final
{
  bool enabled = false;
  std::array<PostAmpEQBandControls, 3> bands{{{250.0, 0.0, 0.7},
                                           {1000.0, 0.0, 0.7},
                                           {3500.0, 0.0, 0.7}}};
  bool highPassEnabled = false;
  double highPassHz = 80.0;
  bool lowPassEnabled = false;
  double lowPassHz = 8000.0;
  double trimDb = 0.0;
  bool operator==(const PostAmpEQControls&) const = default;
};

enum class PostAmpEQParameter : std::uint32_t
{
  enable = 0,
  band1Frequency, band1Gain, band1Q,
  band2Frequency, band2Gain, band2Q,
  band3Frequency, band3Gain, band3Q,
  highPassEnable, highPassFrequency,
  lowPassEnable, lowPassFrequency,
  trim,
  count
};

struct PostAmpEQParameterInfo final
{
  double minimum;
  double maximum;
  double defaultValue;
  bool logarithmic;
  bool boolean;
  const char* label;
  const char* unit;
};

inline constexpr std::size_t kPostAmpEQParameterCount =
  static_cast<std::size_t>(PostAmpEQParameter::count);
inline constexpr std::array<PostAmpEQParameterInfo, kPostAmpEQParameterCount>
  kPostAmpEQParameters{{
    {0.0, 1.0, 0.0, false, true, "Enable", ""},
    {80.0, 8000.0, 250.0, true, false, "Frequency", "Hz"},
    {-12.0, 12.0, 0.0, false, false, "Gain", "dB"},
    {0.35, 4.0, 0.7, true, false, "Q", ""},
    {80.0, 8000.0, 1000.0, true, false, "Frequency", "Hz"},
    {-12.0, 12.0, 0.0, false, false, "Gain", "dB"},
    {0.35, 4.0, 0.7, true, false, "Q", ""},
    {80.0, 8000.0, 3500.0, true, false, "Frequency", "Hz"},
    {-12.0, 12.0, 0.0, false, false, "Gain", "dB"},
    {0.35, 4.0, 0.7, true, false, "Q", ""},
    {0.0, 1.0, 0.0, false, true, "HP", ""},
    {20.0, 200.0, 80.0, true, false, "HP frequency", "Hz"},
    {0.0, 1.0, 0.0, false, true, "LP", ""},
    {3000.0, 16000.0, 8000.0, true, false, "LP frequency", "Hz"},
    {-24.0, 6.0, 0.0, false, false, "Trim", "dB"}
  }};

[[nodiscard]] constexpr bool isPostAmpEQParameter(PostAmpEQParameter parameter) noexcept
{
  return static_cast<std::size_t>(parameter) < kPostAmpEQParameterCount;
}

// Callers use a valid semantic identifier. Invalid identifiers safely describe
// Enable; setters below reject them without touching the controls tuple.
[[nodiscard]] constexpr const PostAmpEQParameterInfo& postAmpEQParameterInfo(
  PostAmpEQParameter parameter) noexcept
{
  return kPostAmpEQParameters[isPostAmpEQParameter(parameter)
    ? static_cast<std::size_t>(parameter) : 0];
}

[[nodiscard]] inline double postAmpEQParameterToNormalized(
  PostAmpEQParameter parameter, double value) noexcept;

[[nodiscard]] inline double postAmpEQParameterFromNormalized(
  PostAmpEQParameter parameter, double normalized) noexcept
{
  const auto& info = postAmpEQParameterInfo(parameter);
  if (!std::isfinite(normalized)) return info.defaultValue;
  const double n = std::clamp(normalized, 0.0, 1.0);
  if (info.boolean) return n >= 0.5 ? 1.0 : 0.0;
  if (n == 0.0) return info.minimum;
  if (n == 1.0) return info.maximum;
  // Preserve semantic defaults exactly when controls return to their canonical
  // normalized coordinate. In particular, FMA contraction of -24 + .8 * 30
  // must not turn the neutral trim default into a tiny nonzero gain value.
  if (n == postAmpEQParameterToNormalized(parameter, info.defaultValue)) return info.defaultValue;
  return info.logarithmic ? info.minimum * std::pow(info.maximum / info.minimum, n)
                          : info.minimum + n * (info.maximum - info.minimum);
}

[[nodiscard]] inline double postAmpEQParameterToNormalized(
  PostAmpEQParameter parameter, double value) noexcept
{
  const auto& info = postAmpEQParameterInfo(parameter);
  const double physical = std::clamp(std::isfinite(value) ? value : info.defaultValue,
                                     info.minimum, info.maximum);
  if (info.boolean) return physical >= 0.5 ? 1.0 : 0.0;
  if (physical == info.minimum) return 0.0;
  if (physical == info.maximum) return 1.0;
  return info.logarithmic ? std::log(physical / info.minimum) / std::log(info.maximum / info.minimum)
                          : (physical - info.minimum) / (info.maximum - info.minimum);
}

[[nodiscard]] inline double postAmpEQParameterValue(const PostAmpEQControls& controls,
                                                   PostAmpEQParameter parameter) noexcept
{
  const auto index = static_cast<std::uint32_t>(parameter);
  if (index >= 1 && index <= 9)
  {
    const auto& band = controls.bands[(index - 1) / 3];
    switch ((index - 1) % 3)
    {
      case 0: return band.frequencyHz;
      case 1: return band.gainDb;
      default: return band.q;
    }
  }
  switch (parameter)
  {
    case PostAmpEQParameter::enable: return controls.enabled ? 1.0 : 0.0;
    case PostAmpEQParameter::highPassEnable: return controls.highPassEnabled ? 1.0 : 0.0;
    case PostAmpEQParameter::highPassFrequency: return controls.highPassHz;
    case PostAmpEQParameter::lowPassEnable: return controls.lowPassEnabled ? 1.0 : 0.0;
    case PostAmpEQParameter::lowPassFrequency: return controls.lowPassHz;
    case PostAmpEQParameter::trim: return controls.trimDb;
    default: return 0.0;
  }
}

// Nonfinite values are rejected; finite out-of-range values are clamped by the
// same canonical descriptors used by the processor and development controls.
inline bool setPostAmpEQParameter(PostAmpEQControls& controls, PostAmpEQParameter parameter,
                                  double value) noexcept
{
  if (!isPostAmpEQParameter(parameter) || !std::isfinite(value)) return false;
  const auto& info = postAmpEQParameterInfo(parameter);
  value = std::clamp(value, info.minimum, info.maximum);
  const auto index = static_cast<std::uint32_t>(parameter);
  if (index >= 1 && index <= 9)
  {
    auto& band = controls.bands[(index - 1) / 3];
    switch ((index - 1) % 3)
    {
      case 0: band.frequencyHz = value; break;
      case 1: band.gainDb = value; break;
      default: band.q = value; break;
    }
    return true;
  }
  switch (parameter)
  {
    case PostAmpEQParameter::enable: controls.enabled = value >= 0.5; break;
    case PostAmpEQParameter::highPassEnable: controls.highPassEnabled = value >= 0.5; break;
    case PostAmpEQParameter::highPassFrequency: controls.highPassHz = value; break;
    case PostAmpEQParameter::lowPassEnable: controls.lowPassEnabled = value >= 0.5; break;
    case PostAmpEQParameter::lowPassFrequency: controls.lowPassHz = value; break;
    case PostAmpEQParameter::trim: controls.trimDb = value; break;
    default: return false;
  }
  return true;
}

// Transactional validation: one nonfinite physical value rejects the whole
// snapshot. The output is only changed after every field has been checked.
[[nodiscard]] inline bool sanitizePostAmpEQControls(const PostAmpEQControls& input,
                                                   PostAmpEQControls& output) noexcept
{
  PostAmpEQControls sanitized = input;
  for (std::size_t i = 0; i < kPostAmpEQParameterCount; ++i)
  {
    const auto id = static_cast<PostAmpEQParameter>(i);
    if (!setPostAmpEQParameter(sanitized, id, postAmpEQParameterValue(input, id))) return false;
  }
  output = sanitized;
  return true;
}
} // namespace holdsworth::dsp
