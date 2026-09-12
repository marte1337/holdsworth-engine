"""Offline M1b envelope evaluation; imports the unchanged M1a experiment."""

import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
import ctypes as ct
import gzip
import json
import math
import os
from pathlib import Path
import sys
import time

import numpy as np

DEST = Path(__file__).resolve().parent
sys.path.insert(0, str(DEST.parent / "m1a"))
from experiment import Experiment, PTR, db
from continuous_reference import reference
from strategy_probes import latency
from validate_numerics import analog_response

P = json.loads((DEST / "envelope.json").read_text())
N = P["fft_length"]


def write(name, data):
    raw = (json.dumps(data, indent=None if name.endswith(".gz") else 2) + "\n").encode()
    (DEST / name).write_bytes(
        gzip.compress(raw, mtime=0) if name.endswith(".gz") else raw
    )


def read(name):
    raw = (DEST / name).read_bytes()
    return json.loads(gzip.decompress(raw) if name.endswith(".gz") else raw)


def bin_for(frequency, rate, n=N, bounded=True):
    k = 2 * round((frequency * n / rate - 1) / 2) + 1
    if bounded:
        while k * rate / n < P["input_frequency_hz"][0]:
            k += 2
        while k * rate / n > P["input_frequency_hz"][1]:
            k -= 2
    return max(k, 1)


def measure(y, rate, k):
    c = np.fft.rfft(y) * 2 / y.size
    bins = np.arange(c.size)
    hz = bins * rate / y.size
    band = (hz >= 20) & (hz <= 20000)
    alias_mask = band & (bins % k != 0)
    peak = np.abs(c)
    i = int(np.argmax(np.where(alias_mask, peak, 0.0)))
    alias = float(peak[i])
    fund = float(peak[k])
    total = float(np.linalg.norm(c[alias_mask]))
    nonmute = fund > 1e-250
    relative = db(alias / max(fund, 1e-300)) if nonmute else None
    total_relative = db(total / max(fund, 1e-300)) if nonmute else None
    return dict(
        fundamental_peak=fund,
        fundamental_dbfs=db(fund),
        alias_hz=float(hz[i]),
        alias_peak=alias,
        alias_dbfs=db(alias),
        alias_dbc=relative,
        total_alias_rms_volts=total / math.sqrt(2),
        total_alias_dbfs_sine_equivalent=db(total),
        total_alias_dbc=total_relative,
        relative_pass=(not nonmute)
        or relative <= P["relative_strongest_alias_limit_dbc"],
        absolute_pass=db(total) <= P["absolute_total_alias_limit_dbfs_sine_equivalent"],
        output_peak=float(np.max(np.abs(y))),
    )


def render(
    e, rate, k, amplitude, factor, mode, gain, tone, n=N, output=1.0, warmup=0.5
):
    x = amplitude * np.sin(np.arange(n) * (2 * np.pi * k / n))
    return e.render(
        x,
        rate,
        factor,
        gain=gain,
        tone=tone,
        output=output,
        s1=mode,
        s2=mode,
        warmup=math.ceil(warmup * rate / n),
    )


