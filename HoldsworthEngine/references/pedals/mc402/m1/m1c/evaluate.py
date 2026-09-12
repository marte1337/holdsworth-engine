"""R2-only periodic offline sweep; original M1b coordinates and gate unchanged."""

import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
import ctypes as ct
import gzip
import json
import math
import os
from pathlib import Path
import re
import sys
import time

import numpy as np
from scipy.fft import rfft, irfft

DEST = Path(__file__).resolve().parent
OLD = DEST.parent / "m1b"
sys.path.insert(0, str(DEST.parent / "m1a"))
from experiment import Experiment, PTR, db

P = json.loads((OLD / "envelope.json").read_text())
N = P["fft_length"]
METHODS = [
    ("ordinary4", 4, 0),
    ("ordinary8", 8, 0),
    ("adaa1_4", 4, 1),
    ("adaa2_4", 4, 2),
    ("adaa2_8", 8, 2),
]
A = -470 / 22
H = 2.5
EXPONENT = 3.321928094887362
HP1 = 1 / (2 * np.pi * 22000 * 47e-9)


def read(path):
    raw = Path(path).read_bytes()
    return json.loads(gzip.decompress(raw) if str(path).endswith(".gz") else raw)


def write(name, value):
    path = DEST / name
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = (
        json.dumps(value, indent=None if name.endswith(".gz") else 2) + "\n"
    ).encode()
    path.write_bytes(gzip.compress(raw, mtime=0) if name.endswith(".gz") else raw)


def library(profile="r2"):
    return (
        os.environ.get("MC402_M1C_LIBRARY_DIR", "/tmp/mc402-m1c") + f"/{profile}.dylib"
    )


def bin_for(f, rate, n=N):
    return 2 * round((f * n / rate - 1) / 2) + 1


def taps():
    header = (DEST.parents[4] / "dsp" / "MC402HalfBandCoefficients.h").read_text()
    return {
        size: np.array(
            [
                float(s.strip())
                for s in re.search(
                    r"kHalfBand" + str(size) + r"\s*\{([^}]+)\}", header, re.S
                )
                .group(1)
                .split(",")
                if s.strip()
            ]
        )
        for size in [33, 65]
    }


def filter_response(hz, rate, cutoff, high=False):
    g = np.tan(np.pi * cutoff / rate)
    c = g / (1 + g)
    z = np.exp(-2j * np.pi * hz / rate)
    return ((1 - c) * (1 - z) if high else c * (1 + z)) / (1 - (1 - 2 * c) * z)


