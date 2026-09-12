"""Worst-case window/phase checks, high-rate reference cross-check and scalars."""

import math
import numpy as np
from evaluate import (
    DEST,
    N,
    P,
    METHODS,
    Periodic,
    read,
    write,
    measure,
    bin_for,
    db,
    EXPONENT,
    A,
    HP1,
)
from analysis import reference_module


def main():
    summary = read(DEST / "summary.json")
    witnesses = []
    highrate = []
    reference = reference_module("r2")
    for rate in P["sample_rates"]:
        for name in ["adaa2_4", "adaa2_8"]:
            row = next(
                r
                for r in summary
                if r["kind"] == "product"
                and r["sample_rate"] == rate
                and r["method"] == name
                and r["resampler"] == "ideal"
            )["worst_relative"][0]
            repeats = []
            for sampler in ["fir", "ideal"]:
                for size, phase in [(N, 0.0), (4 * N, 0.0), (N, 0.173), (N, 0.731)]:
                    p = Periodic(rate, row["factor"], 2, sampler, n=size)
                    k = row["k"] * (size // N)
                    c = p.render_coefficients(
                        k, row["amplitude"], row["gain"], row["tone"], phase=phase
                    )
                    repeats.append(
                        dict(
                            resampler=sampler,
                            fft_length=size,
                            phase=phase,
                            **measure(c, rate, k, size)
                        )
                    )
                subset = [r for r in repeats if r["resampler"] == sampler]
                assert abs(subset[0]["alias_dbc"] - subset[1]["alias_dbc"]) < 1e-7
            first_peak = (
                abs(A)
                * row["amplitude"]
                * row["input_hz"]
                / math.hypot(HP1, row["input_hz"])
            )
            knee_seconds = (
                math.asin(2.75 / first_peak) - math.asin(2.25 / first_peak)
            ) / (2 * math.pi * row["input_hz"])
            witnesses.append(
                dict(
                    case=row,
                    repeats=repeats,
                    s1_knee_crossing_seconds=knee_seconds,
                    s1_knee_crossing_internal_samples=knee_seconds
                    * rate
                    * row["factor"],
                )
            )
            if name == "adaa2_8":
                exact = reference.reference(row, 128)
                runs = []
                for factor in [128, 256]:
                    p = Periodic(rate, factor, 2, "ideal")
                    c = p.render_coefficients(
                        row["k"], row["amplitude"], row["gain"], row["tone"]
                    )
                    intended = c[exact["harmonic"] * row["k"]] * np.exp(
                        2j * np.pi * exact["harmonic"] * row["k"] * (2 / factor) / N
                    )
                    runs.append(
                        dict(
                            factor=factor,
                            **measure(c, rate, row["k"]),
                            intended_harmonic_residual_dbfs_sine_equivalent=db(
                                np.linalg.norm(intended - exact["coefficients"])
                            ),
                            fundamental_error_db=db(
                                abs(intended[0] / exact["coefficients"][0])
                            )
                        )
                    )
                highrate.append(dict(case=row, runs=runs))
    write("window-phase-checks.json", witnesses)
    write("highrate-reference-checks.json", highrate)
    scalars = []
    for rate in P["sample_rates"]:
        for name, factor, mode in METHODS:
            p = Periodic(rate, factor, mode, "fir")
            k = bin_for(5000, rate)
            c = p.render_coefficients(k, 0.5, 1.0, 1.0)
            for output in [0.0, 0.1, 0.5, 1.0]:
                changed = p.render_coefficients(k, 0.5, 1.0, 1.0, output)
                residual = float(np.max(abs(changed - c * output**EXPONENT)))
                assert residual < 1e-13
                for boost in [0.0, 10.0, 20.0]:
                    boosted = changed * 10 ** (boost / 20)
                    expected = c * (output**EXPONENT * 10 ** (boost / 20))
                    error = float(np.max(abs(boosted - expected)))
                    assert error < 1e-13
                    scalars.append(
                        dict(
                            sample_rate=rate,
                            method=name,
                            output=output,
                            boost_db=boost,
                            max_coefficient_difference_volts=error,
                        )
                    )
            silent = p.render_coefficients(k, 4.5, 0.0, 1.0)
            assert np.count_nonzero(silent) == 0
    write(
        "scalar-checks.json",
        dict(
            rows=scalars,
            gain_zero_mute_cases=30,
            note="Gain zero is a provisional profile endpoint, not a hardware fact. Boost applied externally as the unchanged scalar; no production path modified.",
        ),
    )
    print(
        "window/phase witnesses",
        len(witnesses),
        "high-rate references",
        len(highrate),
        "scalar checks",
        len(scalars),
        flush=True,
    )


if __name__ == "__main__":
    main()
