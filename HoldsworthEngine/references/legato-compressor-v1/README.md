# Legato Compressor v1 — M1–M3 objective review

Implementation and objective validation: 2026-10-06; `feature/legato-compressor-v1`; accepted base `52e4325`. Repository preservation and acceptance-status update: 2026-10-20. The objective validation used headless hosts and synthetic fixtures; it did not install plugins, change AU caches, operate REAPER or perform guitar audition.

**Objective validation passed; final musical acceptance remains DEFERRED.** The user reports an encouraging preliminary guitar audition and finds compression beneficial, but wants more playing time before accepting its DSP settings. No final DSP defaults have been accepted. The current values remain experimental implementation defaults. Commit and push of this feature branch are authorized to preserve the work; it must not be merged into main.

**A material level-dependent limitation remains.** At the experimental defaults, the first nominal 150 ms weak note gains level and improves articulation contrast. Louder-input stress reveals substantial first-note attenuation: a -6 dBu picked note followed by an -8 dB, 20 ms weak note loses 2.55 dB; a 0 dBu pick followed by a -12 dBu, 150 ms weak note loses 0.89 dB in its body. DSP settings and UI behavior were not changed during repository preservation.

## 1. Implementation

M1 freezes the approved contract in [spec.md](spec.md) and deterministic fixtures in `../../tests/LegatoCompressorFixtures.h`. M2 adds `LegatoCompressorProcessor` with bounded finite state, deterministic reset, lock-free triple-mailbox control publication, 10 ms transitions, exact finite dry bypass and continuously advancing detector/reduction state. M3 inserts gain processing immediately after `_ProcessInput()` and before `mPreNAMSelector.processSelected()`; Pedal Off remains supported. The physical detector uses `hostToVoltsGain / selectedInputGain`; audible data retains its original domain and arithmetic. Missing physical calibration uses the separately stored normalized dBFS threshold.

Controls: Enable Off initially; -18 dBu / -30 dBFS thresholds; 15 ms Attack; 60 ms Release; +3 dB Makeup. The compact bottom development card adds these controls plus detector/reduction meters with explicit active units. Editor height is 980 px; existing cards stay at their old coordinates. No Assist, Ratio, sidechain EQ, auto makeup, limiter or scalar-boost product control was added. Development controls remain nonserialized.

## 2. Exact static curve

Let `d = L - T`, where L and T have the same RMS unit. Requested positive attenuation in dB is:

```text
q(d) = 0                    if d <= -3
       (d + 3)^2 / 24       if -3 < d < 3
       d / 2                if d >= 3
r(d) = clamp(q(d), 0, 6)
Y    = L + M - r(d)         at a constant settled detector level
```

The knee joins continuously with continuous first derivative. Output slope is 1 below the knee, transitions to 1/2 inside it, is 1/2 for 3 < d < 12, then returns to 1 after the 6 dB attenuation cap. At d = -3/0/3/6/12 dB, requested reduction is exactly 0/0.375/1.5/3/6 dB. Manual M is -6..+4 dB; total instantaneous compressor gain is bounded to +4 dB. The cap limits gain reduction, not signal peaks. There is no limiter, saturation, oversampling, lookahead or audio delay. Periodic detector ripple can bias a steady waveform slightly relative to this constant-level curve.

## 3. Timing

Gain reduction is smoothed in dB with `a = exp(-N/(fs*tau))`; attack when requested attenuation increases, release otherwise. This is the documented 63.2% one-pole time constant. Power uses `aP = exp(-1/(fs*0.004))` every sample. Its 4 ms constant is a power step constant: RMS amplitude has reached sqrt(0.6321), about 79.5%, at that point. The gain-stage measurements hold detector level fixed and exclude detector acquisition. Interpolating amplitude adds a causal control-interval ramp, with no sample delay.

