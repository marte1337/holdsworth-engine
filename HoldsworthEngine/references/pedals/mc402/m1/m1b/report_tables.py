"""Refresh measured tables in the reviewed narrative after reproduction."""

import re
from evaluate import DEST, P, read


def replace_section(text, name, lines):
    body = f"<!-- BEGIN {name} -->\n" + "\n".join(lines) + f"\n<!-- END {name} -->"
    placeholder = f"<!-- {name} -->"
    if placeholder in text:
        return text.replace(placeholder, body)
    pattern = f"<!-- BEGIN {name} -->.*?<!-- END {name} -->"
    assert re.search(pattern, text, re.S), name
    return re.sub(pattern, lambda _: body, text, flags=re.S)


def main():
    summary = read("summary.json")
    groups = [r for r in summary["regions"] if r["region"] == "PRODUCT"]
    lines = [
        f"**{summary['product_cases']:,} distinct PRODUCT renders** across all six rates and three methods, including the dense follow-up.",
        "",
        "| Host Hz | Method | Worst alias dBc | Strongest alias dBFS | Maximum total alias dBFS | Relative failures / cases |",
        "| ---: | --- | ---: | ---: | ---: | ---: |",
    ]
    for g in groups:
        lines.append(
            f"| {g['sample_rate']} | {g['method']} | {g['worst_relative']['alias_dbc']:.3f} | {g['worst_absolute']['alias_dbfs']:.3f} | {g['worst_total']['total_alias_dbfs_sine_equivalent']:.3f} | {g['relative_failures']} / {g['count']} |"
        )
    lines += [
        "",
        """The three maxima in each row may occur at different settings. Complete coordinates,
fundamental/alias levels and aggregate energy are in [worst-cases.md](worst-cases.md)
and [summary.json](summary.json). Every rate/method fails the original relative
gate. The absolute/aggregate gate also fails; neither is relaxed.""",
        "",
        "Global relative winners for the two ADAA candidates:",
        "",
        "| Method | Host Hz | Input Hz | V peak | Gain | Tone | Fundamental dBFS | Alias Hz | Alias dBFS | dBc |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for method in ["adaa2_4", "adaa2_8"]:
        r = max(
            (g["worst_relative"] for g in groups if g["method"] == method),
            key=lambda r: r["alias_dbc"],
        )
        lines.append(
            f"| {method} | {r['sample_rate']} | {r['input_hz']:.9f} | {r['amplitude']:g} | {r['gain']:g} | {r['tone']:g} | {r['fundamental_dbfs']:.6f} | {r['alias_hz']:.9f} | {r['alias_dbfs']:.6f} | {r['alias_dbc']:.6f} |"
        )
    musical = [
        g for g in summary["regions"] if g["region"] == "MUSICAL 0.25–2 V / 2–8 kHz"
    ]
    lines += [
        "",
        "Restricting the peak to 2 V and frequency to 2–8 kHz still gives:",
        "",
        "| Host Hz | Ordinary 4x worst dBc | ADAA2 4x worst dBc | ADAA2 8x worst dBc |",
        "| ---: | ---: | ---: | ---: |",
    ]
    for rate in P["sample_rates"]:
        values = [
            next(
                g for g in musical if g["sample_rate"] == rate and g["method"] == name
            )["worst_relative"]["alias_dbc"]
            for name, _, _ in P["methods"]
        ]
        lines.append(f"| {rate} | " + " | ".join(f"{v:.3f}" for v in values) + " |")
    refs = read("continuous-reference-checks.json")
    witnesses = read("witness-checks.json")
    record_error = max(
        abs(r["checks"][0]["alias_dbc"] - r["checks"][3]["alias_dbc"])
        for r in witnesses
    )
    phase_spread = max(
        max(c["alias_dbc"] for c in r["checks"])
        - min(c["alias_dbc"] for c in r["checks"])
        for r in witnesses
    )
    lines += [
        "",
        f"The independent continuous-time reference is checked on **{len(refs)} selected cases**, including relative/absolute winners and the 1 kHz/5 kHz examples. Doubling quadrature order gives a worst coefficient difference of **{max(r['coefficient_convergence_db'] for r in refs):.2f} dB** relative to reference energy; this is a numerical convergence check, not physical accuracy. The 18 musical winners are checked at 8192/32768 samples and three phases. Maximum same-phase record-length change is **{record_error:.3g} dB**, and maximum phase/length spread is **{phase_spread:.3g} dB**. No detector-error explanation rescues the failures.",
        "",
        "![Measured musical subset](product-aliasing.png)",
    ]
    text = (DEST / "README.md").read_text()
    text = replace_section(text, "RESULTS", lines)
    five = [r for r in read("five-khz-neighborhood.json") if r["original_5khz_bin"]]
    lines = [
        "All rows: 0.5 V peak, Gain 1, Output 1. Each cell is **alias dBc / alias dBFS**.",
        "",
        "| Host Hz | Actual input Hz | Tone | Ordinary 4x | ADAA2 4x | ADAA2 8x |",
        "| ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for rate in P["sample_rates"]:
        for tone in [0.0, 1.0]:
            r = [
                next(
                    r
                    for r in five
                    if r["sample_rate"] == rate
                    and r["tone"] == tone
                    and r["method"] == name
                )
                for name, _, _ in P["methods"]
            ]
            lines.append(
                f"| {rate} | {r[0]['input_hz']:.6f} | {tone:g} | "
                + " | ".join(f"{c['alias_dbc']:.3f} / {c['alias_dbfs']:.3f}" for c in r)
                + " |"
            )
    lines += [
        "",
        """[five-khz-neighborhood.json](five-khz-neighborhood.json) retains all 324 renders,
including four neighboring odd bins on each side. The 48 kHz / bright Tone
ADAA2 4x result reproduces M1a's **-68.523475 dBc / -59.910368 dBFS**;
8x substantially improves that exact case. A single frequency/rate pair does
not establish qualification of its neighborhood or the wider product grid.""",
    ]
    text = replace_section(text, "FIVE_K", lines)
    lines = [
        "| Host Hz | Method | Added host samples | ms | Maximum magnitude error dB | Typical ns/sample | Worst-alias workload ns/sample | Typical / worst CPU % |",
        "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for r in read("performance.json"):
        lines.append(
            f"| {r['sample_rate']} | {r['method']} | {r['total_linear_delay_host_samples']:g} | {r['total_delay_ms']:.6f} | {max(t['max_error_db'] for t in r['small_signal']):.6f} | {r['median_nanoseconds_per_host_sample']:.1f} | {r['worst_alias_workload_ns_median']:.1f} | {r['audio_budget_percent']:.3f} / {r['worst_alias_workload_audio_budget_percent']:.3f} |"
        )
    lines += [
        "",
        """Magnitude checks cover 80 Hz–8 kHz at Tone 0/0.5/1, including exact 80 Hz and
8 kHz endpoints, against the continuous analog linear reduction. Full measurements
and CPU repetitions are in [performance.json](performance.json).""",
    ]
    text = replace_section(text, "PERFORMANCE", lines)
    torture = [
        r for r in summary["torture_and_tail_diagnostics"] if r["kind"] == "torture"
    ]
    lines = [
        "Spectral torture diagnostics (not PRODUCT fidelity passes):",
        "",
        "| Host Hz | Method | Worst alias dBc | That alias dBFS | Gross-artifact flags / cases |",
        "| ---: | --- | ---: | ---: | ---: |",
    ]
    for r in torture:
        w = r["worst_relative"]
        lines.append(
            f"| {r['sample_rate']} | {r['method']} | {w['alias_dbc']:.3f} | {w['alias_dbfs']:.3f} | {r['gross_alias_review_flags']} / {r['count']} |"
        )
    tails = [r for r in summary["torture_and_tail_diagnostics"] if r["kind"] == "tails"]
    lines += [
        "",
        f"There are **{sum(r['count'] for r in torture):,} historical/torture spectral renders** and **{sum(r['count'] for r in tails):,} high-frequency tail renders**. All are finite. Their full coordinates and absolute/aggregate levels remain in the compressed matrices and summary. Outside PRODUCT, fidelity booleans are informational and do not decide robustness; overlapping in-domain points retain PRODUCT criteria.",
    ]
    text = replace_section(text, "TORTURE", lines)
    (DEST / "README.md").write_text(text)


if __name__ == "__main__":
    main()
