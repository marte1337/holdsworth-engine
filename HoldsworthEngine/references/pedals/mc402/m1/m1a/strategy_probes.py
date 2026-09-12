"""Separate nonlinear sampling, FIR effects, passband changes and CPU costs."""

import argparse
import json
import math
import numpy as np
from experiment import Experiment, DEST, N, RATES, db, metrics, write
from run_diagnosis import reduced_cases
from continuous_reference import reference
from validate_numerics import analog_response


def latency(factor):
    lengths = [65, 33, 33, 33, 33, 65]
    return sum((lengths[i] - 1) / (2 ** (i + 1)) for i in range(int(math.log2(factor))))


def probes(e):
    rows = []
    for case in reduced_cases():
        rate = case["sample_rate"]
        k = case["k"]
        truth = reference(case, 128)
        ref = np.zeros(N // 2 + 1, complex)
        ref[truth["harmonic"] * k] = truth["coefficients"]
        band = (np.arange(ref.size) * rate / N >= 20) & (
            np.arange(ref.size) * rate / N <= 20000
        )
        for mode in [0, 1, 2, 3]:
            for factor in [1, 2, 4, 8, 16, 32, 64]:
                for resampler in ["M1_FIR", "ideal_sine_and_output_projection"]:
                    if resampler == "M1_FIR":
                        y = e.sine(
                            rate,
                            k,
                            case["amplitude"],
                            factor,
                            gain=case["gain"],
                            tone=case["tone"],
                            s1=mode,
                            s2=mode,
                        )
                        impulse = np.zeros(N)
                        impulse[0] = 1.0
                        wire = np.fft.rfft(e.render(impulse, rate, factor, wire=True))
                        delay = latency(factor)
                    else:
                        x = case["amplitude"] * np.sin(
                            np.arange(N * factor) * (2 * np.pi * k / (N * factor))
                        )
                        y = e.render(
                            x,
                            rate * factor,
                            gain=case["gain"],
                            tone=case["tone"],
                            s1=mode,
                            s2=mode,
                            warmup=math.ceil(0.5 * rate / N),
                        )
                        wire = np.ones(ref.size, complex)
                        delay = 0.0
                    extra = (0.0 if mode == 0 else (1.0 if mode == 1 else 2.0)) / factor
                    spectrum = (np.fft.rfft(y) * 2 / y.size)[: ref.size]
                    aligned = spectrum * np.exp(
                        2j * np.pi * np.arange(ref.size) * (delay + extra) / N
                    )
                    corrected = (
                        spectrum
                        / wire
                        * np.exp(2j * np.pi * np.arange(ref.size) * extra / N)
                    )
                    rows.append(
                        dict(
                            case=case,
                            factor=factor,
                            s1=mode,
                            s2=mode,
                            resampler=resampler,
                            additional_linear_delay_host_samples=extra,
                            **metrics(y, rate, k),
                            aligned_band_error_db=db(
                                np.linalg.norm((aligned - ref)[band])
                                / np.linalg.norm(ref[band])
                            ),
                            wire_compensated_band_error_db=db(
                                np.linalg.norm((corrected - ref)[band])
                                / np.linalg.norm(ref[band])
                            ),
                            fundamental_gain_error_db=db(abs(spectrum[k]) / abs(ref[k]))
                        )
                    )
        print("strategy probes", rate, case["input_hz"], flush=True)
    write("strategy-probes.json", rows)


def response(e):
    rows = []
    for rate in RATES:
        for factor in [1, 2, 4, 8]:
            impulse = np.zeros(65536)
            impulse[0] = 1e-6
            ordinary = e.render(impulse, rate, factor, gain=0.5, tone=1.0)
            ref = np.fft.rfft(ordinary)
            hz = np.fft.rfftfreq(impulse.size, 1 / rate)
            band = (hz >= 80) & (hz <= 8000)
            for mode in [1, 2, 3]:
                y = e.render(
                    impulse, rate, factor, gain=0.5, tone=1.0, s1=mode, s2=mode
                )
                spectrum = np.fft.rfft(y)
                error = 20 * np.log10(abs(spectrum[band]) / abs(ref[band]))
                omega = 2 * np.pi * hz[band] / (rate * factor)
                per_stage = (
                    (1 + np.exp(-1j * omega)) / 2
                    if mode == 1
                    else (
                        (1 + 2 * np.cos(omega)) / 3 * np.exp(-1j * omega)
                        if mode == 2
                        else (2 + np.cos(omega)) / 3 * np.exp(-1j * omega)
                    )
                )
                predicted = per_stage**2
                actual = spectrum[band] / ref[band]
                linear_error = float(np.max(np.abs(actual - predicted)))
                assert linear_error < 1e-7, (rate, factor, mode, linear_error)
                total_error = 0.0
                for tone in [0.0, 0.5, 1.0]:
                    actual_tone = e.render(
                        impulse, rate, factor, gain=0.5, tone=tone, s1=mode, s2=mode
                    )
                    magnitude = np.abs(np.fft.rfft(actual_tone)[band]) / 1e-6
                    expected = np.abs(analog_response(hz[band], 0.5, tone, 1.0))
                    total_error = max(
                        total_error,
                        float(np.max(np.abs(20 * np.log10(magnitude / expected)))),
                    )
                rows.append(
                    dict(
                        sample_rate=rate,
                        factor=factor,
                        mode=mode,
                        max_additional_magnitude_error_80_8000_db=float(
                            np.max(np.abs(error))
                        ),
                        max_total_analog_magnitude_error_db=total_error,
                        additional_linear_delay_host_samples=(1.0 if mode == 1 else 2.0)
                        / factor,
                        maximum_complex_error_against_analytic_linear_kernel=linear_error,
                    )
                )
    write("adaa-frequency-response.json", rows)


def cpu(e):
    rows = []
    for rate in [44100, 48000, 192000]:
        for mode in [0, 1, 2, 3]:
            for factor in [1, 2, 4, 8, 16]:
                samples = int(rate * 2)
                measurements = []
                for repeat in range(3):
                    checksum = np.zeros(1)
                    elapsed = e.lib.m1a_benchmark(
                        rate,
                        factor,
                        mode,
                        mode,
                        samples,
                        checksum.ctypes.data_as(
                            __import__("ctypes").POINTER(__import__("ctypes").c_double)
                        ),
                    )
                    measurements.append(elapsed / samples * 1e9)
                median = float(np.median(measurements))
                rows.append(
                    dict(
                        sample_rate=rate,
                        factor=factor,
                        mode=mode,
                        median_nanoseconds_per_host_sample=median,
                        audio_budget_percent=median * rate / 1e7,
                        repeats_ns=measurements,
                    )
                )
    write(
        "cpu-estimates.json",
        dict(
            rows=rows,
            experiment_stage_state_bytes=e.lib.m1a_state_size(),
            note="Offline core plus existing FIR; excludes host callback, control adoption and live delay alignment. 0.5 V 997 Hz table, Gain/Tone/Output 1. No optimization.",
        ),
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["probes", "response", "cpu"])
    args = parser.parse_args()
    {"probes": probes, "response": response, "cpu": cpu}[args.command](Experiment())
