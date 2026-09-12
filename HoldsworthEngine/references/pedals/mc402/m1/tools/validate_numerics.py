"""Offline measurements of compiled MC402, never NAM/IR or hardware fitting.

Build MC402OfflineBridge.cpp with the processor as a shared library; pass its path.
Uses coherent rectangular-window FFTs after at least 0.5 seconds of whole-period warmup. Reports
non-harmonic bins as alias candidates and independently checks a high-rate core.
"""

import argparse
import ctypes as ct
import json
import math
from pathlib import Path
import time
import sys

import numpy as np
from scipy import signal
from generate_fir import taps

RATES = [44100, 48000, 88200, 96000, 176400, 192000]
FACTORS = [1, 2, 4, 8, 16, 32, 64]
N = 8192
FREQUENCIES = [100, 1000, 5000, 9500, 19000]
AMPLITUDES = [0.005, 0.02, 0.12, 0.5, 10.0]
GAINS = [0.1, 0.5, 1.0]
TONES = [0.0, 1.0]
WARMUP_SECONDS = 0.5
# Engineering gates from milestones.md; the reference gets a diagnostic 20 dB margin.
ALIAS_GATE_DBC = -70.0
FIR_RIPPLE_LIMIT_DB = 0.1
FIR_STOP_LIMIT_DB = -80.0
SMALL_SIGNAL_LIMIT_DB = 0.5
REFERENCE_ALIAS_LIMIT_DBC = ALIAS_GATE_DBC - 20.0
P = ct.POINTER(ct.c_double)


class Bridge:
    def __init__(self, path):
        self.lib = ct.CDLL(str(Path(path).resolve()))
        self.lib.mc402_create.argtypes = [ct.c_double, ct.c_uint] + [ct.c_double] * 4
        self.lib.mc402_create.restype = ct.c_void_p
        self.lib.mc402_destroy.argtypes = [ct.c_void_p]
        self.lib.mc402_process.argtypes = [ct.c_void_p, P, P, ct.c_size_t, ct.c_int]
        self.lib.mc402_latency.argtypes = [ct.c_void_p]
        self.lib.mc402_latency.restype = ct.c_size_t
        self.lib.mc402_default_factor.argtypes = [ct.c_double]
        self.lib.mc402_default_factor.restype = ct.c_uint

    def render(
        self, x, rate, factor, gain=0.5, tone=0.5, output=1.0, boost=0.0, wire=False
    ):
        x = np.ascontiguousarray(x, dtype=np.float64)
        y = np.empty_like(x)
        handle = self.lib.mc402_create(rate, factor, gain, tone, output, boost)
        if not handle:
            raise RuntimeError("Invalid offline prepare")
        try:
            latency = self.lib.mc402_latency(handle)
            self.lib.mc402_process(
                handle, x.ctypes.data_as(P), y.ctypes.data_as(P), x.size, int(wire)
            )
        finally:
            self.lib.mc402_destroy(handle)
        assert np.all(np.isfinite(y))
        return y, latency


def db(value):
    return float(20 * np.log10(max(float(value), 1e-30)))


def coherent(frequency, rate):
    return int(round(frequency * N / rate)) | 1


