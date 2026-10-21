# Post-Amp EQ timing-outlier investigation

Diagnosis: **no reproducible EQ-specific CPU execution-time failure was found**.
Rare wall-clock spikes were reproduced. In the four full-plugin intervals that
exceeded the deadline, only 0.726–1.115 ms was charged to the benchmark thread;
the wall-minus-thread-CPU gap was 24.863–32.516 ms. This supports off-CPU /
benchmark-timing interruptions rather than tens of milliseconds of EQ work.
The original event has no paired CPU trace, so its precise cause remains unknown.

No DSP, UI, accepted benchmark, preset, branch, commit or acceptance threshold was
changed. All investigation files are under this temporary directory. The original
CPU CSV and summary hashes, 40 frozen production/evidence files, HEAD and vendor
state were verified unchanged.

## Original measurement boundary

The 27.097583 ms maximum was **isolated EQ**, not the complete plugin. Original
`eq-cpu.cpp` lines 85–90 start `steady_clock` after generating/publishing controls,
set the RT audit flag, execute `PostAmpEQProcessor::processBlock`, clear the flag,
and stop the clock. It includes target adoption, coefficient calculation and
sample processing, audit-flag stores and minimal clock/call overhead. It excludes
UI/control generation, setControls publication, vector bookkeeping/checksum,
sorting/printing, NAM, tone stack, cabinet, gate, tuner, pedals, delay and iPlug2.
There was no actual audio-device callback: the 23.219954648526 ms deadline was
calculated as 4096 / 176400 seconds for comparison.

The original stored only sorted p50/p99/max statistics. It has no original sample
index, paired thread CPU time or scheduler trace. Therefore the original count
of individual overruns and precise interruption location cannot be reconstructed.
Its maximum remains preserved exactly.

## Repeat protocol

176.4 kHz, 4,096 frames, unchanged 23.219954648526 ms deadline. For each condition,
12 warm runs x 2,048 measured calls = **24,576 samples**, with 256 unmeasured warm
calls per run and rotating condition order. Every measured sample is retained.
The isolated test uses the original signal/control equations, with an additional
wire/copy harness control. It runs both the original wall-only timing boundary
and a second batch with paired wall/thread-CPU clocks. The full-plugin APP uses
the same paired clock order and real Release `AppProcess` objects.

No competing build, test or benchmark job ran during the measurements. A small
CSV aggregation overlapped the first APP process; that process and all its samples
remain included. The OS was not isolated, no priority/affinity changes were made,
and the existing GUI application was left untouched. These are ordinary CLI
threads, not CoreAudio realtime threads.

The accepted pre-EQ binary directory was empty. The baseline was independently
rebuilt from accepted commit `52e432585918adac8b1b41d53311aa8635c28e44` via git archive
into a temporary directory, with exactly matching vendor revisions and old
headers. The current build uses the existing verified Release objects and current
headers. The full-plugin comparison has no NAM/IR loaded; gate, tone, selected
pedal, tuner and audible delay are disabled. The unchanged delay engine still
advances with silence. This is a matched fallback-path comparison, not a loaded
guitar rig. Export/build/object provenance is retained under `baseline/`.

## Isolated EQ, paired clocks

All times below are **microseconds**, reported as **p50 / p99 / maximum**.
CPU maxima and wall maxima can occur on different samples; paired outlier data
is retained separately. Overruns compare every sample with the unchanged budget.

| Condition | Wall us: p50 / p99 / max | Thread CPU us: p50 / p99 / max | Wall / CPU overruns |
|---|---:|---:|---:|
| wire-control | 0.666 / 0.834 / 19.500 | 0.500 / 0.708 / 12.625 | 0 / 0 |
| bypass-tracking | 70.584 / 81.792 / 222.458 | 70.458 / 79.125 / 194.416 | 0 / 0 |
| neutral-tracking | 70.584 / 80.334 / 228.834 | 70.458 / 78.459 / 163.625 | 0 / 0 |
| settled-five-sections | 73.167 / 91.042 / 3910.250 | 73.000 / 83.625 / 232.208 | 0 / 0 |
| continuous-sweep | 75.709 / 85.959 / 188.167 | 75.542 / 83.958 / 137.375 | 0 / 0 |
| continuous-sweep-enables | 75.167 / 85.875 / 184.709 | 75.000 / 83.583 / 162.542 | 0 / 0 |

## Isolated EQ, original wall-only boundary

CPU time was **not measured** in this batch, rather than recorded as zero.

| Condition | Wall us: p50 / p99 / max | Wall overruns |
|---|---:|---:|
| wire-control | 0.417 / 2.208 / 46.459 | 0 |
| bypass-tracking | 70.333 / 103.125 / 21375.625 | 0 |
| neutral-tracking | 70.333 / 81.375 / 34043.000 | 1 |
| settled-five-sections | 72.875 / 83.709 / 166.875 | 0 |
| continuous-sweep | 75.458 / 86.084 / 239.208 | 0 |
| continuous-sweep-enables | 74.916 / 85.625 / 244.167 | 0 |

The same class of large elapsed spike reproduced with **neutral EQ: 34.043 ms**.
The isolated continuous-sweep overrun itself did not reproduce in either batch:
49,152 measured calls, maximum 0.239208 ms in wall-only and 0.188167 ms in paired
timing. The exact original spike is not claimed explained by this absence.

## Full APP comparison, paired clocks

