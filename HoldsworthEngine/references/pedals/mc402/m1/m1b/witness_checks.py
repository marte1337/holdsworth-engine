"""Record-length/phase checks and detuning around the musical 5 kHz case."""

import math
import numpy as np
from evaluate import P, N, Experiment, all_rows, bin_for, render, measure, write


def main():
    e = Experiment()
    candidates = all_rows("product") + all_rows("dense")
    witnesses = []
    for rate in P["sample_rates"]:
        for name, factor, mode in P["methods"]:
            group = [
                r
                for r in candidates
                if r["sample_rate"] == rate
                and r["method"] == name
                and 2000 <= r["input_hz"] <= 8000
                and 0.25 <= r["amplitude"] <= 2.0
            ]
            worst = max(group, key=lambda r: r["alias_dbc"])
            comparisons = []
            for n, k in [(N, worst["k"]), (4 * N, 4 * worst["k"])]:
                for phase in [0.0, 0.37, 1.11]:
                    x = worst["amplitude"] * np.sin(
                        np.arange(n) * (2 * np.pi * k / n) + phase
                    )
                    y = e.render(
                        x,
                        rate,
                        factor,
                        gain=worst["gain"],
                        tone=worst["tone"],
                        s1=mode,
                        s2=mode,
                        warmup=math.ceil(0.5 * rate / n),
                    )
                    comparisons.append(
                        dict(fft_length=n, phase=phase, **measure(y, rate, k))
                    )
            # Same physical tone, 4x record length: no change of frequency/bin mask.
            assert (
                abs(comparisons[0]["alias_dbc"] - comparisons[3]["alias_dbc"]) < 0.001
            )
            assert (
                abs(
                    comparisons[0]["total_alias_dbfs_sine_equivalent"]
                    - comparisons[3]["total_alias_dbfs_sine_equivalent"]
                )
                < 0.001
            )
            witnesses.append(dict(case=worst, checks=comparisons))
    five_k = []
    for rate in P["sample_rates"]:
        original = bin_for(5000, rate)
        for name, factor, mode in P["methods"]:
            for tone in [0.0, 1.0]:
                for k in range(original - 8, original + 9, 2):
                    y = render(e, rate, k, 0.5, factor, mode, 1.0, tone)
                    five_k.append(
                        dict(
                            sample_rate=rate,
                            method=name,
                            factor=factor,
                            input_hz=k * rate / N,
                            k=k,
                            amplitude=0.5,
                            gain=1.0,
                            tone=tone,
                            output=1.0,
                            original_5khz_bin=k == original,
                            **measure(y, rate, k)
                        )
                    )
    write("witness-checks.json", witnesses)
    write("five-khz-neighborhood.json", five_k)
    print("Record length, phase sensitivity and 5 kHz neighborhood checked", flush=True)


if __name__ == "__main__":
    main()
