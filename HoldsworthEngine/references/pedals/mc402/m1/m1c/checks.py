"""Independent numeric/kernel and periodic-renderer checks for offline R2."""

import argparse
import ctypes as ct
from decimal import Decimal, localcontext
import itertools
import math

import numpy as np
from scipy.integrate import quad

from evaluate import (
    DEST,
    Experiment,
    Periodic,
    P,
    N,
    METHODS,
    library,
    bin_for,
    measure,
    write,
)


def saturation(x):
    q = np.minimum(np.abs(x) / 2.5, 1.1)
    t = np.maximum((q - 0.9) / 0.2, 0.0)
    y = np.where(q <= 0.9, q, 0.9 + 0.2 * t - 0.2 * t**3 + 0.1 * t**4)
    return np.copysign(2.5 * y, x)


def decimal_primitive(x, order):
    a, b, w = Decimal(".9"), Decimal("1.1"), Decimal(".2")
    q = abs(x)
    d = max(q - a, Decimal(0))
    if order == 1:
        return (
            Decimal(".599") + q - b
            if q >= b
            else q * q / 2 - d**4 / (4 * w * w) + d**5 / (10 * w**3)
        )
    y = (
        Decimal(6647) / 30000 + Decimal(".599") * (q - b) + (q - b) ** 2 / 2
        if q >= b
        else q**3 / 6 - d**5 / (20 * w * w) + d**6 / (60 * w**3)
    )
    return -y if x < 0 else y


def numerics():
    e = Experiment(library())
    e.lib.m1c_derivative.argtypes = [ct.c_double, ct.c_int]
    e.lib.m1c_derivative.restype = ct.c_double
    rng = np.random.default_rng(402003)
    triples = [tuple(rng.uniform(-20, 20, 3)) for _ in range(1000)]
    for center in [-1.1, -1.0, -0.9, 0.0, 0.9, 1.0, 1.1]:
        for epsilon in [0.0, 1e-15, 1e-12, 1e-8, 1e-4, 0.01, 0.1]:
            triples.extend(
                itertools.permutations([center - epsilon, center, center + epsilon])
            )
    errors = []

    # Independent adaptive integration of shape times the normalized B-spline.
    def avg(lo, hi, wlo, whi):
        if lo == hi:
            return float(saturation(lo * 2.5) / 2.5) * (wlo + whi) / 2
        cuts = [lo] + [v for v in [-1.1, -0.9, 0.9, 1.1] if lo < v < hi] + [hi]
        result = 0.0
        for left, right in zip(cuts[:-1], cuts[1:]):
            result += (
                quad(
                    lambda t: float(saturation((left + (right - left) * t) * 2.5) / 2.5)
                    * (
                        wlo
                        + (whi - wlo) * ((left - lo) + (right - left) * t) / (hi - lo)
                    ),
                    0.0,
                    1.0,
                    epsabs=1e-13,
                    epsrel=1e-13,
                )[0]
                * (right - left)
                / (hi - lo)
            )
        return result

    for triple in triples:
        x, y, z = sorted(triple)
        expected1 = avg(min(triple[:2]), max(triple[:2]), 1.0, 1.0)
        expected2 = (
            float(saturation(x * 2.5) / 2.5)
            if x == z
            else 2
            * (
                (y - x) / (z - x) * avg(x, y, 0.0, 1.0)
                + (z - y) / (z - x) * avg(y, z, 1.0, 0.0)
            )
        )
        errors.extend(
            [
                abs(e.lib.m1a_adaa(*triple, 1) - expected1),
                abs(e.lib.m1a_adaa(*triple, 2) - expected2),
            ]
        )
    assert max(errors) < 4e-14, max(errors)
    decimal_errors = []
    with localcontext() as ctx:
        ctx.prec = 90
        for triple in triples:
            x, y, z = [Decimal(str(v)) for v in sorted(triple)]
            if min(y - x, z - y) > Decimal("1e-25"):
                second = (
                    2
                    * (
                        (decimal_primitive(z, 2) - decimal_primitive(y, 2)) / (z - y)
                        - (decimal_primitive(y, 2) - decimal_primitive(x, 2)) / (y - x)
                    )
                    / (z - x)
                )
                decimal_errors.append(abs(e.lib.m1a_adaa(*triple, 2) - float(second)))
    assert max(decimal_errors) < 4e-14, max(decimal_errors)
    # Endpoint Hermite conditions from either side, oddness and bounded slope.
    x = np.linspace(-2, 2, 40001)
    native = np.array([e.lib.m1a_shape(float(v), 0) for v in x])
    assert np.max(abs(native - saturation(x * 2.5) / 2.5)) < 2e-15
    assert np.max(abs(native + native[::-1])) < 2e-15
    assert np.max(abs(native)) <= 1.0
    derivative = np.array([e.lib.m1c_derivative(float(v), 1) for v in x])
    assert derivative.min() >= -1e-15 and derivative.max() <= 1 + 1e-15
    for boundary, target in [(0.9, (0.9, 1.0, 0.0)), (1.1, (1.0, 0.0, 0.0))]:
        for order, value in enumerate(target):
            for q in [np.nextafter(boundary, 0), boundary, np.nextafter(boundary, 2)]:
                assert abs(e.lib.m1c_derivative(q, order) - value) < 2e-13
    huge = [
        (-1e150, 0.0, 1e150),
        (1e150, 1e150, 1e150),
        (0.0, 1e-310, -1e-310),
        (0.9, 0.9, 0.9),
        (1.1, 1.1, 1.1),
    ]
    for triple in huge:
        for mode in [1, 2]:
            result = e.lib.m1a_adaa(*triple, mode)
            assert math.isfinite(result) and abs(result) <= 1 + 1e-14
    data = dict(
        triples=len(triples),
        quadrature_max_abs_error=max(errors),
        decimal_cases=len(decimal_errors),
        decimal_max_abs_error=max(decimal_errors),
        static_points=x.size,
        huge_and_repeated_cases=len(huge) * 2,
    )
    write("kernel-checks.json", data)
    print(data, flush=True)


