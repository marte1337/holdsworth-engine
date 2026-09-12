"""M1a offline orchestration. Frozen production files are read, never modified."""

import argparse
import ctypes as ct
import json
import math
import os
from pathlib import Path
import sys
import time
import numpy as np
from scipy import signal, integrate

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from validate_numerics import (
    Bridge,
    RATES,
    N,
    FREQUENCIES,
    AMPLITUDES,
    GAINS,
    TONES,
    coherent,
    db,
)

PTR = ct.POINTER(ct.c_double)
DEST = Path(__file__).resolve().parent


class Experiment:
    def __init__(self, library=None):
        self.lib = ct.CDLL(
            library
            or os.environ.get("MC402_M1A_LIBRARY", "/tmp/mc402-m1a/experiment.dylib")
        )
        self.lib.m1a_create.argtypes = (
            [ct.c_double, ct.c_uint] + [ct.c_double] * 3 + [ct.c_int] * 5
        )
        self.lib.m1a_create.restype = ct.c_void_p
        self.lib.m1a_destroy.argtypes = [ct.c_void_p]
        self.lib.m1a_process.argtypes = [ct.c_void_p, PTR, PTR, ct.c_size_t]
        self.lib.m1a_periodic.argtypes = [ct.c_void_p, PTR, PTR, ct.c_size_t, ct.c_uint]
        self.lib.m1a_shape.argtypes = [ct.c_double, ct.c_int]
        self.lib.m1a_shape.restype = ct.c_double
        self.lib.m1a_adaa.argtypes = [ct.c_double] * 3 + [ct.c_int]
        self.lib.m1a_adaa.restype = ct.c_double
        self.lib.m1a_benchmark.argtypes = [
            ct.c_double,
            ct.c_uint,
            ct.c_int,
            ct.c_int,
            ct.c_size_t,
            PTR,
        ]
        self.lib.m1a_benchmark.restype = ct.c_double
        self.lib.m1a_state_size.restype = ct.c_size_t

    def render(
        self,
        x,
        rate,
        factor=1,
        gain=1.0,
        tone=1.0,
        output=1.0,
        s1=0,
        s2=0,
        stages=3,
        tone_on=True,
        wire=False,
        warmup=0,
    ):
        x = np.ascontiguousarray(x, dtype=np.float64)
        y = np.empty_like(x)
        handle = self.lib.m1a_create(
            rate, factor, gain, tone, output, s1, s2, stages, tone_on, wire
        )
        if not handle:
            raise ValueError("Invalid offline experiment configuration")
        try:
            self.lib.m1a_periodic(
                handle, x.ctypes.data_as(PTR), y.ctypes.data_as(PTR), x.size, warmup
            )
        finally:
            self.lib.m1a_destroy(handle)
        assert np.all(np.isfinite(y))
        return y

    def sine(self, rate, k, amplitude, factor=1, **kwargs):
        x = amplitude * np.sin(np.arange(N) * (2 * np.pi * k / N))
        return self.render(x, rate, factor, warmup=math.ceil(0.5 * rate / N), **kwargs)


def metrics(y, rate, k, n_host=N):
    # The same physical bin spacing is used for high-rate references.
    spectrum = np.fft.rfft(y) * 2 / y.size
    bins = np.arange(spectrum.size)
    hz = bins * rate / n_host
    mask = (hz >= 20) & (hz <= 20000) & (bins % k != 0)
    magnitude = np.abs(spectrum)
    index = int(np.argmax(np.where(mask, magnitude, 0.0)))
    fundamental = magnitude[k]
    alias = magnitude[index]
    return dict(
        fundamental_peak=float(fundamental),
        fundamental_dbfs=db(fundamental),
        alias_hz=float(hz[index]),
        alias_peak=float(alias),
        alias_dbfs=db(alias),
        alias_dbc=db(alias / max(fundamental, 1e-300)),
    )


def write(name, data):
    (DEST / name).write_text(json.dumps(data, indent=2) + "\n")


def scan(e):
    rows = []
    for rate in RATES:
        for factor in [1, 2, 4, 8]:
            for requested in FREQUENCIES:
                k = coherent(requested, rate)
                for amplitude in AMPLITUDES:
                    for gain in GAINS:
                        for tone in TONES:
                            y = e.sine(rate, k, amplitude, factor, gain=gain, tone=tone)
                            rows.append(
                                dict(
                                    sample_rate=rate,
                                    factor=factor,
                                    k=k,
                                    input_hz=k * rate / N,
                                    amplitude=amplitude,
                                    gain=gain,
                                    tone=tone,
                                    output=1.0,
                                    s1=0,
                                    s2=0,
                                    **metrics(y, rate, k)
                                )
                            )
            worst = max(
                (r for r in rows if r["sample_rate"] == rate and r["factor"] == factor),
                key=lambda r: r["alias_dbc"],
            )
            print("ordinary", rate, factor, round(worst["alias_dbc"], 3), flush=True)
    write("ordinary-cases.json", rows)
    return rows


