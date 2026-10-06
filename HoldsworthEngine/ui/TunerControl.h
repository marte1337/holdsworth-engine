#pragma once

#include "IControls.h"
#include "../dsp/ChromaticTuner.h"
#include "../integration/TunerDisplayState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace holdsworth::ui
{
// Main-thread presentation only. A complete snapshot keeps note, cents and
// direction coherent; pitch analysis and tracking remain outside this control.
class TunerControl final : public iplug::igraphics::IControl
{
public:
  TunerControl(const iplug::igraphics::IRECT& bounds, int snapshotMessage)
  : IControl(bounds), mSnapshotMessage(snapshotMessage)
  { SetIgnoreMouse(true); }

  void OnMsgFromDelegate(int message, int size, const void* data) override
  {
    if (message != mSnapshotMessage || size != static_cast<int>(sizeof(mSnapshot)) || data == nullptr) return;
    static_assert(std::is_trivially_copyable_v<integration::TunerDisplaySnapshot>);
    std::memcpy(&mSnapshot, data, sizeof(mSnapshot));
    SetDirty(false);
  }

  void Draw(iplug::igraphics::IGraphics& graphics) override
  {
    using namespace iplug::igraphics;
    using Status = decltype(mSnapshot.status);
    using Direction = decltype(mSnapshot.direction);
    const IColor muted(255, 158, 171, 188);
    const IColor neutral(255, 73, 87, 105);
    const IColor tuned(255, 101, 205, 155);
    const IColor offset(255, 234, 181, 109);
    const bool stable = mSnapshot.status == Status::stable && !mSnapshot.held
      && mSnapshot.midiNote >= 0 && std::isfinite(mSnapshot.cents);
    const bool held = mSnapshot.held && mSnapshot.midiNote >= 0;

    const auto noteBounds = IRECT(mRECT.L, mRECT.T, mRECT.L + 136.f, mRECT.T + 44.f);
    const auto centsBounds = IRECT(mRECT.L, mRECT.T + 44.f, mRECT.L + 136.f, mRECT.B);
    char note[24] = "--";
    char cents[32] = {};
    if (stable || held)
      std::snprintf(note, sizeof(note), "%s%d", dsp::ChromaticTuner::noteName(mSnapshot.midiNote), mSnapshot.octave);
    if (stable)
      std::snprintf(cents, sizeof(cents), "%+03d cents", mSnapshot.wholeCents);
    else
    {
      const char* state = "Listening...";
      if (held || mSnapshot.status == Status::unstable) state = "Unstable";
      else if (mSnapshot.status == Status::off) state = "Off";
      else if (mSnapshot.status == Status::noSignal) state = "No signal";
      else if (mSnapshot.status == Status::unsupportedRate) state = "Unsupported rate";
      std::snprintf(cents, sizeof(cents), "%s", state);
    }
    graphics.DrawText(IText(32, stable ? COLOR_WHITE : muted, "Roboto-Regular"), note, noteBounds);
    graphics.DrawText(IText(stable ? 18 : 13, stable ? COLOR_WHITE : muted, "Roboto-Regular"), cents, centsBounds);

    const auto gauge = IRECT(mRECT.L + 160.f, mRECT.T + 2.f, mRECT.R, mRECT.B);
    const float left = gauge.L + 6.f, right = gauge.R - 6.f;
    const float center = (left + right) * .5f;
    const float lineY = gauge.T + 30.f;
    graphics.DrawLine(neutral, left, lineY, right, lineY, nullptr, 2.f);
    for (int tick = 0; tick <= 4; ++tick)
    {
      const float x = left + static_cast<float>(tick) * (right - left) / 4.f;
      const float halfHeight = tick == 2 ? 10.f : 5.f;
      graphics.DrawLine(neutral, x, lineY - halfHeight, x, lineY + halfHeight, nullptr, 1.5f);
    }
    if (stable && mSnapshot.direction != Direction::none)
    {
      const auto color = mSnapshot.direction == Direction::inTune ? tuned : offset;
      const auto centsPosition = static_cast<float>(std::clamp(mSnapshot.cents, -50., 50.) / 100.);
      graphics.FillCircle(color, center + centsPosition * (right - left), lineY, 4.5f);
    }
    const auto labels = IRECT(gauge.L, gauge.T + 46.f, gauge.R, gauge.B);
    const auto drawDirection = [&](const char* label, int column, Direction direction, const IColor& color) {
      const bool selected = stable && mSnapshot.direction == direction;
      graphics.DrawText(IText(11, selected ? color : muted, "Roboto-Regular"), label,
                        labels.GetGridCell(column, 1, 3));
    };
    drawDirection("FLAT", 0, Direction::flat, offset);
    drawDirection("IN TUNE", 1, Direction::inTune, tuned);
    drawDirection("SHARP", 2, Direction::sharp, offset);
  }

private:
  int mSnapshotMessage;
  integration::TunerDisplaySnapshot mSnapshot{};
};
} // namespace holdsworth::ui
