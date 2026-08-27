# Official Yamaha Sources

Use official Yamaha documentation as the authoritative source for documented
UD-Stomp / Magicstomp behavior and preset values.

## Primary sources

### UD-Stomp Owner's Manual

Official Yamaha PDF:

https://usa.yamaha.com/files/download/other_assets/6/313746/udstomp_en.pdf

Use this primarily for:

- parameter semantics;
- operating behavior;
- TAP;
- PHASE;
- SYNC;
- CONNECT;
- GROUP;
- worked examples and diagnostic exercises.

### UD-Stomp Patch List

Official Yamaha PDF:

https://usa.yamaha.com/files/download/other_assets/7/313747/udstomp_en2.pdf

Use this primarily for:

- factory preset identity;
- preset number / bank / patch;
- author attribution when documented;
- per-band source settings;
- global source settings.

### Japanese UD-Stomp documentation

Official Yamaha PDF:

https://jp.yamaha.com/files/download/other_assets/7/316047/udstomp_ja.pdf

Use this only when the English documentation is ambiguous or another official
reference is useful.

## Source priority

When sources conflict or appear inconsistent, use this order:

1. the official Yamaha document directly describing the behavior being studied;
2. the official Yamaha patch list for stored factory preset values;
3. repository-maintained condensed reference notes;
4. user-provided or third-party transcriptions.

Do not silently reconcile conflicting official sources.

Report the discrepancy.

## Source versus diagnostic state

A Yamaha manual exercise may instruct temporarily modifying a parameter of a
factory preset.

Keep these concepts separate:

- stored Yamaha factory/source state;
- temporary manual exercise state;
- HoldsworthEngine diagnostic DSP configuration.

Do not rewrite factory metadata to make it match a diagnostic experiment.

Example:

A factory preset may store PHASE NOR while the manual asks the user to switch
that band temporarily to REV for comparison.

The source metadata remains NOR.

The REV configuration is a diagnostic state.

## Local cached manuals

If official Yamaha PDFs are available locally, prefer reading the local copy
rather than downloading the same file repeatedly.

Do not commit copyrighted Yamaha PDF files to the repository unless their
redistribution is known to be permitted.

Repository-maintained summaries should remain concise and point back to the
official document when exact verification is required.
