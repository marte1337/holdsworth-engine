"""Native R2 CPU, measured latency and unmodified small-signal response."""

import ctypes as ct
import platform
import time
from pathlib import Path
import numpy as np
from evaluate import (
    DEST,
    P,
    METHODS,
    Experiment,
    PTR,
    Periodic,
    library,
    read,
    write,
    db,
)
from validate_numerics import analog_response


def main():
    e = Experiment(library())
    old = Experiment(library("v1"))
    bench = ct.CDLL(str(Path(library()).parent / "benchmark.dylib"))
    bench.m1c_benchmark.argtypes = (
        [ct.c_double, ct.c_uint, ct.c_int] + [ct.c_double] * 4 + [ct.c_size_t, PTR]
    )
    bench.m1c_benchmark.restype = ct.c_double
    summary = read(DEST / "summary.json")
    rows = []
    for rate in P["sample_rates"]:
        x = np.zeros(131072)
        x[0] = 1e-6
        hz = np.fft.rfftfreq(x.size, 1 / rate)
        band = (hz >= 80) & (hz <= 8000)
        for name, factor, mode in METHODS:
            wire = e.render(x, rate, factor, wire=True)
            fir_delay = float(np.argmax(abs(wire)))
            assert fir_delay == {4: 40.0, 8: 44.0}[factor]
            mags = []
            delay_errors = []
            old_difference = 0.0
            for tone in [0.0, 0.5, 1.0]:
                y = e.render(x, rate, factor, gain=0.5, tone=tone, s1=mode, s2=mode)
                yo = old.render(x, rate, factor, gain=0.5, tone=tone, s1=mode, s2=mode)
                old_difference = max(old_difference, float(np.max(abs(y - yo))))
                ordinary = e.render(x, rate, factor, gain=0.5, tone=tone)
                c = np.fft.rfft(y) / 1e-6
                error = 20 * np.log10(
                    abs(c[band]) / abs(analog_response(hz[band], 0.5, tone, 1.0))
                )
                ratio = np.fft.rfft(y)[band] / np.fft.rfft(ordinary)[band]
                extra = -np.angle(ratio) / (2 * np.pi * hz[band] / rate)
                delay_errors.append(float(np.max(abs(extra - mode / factor))))
                worst = int(np.argmax(abs(error)))
                maximum = float(abs(error[worst]))
                worst_hz = float(hz[band][worst])
                endpoints = []
                for f in [80.0, 8000.0]:
                    actual = (
                        np.sum(y * np.exp(-2j * np.pi * f * np.arange(x.size) / rate))
                        / 1e-6
                    )
                    delta = db(
                        abs(actual)
                        / abs(analog_response(np.array([f]), 0.5, tone, 1.0)[0])
                    )
                    endpoints.append(dict(frequency_hz=f, error_db=delta))
                    if abs(delta) > maximum:
                        maximum, worst_hz = abs(delta), f
                mags.append(
                    dict(
                        tone=tone,
                        max_abs_error_db=maximum,
                        worst_hz=worst_hz,
                        endpoints=endpoints,
                    )
                )
            assert max(delay_errors) < 1e-7
            assert old_difference < 1e-18
            worst = next(
                r
                for r in summary
                if r["kind"] == "product"
                and r["sample_rate"] == rate
                and r["method"] == name
                and r["resampler"] == "fir"
            )["worst_relative"][0]
            times = []
            for label, f, amplitude, gain, tone in [
                ("typical", 997.0, 0.5, 1.0, 1.0),
                (
                    "worst_product",
                    worst["input_hz"],
                    worst["amplitude"],
                    worst["gain"],
                    worst["tone"],
                ),
            ]:
                reps = []
                checksum = np.zeros(1)
                count = int(rate * 3)
                for _ in range(5):
                    seconds = bench.m1c_benchmark(
                        rate,
                        factor,
                        mode,
                        f,
                        amplitude,
                        gain,
                        tone,
                        count,
                        checksum.ctypes.data_as(PTR),
                    )
                    reps.append(seconds * 1e9 / count)
                median = float(np.median(reps))
                times.append(
                    dict(
                        workload=label,
                        input_hz=f,
                        amplitude=amplitude,
                        gain=gain,
                        tone=tone,
                        nanoseconds_per_host_sample=reps,
                        median_ns_per_sample=median,
                        median_realtime_one_core_percent=median * rate * 1e-7,
                    )
                )
            rows.append(
                dict(
                    sample_rate=rate,
                    method=name,
                    factor=factor,
                    mode=mode,
                    measured_fir_delay_host_samples=fir_delay,
                    adaa_extra_host_samples=mode / factor,
                    total_delay_host_samples=fir_delay + mode / factor,
                    total_delay_ms=(fir_delay + mode / factor) * 1000 / rate,
                    ideal_experiment_adaa_delay_host_samples=mode / factor,
                    max_adaa_delay_measurement_error_samples=max(delay_errors),
                    small_signal=mags,
                    old_r2_max_small_signal_sample_difference_volts=old_difference,
                    cpu=times,
                )
            )
            print(rate, name, times[0]["median_ns_per_sample"], flush=True)
    write(
        "performance.json",
        dict(
            platform=platform.platform(),
            machine=platform.machine(),
            timestamp_utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            measurements=rows,
            antialias_struct_bytes=e.lib.m1a_state_size(),
            note="Serial native benchmark, not Python sweep timing; 5 runs of 3 s audio per workload, warmed up. Offline ideal projection has no finite causal FIR latency or realtime CPU estimate.",
        ),
    )


if __name__ == "__main__":
    main()
