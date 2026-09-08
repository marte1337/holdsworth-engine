# TC BLD M1 offline engaged-CLEAN-BOOST oracle

Status: **documentary nominal, schematic-derived, deterministic, not
hardware-calibrated**.

This directory is the intentionally narrow M1 handoff for the first realtime
Holdsworth-relevant CLEAN BOOST prototype in M2. It contains no realtime DSP,
NAM integration, delay changes, UI, or production controls.

## Frozen input identity

The oracle refuses to run unless the independently audited M0a freeze still
matches all of the following:

- documentary profile:
  `TC-BLD-DOC-NOMINAL__S1501-3__B1501-06__L1501-6__AS-M0A-001`;
- configuration: `STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY`;
- normative M0a bundle SHA-256:
  `7eac2783d1648242ad1bcf757ace2ea6ddd142df5d483b911dfcac0a3684c1aa`;
- resolved M0a handoff SHA-256:
  `eba45473391f8f98f855f4516ac610155714e7b101219e16428d2e592085202e`;
- mode `Boost`, Q1 `channel_off`, Q2 `channel_off`/full-level;
- `R35-PRESET-SERVICE-NOMINAL-MAXIMUM-V1`;
- P1 `TAPER-LOG-10PCT-MID-V1`, P2/P3 `TAPER-LIN-IDEAL-V1`;
- `OPAMP-4741-FAMILY-GENERIC-V1`;
- `SUPPLY-9V-EFFECTIVE-V1`;
- `INPUT-BOUNDARY-SERVICE-INTERNAL-V1` (R13 remains the service 1 MOhm);
- `BOUNDARY-STUDIO-GENERIC-V1` (1 kOhm source, 1 MOhm || 100 pF load).

P4/P5 profiles and the rest of the selected M0a assumption set remain in the
resolved identity even though their dynamic functions are outside this narrow
oracle.

The frozen `circuit.json` terminal bindings put IC1.10, IC1.12, R41.2,
R44.2, Q1.S, and Q4.B directly on `NREF_AUDIO`. The primary resolved handoff
preserves those bindings; its `NREF_AUDIO` equivalence class contains only
`NREF_AUDIO`, not `VREF`. R18 connects these distinct reference nodes. The
pin-12 notation in the human-readable tone-stage section does not declare a
separate tone net. A focused M1 mapping review therefore found no bug in
these connections; the oracle and golden bytes remain unchanged, including
the non-monotonic P1 result and manual tone-range mismatch documented below.

## Architecture and electrical scope

`tools/generate_oracle.py` implements complex dense modified nodal analysis in
the Python standard library. It uses deterministic maximum-magnitude partial
pivoting, rejects singular/non-finite systems, and reports a scaled equation
residual. Pot endpoints use exact zero-volt MNA constraints rather than tiny
resistor substitutions.

The circuit includes the M2-relevant selected topology:

```text
finite R2/R3/C1 VREF
-> input C8/R13/R14/C9 and Boost follower
-> retained R21/R22/R20/C14 distortion-branch loading
-> C15/P1 active Gain
-> NREF_AUDIO and the Q4 base/emitter load
-> coupled P2/P3/C27-C30/R41-R46 tone network
-> C24/R38/Q2-off final feedback and line driver
-> C25/R39/R40 and declared load
```

The Q1 branch is retained with C13, R19, C31 and the selected generic off-state
Rds/Cds. The known passive C17/R33-R36/R16/C22/C26 paths are retained because
they load OG/OUT14 even while Q2 is frozen. Static Q1/Q2 control fixtures hold
their gates for small-signal analysis; the unreadable latch nodes are not
invented.

The 4741 mapping consumes the frozen generic A0 = 50,000 V/V, GBW = 3.5 MHz,
and open-loop Rout = 300 Ohm. Each used section is an MNA differential source
with one dominant pole and series Rout. The DC solution selects the
zero-signal capacitor-charge equilibrium around the local non-inverting input;
the profile's condition-dependent offset/bias magnitudes are not assigned an
unsupported sign at 9 V. Q4's unavoidable NREF_AUDIO load uses a declared
generic beta = 200, VBE = 0.65 V emitter-follower reduction. Neither choice is
a fitted vintage-device claim.