class Periodic:
    """Exact periodic steady state of the unchanged feed-forward digital cascade.

    Nonlinear kernels are the native sample-wise kernels, seeded with periodic
    history. LTI recurrences/FIRs are solved in Fourier space, without startup.
    """

    def __init__(self, rate, factor, mode, resampler, profile="r2", n=N):
        self.rate, self.factor, self.mode, self.resampler = (
            rate,
            factor,
            mode,
            resampler,
        )
        self.n, self.m = n, n * factor
        self.lib = ct.CDLL(library(profile))
        self.lib.m1c_stage.argtypes = [PTR, PTR, ct.c_size_t, ct.c_double, ct.c_int]
        self.hz = np.fft.rfftfreq(self.m, 1 / (rate * factor))
        self.hp1 = A * filter_response(self.hz, rate * factor, HP1, True)
        self.hp2 = A * filter_response(self.hz, rate * factor, 154.0, True)
        self.fir = np.ones(self.hz.size, complex)
        bank = taps()
        for j in range(int(math.log2(factor))):
            h = bank[65 if j == 0 else 33]
            # Equivalent high-rate FIR: insert zero taps between lower-rate taps.
            expanded = np.zeros(self.m)
            expanded[np.arange(h.size) * (factor // 2 ** (j + 1))] = h
            self.fir *= rfft(expanded)
        idx = np.arange(n // 2 + 1)[None, :] + n * np.arange(factor)[:, None]
        self.conjugate = idx > self.m // 2
        self.indices = np.where(self.conjugate, self.m - idx, idx)
        self.post = {}

    def stage(self, x, mode=None):
        x = np.ascontiguousarray(x, dtype=np.float64)
        y = np.empty_like(x)
        self.lib.m1c_stage(
            x.ctypes.data_as(PTR),
            y.ctypes.data_as(PTR),
            x.size,
            H,
            self.mode if mode is None else mode,
        )
        return y

    def input(self, k, phase=0.0):
        if self.resampler == "ideal":
            angle = 2 * np.pi * k * np.arange(self.m) / self.m + phase
            return np.imag(np.exp(1j * angle) * self.hp1[k])
        x = np.zeros(self.m)
        x[:: self.factor] = self.factor * np.sin(
            2 * np.pi * k * np.arange(self.n) / self.n + phase
        )
        return irfft(rfft(x) * self.fir * self.hp1, self.m)

    def after_s1(self, first_input):
        return irfft(rfft(self.stage(first_input)) * self.hp2, self.m)

    def after_s2(self, second_input, gain):
        return rfft(self.stage(second_input * gain**EXPONENT)) * 2 / self.m

    def output(self, spectrum, tone, output=1.0):
        if tone not in self.post:
            self.post[tone] = filter_response(
                self.hz, self.rate * self.factor, 500 * 16**tone
            ) * filter_response(self.hz, self.rate * self.factor, 10.0, True)
        c = spectrum * self.post[tone] * output**EXPONENT
        if self.resampler == "ideal":
            return c[: self.n // 2 + 1]
        c = (c * self.fir)[self.indices]
        return np.sum(np.where(self.conjugate, np.conj(c), c), axis=0)

    def render_coefficients(self, k, amplitude, gain, tone, output=1.0, phase=0.0):
        return self.output(
            self.after_s2(self.after_s1(self.input(k, phase) * amplitude), gain),
            tone,
            output,
        )

    def render_input(self, x, gain, tone, output=1.0):
        xh = np.zeros(self.m)
        xh[:: self.factor] = self.factor * x
        first = irfft(rfft(xh) * self.fir * self.hp1, self.m)
        c = self.output(self.after_s2(self.after_s1(first), gain), tone, output)
        return irfft(c * self.n / 2, self.n)


def measure(c, rate, k, n=N):
    bins = np.arange(c.size)
    hz = bins * rate / n
    mask = (hz >= 20) & (hz <= 20000) & (bins % k != 0)
    magnitudes = np.abs(c)
    i = int(np.argmax(np.where(mask, magnitudes, 0.0)))
    alias, fund = float(magnitudes[i]), float(magnitudes[k])
    total = float(np.linalg.norm(c[mask]))
    relative = db(alias / max(fund, 1e-300)) if fund > 1e-250 else None
    return dict(
        fundamental_peak=fund,
        fundamental_dbfs=db(fund),
        alias_hz=float(hz[i]),
        alias_peak=alias,
        alias_dbfs=db(alias),
        alias_dbc=relative,
        total_alias_rms_volts=total / math.sqrt(2),
        total_alias_dbfs_sine_equivalent=db(total),
        relative_pass=relative is None or relative <= -70.0,
        absolute_pass=db(total) <= -80.0,
    )


def coordinates(rate, kind="product"):
    # Preserve measured M1b coherent coordinates, including dense deduplication.
    kinds = ["product", "dense"] if kind == "product" else [kind]
    source = [
        r for kind_ in kinds for r in read(OLD / f"{kind_}-{rate}-ordinary4.json.gz")
    ]
    fields = [
        "sample_rate",
        "k",
        "fft_length",
        "input_hz",
        "amplitude",
        "gain",
        "tone",
        "output",
        "boost_db",
    ]
    return [{field: r[field] for field in fields} for r in source]


def job(task):
    rate, name, factor, mode, resampler, kind = task
    start = time.monotonic()
    e = Periodic(rate, factor, mode, resampler)
    cases = sorted(
        coordinates(rate, kind),
        key=lambda r: (r["k"], r["amplitude"], r["gain"], r["tone"]),
    )
    previous = (None, None, None)
    rows = []
    for case in cases:
        key = case["k"], case["amplitude"], case["gain"]
        if key[0] != previous[0]:
            first_input = e.input(key[0])
        if key[:2] != previous[:2]:
            second_input = e.after_s1(first_input * key[1])
        if key != previous:
            spectrum = e.after_s2(second_input, key[2])
        c = e.output(spectrum, case["tone"])
        rows.append(
            dict(
                **case,
                method=name,
                factor=factor,
                mode=mode,
                resampler=resampler,
                **measure(c, rate, key[0]),
            )
        )
        previous = key
    filename = f"data/{kind}-{rate}-{name}-{resampler}.json.gz"
    write(filename, rows)
    return (
        filename,
        len(rows),
        max(r["alias_dbc"] for r in rows),
        time.monotonic() - start,
    )


def sweep(kind):
    tasks = [
        (rate, name, factor, mode, sampler, kind)
        for rate in P["sample_rates"]
        for name, factor, mode in METHODS
        for sampler in ["fir", "ideal"]
    ]
    with ProcessPoolExecutor(
        max_workers=int(os.environ.get("MC402_M1C_WORKERS", "4"))
    ) as pool:
        for result in as_completed([pool.submit(job, task) for task in tasks]):
            print(result.result(), flush=True)


def summarize():
    groups = []
    for kind in ["product", "torture"]:
        for rate in P["sample_rates"]:
            for name, _, _ in METHODS:
                for sampler in ["fir", "ideal"]:
                    file = DEST / f"data/{kind}-{rate}-{name}-{sampler}.json.gz"
                    if not file.exists():
                        continue
                    rows = read(file)
                    primary_count = len(rows)
                    if kind == "product":
                        # Historical rows inside the accepted product domain
                        # retain its gate even when stored in a torture file.
                        extra = DEST / f"data/torture-{rate}-{name}-{sampler}.json.gz"
                        keys = {
                            tuple(r[f] for f in ["k", "amplitude", "gain", "tone"])
                            for r in rows
                        }
                        if extra.exists():
                            rows += [
                                r
                                for r in read(extra)
                                if 40 <= r["input_hz"] <= 10000
                                and r["amplitude"] <= 4.5
                                and tuple(
                                    r[f] for f in ["k", "amplitude", "gain", "tone"]
                                )
                                not in keys
                            ]
                    groups.append(
                        dict(
                            kind=kind,
                            sample_rate=rate,
                            method=name,
                            resampler=sampler,
                            count=len(rows),
                            primary_count=primary_count,
                            historical_product_supplement_count=len(rows)
                            - primary_count,
                            relative_failures=sum(not r["relative_pass"] for r in rows),
                            absolute_failures=sum(not r["absolute_pass"] for r in rows),
                            either_failures=sum(
                                not (r["relative_pass"] and r["absolute_pass"])
                                for r in rows
                            ),
                            worst_relative=sorted(
                                rows, key=lambda r: r["alias_dbc"], reverse=True
                            )[:3],
                            worst_absolute=sorted(
                                rows,
                                key=lambda r: r["total_alias_dbfs_sine_equivalent"],
                                reverse=True,
                            )[:3],
                            worst_musical=sorted(
                                [
                                    r
                                    for r in rows
                                    if 0.25 <= r["amplitude"] <= 2.0
                                    and 1950 <= r["input_hz"] <= 8050
                                ],
                                key=lambda r: r["alias_dbc"],
                                reverse=True,
                            )[:3],
                        )
                    )
    write("summary.json", groups)
    for r in groups:
        print(
            r["kind"],
            r["sample_rate"],
            r["method"],
            r["resampler"],
            r["count"],
            r["worst_relative"][0]["alias_dbc"],
            r["either_failures"],
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["product", "torture", "summarize"])
    args = parser.parse_args()
    summarize() if args.action == "summarize" else sweep(args.action)