def job(task):
    rate, method, factor, mode, kind = task
    e = Experiment()
    rows = []
    if kind == "product":
        frequencies = P["frequencies_hz"]
        amplitudes = P["amplitudes_volts_peak"]
        gains, tones = P["gains"], P["tones"]
    elif kind == "dense":
        frequencies = range(2000, 8001, P["dense_followup_frequency_step_hz"])
        amplitudes = P["dense_followup_amplitudes"]
        gains, tones = P["dense_followup_gains"], P["dense_followup_tones"]
    elif kind == "torture":
        frequencies = [100, 1000, 5000, 9500, 19000, 0.45 * rate, 0.49 * rate]
        amplitudes = [0.005, 0.02, 0.12, 0.5, 10.0]
        gains, tones = [0.1, 0.5, 1.0], [0.0, 1.0]
    else:
        frequencies = [11000, 12500, 15000, 17500, 19000]
        amplitudes = [0.005, 0.02, 0.05]
        gains, tones = [0.5, 0.9, 1.0], [0.0, 1.0]
    # Avoid duplicate coherent low-frequency cases at high sample rates.
    bins = sorted(
        set(bin_for(f, rate, bounded=kind in ["product", "dense"]) for f in frequencies)
    )
    product_bins = {bin_for(f, rate) for f in P["frequencies_hz"]}
    for k in bins:
        for amplitude in amplitudes:
            for gain in gains:
                for tone in tones:
                    if (
                        kind == "dense"
                        and k in product_bins
                        and amplitude in P["amplitudes_volts_peak"]
                        and gain in P["gains"]
                        and tone in P["tones"]
                    ):
                        continue  # Already measured by the product sweep.
                    y = render(e, rate, k, amplitude, factor, mode, gain, tone)
                    rows.append(
                        dict(
                            sample_rate=rate,
                            method=method,
                            factor=factor,
                            mode=mode,
                            k=k,
                            fft_length=N,
                            input_hz=k * rate / N,
                            amplitude=amplitude,
                            gain=gain,
                            tone=tone,
                            output=1.0,
                            boost_db=0.0,
                            **measure(y, rate, k),
                        )
                    )
    name = f"{kind}-{rate}-{method}.json.gz"
    write(name, rows)
    worst = max(rows, key=lambda r: r["alias_dbc"])
    return name, len(rows), worst["alias_dbc"], time.time()


def sweep(kind):
    tasks = [
        (rate, name, factor, mode, kind)
        for rate in P["sample_rates"]
        for name, factor, mode in P["methods"]
    ]
    with ProcessPoolExecutor(
        max_workers=int(os.environ.get("MC402_M1B_WORKERS", "4"))
    ) as pool:
        futures = [pool.submit(job, task) for task in tasks]
        for future in as_completed(futures):
            print(future.result(), flush=True)


def all_rows(kind):
    return [
        r
        for rate in P["sample_rates"]
        for name, _, _ in P["methods"]
        for r in read(f"{kind}-{rate}-{name}.json.gz")
    ]