| Rate | Control interval ms | Attack measured ms | Release measured ms | Power measured ms |
|---:|---:|---:|---:|---:|
| 44100 | 0.498866 | 15.000530 | 60.000412 | 4.000015 |
| 48000 | 0.500000 | 15.000000 | 60.000000 | 4.000000 |
| 88200 | 0.498866 | 15.000530 | 60.000412 | 4.000003 |
| 96000 | 0.500000 | 15.000000 | 60.000000 | 4.000000 |
| 176400 | 0.498866 | 15.000530 | 60.000412 | 4.000001 |
| 192000 | 0.500000 | 15.000000 | 60.000000 | 4.000000 |

All 8/15/30 ms attack and 40/60/180 ms release combinations at all six rates: maximum stage error 0.006879%, below the 5% requirement. Maximum power-stage interpolation error is 0.000386%. Enable settles after ceil(fs * 0.010) samples. Reset, in-place and irregular partitions remain deterministic.

## 4. First weak note and picked transient

The frozen nominal fixture has a 300 ms -12 dBu harmonic picked note, immediately followed by the FIRST 150 ms -24 dBu weak note. No later recovered note is substituted. A 50 ms silent preroll settles Enable. Picked body is 100..280 ms; weak body is 40..140 ms after weak onset. Reduction from the picked note is still recovering in the weak body.

| Nominal 48 kHz metric | Result |
|---|---:|
| First weak whole-note gain | +1.8615 dB |
| First weak body gain | +2.1685 dB |
| First weak body contrast improvement | +2.3427 dB |
| First weak first 5 ms gain | -0.1428 dB |
| Picked first 3 ms peak vs plain +3 dB | -0.00674 dB |

Across all six rates, body gain is +2.16825..+2.16854 dB and body contrast improvement +2.34254..+2.34275 dB. Whole-note gain is +1.86093..+1.86149 dB. The onset remains slightly attenuated (-0.14358..-0.14279 dB in its first 5 ms). Picked peak attenuation is 0.00499..0.00860 dB, below 1 dB. Nominal body acceptance passes; a small weak-onset deficit remains.

Level stress at the unchanged settings:

| Pick / first 150 ms weak | Whole weak gain | Weak body gain | First 5 ms | Body contrast improvement |
|---|---:|---:|---:|---:|
| -6 / -18 dBu | +0.617 | +1.102 | -2.890 | +4.013 |
| 0 / -12 dBu | -1.207 | -0.888 | -3.000 | +2.111 |

**The 0 dBu stress fixture fails positive absolute weak-note lift despite improved contrast.** At -6 dBu, its body recovers but the first weak onset remains strongly suppressed. These are limitations of the fixed threshold, 60 ms recovery and +3 dB makeup; later notes do not excuse them.

## 5. Rapid runs

Each weak note is reported separately. Columns N1..N6 are -8/-12/-16/-12/-8/-12 dB relative to the nominal pick; values are whole-note RMS gain at 48 kHz.

| Spacing ms | N1 | N2 | N3 | N4 | N5 | N6 |
|---:|---:|---:|---:|---:|---:|---:|
| 20 | +0.177 | +0.948 | +1.520 | +1.945 | +2.235 | +2.452 |
| 40 | +0.564 | +1.720 | +2.341 | +2.661 | +2.819 | +2.898 |
| 60 | +0.891 | +2.206 | +2.704 | +2.892 | +2.942 | +2.960 |
| 80 | +1.166 | +2.491 | +2.866 | +2.965 | +2.957 | +2.967 |
| 120 | +1.584 | +2.785 | +2.971 | +2.996 | +2.953 | +2.973 |

No nominal weak note is more than 0.5 dB quieter at any tested rate. The first weak first 5 ms is about -0.144 dB at every spacing; short runs get much less whole-note benefit than long ones.

| Louder 20 ms stress | N1 | N2 | N3 | N4 | N5 | N6 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 6 dB above nominal | -2.552 **FLAG** | -1.410 **FLAG** | -0.245 | +0.659 | +1.071 | +1.262 |
| 12 dB above nominal | -2.967 **FLAG** | -2.518 **FLAG** | -1.616 **FLAG** | -0.763 **FLAG** | -0.923 **FLAG** | -1.239 **FLAG** |

