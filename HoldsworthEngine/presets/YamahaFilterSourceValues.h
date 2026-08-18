#pragma once

namespace holdsworth::presets
{

// Values transcribed from Yamaha's delay-band filter controls. They are
// source metadata only and deliberately provide no conversion to physical DSP
// cutoff-frequency types.
enum class YamahaFilterControlState
{
  off,
  numericValue
};

struct YamahaLowCutControlValue final
{
  [[nodiscard]] static constexpr YamahaLowCutControlValue off() noexcept
  {
    return YamahaLowCutControlValue{YamahaFilterControlState::off, 0.0};
  }

  [[nodiscard]] static constexpr YamahaLowCutControlValue setting(
    const double controlValue) noexcept
  {
    return YamahaLowCutControlValue{YamahaFilterControlState::numericValue, controlValue};
  }

  YamahaFilterControlState state = YamahaFilterControlState::off;
  double value = 0.0;

private:
  explicit constexpr YamahaLowCutControlValue(
    const YamahaFilterControlState controlState,
    const double controlValue) noexcept
  : state(controlState)
  , value(controlValue)
  {
  }
};

struct YamahaHighCutControlValue final
{
  [[nodiscard]] static constexpr YamahaHighCutControlValue off() noexcept
  {
    return YamahaHighCutControlValue{YamahaFilterControlState::off, 0.0};
  }

  [[nodiscard]] static constexpr YamahaHighCutControlValue setting(
    const double controlValue) noexcept
  {
    return YamahaHighCutControlValue{YamahaFilterControlState::numericValue, controlValue};
  }

  YamahaFilterControlState state = YamahaFilterControlState::off;
  double value = 0.0;

private:
  explicit constexpr YamahaHighCutControlValue(
    const YamahaFilterControlState controlState,
    const double controlValue) noexcept
  : state(controlState)
  , value(controlValue)
  {
  }
};

// A frequency printed in Yamaha's filter-control documentation. This remains
// source evidence rather than a DSP configuration value.
struct YamahaDocumentedFilterFrequencyHz final
{
  explicit constexpr YamahaDocumentedFilterFrequencyHz(
    const double documentedFrequencyHz = 0.0) noexcept
  : value(documentedFrequencyHz)
  {
  }

  double value = 0.0;
};

struct YamahaLowCutReferencePoint final
{
  YamahaLowCutControlValue controlValue;
  YamahaDocumentedFilterFrequencyHz documentedFrequency;
};

struct YamahaHighCutReferencePoint final
{
  YamahaHighCutControlValue controlValue;
  YamahaDocumentedFilterFrequencyHz documentedFrequency;
};

// These are isolated documentation facts, not points in an implemented
// Yamaha-control-to-hertz mapping.
inline constexpr YamahaLowCutReferencePoint kDocumentedLowCutControl10At1000Hz{
  YamahaLowCutControlValue::setting(10.0), YamahaDocumentedFilterFrequencyHz{1000.0}};

inline constexpr YamahaHighCutReferencePoint kDocumentedHighCutControl10At1000Hz{
  YamahaHighCutControlValue::setting(10.0), YamahaDocumentedFilterFrequencyHz{1000.0}};

} // namespace holdsworth::presets
