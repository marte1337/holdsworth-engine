#pragma once

#include "IControls.h"
#include "../integration/DevelopmentControlDefaults.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace holdsworth::ui
{
using namespace iplug;
using namespace iplug::igraphics;

inline IColor panelBackground() { return IColor(255, 19, 23, 29); }
inline IColor cardBackground() { return IColor(255, 27, 33, 41); }
inline IColor mutedText() { return IColor(255, 158, 171, 188); }

inline IVStyle developmentStyle()
{
  return DEFAULT_STYLE.WithDrawFrame(false).WithDrawShadows(false)
    .WithLabelText(IText(15, COLOR_WHITE, "Roboto-Regular"))
    .WithValueText(IText(16, COLOR_WHITE, "Roboto-Regular"))
    .WithColor(EVColor::kBG, cardBackground())
    .WithColor(EVColor::kFG, IColor(255, 103, 170, 224))
    .WithColor(EVColor::kON, IColor(255, 45, 100, 147))
    .WithColor(EVColor::kOFF, IColor(255, 40, 49, 61))
    .WithColor(EVColor::kFR, IColor(255, 62, 76, 92));
}

// Decorative controls never participate in hit testing.
class Card final : public IControl
{
public:
  explicit Card(const IRECT& bounds) : IControl(bounds) { SetIgnoreMouse(true); }
  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(cardBackground(), mRECT, 10.f);
    g.DrawRoundRect(IColor(255, 49, 59, 73), mRECT, 10.f);
  }
};

// Keep the framework's slider behavior, but reserve distinct label, track,
// and value regions. Middle-aligned value text in MakeRects() used to overlap
// the Wet track; the old 34 px rectangle also left almost no drag area.
class PositionSlider : public IVSliderControl
{
public:
  PositionSlider(const IRECT& bounds, IActionFunction action, const char* label,
                 double defaultValue, bool percent = false)
  : IVSliderControl(bounds, action, label, developmentStyle(), false, EDirection::Horizontal)
  , mPercent(percent), mDefault(defaultValue) { IControl::SetValue(defaultValue); }

  void OnInit() override { IVSliderControl::OnInit(); UpdateValue(); }
  void DrawTrack(IGraphics& g, const IRECT& filledArea) override
  {
    const bool emphasized = mMouseIsOver || mMouseDown || g.ControlIsCaptured(this);
    const float radius = mTrackBounds.H() * 0.5f;
    g.FillRoundRect(IColor(255, 73, 87, 105), mTrackBounds, radius, &mBlend);
    if (filledArea.W() > 0.f)
      g.FillRoundRect(emphasized ? IColor(255, 105, 174, 224) : IColor(255, 80, 145, 195),
                      filledArea, radius, &mBlend);
  }
  void DrawHandle(IGraphics& g, const IRECT& bounds) override
  {
    const bool emphasized = mMouseIsOver || mMouseDown || g.ControlIsCaptured(this);
    const float radius = bounds.W() * 0.5f;
    g.FillCircle(emphasized ? IColor(255, 219, 232, 241) : IColor(255, 184, 204, 219),
                 bounds.MW(), bounds.MH(), radius, &mBlend);
    g.DrawCircle(emphasized ? IColor(255, 119, 186, 233) : IColor(255, 86, 145, 188),
                 bounds.MW(), bounds.MH(), radius - 0.75f, &mBlend, 1.5f);
  }
  void OnResize() override
  {
    mLabelBounds = mRECT.GetFromTop(22.f);
    mValueBounds = mRECT.GetFromBottom(22.f);
    mWidgetBounds = mRECT.GetReducedFromTop(26.f).GetReducedFromBottom(26.f);
    mTrackBounds = mWidgetBounds.GetPadded(-mHandleSize).GetMidVPadded(mTrackSize);
    SetTargetRECT(mRECT);
    SetDirty(false);
  }
  bool IsHit(float x, float y) const override { return mRECT.Contains(x, y); }
  void OnMouseDblClick(float, float, const IMouseMod&) override { ResetToDefault(); }
  virtual void ResetToDefault()
  {
    SetValue(mDefault);
    SetDirty(true);
  }
  virtual double underlyingValue() const { return GetValue(); }
  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    // These non-parameter values have no text-entry prompt. Allow dragging
    // from the label and value rows as well as the track.
    ISliderControlBase::OnMouseDown(x, y, mod);
  }
  void SetDirty(bool push, int valIdx = kNoValIdx) override
  {
    IVSliderControl::SetDirty(push, valIdx);
    UpdateValue();
  }
