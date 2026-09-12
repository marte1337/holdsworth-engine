"""Focused stage, reference and ADAA matrices. No production edits."""

import argparse
import gzip
import json
from pathlib import Path
import time
import numpy as np
from experiment import (
    Experiment,
    DEST,
    RATES,
    N,
    FREQUENCIES,
    AMPLITUDES,
    GAINS,
    TONES,
    coherent,
    db,
    metrics,
    write,
)
from continuous_reference import reference


def read_cases():
    return json.loads((DEST / "ordinary-cases.json").read_text())


def reduced_cases():
    rows = read_cases()
    selected = []
    for rate in RATES:
        for factor in [1, 8]:
            row = max(
                (r for r in rows if r["sample_rate"] == rate and r["factor"] == factor),
                key=lambda r: r["alias_dbc"],
            )
            if not any(
                all(
                    row[k] == s[k]
                    for k in ["sample_rate", "k", "amplitude", "gain", "tone"]
                )
                for s in selected
            ):
                selected.append(row)
    # Full-grid ADAA's worst 4x region is Gain 0.5 at this frequency, so
    # include it in the independent reference set as well as the ordinary worst.
    k = coherent(19000, 44100)
    selected.append(
        dict(
            sample_rate=44100,
            k=k,
            input_hz=k * 44100 / N,
            amplitude=10.0,
            gain=0.5,
            tone=0.0,
            output=1.0,
        )
    )
    for rate in [44100, 48000]:
        for hz, amp, gain in [(1000, 0.12, 0.5), (5000, 0.5, 1.0)]:
            k = coherent(hz, rate)
            selected.append(
                dict(
                    sample_rate=rate,
                    k=k,
                    input_hz=k * rate / N,
                    amplitude=amp,
                    gain=gain,
                    tone=1.0,
                    output=1.0,
                )
            )
    return selected


def localize(e):
    result = []
    for case in reduced_cases():
        rate = case["sample_rate"]
        k = case["k"]
        for factor in [1, 2, 4, 8]:
            for stages in [1, 2, 3]:
                for tone_on in [False, True]:
                    y = e.sine(
                        rate,
                        k,
                        case["amplitude"],
                        factor,
                        gain=case["gain"],
                        tone=case["tone"],
                        stages=stages,
                        tone_on=tone_on,
                    )
                    result.append(
                        dict(
                            case=case,
                            factor=factor,
                            active_saturators=stages,
                            tone_on=tone_on,
                            **metrics(y, rate, k)
                        )
                    )
    write("stage-localization.json", result)
    print("Stage localization complete", len(result), flush=True)