def periodic():
    rows = []
    for profile in ["v1", "r2"]:
        e = Experiment(library(profile))
        for rate in P["sample_rates"]:
            for name, factor, mode in METHODS:
                p = Periodic(rate, factor, mode, "fir", profile)
                for f, amplitude, gain, tone in [
                    (1000, 0.005, 0.5, 0.5),
                    (5000, 0.5, 1.0, 1.0),
                    (7250, 2.0, 0.6, 0.0),
                    (19000, 10.0, 1.0, 1.0),
                ]:
                    k = bin_for(f, rate)
                    x = amplitude * np.sin(2 * np.pi * k * np.arange(N) / N)
                    fast = p.render_input(x, gain, tone)
                    native = e.render(
                        x,
                        rate,
                        factor,
                        gain=gain,
                        tone=tone,
                        s1=mode,
                        s2=mode,
                        warmup=math.ceil(rate / N),
                    )
                    error = float(np.max(abs(fast - native)))
                    assert error < 2e-10, (profile, rate, name, f, error)
                    rows.append(
                        dict(
                            profile=profile,
                            sample_rate=rate,
                            method=name,
                            input_hz=k * rate / N,
                            amplitude=amplitude,
                            gain=gain,
                            tone=tone,
                            max_sample_error=error,
                        )
                    )
                # Arbitrary periodic multitone, not just a sine-specialized check.
                x = 0.5 * np.sin(2 * np.pi * 137 * np.arange(N) / N) + 0.3 * np.cos(
                    2 * np.pi * 997 * np.arange(N) / N
                )
                fast = p.render_input(x, 0.75, 0.5)
                native = e.render(
                    x,
                    rate,
                    factor,
                    gain=0.75,
                    tone=0.5,
                    s1=mode,
                    s2=mode,
                    warmup=math.ceil(rate / N),
                )
                error = float(np.max(abs(fast - native)))
                assert error < 2e-10, (profile, rate, name, "multitone", error)
                rows.append(
                    dict(
                        profile=profile,
                        sample_rate=rate,
                        method=name,
                        signal="periodic multitone",
                        max_sample_error=error,
                    )
                )
    # Ideal interpolation/projection independently compared to a native high-rate run.
    for rate in [44100, 96000, 192000]:
        for name, factor, mode in METHODS:
            p = Periodic(rate, factor, mode, "ideal")
            k = bin_for(4875, rate)
            x = 0.5 * np.sin(2 * np.pi * k * np.arange(N * factor) / (N * factor))
            e = Experiment(library())
            y = e.render(
                x,
                rate * factor,
                1,
                gain=0.6,
                tone=0.0,
                s1=mode,
                s2=mode,
                warmup=math.ceil(rate / N),
            )
            expected = np.fft.rfft(y) * 2 / y.size
            c = p.render_coefficients(k, 0.5, 0.6, 0.0)
            error = float(np.max(abs(c - expected[: N // 2 + 1])))
            assert error < 2e-10, error
            rows.append(
                dict(
                    profile="r2",
                    sample_rate=rate,
                    method=name,
                    resampler="ideal",
                    max_coefficient_error=error,
                )
            )
    write("periodic-checks.json", rows)
    print(
        "periodic checks",
        len(rows),
        "max",
        max(r.get("max_sample_error", 0) for r in rows),
        flush=True,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["numerics", "periodic"])
    args = parser.parse_args()
    numerics() if args.action == "numerics" else periodic()