All rates and every note, including multiple -8/-12/-16 dB legato notes, alternating pick/legato, equal-level controls and silence/note, are in [notes.csv](evidence/notes.csv). Stress is supplementary to the frozen nominal fixture, never a replacement for it.

## 6. Plain scalar boost comparison

Equivalent +3 dB scalar boost adds 3 dB to both picked and weak notes and improves phrase contrast by exactly 0 dB. The nominal compressor gives -0.17421 dB picked-body gain and +2.16854 dB first-weak-body gain, improving contrast by 2.34275 dB. Compared with +3 dB scalar boost, the picked body is 3.17421 dB lower and weak body 0.83146 dB lower. With scalar gain matched to the compressor picked body, the compressor weak body is 2.34275 dB louder. This is a measured articulation change beyond raising pre-NAM level; its behavior after a selected nonlinear NAM remains a musical question. Scalar boost is test infrastructure only.

## 7. Calibration equivalence

The hard test uses the SAME physical waveform under six host normalizations (-6/0/6/12/18/24 dBu), four model calibrations (-6/6/12/18 dBu), both Pedal Off native NAM-domain and selected-pedal volts-domain representations, and all six rates: 288 cases. The +3 dB trim is represented consistently. Full per-sample detector-level/power/applied-gain traces are compared, not merely final levels.

Maximum level error: 7.11e-15 dB; applied-gain error 1.49e-14; power error 2.78e-17.

Actual APP/VST3/AU hosts additionally load a real calibrated Linear NAM and compare the same voltage across six host settings in Off and TC domains. APP/VST3 errors: 5.33e-15 dB level, 3.55e-15 gain. AU float32 input quantization: 1.64e-7 dB level, 2.00e-9 gain; below the declared 2e-6 dB / 2e-7 tolerances. Normalized mode uses independent dBFS values and never presents dBu. The mode flag and meter values are published coherently in one lock-free atomic word.

## 8. Noise, modulation and decay

Below-threshold seeded broadband noise plus 50 Hz hum settles at exactly +3 dB gain, 0 dB peak-to-peak gain ripple and zero reduction. Makeup raises the noise floor. For settled pure tones at -12 dBu, analyzed over seconds 1..2 at 48 kHz:

| Tone Hz | Mean net gain dB | Gain ripple p-p dB | Output 3f dBc | Largest 1..80 Hz gain modulation |
|---:|---:|---:|---:|---:|
| 82.4069 | -0.17895 | 0.0291138 | -61.77 | 22 Hz, -94.22 dB relative to mean gain |
| 110.0000 | -0.14146 | 0.0164786 | -66.91 | 20 Hz, -89.60 dB relative to mean gain |
| 220.0000 | -0.07626 | 0.00475411 | -79.73 | 40 Hz, -96.68 dB relative to mean gain |
| 440.0000 | -0.03954 | 0.00215171 | -93.80 | 80 Hz, -108.47 dB relative to mean gain |
| 1000.0000 | -0.00480 | 9.45563e-14 | -305.83 | 1 Hz, -245.57 dB relative to mean gain |

3f is fitted jointly with the carrier/DC to avoid noninteger-frequency leakage; pure-input controls are at the numerical floor. Low-frequency applied gain uses full-rate, Hann-windowed demodulation on a 1 Hz grid. These measurements show small periodic detector/control modulation; they do not establish an audibility verdict. The 1000 Hz tone locks to the 2 kHz control cycle and has no resolved ripple. Frequency dependence at equal RMS is reported rather than hidden.

The sustained decay shows gain +0.281 dB at 50 ms, +1.360 dB at 200 ms, +2.669 dB at 400 ms, +2.99946 dB at 800 ms and +3 dB by 1.2 s. Its tail approaches manual makeup smoothly; no gate, extra limiter or decay truncation was added. Silence starts from bounded power and reacquires a note causally.

## 9. CPU and realtime

Power updates per sample; gain computer runs at round(fs/2000) sample intervals with amplitude interpolation. A per-sample reference changes nominal picked-body level by at most 0.000400 dB, weak body by at most 0.003474 dB and instantaneous sample gain by at most 0.049594 dB. No further optimization was made.