| Condition | Wall us: p50 / p99 / max | Thread CPU us: p50 / p99 / max | Wall / CPU overruns |
|---|---:|---:|---:|
| baseline | 279.500 / 321.375 / 870.333 | 279.167 / 309.042 / 863.458 | 0 / 0 |
| bypass | 349.458 / 418.541 / 33241.667 | 349.042 / 393.000 / 1078.500 | 1 / 0 |
| neutral | 349.125 / 408.250 / 7630.333 | 348.791 / 388.792 / 1112.959 | 0 / 0 |
| active | 350.792 / 434.875 / 26539.125 | 350.333 / 403.334 / 1119.458 | 1 / 0 |
| sweep | 354.708 / 423.417 / 20130.167 | 353.834 / 397.250 / 1231.708 | 0 / 0 |
| sweep_enable | 357.750 / 441.625 / 27409.417 | 357.167 / 410.417 / 1228.875 | 2 / 0 |

The pre-EQ baseline had no overruns in this finite sample. Current-bypass, active
and enable-sweep wall spikes did occur; every such spike was predominantly time
not charged to the benchmark thread, as shown below. No measured CPU interval
in any condition approached the deadline; maximum full-APP CPU was 1.231708 ms.

| Mode / trial / callback | Wall us | Paired thread CPU us | Wall minus CPU us |
|---|---:|---:|---:|
| bypass / 1 / 1954 | 33241.667 | 725.666 | 32516.001 |
| sweep_enable / 1 / 671 | 25977.792 | 1114.500 | 24863.292 |
| sweep_enable / 4 / 1118 | 27409.417 | 930.208 | 26479.209 |
| active / 9 / 876 | 26539.125 | 849.417 | 25689.708 |

The rebuilt baseline median thread CPU was 279.167 us; current EQ modes were
348.791–357.167 us. The incremental cost is about **70–78 us per 4096-frame block**
(0.30–0.34% of its budget), consistent with the independently measured isolated
EQ cost. Bypass intentionally keeps five section histories warm. Continuously
changing parameters add about 5 us versus neutral in the full APP median. This
is bounded expected processing cost, not a reproduced 27 ms execution-time path.

## Clock / realtime checks and interpretation

`CLOCK_THREAD_CPUTIME_ID` measures execution time of the calling thread, unlike
elapsed wall time: [POSIX clock specification](https://pubs.opengroup.org/onlinepubs/000095399/functions/clock_getres.html).
The measured sequence is wall-start, CPU-start, audio call, CPU-end, wall-end.
The clocks reported 42 ns CPU-clock resolution. The empty timing envelope had
0.333 us wall / 0.167 us CPU median, and 1.125 us maximum wall overhead. Nothing
was subtracted from any result. A deliberate 30 ms sleep calibration had median
32.790166 ms wall but only 0.021208 ms thread CPU, confirming that off-CPU waits
are excluded by the CPU clock.

The RT interposer's positive allocation/free/mutex controls passed. The isolated
run and all 72 APP processes had zero audited forbidden calls during measured
processing. Full-plugin first-use storage was warmed; no later calls were waived.

The isolated paired test also captured 3.910250 ms wall with 0.176541 ms CPU in
active processing. Together with the four paired full-plugin overruns, this is
direct evidence of large off-CPU / timing-boundary gaps on this system. It does
not identify the exact scheduler, kernel, hypervisor, timer-call or interruption
mechanism. Cache stalls and slow arithmetic while a thread is running are
charged as thread CPU time; they cannot alone explain these large gaps.

The original isolated result cannot be blamed on pre-existing NAM/framework
processing, because those were absent from its timer. Evidence instead favors
benchmark/system scheduling interruptions. Current-build-only wall overruns in
this limited full-APP sample do not establish that tail probabilities are equal
to the baseline, nor prove a product scheduling guarantee.

## Diagnosis and proposed action

No EQ-specific computational regression requiring a DSP fix was found. Do not
change the filters, smoothing, bypass state tracking or acceptance thresholds
on this evidence. A useful future reporting improvement is to retain paired CPU
and wall timing, raw sample indices and overrun counts. That diagnostic approach
was used here only in temporary harnesses; the accepted benchmark is untouched.

Preserve the original 27.097583 ms finding, the neutral 34.043 ms repeat and all
four full-plugin overruns. There is no claim that they cannot occur in a real
host: a wall-time interruption can still miss an audio deadline. Exact original
cause and behavior under a loaded NAM/IR and actual realtime host scheduling
remain uncertain. This investigation stops at diagnosis; no fix is implemented.

## Retained evidence

- `diagnosis.json`: machine-readable result, complete aggregates, provenance,
  limitations and hashes of every raw CSV.
- `isolated/raw.csv`, `isolated/summary.csv`, `isolated/calibration.csv` and
  `isolated/run.stderr`: all isolated observations and RT checks.
- `app/run-*.csv`, corresponding stderr, `app/per-run-summary.json` and
  `app/overruns.json`: every native callback, per-run results and every overrun.
- `baseline/`: exact-source rebuild, compile commands, matching headers/object
  hashes, retained initial cache failure and successful build logs.
- `initial-source-manifest.json` and `final-source-verification.json`: unchanged
  source, original measurement records, HEAD, branch and vendor state.

Reproduce the isolated investigation using `isolated-timing.cpp` compiled with
unchanged `PostAmpEQProcessor.cpp`, C++20/-O3/-DNDEBUG and the original dyld RT
interposer. Native APP reproduction commands and header/object provenance are
in `baseline/`; `run-app-matrix.py` retains all twelve serial rotated trials.