private:
  void UpdateValue()
  {
    const double value = std::clamp(underlyingValue(), 0.0, 1.0);
    if (mPercent)
      mValueStr.SetFormatted(16, "%.0f%%", std::round(100.0 * value));
    else
      mValueStr.SetFormatted(16, "%.3f", value);
  }
  bool mPercent;
  double mDefault;
};

class GainSlider final : public PositionSlider
{
public:
  GainSlider(const IRECT& bounds, IActionFunction action)
  : PositionSlider(bounds, action, "Gain", integration::kDevelopmentGainDefault)
  { IControl::SetValue(mRange.position()); }

  double underlyingValue() const override { return mRange.value; }
  void SetValue(double position, int valIdx = 0) override
  {
    // IControl::SetDirty also calls SetValue with the unchanged thumb value.
    // Do not round-trip the real coordinate on redraw, range switch or reset.
    if (position != GetValue(valIdx))
      mRange.setPosition(position);
    IControl::SetValue(position, valIdx);
  }
  void SetValueFromDelegate(double value, int = 0) override
  {
    mRange.value = std::clamp(value, 0.0, 1.0);
    // Reopening an editor must not change audio to force a saved value into
    // Focus. Reveal Full if the current delegate value is outside Focus.
    if (mRange.value < 0.10 || mRange.value > 0.30)
      mRange.focus = false;
    Refresh(false);
  }
  void SetRangeControl(IControl* control) { mRangeControl = control; Refresh(false); }
  void SetFocus(bool focus) { const bool changed = mRange.setFocus(focus); Refresh(changed); }
  void ResetToDefault() override { mRange.reset(); Refresh(true); }
private:
  void Refresh(bool send)
  {
    IControl::SetValue(mRange.position());
    if (mRangeControl)
    {
      mRangeControl->SetValue(mRange.focus ? 0.0 : 1.0);
      mRangeControl->SetDirty(false);
    }
    SetDirty(send);
  }
  integration::DevelopmentGainRange mRange;
  IControl* mRangeControl = nullptr;
};

// Only presentation order changes. Each button remains at its original
// normalized message index: Lead121, Chorus011, Chorus031, 223, 122.
class PresetGrid final : public IVTabSwitchControl
{
public:
  PresetGrid(const IRECT& bounds, IActionFunction action)
  : IVTabSwitchControl(bounds, action,
      {"Lead 121", "Chorus 011", "Chorus 031", "Holdsworth 223", "Holdsworth 122"},
      "", developmentStyle().WithShowLabel(false), EVShape::AllRounded) {}

  void OnResize() override
  {
    SetTargetRECT(mRECT);
    mWidgetBounds = mRECT;
    mButtons.Resize(0);
    constexpr std::array<int, 5> cells{{0, 2, 3, 4, 1}};
    const auto buttons = mRECT.GetReducedFromLeft(62.f);
    for (int cell : cells)
      mButtons.Add(buttons.GetGridCell(cell, 3, 2).GetPadded(-4.f));
    SetDirty(false);
  }
  void Draw(IGraphics& g) override
  {
    IVTabSwitchControl::Draw(g);
    constexpr std::array<const char*, 3> labels{{"Lead", "Chorus", "Other"}};
    const auto text = IText(13, mutedText(), "Roboto-Regular", EAlign::Near);
    for (int row = 0; row < 3; ++row)
      g.DrawText(text, labels[row], mRECT.GetFromLeft(60.f).GetGridCell(row, 3, 1));
  }
};

// Same nonserialized UI-to-DSP message convention as the original panel.
inline void sendDevelopmentValue(IControl* caller, int messageTag, double value)
{
  auto* delegate = caller->GetDelegate();
  delegate->OnMessage(messageTag, caller->GetTag(), static_cast<int>(sizeof(value)), &value);
  delegate->SendArbitraryMsgFromUI(
    messageTag, caller->GetTag(), static_cast<int>(sizeof(value)), &value);
}

inline IActionFunction developmentMessage(const int messageTag)
{
  return [messageTag](IControl* caller) {
    sendDevelopmentValue(caller, messageTag, caller->GetValue());
  };
}

