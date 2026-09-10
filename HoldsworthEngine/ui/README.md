# Development UI v2

The macOS development build uses a 1100 x 660 logical editor with the existing
corner scale resizer. Non-development builds retain 600 x 400. `DevelopmentPanel.h`
owns the development cards; the NAM wrapper attaches its original 600 x 400
control module beside them. No audio routing or parameters are implemented here.

- The header states the input / boost / amp / cab / delay / output signal chain.
- The Boost / Drive card contains TC BLD bypass and Gain, Bass, Treble sliders.
- The Delay card contains bypass, a separate Wet slider and three preset rows.
- Lead: Lead 121 and Holdsworth 122. Chorus: Chorus 011 and Chorus 031.
  Other: Holdsworth 223. This is presentation grouping only.
- NAM retains input/output, noise gate, EQ, model/IR loading, IR bypass, meters,
  and Slim. NAM Settings opens the existing calibration/model-info/about utility
  over the NAM module; the former NAM/Development page switch is removed.

The preset control retains its original five indices and normalized values:
Lead 121=0, Chorus 011=0.25, Chorus 031=0.5, Holdsworth 223=0.75,
Holdsworth 122=1. Only their rectangles are rearranged. All seven development
control tags and message payloads are unchanged; UI Bass/Treble still run from
cut to boost through the existing wrapper mapping.

## Wet collision

Previously Wet used a 34 px-high rectangle and a middle-aligned value style.
iPlug's `IVectorBase::MakeRects()` places a middle-aligned value inside the
remaining widget area, so the percentage and slider occupied the same region.
`IVSliderControl::IsHit()` accepted only the small widget region (the disabled
text prompt made the value area unsuitable for interaction).

The new shared slider adapter uses separate 22 px label/value rows with a
40 px widget region and 4 px gaps, within a 92 px-high control. It delegates
dragging to iPlug's slider base and accepts the entire control rectangle,
including label/value rows, without opening a text prompt. Decorative cards
and headings ignore mouse events. The old delay-toggle rectangle also used
52% of the full width inside a 50% slice, extending left across the BLD toggle;
the independent cards eliminate that separate geometry defect.

TC BLD update crackle remains existing DSP debt. This UI does not smooth,
debounce, or change coefficient updates.

## Control polish

Double-click Gain to send exactly `0.132430924210101` and select Focus;
Bass/Treble reset to `0.5`, and Wet resets to the existing startup value `10%`.
Resets use the same development messages as dragging. Gain's wrapper storage
retains the message double instead of rounding to millionths, including when
the editor reopens or audio resets. Its processor coordinate remains `[0,1]`.

The small Gain selector starts in Focus (`0.10..0.30`). Full exposes `0..1`.
Displayed values and message payloads are always real Gain coordinates.
Focus to Full preserves the value exactly and sends no audio change. Full to
Focus preserves an in-range value, or clamps to the nearest boundary and sends
that change. Double-click always returns to Focus and the exact default.
When an editor reopens with an out-of-Focus delegate value it reveals Full
without changing audio. Range state is local to the editor and not serialized.

BLD and Delay toggles show blue `ACTIVE` when enabled and neutral `BYPASS`
when disabled, using iPlug's actual FG/PR pressable-state colors.

## Manual audition checks

After launching the built app, check the new default size and corner scaling,
label readability, Wet dragging from its track/label/value, each preset's
selected state, BLD cut-to-boost direction, and opening/closing NAM Settings.
Model/IR loading, Slim visibility, and meters should behave as before.
Visual inspection is manual; do not automate screenshots or UI interaction.
