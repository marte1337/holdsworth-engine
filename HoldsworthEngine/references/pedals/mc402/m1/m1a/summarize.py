"""Summarize recorded data without changing gates or selecting a production mode."""

import gzip
import json
import numpy as np
from experiment import DEST, RATES, N, Experiment, db, write


def load(name):
    return json.loads((DEST / name).read_text())


def main():
    ordinary = load("ordinary-cases.json")
    adaa = json.loads(gzip.decompress((DEST / "adaa-cases.json.gz").read_bytes()))
    all_rows = ordinary + adaa
    top = []
    for rate in RATES:
        for factor in [1, 2, 4, 8]:
            top.extend(
                sorted(
                    [
                        r
                        for r in ordinary
                        if r["sample_rate"] == rate and r["factor"] == factor
                    ],
                    key=lambda r: r["alias_dbc"],
                    reverse=True,
                )[:3]
            )
    lines = [
        "# Worst three observed ordinary cases per rate/factor",
        "",
        "Output is 1; Boost is off. Levels are peak-sinusoid dBFS, 0 dBFS = 1 provisional V peak.",
        "These are maxima on the recorded M1 grid, not a continuous global maximum.",
        "",
        "| Fs | Factor | Input Hz | Peak V | Gain | Tone | Output | Fundamental dBFS | Alias Hz | Alias dBFS | Alias dBc |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for r in top:
        lines.append(
            f"| {r['sample_rate']} | {r['factor']} | {r['input_hz']:.6f} | {r['amplitude']:g} | {r['gain']:g} | {r['tone']:g} | 1 | {r['fundamental_dbfs']:.6f} | {r['alias_hz']:.6f} | {r['alias_dbfs']:.6f} | {r['alias_dbc']:.6f} |"
        )
    (DEST / "worst-cases.md").write_text("\n".join(lines) + "\n")
    mandatory = []
    for rate in RATES:
        for mode in [0, 1, 2]:
            for factor in [1, 2, 4]:
                subset = [
                    r
                    for r in all_rows
                    if r["sample_rate"] == rate
                    and r["factor"] == factor
                    and r["s1"] == mode
                    and r["s2"] == mode
                ]
                mandatory.append(
                    dict(
                        sample_rate=rate,
                        mode=mode,
                        factor=factor,
                        cases=len(subset),
                        passing=sum(r["alias_dbc"] <= -70 for r in subset),
                        worst=max(subset, key=lambda r: r["alias_dbc"]),
                    )
                )
    tracked = []
    e = Experiment()
    for rate in RATES:
        case = max(
            [r for r in ordinary if r["sample_rate"] == rate and r["factor"] == 1],
            key=lambda r: r["alias_dbc"],
        )
        k = case["k"]
        index = round(case["alias_hz"] * N / rate)
        y = e.sine(
            rate,
            k,
            case["amplitude"],
            gain=case["gain"],
            tone=case["tone"],
            tone_on=False,
        )
        s = np.abs(np.fft.rfft(y)) * 2 / N
        harmonic = next(
            h
            for h in range(3, 100000, 2)
            if abs((h * k + N // 2) % N - N // 2) == index
        )
        tracked.append(
            dict(
                case=case,
                lowest_matching_odd_folded_harmonic=harmonic,
                tone_bypassed_same_alias_dbfs=db(s[index]),
                tone_bypassed_fundamental_dbfs=db(s[k]),
                tone_fundamental_attenuation_db=case["fundamental_dbfs"] - db(s[k]),
                tone_tracked_alias_attenuation_db=case["alias_dbfs"] - db(s[index]),
            )
        )
    summary = dict(
        profile="MC402-BOUNDED-V1-PROVISIONAL",
        recommendation="D",
        production_solution_selected=False,
        alias_gate_dbc=-70,
        reference_margin_dbc=-90,
        gate_passed=False,
        ordinary_case_count=len(ordinary),
        adaa_case_count=len(adaa),
        mandatory_matrix=mandatory,
        positive_dbc_diagnosis=tracked,
        recommendation_scope="No tested low-order ADAA/modest-FIR strategy meets the full unchanged envelope. Return the frozen profile and numerical requirements to review; this is not a proof that every exact-transfer antialias algorithm is impossible.",
    )
    write("summary.json", summary)
    print("Summary written. Recommendation D; no gate or production profile changed.")


if __name__ == "__main__":
    main()