// Independent cards can be extended without touching NAM's settings container.
inline void attachDevelopmentPanel(IGraphics& g)
{
  const auto label = [&g](const IRECT& rect, const char* text, float size, IColor color) {
    auto* control = new IVLabelControl(rect, text,
      developmentStyle().WithValueText(IText(size, color, "Roboto-Regular", EAlign::Near)));
    control->SetIgnoreMouse(true);
    g.AttachControl(control);
  };
  g.AttachPanelBackground(panelBackground());
  label(IRECT(28, 22, 780, 58), "HOLDSWORTH ENGINE", 26, COLOR_WHITE);
  label(IRECT(28, 65, 1072, 99),
        "INPUT   >   BOOST / DRIVE   >   AMP   >   CAB   >   DELAY   >   OUTPUT", 14, mutedText());
  label(IRECT(902, 25, 1072, 53), "DEVELOPMENT  /  V2", 12, mutedText());

  g.AttachControl(new Card(IRECT(24, 116, 464, 326)));
  label(IRECT(44, 132, 294, 155), "BOOST / DRIVE", 14, mutedText());
  label(IRECT(44, 161, 272, 190), "TC BLD  /  Clean Boost", 19, COLOR_WHITE);
  // iPlug's pressable controls use FG/PR for neutral/pressed, not OFF/ON.
  const auto toggleStyle = developmentStyle().WithShowLabel(false)
    .WithColor(EVColor::kFG, IColor(255, 40, 49, 61))
    .WithColor(EVColor::kPR, IColor(255, 45, 100, 147));
  g.AttachControl(new IVToggleControl(IRECT(332, 150, 444, 190),
    developmentMessage(kMsgTagTCBldEnabled), "", toggleStyle, "BYPASS", "ACTIVE", false),
    kCtrlTagTCBldEnabled);
  auto* gain = new GainSlider(IRECT(44, 210, 164, 302), [](IControl* caller) {
    sendDevelopmentValue(caller, kMsgTagTCBldGain, static_cast<GainSlider*>(caller)->underlyingValue());
  });
  g.AttachControl(gain, kCtrlTagTCBldGain)->SetTooltip(
    "Real Gain coordinate. Double-click: 0.132430924210101 and Focus. Gain response is non-monotonic.");
  auto* range = new IVTabSwitchControl(IRECT(44, 303, 164, 323),
    [gain](IControl* caller) { gain->SetFocus(caller->GetValue() < 0.5); },
    {"FOCUS", "FULL"}, "", toggleStyle
      .WithValueText(IText(11, IColor(255, 215, 226, 235), "Roboto-Regular")));
  g.AttachControl(range)->SetTooltip(
    "Focus: 0.10-0.30. Full: 0-1. Switching to Focus clamps Gain only if outside its range.");
  gain->SetRangeControl(range);
  g.AttachControl(new PositionSlider(IRECT(184, 210, 304, 302),
    developmentMessage(kMsgTagTCBldBass), "Bass", integration::kDevelopmentToneDefault), kCtrlTagTCBldBass)
    ->SetTooltip("Left: cut. Right: boost. Double-click: 0.500.");
  g.AttachControl(new PositionSlider(IRECT(324, 210, 444, 302),
    developmentMessage(kMsgTagTCBldTreble), "Treble", integration::kDevelopmentToneDefault), kCtrlTagTCBldTreble)
    ->SetTooltip("Left: cut. Right: boost. Double-click: 0.500.");

  g.AttachControl(new Card(IRECT(24, 346, 464, 636)));
  label(IRECT(44, 361, 212, 385), "DELAY", 14, mutedText());
  g.AttachControl(new IVToggleControl(IRECT(44, 397, 156, 437),
    developmentMessage(kMsgTagHoldsworthDelayEnabled), "", toggleStyle, "BYPASS", "ACTIVE", false),
    kCtrlTagHoldsworthDelayEnabled)->SetTooltip("Bypass lets existing delay tails decay.");
  g.AttachControl(new PositionSlider(IRECT(230, 358, 444, 450),
    developmentMessage(kMsgTagHoldsworthDelayWetLevel), "Delay Wet", integration::kDevelopmentWetDefault, true),
    kCtrlTagHoldsworthDelayWetLevel)->SetTooltip("Wet level after the preset's own mix. Double-click: 10%.");
  g.AttachControl(new PresetGrid(IRECT(44, 462, 444, 618),
    developmentMessage(kMsgTagHoldsworthDelayPreset)), kCtrlTagHoldsworthDelayPreset)
    ->SetTooltip("Select an existing Holdsworth delay preset. Existing tails may continue during a change.");

  g.AttachControl(new Card(IRECT(480, 116, 1080, 636)));
  label(IRECT(510, 558, 1048, 582), "NAM model, cabinet and input / output controls", 14, mutedText());
}
} // namespace holdsworth::ui