Not modeled: latch/footswitch dynamics, a dynamic Noise Suppressor, Distortion
conduction, diode junction capacitance, 4741 slew/rail/current overload,
recovery, stochastic noise, component aging/tolerances, pickup/cable
de-embedding, or hardware calibration. Those exclusions are deliberate.

## Golden data

[`golden/engaged-clean-boost-primary.json`](golden/engaged-clean-boost-primary.json)
contains:

- DC operating points and VREF checks;
- gain and principal phase from 5 Hz to 100 kHz on a fixed 41-point log grid;
- intrinsic input and output impedance on the same grid;
- P1 Gain curves through attenuation, full-oracle 1 kHz unity, isolated
  active-stage unity, and high-boost regions;
- five-position P2 and P3 sweeps;
- the 3 x 3 Bass/Treble interaction grid;
- source/load and material M0a assumption sensitivities;
- 9 V headroom bounds plus the named 18 V documentary sensitivity;
- independent resistor-ratio and simple RC checks.

The file has no timestamp or platform metadata. `results_sha256` covers its
canonical `results` object. Regenerate and verify it with:

```sh
python3 HoldsworthEngine/references/pedals/tc-bld/m1/tools/generate_oracle.py
python3 HoldsworthEngine/references/pedals/tc-bld/m1/tools/generate_oracle.py --check
```

## Primary numerical results

At the nominal 9 V zero-signal equilibrium:

| Node/result | Value |
|---|---:|
| VREF | 4.486428 V |
| NREF_AUDIO | 4.480653 V |
| O1 | 4.486428 V |
| OG / OUT14 | about 4.480653 V |
| post-C25 output DC | 0 V |
| maximum scaled DC residual | 1.56e-16 |

The 13.57 mV VREF displacement from an unloaded 4.5 V divider is produced by
the explicitly declared generic Q4 base/emitter load. It is not a measured
pedal offset.

P1's independent active-stage relationship is exactly:

```text
47 kOhm / 1.5 kOhm = 31.333333 V/V = +29.920132 dB
```

The generic LOG law places that isolated-stage unity resistance at normalized
shaft `x = 0.3545333873`. Because the frozen graph couples Gain, NREF_AUDIO, and
the active tone/final stage, the complete nominal oracle reaches 1 kHz terminal
unity at `x = 0.1324309242` with Bass/Treble at electrical center. At that
terminal-unity reference the response is approximately:

| Frequency | Gain | Phase |
|---:|---:|---:|
| 20 Hz | -4.506 dB | +53.32 deg |
| 100 Hz | -2.482 dB | +18.49 deg |
| 1 kHz | 0.000 dB | +4.59 deg |
| 10 kHz | -0.030 dB | -33.21 deg |
| 20 kHz | -1.671 dB | -62.40 deg |

The same full circuit predicts +23.91 dB at 1 kHz at the isolated-stage-unity
shaft point and +21.44 dB at the P1 maximum. That non-monotonic terminal result
is not tuned away: it follows the frozen NREF_AUDIO/tone coupling and is a
documentary review flag for M2 and later hardware work, while the local
+29.92 dB resistor relationship remains exact.

At the terminal-unity reference, the selected P2 direction produces +3.98 dB,
-2.48 dB, and -14.78 dB at 100 Hz for x = 0, 0.5, and 1. P3 produces +8.76 dB,
-0.03 dB, and -13.80 dB at 10 kHz for the same positions. These are terminal
responses, not claims that the nominal service graph satisfies the manual's
published +/-16 dB figures. Reversing either shaft sense is potentially
material and simply exchanges which physical direction reaches the electrical
curves.

The combined grid shows real interaction: with Bass/Treble at their 0/0.5/1
positions, 1 kHz ranges from +2.61 dB to -3.08 dB rather than remaining fixed,
and the opposite-band setting shifts the nominal endpoint curves. M2 should
therefore preserve the coupled network rather than use independent shelves.

Selected intrinsic impedances are approximately:

