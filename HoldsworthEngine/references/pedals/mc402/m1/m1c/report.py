"""Generate detailed review tables from retained measurements (no manual rounding inputs)."""

import math
from evaluate import DEST, P, METHODS, read, write


def table(headers, rows):
    return (
        "\n".join(
            [
                "| " + " | ".join(headers) + " |",
                "| " + " | ".join(["---"] * len(headers)) + " |",
            ]
            + ["| " + " | ".join(str(v) for v in row) + " |" for row in rows]
        )
        + "\n"
    )


def case_row(r):
    return [
        r["sample_rate"],
        f'{r["input_hz"]:.9f}',
        r["amplitude"],
        r["gain"],
        r["tone"],
        f'{r["fundamental_dbfs"]:.6f}',
        f'{r["alias_hz"]:.9f}',
        f'{r["alias_dbfs"]:.6f}',
        f'{r["alias_dbc"]:.6f}',
        f'{r["total_alias_dbfs_sine_equivalent"]:.6f}',
    ]


CASE_HEADERS = [
    "Fs",
    "Input Hz",
    "V peak",
    "Gain",
    "Tone",
    "Fund dBFS",
    "Alias Hz",
    "Alias dBFS",
    "Alias dBc",
    "Total alias dBFS-SE",
]


def main():
    summary = read(DEST / "summary.json")
    product = [r for r in summary if r["kind"] == "product"]
    lines = [
        "# Detailed R2 M1c measurements",
        "",
        "Offline `MC402-BOUNDED-V1-PROVISIONAL-R2`. Output = 1 and Boost = 0 dB throughout these alias tables. "
        "dBFS uses one provisional volt peak as 0 dBFS; SE denotes sine-equivalent aggregate amplitude. "
        "Every PRODUCT row must pass both -70 dBc strongest alias and -80 dBFS-SE total alias. "
        "Torture counts below are separate. Exact unrounded values are retained in the linked JSON files.",
        "",
        "## Product coverage and global worst cases",
        "",
    ]
    counts = []
    globalrows = []
    for name, _, _ in METHODS:
        for sampler in ["fir", "ideal"]:
            groups = [
                r for r in product if r["method"] == name and r["resampler"] == sampler
            ]
            worst = max(
                (r["worst_relative"][0] for r in groups), key=lambda r: r["alias_dbc"]
            )
            absolute = max(
                (r["worst_absolute"][0] for r in groups),
                key=lambda r: r["total_alias_dbfs_sine_equivalent"],
            )
            counts.append(
                [
                    name,
                    sampler,
                    sum(r["count"] for r in groups),
                    sum(r["relative_failures"] for r in groups),
                    sum(r["absolute_failures"] for r in groups),
                    sum(r["either_failures"] for r in groups),
                    f'{absolute["total_alias_dbfs_sine_equivalent"]:.6f}',
                ]
            )
            globalrows.append([name, sampler] + case_row(worst))
    lines += [
        table(
            [
                "Method",
                "Resampler",
                "Cases",
                "Relative failures",
                "Absolute failures",
                "Either failure",
                "Worst total dBFS-SE",
            ],
            counts,
        ),
        table(["Method", "Resampler"] + CASE_HEADERS, globalrows),
        "Each method/resampler evaluates 80,388 main+dense coordinates plus 198 additional historical cases inside "
        "the product domain: 80,586 unique PRODUCT cases. Those 198 retain the product gate despite being stored in "
        "the historical torture files. Total: 805,860 PRODUCT evaluations across five methods and two resamplers.",
        "",
        "## Worst relative alias at each sample rate",
        "",
    ]
    for sampler in ["fir", "ideal"]:
        rows = []
        for rate in P["sample_rates"]:
            rows.append(
                [rate]
                + [
                    f'{next(r for r in product if r["sample_rate"]==rate and r["method"]==name and r["resampler"]==sampler)["worst_relative"][0]["alias_dbc"]:.6f}'
                    for name, _, _ in METHODS
                ]
            )
        lines += ["### " + sampler, "", table(["Fs"] + [r[0] for r in METHODS], rows)]
    lines += [
        "## Exact worst ADAA2 witnesses per rate",
        "",
        "Three highest-dBc cases per method/rate/resampler are retained in [summary.json](summary.json); "
        "the first is printed here. Ties differing only in Tone are retained as measured, not fabricated independent failures.",
        "",
    ]
    for name in ["adaa2_4", "adaa2_8"]:
        for sampler in ["fir", "ideal"]:
            groups = [
                r for r in product if r["method"] == name and r["resampler"] == sampler
            ]
            lines += [
                "### " + name + " / " + sampler,
                "",
                table(CASE_HEADERS, [case_row(r["worst_relative"][0]) for r in groups]),
            ]
    lines += [
        "## Musical subset: 0.25–2 V, dense 2–8 kHz requests",
        "",
        "Coherent frequencies near the 2/8 kHz requests are retained exactly. This subset illustrates musical "
        "failures; it is not a smaller replacement qualification domain.",
        "",
    ]
    for name in ["adaa2_4", "adaa2_8"]:
        for sampler in ["fir", "ideal"]:
            groups = [
                r for r in product if r["method"] == name and r["resampler"] == sampler
            ]
            lines += [
                "### " + name + " / " + sampler,
                "",
                table(CASE_HEADERS, [case_row(r["worst_musical"][0]) for r in groups]),
            ]
    previous = read(DEST / "previous-witnesses.json.gz")
    lines += [
        "## Previous 5 kHz / 0.5 V / Gain 1",
        "",
        "All six sample rates, both Tone extremes and both resamplers. '5 kHz' is the nominal request; "
        "Input Hz is the actual coherent frequency. Additional M1b worst cases and all five method comparisons "
        "are in [previous-witnesses.json.gz](previous-witnesses.json.gz).",
        "",
    ]
    for name in ["adaa2_4", "adaa2_8"]:
        for sampler in ["fir", "ideal"]:
            rows = [
                r
                for r in previous
                if r["profile"] == "r2"
                and r["method"] == name
                and r["resampler"] == sampler
                and "requested 5 kHz / 0.5 V" in r["labels"]
            ]
            lines += [
                "### " + name + " / " + sampler,
                "",
                table(CASE_HEADERS, [case_row(r) for r in rows]),
            ]
    lines += [
        "## CPU, latency and small-signal magnitude",
        "",
        "Native double-precision C++ with the existing FIR; five warmed repetitions of three seconds of audio "
        "per workload, serial after the sweep. CPU is an estimate on this arm64 Mac, not a callback deadline "
        "guarantee. 'Typical' is 997 Hz / 0.5 V / Gain 1 / Tone 1; worst uses each method's highest-dBc product "
        "case at that rate. Magnitude is maximum absolute error versus the frozen analog small-signal response "
        "over 80 Hz–8 kHz, including exact endpoints and Tone 0/.5/1.",
        "",
    ]
    performance = read(DEST / "performance.json")
    rows = []
    for r in performance["measurements"]:
        rows.append(
            [
                r["sample_rate"],
                r["method"],
                r["total_delay_host_samples"],
                f'{r["total_delay_ms"]:.9f}',
                f'{r["cpu"][0]["median_ns_per_sample"]:.3f}',
                f'{r["cpu"][1]["median_ns_per_sample"]:.3f}',
                f'{r["cpu"][1]["median_realtime_one_core_percent"]:.3f}',
                f'{max(v["max_abs_error_db"] for v in r["small_signal"]):.9f}',
            ]
        )
    lines += [
        table(
            [
                "Fs",
                "Method",
                "Delay samples",
                "Delay ms",
                "Typical ns/sample",
                "Worst ns/sample",
                "Worst % one core",
                "Max magnitude error dB",
            ],
            rows,
        ),
        "Small-signal R2 and V1 outputs agree within the retained [performance measurements](performance.json). "
        "The ideal FFT projection is an offline operation without a finite causal latency/CPU proposal. "
        "Only its nonlinear ADAA delay (1/F for ADAA1, 2/F for ADAA2) can be quoted as a realtime component.",
        "",
        "## Torture robustness",
        "",
    ]
    rows = []
    for name, _, _ in METHODS:
        for sampler in ["fir", "ideal"]:
            data = [
                r
                for rate in P["sample_rates"]
                for r in read(DEST / f"data/torture-{rate}-{name}-{sampler}.json.gz")
                if not (40 <= r["input_hz"] <= 10000 and r["amplitude"] <= 4.5)
            ]
            worst = max(data, key=lambda r: r["alias_dbc"])
            flags = sum(r["alias_dbc"] > -30 and r["alias_dbfs"] > -40 for r in data)
            rows.append(
                [
                    name,
                    sampler,
                    len(data),
                    flags,
                    f'{worst["alias_dbc"]:.6f}',
                    f'{worst["alias_dbfs"]:.6f}',
                ]
            )
    lines += [
        table(
            [
                "Method",
                "Resampler",
                "Outside-product cases",
                "Gross artifact flags",
                "Worst dBc",
                "Alias dBFS at worst dBc",
            ],
            rows,
        ),
        "Historical files contain 1,260 cases per method/resampler; 576 fall inside the product domain and "
        "684 are out of it. A gross-artifact flag retains the M1b diagnostic condition: alias > -30 dBc AND "
        "> -40 dBFS. This is triage, not a fidelity pass. All periodic spectra are finite. "
        "[robustness.json](robustness.json) separately retains 60 native 30-second ±10 V DC/near-Nyquist/"
        "multitone/burst runs, all finite and recovering to silence; no gross alias pass is inferred from stability.",
        "",
    ]
    (DEST / "results.md").write_text("\n".join(lines) + "\n")
    print("Wrote detailed result tables", flush=True)


if __name__ == "__main__":
    main()