Isolated processor at 48 kHz / 128 samples: median 1.625 us, p99 1.792 us (0.0672% of callback budget). Full-rate reference median 4.083 us. Worst production p99 for blocks >=64: 0.269% (192000 Hz, block 128, on). 264 production cases plus 66 reference cases, 2048 observations each; 0 raw production maximum deadline outliers in this run.

Actual whole-plugin unloaded NAM/IR callback results include existing downstream/framework cost, with pedals/delay/tuner disabled. Warm control servicing occurs outside callback timing.

| Host | 48k/128 compressor ON median us | ON p99 us | Worst p99 budget, blocks >=64 | Raw maximum deadline outlier cases |
|---|---:|---:|---:|---:|
| APP | 10.792 | 30.458 | 6.627% | 11 |
| VST3 | 10.833 | 36.791 | 7.476% | 19 |
| AU | 10.334 | 12.000 | 1.922% | 2 |

Interposition self-test proves malloc/free/mutex detection, then processor and native-host runs record zero allocation/free/lock/wait calls in processing. Existing downstream first-use allocations are explicitly warmed before whole-plugin auditing; first compressor enable remains audited. Source inspection confirms no filesystem, logging, UI, exception or waiting calls in compressor processing. All state is fixed storage, and atomic publication is statically lock-free. TSan includes concurrent 20,000-tuple mailbox publication. Timer granularity limits tiny-block medians; scheduler outliers are retained in CSV. These are CPU measurements, not a hard realtime OS or every-model headroom guarantee.

## 10. Tests, builds and audits

| Check | Result |
|---|---|
| Debug engine | All 324 pass |
| Release engine | All 324 pass |
| ASan + UBSan | All 324 pass, no diagnostics |
| TSan | All 324 pass, no diagnostics |
| Strict warnings | New processor, tests and objective driver pass -Wall/-Wextra/-Wpedantic/-Wconversion/-Wsign-conversion/-Wshadow/-Werror |
| Clang static analyzer | New processor, both test files and driver: no diagnostics, analyzer-werror enabled |
| APP / VST3 / AU | Release arm64 built and signatures/plists verified |
| Native formats | All six rates; 11 block sizes including 1 and irregular 37; normal/host bypass; zero audited calls |
| Framework latency regression | Real IPlugProcessor: six rates x seven blocks; initial/model+32 totals, host queries/restarts, concurrency and readiness faults pass |
| Source / project / diff | Accepted arithmetic after precise insertion removal; old DSP/reference/submodule files frozen; serialization and IDs preserved; source enrollment; plutil and diff checks pass |

15 new tests join 309 accepted tests. The existing TC bridge has one pre-existing local-shadow diagnostic under the stricter -Wshadow flag; it remains untouched. Product builds retain existing dependency warnings. Source audit preserves gate, tone, NAM, IR, tuner tap, Yamaha delay and framework latency arithmetic. Native hosts exercise Off/TC/MC/AH, real NAM/IR state loading, compressor active tuner acquisition, and real 32 -> 61 -> 32 NAM-resampler latency changes. Compressor toggles cause no additional host PDC changes or VST3 restart notifications.

Repository-preservation checks on 2026-10-20 reran all 324 Debug and all 324 Release tests successfully, plus the existing source audit, project lint and `git diff --check`. DSP, UI, integration, tests and project files match the source hashes from objective validation. The sanitizer/native-format results above remain the 2026-10-06 validation record; this housekeeping task did not change processing behavior or rerun musical acceptance. Fresh build outputs and test logs remain under the ignored `build-legato-compressor-v1-review/housekeeping-20261020` directory.

Compressor latency is exactly zero and settled local bypass returns exact finite samples with warm detector/reduction history. Matching downstream histories give bit-exact whole-plugin bypass. Earlier processed audio naturally changes downstream recursive filter history; a 2 s settling measurement gives zero residual in all native formats/rates. Settled framework bypass is bit-exact regardless of compressor enable and advances identical compressor diagnostic traces. Existing 10 ms framework fades retain their processed contribution during transition.