def references(e):
    result = []
    for case in reduced_cases():
        rate = case["sample_rate"]
        k = case["k"]
        q64 = reference(case, 64)
        q128 = reference(case, 128)
        error = db(
            np.linalg.norm(q64["coefficients"] - q128["coefficients"])
            / np.linalg.norm(q128["coefficients"])
        )
        assert error < -120, (case, error)
        ref = np.zeros(N // 2 + 1, complex)
        ref[q128["harmonic"] * k] = q128["coefficients"]
        band = (np.arange(ref.size) * rate / N >= 20) & (
            np.arange(ref.size) * rate / N <= 20000
        )
        variants = []
        spectra = {}
        # Ordinary convergence first; ADAA1 at the same high rates cross-checks
        # the independent event-split quadrature and is not assumed to be truth.
        for factor in [8, 16, 32, 64, 128, 256, 512]:
            if factor == 512:
                prior = next(v for v in variants if v["factor"] == 256 and v["s1"] == 0)
                if (
                    prior["alias_dbc"] <= -90
                    and prior["band_error_vs_continuous_db"] <= -90
                ):
                    break
            x = case["amplitude"] * np.sin(
                np.arange(N * factor) * (2 * np.pi * k / (N * factor))
            )
            for mode in [0, 1]:
                y = e.render(
                    x,
                    rate * factor,
                    gain=case["gain"],
                    tone=case["tone"],
                    s1=mode,
                    s2=mode,
                    warmup=int(np.ceil(0.5 * rate / N)),
                )
                full = np.fft.rfft(y) * 2 / y.size
                # Two ADAA1 stages add one internal sample of linear delay.
                spectrum = full[: ref.size] * np.exp(
                    2j * np.pi * np.arange(ref.size) * mode / (N * factor)
                )
                spectra[factor, mode] = spectrum
                variants.append(
                    dict(
                        factor=factor,
                        s1=mode,
                        s2=mode,
                        **metrics(y, rate, k),
                        band_error_vs_continuous_db=db(
                            np.linalg.norm((spectrum - ref)[band])
                            / np.linalg.norm(ref[band])
                        )
                    )
                )
        for row in variants:
            factor = row["factor"]
            mode = row["s1"]
            if factor >= 16:
                row["successive_band_difference_db"] = db(
                    np.linalg.norm(
                        (spectra[factor, mode] - spectra[factor // 2, mode])[band]
                    )
                    / np.linalg.norm(ref[band])
                )
        result.append(
            dict(
                case=case,
                quadrature_64_to_128_db=error,
                periodic_state_error=q128["periodic_state_error"],
                roots=q128["second_stage_knee_roots"],
                reference_harmonics=q128["harmonic"].tolist(),
                reference_real=q128["coefficients"].real.tolist(),
                reference_imag=q128["coefficients"].imag.tolist(),
                variants=variants,
            )
        )
        write("reference-convergence.json", result)
        print(
            "reference",
            rate,
            case["input_hz"],
            error,
            "last_factor",
            variants[-2]["factor"],
            "ordinary_error",
            variants[-2]["band_error_vs_continuous_db"],
            "ADAA_error",
            variants[-1]["band_error_vs_continuous_db"],
            flush=True,
        )
    return result


def adaa(e):
    rows = []
    for mode1, mode2 in [(1, 0), (0, 1), (1, 1), (2, 0), (0, 2), (2, 2)]:
        for rate in RATES:
            for factor in [1, 2, 4]:
                subset = []
                for requested in FREQUENCIES:
                    k = coherent(requested, rate)
                    for amplitude in AMPLITUDES:
                        for gain in GAINS:
                            for tone in TONES:
                                y = e.sine(
                                    rate,
                                    k,
                                    amplitude,
                                    factor,
                                    gain=gain,
                                    tone=tone,
                                    s1=mode1,
                                    s2=mode2,
                                )
                                subset.append(
                                    dict(
                                        sample_rate=rate,
                                        factor=factor,
                                        s1=mode1,
                                        s2=mode2,
                                        k=k,
                                        input_hz=k * rate / N,
                                        amplitude=amplitude,
                                        gain=gain,
                                        tone=tone,
                                        output=1.0,
                                        **metrics(y, rate, k)
                                    )
                                )
                rows.extend(subset)
                print(
                    "ADAA",
                    mode1,
                    mode2,
                    rate,
                    factor,
                    max(r["alias_dbc"] for r in subset),
                    flush=True,
                )
    payload = json.dumps(rows, separators=(",", ":")).encode()
    (DEST / "adaa-cases.json.gz").write_bytes(gzip.compress(payload, mtime=0))
    summary = []
    for mode1, mode2 in [(1, 0), (0, 1), (1, 1), (2, 0), (0, 2), (2, 2)]:
        for rate in RATES:
            for factor in [1, 2, 4]:
                subset = [
                    r
                    for r in rows
                    if r["s1"] == mode1
                    and r["s2"] == mode2
                    and r["sample_rate"] == rate
                    and r["factor"] == factor
                ]
                summary.extend(
                    sorted(subset, key=lambda r: r["alias_dbc"], reverse=True)[:3]
                )
    write("adaa-worst-cases.json", summary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["localize", "references", "adaa"])
    args = parser.parse_args()
    e = Experiment()
    {"localize": localize, "references": references, "adaa": adaa}[args.command](e)


if __name__ == "__main__":
    main()