def alias_sweep(bridge):
    rows = []
    bins = np.arange(N // 2 + 1)
    for rate in RATES:
        for factor in FACTORS:
            worst = dict(alias_dbc=-600.0)
            cases = 0
            for requested in FREQUENCIES:
                k = coherent(requested, rate)
                f = k * rate / N
                periods = math.ceil(WARMUP_SECONDS * rate / N) + 1
                phase = np.arange(N * periods) * (2 * np.pi * k / N)
                mask = (bins * rate / N >= 20) & (
                    bins * rate / N <= min(20000, rate / 2)
                )
                # Legitimate harmonics are the UNFOLDED multiples below Nyquist.
                # A high harmonic folded by sampling lands elsewhere since k is odd.
                mask[np.arange(1, (N // 2) // k + 1) * k] = False
                for amplitude in AMPLITUDES:
                    x = amplitude * np.sin(phase)
                    for gain in GAINS:
                        for tone in TONES:
                            y, latency = bridge.render(x, rate, factor, gain, tone)
                            spectrum = np.abs(np.fft.rfft(y[-N:])) * (2 / N)
                            masked = np.where(mask, spectrum, 0.0)
                            peak_bin = int(np.argmax(masked))
                            level = db(masked[peak_bin] / max(spectrum[k], 1e-30))
                            cases += 1
                            if level > worst["alias_dbc"]:
                                worst = dict(
                                    alias_dbc=level,
                                    input_hz=f,
                                    amplitude_volts=amplitude,
                                    gain=gain,
                                    tone=tone,
                                    output=1.0,
                                    boost_db=0.0,
                                    alias_hz=peak_bin * rate / N,
                                    fundamental_volts=float(spectrum[k]),
                                )
            row = dict(
                sample_rate=rate,
                factor=factor,
                cases=cases,
                latency_samples=latency,
                latency_ms=1000 * latency / rate,
                passes_alias=worst["alias_dbc"] <= ALIAS_GATE_DBC,
                **worst,
            )
            rows.append(row)
            print(
                f'alias {rate} Hz {factor}x: {worst["alias_dbc"]:.2f} dBc', flush=True
            )
    return rows


def fir_checks(bridge):
    banks = []
    for length, edge in [(65, 0.20), (33, 0.15)]:
        h = taps(length, edge)
        f, response = signal.freqz(h, worN=262144, fs=1)
        pass_error = float(np.max(np.abs(20 * np.log10(np.abs(response[f <= edge])))))
        stop = db(np.max(np.abs(response[f >= 0.5 - edge])))
        banks.append(
            dict(
                length=length,
                pass_edge=edge,
                stop_edge=0.5 - edge,
                pass_error_db=pass_error,
                stop_db=stop,
                passes=pass_error <= FIR_RIPPLE_LIMIT_DB and stop <= FIR_STOP_LIMIT_DB,
            )
        )
    rows = []
    for rate in RATES:
        for factor in FACTORS:
            impulse = np.zeros(N)
            impulse[0] = 1.0
            y, declared = bridge.render(impulse, rate, factor, wire=True)
            peak = int(np.argmax(np.abs(y)))
            f = np.fft.rfftfreq(N, 1 / rate)
            response = np.fft.rfft(y)
            mask = (f >= 20) & (f <= 10000)
            ripple = float(np.max(np.abs(20 * np.log10(np.abs(response[mask])))))
            # Round-trip FIR impulse must be even about the declared center.
            symmetry = float(
                np.max(np.abs(y[: 2 * declared + 1] - y[: 2 * declared + 1][::-1]))
            )
            rows.append(
                dict(
                    sample_rate=rate,
                    factor=factor,
                    declared_samples=declared,
                    measured_peak_samples=peak,
                    symmetry_error=symmetry,
                    ripple_db=ripple,
                    passes=peak == declared
                    and symmetry < 1e-12
                    and ripple <= FIR_RIPPLE_LIMIT_DB,
                )
            )
    return dict(banks=banks, round_trip=rows)


def analog_response(f, gain, tone, output):
    hp1 = 1 / (2 * math.pi * 22000 * 47e-9)
    s = 1j * np.asarray(f)
    return (
        (470 / 22) ** 2
        * gain**3.321928094887362
        * output**3.321928094887362
        * s
        / (s + hp1)
        * s
        / (s + 154)
        * (500 * 16**tone)
        / (s + 500 * 16**tone)
        * s
        / (s + 10)
    )


def response_checks(bridge):
    rows = []
    for rate in RATES:
        factor = bridge.lib.mc402_default_factor(rate)
        worst = 0.0
        for tone in [0.0, 0.5, 1.0]:
            # Small impulse stays below both knees; long enough for 10 Hz decay.
            impulse = np.zeros(32768)
            impulse[0] = 1e-5
            y, latency = bridge.render(impulse, rate, factor, gain=0.5, tone=tone)
            f = np.fft.rfftfreq(impulse.size, 1 / rate)
            measured = np.fft.rfft(y) / 1e-5
            selected = (f >= 80) & (f <= 8000)
            expected = analog_response(f[selected], 0.5, tone, 1.0)
            error = 20 * np.log10(np.abs(measured[selected]) / np.abs(expected))
            worst = max(worst, float(np.max(np.abs(error))))
        rows.append(
            dict(
                sample_rate=rate,
                factor=factor,
                max_magnitude_error_db=worst,
                passes=worst <= SMALL_SIGNAL_LIMIT_DB,
            )
        )
    return rows


def reference_checks(bridge, alias_rows):
    rows = []
    # Each rate's 8x worst case: same physical stimulus at directly evaluated
    # 16/32/64x core rates, without the production FIR; ideal band selection in FFT.
    # Compare complex audio-band spectra without labelling phase/BLT
    # differences as aliasing. Non-harmonic bins independently measure folding.
    for rate in RATES:
        worst = next(
            r for r in alias_rows if r["sample_rate"] == rate and r["factor"] == 8
        )
        k = coherent(worst["input_hz"], rate)
        spectra = {}
        core_alias = {}
        for factor in [16, 32, 64]:
            count = N * factor
            x = worst["amplitude_volts"] * np.sin(
                np.arange(count * (math.ceil(WARMUP_SECONDS * rate / N) + 1))
                * (2 * np.pi * k / count)
            )
            y, _ = bridge.render(x, rate * factor, 1, worst["gain"], worst["tone"])
            full = np.fft.rfft(y[-count:]) * (2 / count)
            band = np.arange(len(full)) * rate / N <= 20000
            legit = np.zeros(len(full), bool)
            legit[::k] = True
            core_alias[factor] = db(np.max(np.abs(full[band & ~legit])) / abs(full[k]))
            spectra[factor] = full[: int(20000 * N / rate) + 1]

        def residual(a, b):
            return db(
                np.linalg.norm(spectra[a] - spectra[b]) / np.linalg.norm(spectra[b])
            )

        comparisons = []
        count = N * (math.ceil(WARMUP_SECONDS * rate / N) + 1)
        host_input = worst["amplitude_volts"] * np.sin(
            np.arange(count) * (2 * np.pi * k / N)
        )
        for factor in [1, 2, 4, 8]:
            y, delay = bridge.render(
                host_input, rate, factor, worst["gain"], worst["tone"]
            )
            actual = np.fft.rfft(y[-N:]) * (2 / N)
            impulse = np.zeros(N)
            impulse[0] = 1.0
            wire, _ = bridge.render(impulse, rate, factor, wire=True)
            wire_response = np.fft.rfft(wire)
            # De-embed the measured COMPLEX wire response, including known L.
            # This is a linear correction, not an inverse of nonlinear input
            # filtering, and the direct reference itself remains unqualified.
            corrected = actual / np.where(
                np.abs(wire_response) > 1e-12, wire_response, 1.0
            )
            for reference_factor in [32, 64]:
                reference = spectra[reference_factor]
                band = np.arange(reference.size) * rate / N >= 20
                reference = reference[band]
                error = corrected[: band.size][band] - reference
                comparisons.append(
                    dict(
                        factor=factor,
                        reference_factor=reference_factor,
                        latency_samples=delay,
                        wire_compensated_residual_db=db(
                            np.linalg.norm(error) / np.linalg.norm(reference)
                        ),
                        interpretation="Linear resampler magnitude/phase removed; BLT and nonlinear input-filter interaction remain. Reference is not alias-qualified.",
                    )
                )

        rows.append(
            dict(
                sample_rate=rate,
                stimulus=worst,
                production_vs_direct_reference=comparisons,
                core_alias_dbc={str(k): v for k, v in core_alias.items()},
                residual_16_to_32_db=residual(16, 32),
                residual_32_to_64_db=residual(32, 64),
                reference_32_passes_alias=core_alias[32] <= REFERENCE_ALIAS_LIMIT_DBC,
                reference_64_passes_alias=core_alias[64] <= REFERENCE_ALIAS_LIMIT_DBC,
            )
        )
    return rows


def multitone_alias(bridge):
    rows = []
    for rate in RATES:
        # Common odd FFT-bin divisor makes intended intermodulation identifiable:
        # every unaliased product is an integer multiple of this base frequency.
        base = coherent(200, rate)
        bins = np.arange(N // 2 + 1)
        mask = (bins * rate / N >= 20) & (bins * rate / N <= 20000) & (bins % base != 0)
        periods = math.ceil(WARMUP_SECONDS * rate / N) + 1
        phase = np.arange(N * periods) * (2 * np.pi * base / N)
        for factor in FACTORS:
            worst = dict(alias_db_relative_total_rms=-600.0)
            for amplitude in [0.005, 0.12, 5.0]:
                x = amplitude * (np.sin(5 * phase) + np.sin(23 * phase))
                for gain in GAINS:
                    for tone in TONES:
                        y, _ = bridge.render(x, rate, factor, gain, tone)
                        spectrum_rms = np.abs(np.fft.rfft(y[-N:])) * (np.sqrt(2) / N)
                        masked = np.where(mask, spectrum_rms, 0.0)
                        peak = int(np.argmax(masked))
                        level = db(masked[peak] / np.sqrt(np.mean(y[-N:] ** 2)))
                        if level > worst["alias_db_relative_total_rms"]:
                            worst = dict(
                                alias_db_relative_total_rms=level,
                                input_hz=[5 * base * rate / N, 23 * base * rate / N],
                                each_tone_peak_volts=amplitude,
                                gain=gain,
                                tone=tone,
                                alias_hz=peak * rate / N,
                            )
            rows.append(
                dict(
                    sample_rate=rate,
                    factor=factor,
                    cases=18,
                    passes_alias=worst["alias_db_relative_total_rms"] <= ALIAS_GATE_DBC,
                    **worst,
                )
            )
    return rows


def multitone_and_sweep(bridge):
    rows = []
    # 997/5039 Hz approximated by coherent odd bins. In-band intermodulation
    # legitimately fills many bins, so report CONVERGENCE residual, not alias dBc.
    for rate in [44100, 48000]:
        time_axis = np.arange(N * 3) / rate
        k1, k2 = coherent(997, rate), coherent(5039, rate)
        x = 0.25 * (
            np.sin(2 * np.pi * k1 * rate / N * time_axis)
            + np.sin(2 * np.pi * k2 * rate / N * time_axis)
        )
        for stimulus, x in [
            ("two_tone", x),
            (
                "log_sweep",
                signal.chirp(time_axis, 100, time_axis[-1], 19000, method="logarithmic")
                * 0.5,
            ),
        ]:
            rendered = {}
            for factor in FACTORS:
                y, delay = bridge.render(x, rate, factor, 1, 1)
                rendered[factor] = y[delay:]
            common = min(map(len, rendered.values()))
            # Align by starting from the same post-delay sample, not array ends.
            reference = rendered[64][:common]
            start = N
            for factor in FACTORS[:-1]:
                actual = rendered[factor][:common]
                error = np.linalg.norm(
                    actual[start:] - reference[start:]
                ) / np.linalg.norm(reference[start:])
                rows.append(
                    dict(
                        sample_rate=rate,
                        stimulus=stimulus,
                        factor=factor,
                        residual_vs_64x_db=db(error),
                        interpretation="convergence, not isolated alias",
                    )
                )
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    start = time.perf_counter()
    bridge = Bridge(args.library)
    alias = alias_sweep(bridge)
    report = dict(
        profile="MC402-BOUNDED-V1-PROVISIONAL",
        method=dict(
            fft_samples=N,
            window="rectangular, coherent",
            minimum_warmup_seconds=WARMUP_SECONDS,
            warmup="ceil(0.5 * host_rate / 8192) whole periods",
            analysis_band_hz=[20, 20000],
            rates=RATES,
            factors=FACTORS,
            requested_sine_hz=FREQUENCIES,
            amplitude_volts=AMPLITUDES,
            gain=GAINS,
            tone=TONES,
            output=1.0,
            boost_db=0.0,
            note="Gate failure is retained; reference mismatch is not called aliasing.",
        ),
        alias=alias,
        fir=fir_checks(bridge),
        small_signal=response_checks(bridge),
        reference=reference_checks(bridge, alias),
        multitone_alias=multitone_alias(bridge),
        complex_stimuli=multitone_and_sweep(bridge),
    )
    report["elapsed_seconds"] = time.perf_counter() - start
    report["all_fir_checks_pass"] = all(
        r["passes"] for r in report["fir"]["banks"] + report["fir"]["round_trip"]
    )
    report["all_small_signal_checks_pass"] = all(
        r["passes"] for r in report["small_signal"]
    )
    report["passing_factors_by_rate"] = {
        str(rate): [
            r["factor"]
            for r in alias
            if r["sample_rate"] == rate
            and r["passes_alias"]
            and next(
                m["passes_alias"]
                for m in report["multitone_alias"]
                if m["sample_rate"] == rate and m["factor"] == r["factor"]
            )
        ]
        for rate in RATES
    }
    report["alias_gate_passed"] = all(report["passing_factors_by_rate"].values())
    report["selected_qualified_factors"] = None  # Never promote a failing factor.
    report["evaluated_default_factors"] = {
        str(rate): bridge.lib.mc402_default_factor(rate) for rate in RATES
    }
    Path(args.output).write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                k: report[k]
                for k in [
                    "elapsed_seconds",
                    "all_fir_checks_pass",
                    "all_small_signal_checks_pass",
                    "passing_factors_by_rate",
                ]
            },
            indent=2,
        )
    )

    return (
        0
        if (
            report["alias_gate_passed"]
            and report["all_fir_checks_pass"]
            and report["all_small_signal_checks_pass"]
        )
        else 1
    )


if __name__ == "__main__":
    sys.exit(main())