def performance():
    e = Experiment()
    benchmark = ct.CDLL(
        os.environ.get("MC402_M1B_BENCHMARK", "/tmp/mc402-m1b/benchmark.dylib")
    )
    benchmark.m1b_benchmark.argtypes = (
        [ct.c_double, ct.c_uint, ct.c_int] + [ct.c_double] * 4 + [ct.c_size_t, PTR]
    )
    benchmark.m1b_benchmark.restype = ct.c_double
    product = all_rows("product") + all_rows("dense")
    rows = []
    for rate in P["sample_rates"]:
        impulse = np.zeros(131072)
        impulse[0] = 1e-6
        hz = np.fft.rfftfreq(impulse.size, 1 / rate)
        band = (hz >= 80) & (hz <= 8000)
        for name, factor, mode in P["methods"]:
            wire = e.render(impulse, rate, factor, wire=True)
            wire_delay = int(np.argmax(wire))
            assert wire_delay == latency(factor)
            magnitude_rows = []
            extra_delay_errors = []
            for tone in [0.0, 0.5, 1.0]:
                y = e.render(
                    impulse, rate, factor, gain=0.5, tone=tone, s1=mode, s2=mode
                )
                c = np.fft.rfft(y) / 1e-6
                expected = analog_response(hz[band], 0.5, tone, 1.0)
                errors = 20 * np.log10(np.abs(c[band]) / np.abs(expected))
                i = int(np.argmax(np.abs(errors)))
                ordinary = np.fft.rfft(
                    e.render(impulse, rate, factor, gain=0.5, tone=tone)
                )
                relative = np.fft.rfft(y)[band] / ordinary[band]
                measured_extra = -np.angle(relative) / (2 * np.pi * hz[band] / rate)
                extra = (2 / factor) if mode == 2 else 0.0
                extra_delay_errors.append(float(np.max(np.abs(measured_extra - extra))))
                maximum, worst_hz = float(np.max(np.abs(errors))), float(hz[band][i])
                endpoints = []
                for f in [80.0, 8000.0]:
                    actual = (
                        np.sum(y * np.exp(-2j * np.pi * f * np.arange(y.size) / rate))
                        / 1e-6
                    )
                    expected_endpoint = analog_response(np.array([f]), 0.5, tone, 1.0)[
                        0
                    ]
                    error_endpoint = db(abs(actual) / abs(expected_endpoint))
                    endpoints.append(dict(hz=f, magnitude_error_db=error_endpoint))
                    if abs(error_endpoint) > maximum:
                        maximum, worst_hz = abs(error_endpoint), f
                magnitude_rows.append(
                    dict(
                        tone=tone,
                        max_error_db=maximum,
                        worst_hz=worst_hz,
                        exact_endpoints=endpoints,
                    )
                )
            assert max(extra_delay_errors) < 1e-7
            repetitions = []
            for _ in range(5):
                checksum = np.zeros(1)
                count = int(rate * 3)
                elapsed = e.lib.m1a_benchmark(
                    rate, factor, mode, mode, count, checksum.ctypes.data_as(PTR)
                )
                repetitions.append(elapsed * 1e9 / count)
            median = float(np.median(repetitions))
            worst = max(
                (
                    r
                    for r in product
                    if r["sample_rate"] == rate and r["method"] == name
                ),
                key=lambda r: r["alias_dbc"],
            )
            worst_times = []
            for _ in range(5):
                checksum = np.zeros(1)
                count = int(rate * 3)
                elapsed = benchmark.m1b_benchmark(
                    rate,
                    factor,
                    mode,
                    worst["input_hz"],
                    worst["amplitude"],
                    worst["gain"],
                    worst["tone"],
                    count,
                    checksum.ctypes.data_as(PTR),
                )
                worst_times.append(elapsed * 1e9 / count)
            extra = (2 / factor) if mode == 2 else 0.0
            rows.append(
                dict(
                    sample_rate=rate,
                    method=name,
                    factor=factor,
                    mode=mode,
                    fir_lengths=[65, 33] if factor == 4 else [65, 33, 33],
                    measured_wire_delay_host_samples=wire_delay,
                    additional_adaa_linear_delay_host_samples=extra,
                    measured_delay_error_samples=max(extra_delay_errors),
                    total_linear_delay_host_samples=wire_delay + extra,
                    total_delay_ms=1000 * (wire_delay + extra) / rate,
                    small_signal=magnitude_rows,
                    median_nanoseconds_per_host_sample=median,
                    audio_budget_percent=median * rate / 1e7,
                    repetitions_nanoseconds_per_host_sample=repetitions,
                    worst_alias_workload=worst,
                    worst_alias_workload_ns_median=float(np.median(worst_times)),
                    worst_alias_workload_audio_budget_percent=float(
                        np.median(worst_times)
                    )
                    * rate
                    / 1e7,
                    worst_alias_workload_repetitions_ns=worst_times,
                    stage_history_doubles=2 if mode == 2 else 0,
                )
            )
            print("performance", rate, name, median, flush=True)
    write("performance.json", rows)


