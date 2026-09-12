"""Control-region isolation and independent linear-reference checks."""

import argparse
import numpy as np
from experiment import Experiment, N, coherent, metrics, write
from continuous_reference import reference
from validate_numerics import analog_response


def regions():
    e = Experiment()
    rows = []
    for rate in [44100, 48000]:
        for requested in [1000, 5000, 19000]:
            k = coherent(requested, rate)
            for amplitude in [0.02, 0.12, 0.5, 10.0]:
                for gain in [0.1, 0.5, 1.0]:
                    for tone in [0.0, 1.0]:
                        rendered = {}
                        for stages in [1, 2, 3]:
                            y = e.sine(
                                rate,
                                k,
                                amplitude,
                                4,
                                gain=gain,
                                tone=tone,
                                stages=stages,
                            )
                            rendered[stages] = y
                            rows.append(
                                dict(
                                    sample_rate=rate,
                                    input_hz=k * rate / N,
                                    amplitude=amplitude,
                                    gain=gain,
                                    tone=tone,
                                    output=1.0,
                                    factor=4,
                                    stages=stages,
                                    **metrics(y, rate, k)
                                )
                            )
                        if gain == 0.1:
                            assert np.array_equal(rendered[1], rendered[3])
                        if amplitude == 0.02:
                            assert np.array_equal(rendered[2], rendered[3])
    write("stage-control-regions.json", rows)
    print("Control-region checks pass:", len(rows), "renders")


def linear_reference():
    rows = []
    for frequency in [80.0, 1000.0, 5000.0, 19000.0]:
        for tone in [0.0, 0.5, 1.0]:
            case = dict(
                input_hz=frequency, amplitude=1e-5, gain=0.5, tone=tone, output=1.0
            )
            r = reference(case, 256)
            expected = (
                -1j * case["amplitude"] * analog_response(frequency, 0.5, tone, 1.0)
            )
            error = abs(r["coefficients"][0] - expected) / abs(expected)
            other = float(np.max(np.abs(r["coefficients"][1:]), initial=0.0))
            assert error < 1e-12 and other < 1e-13, (case, error, other)
            rows.append(
                dict(
                    case=case,
                    fundamental_relative_error=float(error),
                    largest_unexpected_harmonic=other,
                    periodic_state_error=r["periodic_state_error"],
                )
            )
    write("continuous-reference-sanity.json", rows)
    print(
        "Continuous reference matches independent linear transfer:", len(rows), "cases"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["regions", "reference"])
    args = parser.parse_args()
    {"regions": regions, "reference": linear_reference}[args.command]()
