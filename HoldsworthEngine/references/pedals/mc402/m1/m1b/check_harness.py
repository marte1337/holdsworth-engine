"""Verify M1b power metrics, endpoints, scalar laws and record convergence."""

import math
import numpy as np
from evaluate import DEST, P, N, Experiment, bin_for, render, measure, write


def main():
    e = Experiment()
    rate = 48000
    k = bin_for(5000, rate)
    n = np.arange(N)
    x = 0.5 * np.sin(2 * np.pi * k * n / N)
    # Known two separate alias bins and an intended third harmonic.
    y = (
        x
        + 0.01 * np.sin(2 * np.pi * (k + 2) * n / N)
        + 0.02 * np.sin(2 * np.pi * (k + 4) * n / N)
    )
    y += 0.15 * np.sin(2 * np.pi * 3 * k * n / N)
    m = measure(y, rate, k)
    assert abs(m["alias_peak"] - 0.02) < 1e-13
    assert abs(m["total_alias_rms_volts"] - math.sqrt((0.01**2 + 0.02**2) / 2)) < 1e-13
    assert abs(m["fundamental_peak"] - 0.5) < 1e-13
    rows = []
    for rate in P["sample_rates"]:
        k = bin_for(5000, rate)
        for method, factor, mode in P["methods"]:
            for tone in [0.0, 1.0]:
                base = render(e, rate, k, 0.5, factor, mode, 1.0, tone)
                errors = []
                for output in P["output_scalar_checks"]:
                    actual = render(
                        e, rate, k, 0.5, factor, mode, 1.0, tone, output=output
                    )
                    scalar = output**3.321928094887362
                    errors.append(float(np.max(np.abs(actual - scalar * base))))
                    assert errors[-1] < 1e-12
                    if output == 0:
                        assert np.count_nonzero(actual) == 0
                mute = render(e, rate, k, 0.5, factor, mode, 0.0, tone)
                assert np.count_nonzero(mute) == 0  # Named provisional profile only.
                low_gain_base = render(e, rate, k, 4.5, factor, mode, 0.25, tone)
                low_gain_error = 0.0
                for gain in [0.001, 0.01, 0.05, 0.1, 0.25]:
                    actual = render(e, rate, k, 4.5, factor, mode, gain, tone)
                    expected = low_gain_base * (gain / 0.25) ** 3.321928094887362
                    relative_error = float(
                        np.max(np.abs(actual - expected)) / np.max(np.abs(expected))
                    )
                    low_gain_error = max(low_gain_error, relative_error)
                    assert relative_error < 1e-10
                for boost in P["boost_db_scalar_checks"]:
                    out = measure(base * 10 ** (boost / 20), rate, k)
                    expected = measure(base, rate, k)
                    # FFT error dominates comparisons of very tiny alias bins.
                    assert (
                        abs(out["alias_dbfs"] - expected["alias_dbfs"] - boost) < 1e-4
                    )
                    assert abs(out["alias_dbc"] - expected["alias_dbc"]) < 1e-4
                longer = render(e, rate, k, 0.5, factor, mode, 1.0, tone, warmup=1.0)
                warmup_error = float(np.max(np.abs(base - longer)))
                assert warmup_error < 1e-12
                rows.append(
                    dict(
                        sample_rate=rate,
                        method=method,
                        tone=tone,
                        maximum_output_scalar_error=max(errors),
                        low_gain_linear_scaling_max_relative_error=low_gain_error,
                        provisional_gain_zero_mute_exact=True,
                        output_zero_mute_exact=True,
                        half_second_vs_one_second_warmup_error=warmup_error,
                    )
                )
    write("harness-checks.json", dict(known_power_case=m, controls_and_warmup=rows))
    print(
        "M1b aggregate alias metric, Output/Boost laws, provisional mute and warmup: pass"
    )


if __name__ == "__main__":
    main()