def sanity(e):
    rows = []
    rate = 48000
    k = coherent(997, rate)
    x = 0.5 * np.sin(np.arange(N) * (2 * np.pi * k / N))
    for label, y in [
        ("identity", x),
        ("boost20", x * 10),
        ("cubic_no_fold", x + 0.2 * x**3),
        (
            "linear_filters",
            e.render(x, rate, stages=0, warmup=math.ceil(0.5 * rate / N)),
        ),
    ]:
        m = metrics(y, rate, k)
        assert m["alias_dbc"] < -200, (label, m)
        rows.append(dict(case=label, sample_rate=rate, input_hz=k * rate / N, **m))
    high = coherent(19000, rate)
    x = 0.5 * np.sin(np.arange(N) * (2 * np.pi * high / N))
    m = metrics(x + 0.2 * x**3, rate, high)
    folded = abs(((3 * high + N // 2) % N) - N // 2) * rate / N
    assert abs(m["alias_hz"] - folded) < 1e-9
    assert abs(m["alias_peak"] - 0.00625) < 1e-13
    assert abs(m["fundamental_peak"] - 0.51875) < 1e-13
    rows.append(
        dict(
            case="cubic_known_fold",
            sample_rate=rate,
            input_hz=high * rate / N,
            expected_alias_peak=0.00625,
            expected_alias_hz=folded,
            **m
        )
    )
    k = coherent(80, rate)
    x = 2.75 * np.sin(np.arange(N) * (2 * np.pi * k / N))
    y = np.array([2.5 * e.lib.m1a_shape(float(v / 2.5), 0) for v in x])
    m = metrics(y, rate, k)
    assert m["alias_dbc"] < -90, m
    rows.append(
        dict(
            case="frozen_saturator_low_frequency",
            sample_rate=rate,
            input_hz=k * rate / N,
            **m
        )
    )
    # Actual production vs offline ordinary clone, with the same complete input.
    production = Bridge(
        os.environ.get("MC402_M1_LIBRARY", "/tmp/mc402-m1a/production.dylib")
    )
    comparisons = []
    rng = np.random.default_rng(402)
    for rate in RATES:
        for factor in [1, 2, 4, 8]:
            x = rng.uniform(-2.0, 2.0, 4096)
            actual = e.render(x, rate, factor, gain=0.7, tone=0.4)
            expected, _ = production.render(x, rate, factor, gain=0.7, tone=0.4)
            error = float(np.max(np.abs(actual - expected)))
            assert error < 1e-12, (rate, factor, error)
            comparisons.append(
                dict(sample_rate=rate, factor=factor, max_sample_error=error)
            )
    write("harness-sanity.json", dict(cases=rows, production_equivalence=comparisons))
    print("Harness sanity and production equivalence pass", flush=True)


def antiderivative_tests(e):
    rng = np.random.default_rng(4021)
    errors = []

    def f(x):
        return e.lib.m1a_shape(float(x), 0)

    for _ in range(300):
        x, y, z = rng.uniform(-10.0, 10.0, 3)
        expected = integrate.quad(
            f,
            min(x, y),
            max(x, y),
            points=[v for v in [-1.1, -0.9, 0.9, 1.1] if min(x, y) < v < max(x, y)],
            epsabs=1e-12,
        )[0] / abs(x - y)
        errors.append(abs(expected - e.lib.m1a_adaa(x, y, z, 1)))
        lo, mid, hi = sorted([x, y, z])
        left = integrate.quad(
            lambda t: (t - lo) / (mid - lo) * f(t),
            lo,
            mid,
            points=[v for v in [-1.1, -0.9, 0.9, 1.1] if lo < v < mid],
            epsabs=1e-12,
        )[0]
        right = integrate.quad(
            lambda t: (hi - t) / (hi - mid) * f(t),
            mid,
            hi,
            points=[v for v in [-1.1, -0.9, 0.9, 1.1] if mid < v < hi],
            epsabs=1e-12,
        )[0]
        errors.append(abs(2 * (left + right) / (hi - lo) - e.lib.m1a_adaa(x, y, z, 2)))
    repeated = []
    for value in [-100.0, -1.1, -0.9, 0.0, 0.9, 1.1, 100.0]:
        for delta in [0.0, 1e-15, 1e-12, 1e-9, 1e-6]:
            for order in [1, 2]:
                out = e.lib.m1a_adaa(value, value + delta, value - delta, order)
                assert math.isfinite(out) and abs(out) <= 1.0 + 1e-12
                assert abs(out - f(value)) <= delta + 2e-14
                repeated.append(
                    dict(
                        value=value,
                        delta=delta,
                        order=order,
                        error_from_static=out - f(value),
                    )
                )
    assert max(errors) < 1e-12, max(errors)
    write(
        "antiderivative-checks.json",
        dict(
            max_quadrature_error=max(errors), random_cases=600, repeated_nodes=repeated
        ),
    )
    print("Antiderivatives and repeated nodes pass", max(errors), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["sanity", "scan"])
    args = parser.parse_args()
    e = Experiment()
    if args.command == "sanity":
        sanity(e)
        antiderivative_tests(e)
    if args.command == "scan":
        scan(e)


if __name__ == "__main__":
    main()