| Frequency | Input magnitude | Output magnitude |
|---:|---:|---:|
| 100 Hz | 996 kOhm | 86.3 Ohm |
| 1 kHz | 588 kOhm | 47.1 Ohm |
| 10 kHz | 72.4 kOhm | 77.6 Ohm |
| 20 kHz | 36.3 kOhm | 111.6 Ohm |

The frequency-dependent input magnitude includes the documented 220 pF input
RF capacitor; it does not rewrite R13 or the separate manual 3-3.3 MOhm claim.

## Sensitivity conclusions

The numerical classification threshold is 0.1 dB or 1 degree over 20 Hz to
20 kHz. The golden file carries full alternate curves and exact deltas.

| Case | Result | Largest reported delta / reason |
|---|---|---|
| Q1 selected off envelope -> ideal-open channel | negligible | 0.049 dB, 0.11 deg |
| Q2 selected off envelope -> ideal-open engineering bound | potentially material | 1.88 dB, 28.6 deg |
| R35 2.2 MOhm -> named 1.1 MOhm midpoint | negligible | 0.00013 dB, 0.0012 deg |
| generic finite 4741 -> ideal op amp | potentially material | 0.79 dB, 17.7 deg |
| effective 9 V -> named 18 V sensitivity | headroom potentially material | AC is nearly unchanged; mapped margin rises about 7.63 dB |
| reverse P1/P2/P3 shaft senses | potentially material | control position is relabeled; large same-angle deltas |
| 1 kOhm -> 10 kOhm / 100 kOhm source | potentially material | 0.41 dB / 8.66 dB plus HF phase change |
| 1 MOhm -> named 10 kOhm output load | potentially material by phase rule | 0.079 dB, 1.84 deg |
| manual 3.15 MOhm boundary versus service 1 MOhm | source-dependent | 0.006 dB at 1 kOhm, 0.56 dB at 100 kOhm in the independent divider check |
| power-jack contact alternatives under effective VPLUS | negligible | exact no-effect in this fixture |
| P4 orientation and JFET D/S swap in the linear reduction | negligible | exact no-effect in this declared domain |
| mode crossbar alternate / C17 unknown crossing | major uncertainty | no M0a-defined alternate net exists, so no topology is invented |
| dynamic Q2 suppressor | major uncertainty outside M1 | M0a marks it non-runnable; M2 uses frozen full level |

The simple headroom bound maps the generic family-typical 1.3 V distance from
each rail to the 9 V single supply. It estimates about 2.25 V RMS source level
at 1 kHz at terminal unity and 0.190 V RMS at P1 maximum before the first
predicted stage limit. At maximum Gain the 20 Hz bound is lower (0.0468 V RMS)
because the coupled nominal response peaks there. This is only where the
linear oracle should stop being trusted; it is not clipping/slew simulation.

## M2 handoff and remaining blockers

Nothing blocks implementing an M2 processor **relative to this exact named
oracle and assumption set**. M2 must retain profile identity and should compare
complex response/impedance over the stored control grid.

The non-monotonic full-circuit P1 result, manual tone-range mismatch, undefined
mode-crossbar alternate, and undefined alternate C17 destination do block any
claim that the M2 result is a hardware-calibrated or generally revision-faithful
vintage BLD. They must remain explicit rather than being corrected by taste.
Dynamic suppressor behavior, large-signal 4741 behavior, and hardware
calibration remain later milestones.

## Focused validation

Run:

```sh
python3 -m unittest discover \
  -s HoldsworthEngine/references/pedals/tc-bld/m1/tests -p 'test_*.py' -v
```

The tests cover the MNA sign/ratio primitives, M0a freeze/profile/digest
association, deterministic byte-for-byte regeneration, schema/identity and
result digest, finite endpoint/control-grid solutions, scaled residuals, VREF
and output-coupling DC sanity, the +29.920132 dB hand calculation, RC checks,
terminal unity, material tone span, and sensitivity-category coverage.
The reference-node regression also checks the actual gain/tone op-amp input,
R18/R19/R41/R44, Q4-base load, and Q1 off-state Rds/Cds stamps against the
digest-verified frozen M0a terminal bindings and primary resolved handoff.