def reference_checks():
    rows = all_rows("product") + all_rows("dense")
    selected = {}
    for rate in P["sample_rates"]:
        for method, _, _ in P["methods"]:
            group = [
                r for r in rows if r["sample_rate"] == rate and r["method"] == method
            ]
            for metric in [
                "alias_dbc",
                "alias_dbfs",
                "total_alias_dbfs_sine_equivalent",
            ]:
                for region in [
                    group,
                    [
                        r
                        for r in group
                        if r["amplitude"] <= 2 and 2000 <= r["input_hz"] <= 8000
                    ],
                    [
                        r
                        for r in group
                        if r["amplitude"] <= 0.5 and 2000 <= r["input_hz"] <= 8000
                    ],
                ]:
                    r = max(region, key=lambda r: r[metric])
                    key = (rate, r["k"], r["amplitude"], r["gain"], r["tone"])
                    selected[key] = {
                        k: r[k]
                        for k in [
                            "sample_rate",
                            "k",
                            "input_hz",
                            "amplitude",
                            "gain",
                            "tone",
                            "output",
                        ]
                    }
        for tone in [0.0, 1.0]:
            for frequency, amp, gain in [(5000, 0.5, 1.0), (1000, 0.12, 0.5)]:
                k = bin_for(frequency, rate)
                key = (rate, k, amp, gain, tone)
                selected[key] = dict(
                    sample_rate=rate,
                    k=k,
                    input_hz=k * rate / N,
                    amplitude=amp,
                    gain=gain,
                    tone=tone,
                    output=1.0,
                )
    e = Experiment()
    results = []
    for case in selected.values():
        # The selected alias winners are high-band cases. Require convergence;
        # do not assume fixed quadrature order suffices for low fundamentals.
        order = max(64, int(math.ceil(40000 / case["input_hz"])))
        truth1, truth2 = reference(case, order), reference(case, order * 2)
        error = np.linalg.norm(truth2["coefficients"] - truth1["coefficients"])
        refnorm = np.linalg.norm(truth2["coefficients"])
        convergence = db(error / max(refnorm, 1e-300))
        assert convergence < -100, (case, convergence)
        rate, k = case["sample_rate"], case["k"]
        ref = np.zeros(N // 2 + 1, complex)
        ref[truth2["harmonic"] * k] = truth2["coefficients"]
        hz = np.arange(ref.size) * rate / N
        band = (hz >= 20) & (hz <= 20000)
        comparisons = []
        for name, factor, mode in P["methods"]:
            y = render(
                e, rate, k, case["amplitude"], factor, mode, case["gain"], case["tone"]
            )
            c = np.fft.rfft(y) * 2 / N
            delay = latency(factor) + (2 / factor if mode == 2 else 0)
            aligned = c * np.exp(2j * np.pi * np.arange(c.size) * delay / N)
            # M1a's ideal-resampler diagnosis only. It does not replace any
            # qualification render or change the actual FIRs under test.
            x_high = case["amplitude"] * np.sin(
                np.arange(N * factor) * (2 * np.pi * k / (N * factor))
            )
            ideal_y = e.render(
                x_high,
                rate * factor,
                gain=case["gain"],
                tone=case["tone"],
                s1=mode,
                s2=mode,
                warmup=math.ceil(0.5 * rate / N),
            )
            comparisons.append(
                dict(
                    method=name,
                    factor=factor,
                    **measure(y, rate, k),
                    reference_fundamental_dbfs=db(abs(ref[k])),
                    fundamental_magnitude_error_db=db(abs(c[k]) / abs(ref[k])),
                    aligned_full_band_residual_db=db(
                        np.linalg.norm((aligned - ref)[band])
                        / np.linalg.norm(ref[band])
                    ),
                    ideal_resampler_diagnostic=measure(ideal_y, rate * factor, k),
                )
            )
        results.append(
            dict(
                case=case,
                quadrature_orders=[order, order * 2],
                coefficient_convergence_db=convergence,
                periodic_state_error=truth2["periodic_state_error"],
                reference_harmonics=truth2["harmonic"].tolist(),
                reference_coefficients_real=truth2["coefficients"].real.tolist(),
                reference_coefficients_imag=truth2["coefficients"].imag.tolist(),
                comparisons=comparisons,
            )
        )
        print("reference", len(results), "of", len(selected), flush=True)
    write("continuous-reference-checks.json", results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "command",
        choices=["product", "dense", "torture", "tails", "performance", "reference"],
    )
    args = parser.parse_args()
    if args.command in ["product", "dense", "torture", "tails"]:
        sweep(args.command)
    elif args.command == "performance":
        performance()
    else:
        reference_checks()
