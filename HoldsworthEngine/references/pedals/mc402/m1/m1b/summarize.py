"""Build review tables without converting diagnostic success into DSP approval."""

from evaluate import P, all_rows, read, write, DEST


def group_summary(rows):
    return dict(
        count=len(rows),
        relative_failures=sum(not r["relative_pass"] for r in rows),
        absolute_failures=sum(not r["absolute_pass"] for r in rows),
        both_criteria_passes=sum(
            r["relative_pass"] and r["absolute_pass"] for r in rows
        ),
        worst_relative=max(rows, key=lambda r: r["alias_dbc"]),
        worst_absolute=max(rows, key=lambda r: r["alias_dbfs"]),
        worst_total=max(rows, key=lambda r: r["total_alias_dbfs_sine_equivalent"]),
    )


def main():
    product = all_rows("product") + all_rows("dense")
    tables = []
    appendix = [
        "# M1b exact worst measured cases",
        "",
        "All cases: Output 1, Boost off. V means provisional pedal-port volts peak.",
        "dBFS uses 1 V peak per sine; total uses sine-equivalent aggregate power.",
        "Every reported winner is from a finite grid, not a proven continuum maximum.",
        "",
    ]
    for label, subset in [
        ("PRODUCT", product),
        (
            "MUSICAL 0.25–2 V / 2–8 kHz",
            [
                r
                for r in product
                if 0.25 <= r["amplitude"] <= 2 and 2000 <= r["input_hz"] <= 8000
            ],
        ),
        (
            "HIGH GAIN musical subset",
            [
                r
                for r in product
                if 0.25 <= r["amplitude"] <= 2
                and 2000 <= r["input_hz"] <= 8000
                and r["gain"] >= 0.75
            ],
        ),
        (
            "UP TO 0.5 V / 2–8 kHz",
            [
                r
                for r in product
                if r["amplitude"] <= 0.5 and 2000 <= r["input_hz"] <= 8000
            ],
        ),
    ]:
        appendix += [
            f"## {label}",
            "",
            "| Rate | Method | Input Hz | V peak | Gain | Tone | Fundamental dBFS | Alias Hz | Alias dBFS | Alias dBc | Total alias dBFS |",
            "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
        for rate in P["sample_rates"]:
            for method, _, _ in P["methods"]:
                group = [
                    r
                    for r in subset
                    if r["sample_rate"] == rate and r["method"] == method
                ]
                s = group_summary(group)
                tables.append(dict(region=label, sample_rate=rate, method=method, **s))
                r = s["worst_relative"]
                appendix.append(
                    f"| {rate} | {method} | {r['input_hz']:.9f} | {r['amplitude']:g} | {r['gain']:g} | {r['tone']:g} | {r['fundamental_dbfs']:.6f} | {r['alias_hz']:.9f} | {r['alias_dbfs']:.6f} | {r['alias_dbc']:.6f} | {r['total_alias_dbfs_sine_equivalent']:.6f} |"
                )
        appendix.append("")
    # Other metrics have separate winners; do not combine unrelated components.
    appendix += [
        "## Absolute and aggregate PRODUCT winners",
        "",
        "| Rate | Method | Metric | Input Hz | V peak | Gain | Tone | Fundamental dBFS | Alias Hz | Alias dBFS | Alias dBc | Total alias dBFS |",
        "| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for t in tables:
        if t["region"] != "PRODUCT":
            continue
        for key in ["worst_absolute", "worst_total"]:
            r = t[key]
            appendix.append(
                f"| {t['sample_rate']} | {t['method']} | {key} | {r['input_hz']:.9f} | {r['amplitude']:g} | {r['gain']:g} | {r['tone']:g} | {r['fundamental_dbfs']:.6f} | {r['alias_hz']:.9f} | {r['alias_dbfs']:.6f} | {r['alias_dbc']:.6f} | {r['total_alias_dbfs_sine_equivalent']:.6f} |"
            )
    stress = []
    for kind in ["torture", "tails"]:
        rows = all_rows(kind)
        for rate in P["sample_rates"]:
            for method, _, _ in P["methods"]:
                group = [
                    r
                    for r in rows
                    if r["sample_rate"] == rate and r["method"] == method
                ]
                stress.append(
                    dict(
                        kind=kind,
                        sample_rate=rate,
                        method=method,
                        **group_summary(group),
                        gross_alias_review_flags=sum(
                            r["alias_dbc"] > -30 and r["alias_dbfs"] > -40
                            for r in group
                        ),
                    )
                )
    assert all(t["relative_failures"] > 0 for t in tables if t["region"] == "PRODUCT")
    # Dataset names do not waive fidelity for points inside PRODUCT.
    overlap = [
        r
        for r in all_rows("torture")
        if P["input_frequency_hz"][0] <= r["input_hz"] <= P["input_frequency_hz"][1]
        and r["amplitude"] <= P["input_peak_volts_max"]
    ]

    def case_key(r):
        return tuple(
            r[k] for k in ["sample_rate", "method", "k", "amplitude", "gain", "tone"]
        )

    main_keys = {case_key(r) for r in product}
    additional = [r for r in overlap if case_key(r) not in main_keys]
    overlap_summaries = []
    for method, _, _ in P["methods"]:
        additional_method = [r for r in additional if r["method"] == method]
        main_method = [r for r in product if r["method"] == method]
        for metric in ["alias_dbc", "alias_dbfs", "total_alias_dbfs_sine_equivalent"]:
            assert max(r[metric] for r in additional_method) <= max(
                r[metric] for r in main_method
            )
        overlap_summaries.append(
            dict(method=method, **group_summary(additional_method))
        )
    write(
        "summary.json",
        dict(
            decision="C",
            product_qualified=False,
            original_relative_criterion_alone_qualified=False,
            product_cases=len(product),
            regions=tables,
            torture_and_tail_diagnostics=stress,
            historical_grid_product_overlap=dict(
                count=len(overlap),
                additional_unique_cases=len(additional),
                additional_cases=overlap_summaries,
                changes_global_method_maxima=False,
            ),
            note="Finite-grid failures establish nonqualification. No production solution implemented. Outside PRODUCT, fidelity booleans are informational and do not decide robustness. Historical-grid points inside PRODUCT retain PRODUCT criteria.",
        ),
    )
    (DEST / "worst-cases.md").write_text("\n".join(appendix) + "\n")
    print("PRODUCT: FAIL under original -70 dBc alone. Review recommendation C.")


if __name__ == "__main__":
    main()