## 11. Proposed artifacts for later audition

Original objective-validation Release arm64 artifacts, ad hoc signed and verified, were copied to the ignored review directory below. These generated bundles are excluded from commits. The user's later preliminary guitar audition is encouraging, but these build results do not establish final musical acceptance or acceptance of the DSP defaults.

- APP: `build-legato-compressor-v1-review/products/NeuralAmpModeler.app`
  - Executable SHA-256: `e9d4223b9e57c97cc95702a29d4431dfbda5256c20d7f4aa67e63ded378f32e8`
- VST3: `build-legato-compressor-v1-review/products/NeuralAmpModeler.vst3`
  - Executable SHA-256: `4885f22057c157568bfaede2226c131a5ae6c8d49016fa1f2dd6a417d00390f4`
- AU: `build-legato-compressor-v1-review/products/NeuralAmpModeler.component`
  - Executable SHA-256: `944822d87640f2394bd0ce6293f36badc1e4b1acc8c96e0f49d5774daa48bb37`

## 12. Remaining musical uncertainties

- The user wants more playing time before accepting the compressor's DSP settings. Compression has been beneficial in preliminary guitar audition; final musical acceptance and final defaults remain pending.
- First weak notes can lose substantial absolute level at hotter input levels, especially rapid runs. The full report includes these failures; default release was not shortened or otherwise redesigned.
- Nominal weak onset remains about 0.14 dB below dry despite positive body lift.
- Manual makeup raises noise/hum by 3 dB; actual guitar/interface noise and gate interaction need the settled baseline.
- Small low-frequency ripple/sidebands and RMS frequency bias require listening with the selected NAM/IR; synthetic metrics are not a musical verdict.
- Drive and nonlinear NAM may alter perceived articulation and onset balance. No final amp/IR chain or real guitar phrase was accepted.
- Visual UI acceptance remains pending. Expected layout: independent bottom LEGATO COMPRESSOR card, ON/OFF on left, Threshold/Attack/Release/Makeup across the card, detector/reduction below, explicit threshold dBu or dBFS.

## Reproduction and evidence

Run from the repository with Xcode selected:

```sh
LEGATO_BUILD=/private/tmp/nam-legato-v1 bash HoldsworthEngine/references/legato-compressor-v1/validate-engine.sh
LEGATO_BUILD=/private/tmp/nam-legato-v1 bash HoldsworthEngine/references/legato-compressor-v1/validate-objective.sh
LEGATO_BUILD=/private/tmp/nam-legato-v1 bash HoldsworthEngine/references/legato-compressor-v1/package-products.sh
LEGATO_BUILD=/private/tmp/nam-legato-v1 python3 HoldsworthEngine/references/legato-compressor-v1/compile-host-tests.py
LEGATO_BUILD=/private/tmp/nam-legato-v1 python3 HoldsworthEngine/references/legato-compressor-v1/validate-hosts.py
```

Xcode module caching and AudioComponentRegister require the normal macOS process environment; restricted runs may fail before executing validation. The AU factory is registered only inside the test process, with no installation or global cache operation. Headless APP runner links exact Release target objects; VST3/AU runners load the actual Release bundles. Their between-callback diagnostic helpers use the matching compiled layout.

[spec.md](spec.md) freezes M1. [summary.json](evidence/summary.json), [notes.csv](evidence/notes.csv), [timing.csv](evidence/timing.csv), [detector-timing.csv](evidence/detector-timing.csv), [calibration.csv](evidence/calibration.csv), [control-rate.csv](evidence/control-rate.csv), [cpu.csv](evidence/cpu.csv), [validation.json](evidence/validation.json), [source-audit.json](evidence/source-audit.json), [host-validation.json](evidence/host-validation.json) and retained native/engine logs preserve objective results. These are deliberate validation records. Generated bundles, compiler/analyzer dumps, machine-local artifact manifests, larger 48 kHz f64 traces and full product build logs stay in the ignored `build-legato-compressor-v1-review` directory. Synthetic test renders were not used as musical acceptance; the user's subsequent preliminary guitar audition is recorded separately above.
