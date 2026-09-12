"""Isolated old/R2 behavior, continuous-time reference and witness comparisons."""

import argparse
import importlib.util
import math
import sys

import numpy as np
from scipy.optimize import minimize_scalar
from scipy.io import wavfile

from evaluate import (
    DEST,
    OLD,
    N,
    P,
    METHODS,
    Periodic,
    read,
    write,
    measure,
    bin_for,
    db,
    EXPONENT,
)
from checks import saturation


def reference_module(profile):
    spec = importlib.util.spec_from_file_location(
        "reference_" + profile, DEST.parent / "m1a" / "continuous_reference.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if profile == "r2":
        module.saturation = saturation
    return module


def static():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from scipy.integrate import quad

    old, new = reference_module("v1"), reference_module("r2")
    q = np.linspace(0, 1.3, 13001)
    old_y = old.saturation(2.5 * q) / 2.5
    new_y = saturation(2.5 * q) / 2.5
    t = np.clip((q - 0.9) / 0.2, 0, 1)
    old_d = 1 - t
    new_d = 1 - 3 * t * t + 2 * t**3

    def difference_db(t):
        original = 0.9 + 0.2 * t - 0.1 * t * t
        candidate = 0.9 + 0.2 * t - 0.2 * t**3 + 0.1 * t**4
        return 20 * np.log10(candidate / original)

    maximum = minimize_scalar(
        lambda t: -difference_db(t), bounds=(0, 1), method="bounded"
    )
    result = dict(
        profile="MC402-BOUNDED-V1-PROVISIONAL-R2",
        normalized_knee_coefficients_in_t=[0.9, 0.2, 0.0, -0.2, 0.1, 0.0],
        max_value_difference_normalized=0.00625,
        max_value_difference_volts=0.015625,
        maximum_at_q=1.0,
        old_at_q1_volts=2.4375,
        r2_at_q1_volts=2.453125,
        max_derivative_difference=math.sqrt(3) / 18,
        max_local_level_increase_db=-maximum.fun,
        max_local_level_increase_q=0.9 + 0.2 * maximum.x,
        exactly_unchanged_regions="abs(q)<=0.9 and abs(q)>=1.1",
    )
    # Exact interval-integrated single-stage harmonic amplitudes (no aliasing).
    stage = []
    for drive in [0.5, 0.9, 0.95, 1.0, 1.05, 1.1, 1.5, 2.0, 5.0, 10.0, 40.0]:
        boundaries = [0.0, np.pi / 2, np.pi]
        for threshold in [0.9, 1.1]:
            if drive > threshold:
                b = np.arcsin(threshold / drive)
                boundaries += [b, np.pi - b]
        boundaries = sorted(set(boundaries))
        coeff = []
        for shape in [old.saturation, saturation]:
            harmonics = []
            for h in range(1, 32, 2):
                v = (
                    sum(
                        quad(
                            lambda theta: float(shape(2.5 * drive * np.sin(theta)))
                            * np.sin(h * theta),
                            lo,
                            hi,
                            epsabs=1e-12,
                            epsrel=1e-12,
                        )[0]
                        for lo, hi in zip(boundaries[:-1], boundaries[1:])
                    )
                    * 2
                    / np.pi
                )
                harmonics.append(v)
            coeff.append(np.array(harmonics))
        stage.append(
            dict(
                normalized_drive_peak=drive,
                harmonic=list(range(1, 32, 2)),
                old_peak_volts=coeff[0].tolist(),
                r2_peak_volts=coeff[1].tolist(),
                old_dbfs=[db(abs(v)) for v in coeff[0]],
                r2_dbfs=[db(abs(v)) for v in coeff[1]],
                fundamental_difference_db=db(abs(coeff[1][0] / coeff[0][0])),
            )
        )
    result["single_stage_spectra"] = stage
    # Full two-stage continuous-time level/compression curves. Exactly same
    # filters/controls and converged integration, so no ADAA or FIR confound here.
    levels = []
    for f in [100.0, 1000.0, 5000.0, 8000.0]:
        for gain in [0.25, 0.5, 1.0]:
            for amplitude in sorted(
                set(
                    np.geomspace(0.005, 4.5, 31).tolist() + [0.125, 0.25, 0.5, 1.0, 2.0]
                )
            ):
                case = dict(
                    input_hz=f, amplitude=amplitude, gain=gain, tone=1.0, output=1.0
                )
                order = max(64, int(math.ceil(40000 / f)))
                co = old.reference(case, order)["coefficients"]
                cn = new.reference(case, order)["coefficients"]
                for tone in [0.0, 0.5, 1.0]:
                    hz = np.arange(1, co.size + 1) * f
                    ratio = (500 * 16**tone / (500 * 16**tone + 1j * hz)) / (
                        8000 / (8000 + 1j * hz)
                    )
                    o, n = co * ratio, cn * ratio
                    levels.append(
                        dict(
                            **dict(case, tone=tone),
                            old_fundamental_dbfs=db(abs(o[0])),
                            r2_fundamental_dbfs=db(abs(n[0])),
                            fundamental_difference_db=db(abs(n[0] / o[0])),
                            old_inband_rms_volts=float(np.linalg.norm(o) / np.sqrt(2)),
                            r2_inband_rms_volts=float(np.linalg.norm(n) / np.sqrt(2)),
                            rms_difference_db=db(np.linalg.norm(n) / np.linalg.norm(o)),
                            waveform_difference_dbfs_sine_equivalent=db(
                                np.linalg.norm(n - o)
                            ),
                            waveform_difference_relative_db=db(
                                np.linalg.norm(n - o) / np.linalg.norm(o)
                            ),
                            old_thd_db=db(np.linalg.norm(o[1:]) / abs(o[0])),
                            r2_thd_db=db(np.linalg.norm(n[1:]) / abs(n[0])),
                        )
                    )
    write("level-sweeps.json.gz", levels)
    result["level_sweep_count"] = len(levels)
    result["max_abs_fundamental_level_difference"] = max(
        levels, key=lambda r: abs(r["fundamental_difference_db"])
    )
    result["max_abs_rms_level_difference"] = max(
        levels, key=lambda r: abs(r["rms_difference_db"])
    )
    result["max_relative_waveform_difference"] = max(
        levels, key=lambda r: r["waveform_difference_relative_db"]
    )
    # Same-method isolated host renders, retained as raw provisional-volts data.
    renders = []
    for i, (frequency, amplitude, gain, tone) in enumerate(
        [
            (997, 0.02, 0.5, 0.5),
            (997, 0.125, 0.5, 1.0),
            (997, 0.5, 1.0, 1.0),
            (5000, 0.5, 1.0, 0.0),
            (5000, 0.5, 1.0, 1.0),
            (7250, 2.0, 0.5, 0.0),
        ]
    ):
        rate = 48000
        k = bin_for(frequency, rate)
        waves = []
        for profile in ["v1", "r2"]:
            c = Periodic(rate, 8, 2, "fir", profile).render_coefficients(
                k, amplitude, gain, tone
            )
            waves.append(np.fft.irfft(c * N / 2, N))
        path = DEST / f"renders/isolated-{i+1}.npz"
        path.parent.mkdir(exist_ok=True)
        np.savez_compressed(
            path, old_volts=waves[0], r2_volts=waves[1], sample_rate=rate
        )
        wavfile.write(
            str(path.with_suffix(".wav")), rate, np.array(waves, dtype=np.float32).T
        )
        renders.append(
            dict(
                file=str(path.relative_to(DEST)),
                wav_channels=["V1", "R2"],
                units="unnormalized provisional volts; floating WAV may exceed unity",
                input_hz=k * rate / N,
                amplitude=amplitude,
                gain=gain,
                tone=tone,
                output=1.0,
                method="adaa2_8",
                resampler="fir",
                max_sample_difference_volts=float(np.max(abs(waves[1] - waves[0]))),
                rms_difference_relative_db=db(
                    np.linalg.norm(waves[1] - waves[0]) / np.linalg.norm(waves[0])
                ),
            )
        )
    result["isolated_renders"] = renders
    write("static-comparison.json", result)
    fig, ax = plt.subplots(2, 2, figsize=(11, 8), constrained_layout=True)
    ax[0, 0].plot(q, old_y, label="V1 quadratic")
    ax[0, 0].plot(q, new_y, label="R2 quartic")
    ax[0, 0].set(
        xlim=(0.85, 1.15),
        ylim=(0.85, 1.02),
        title="Normalized static transfer",
        xlabel="|input| / H",
        ylabel="|output| / H",
    )
    ax[0, 1].plot(q, old_d)
    ax[0, 1].plot(q, new_d)
    ax[0, 1].set(
        xlim=(0.85, 1.15),
        title="First derivative",
        xlabel="|input| / H",
        ylabel="slope",
    )
    ax[1, 0].plot(q, (new_y - old_y) * 2500)
    ax[1, 0].set(
        xlim=(0.85, 1.15),
        title="Local voltage difference, H = 2.5 V",
        xlabel="|input| / H",
        ylabel="R2 − V1 (mV)",
    )
    for profile, style in [("old", "-"), ("r2", "--")]:
        data = [
            r
            for r in levels
            if r["input_hz"] == 1000 and r["gain"] == 0.5 and r["tone"] == 1
        ]
        ax[1, 1].semilogx(
            [r["amplitude"] for r in data],
            [r[f"{profile}_fundamental_dbfs"] for r in data],
            style,
            label=profile,
        )
    ax[1, 1].set(
        title="Isolated cascade: 1 kHz, Gain .5, Tone 1",
        xlabel="Input peak (provisional V)",
        ylabel="Fundamental (provisional dBFS)",
    )
    for a in ax.flat:
        a.grid(alpha=0.25)
    ax[0, 0].legend()
    ax[1, 1].legend()
    fig.savefig(DEST / "static-comparison.png", dpi=160)
    plt.close(fig)
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.7), constrained_layout=True)
    for axis, drive in zip(axes, [0.95, 1.1, 5.0]):
        row = next(r for r in stage if r["normalized_drive_peak"] == drive)
        for profile in ["old", "r2"]:
            axis.plot(row["harmonic"], row[profile + "_dbfs"], "o-", label=profile)
        axis.set(
            title=f"Stage input peak = {drive} H",
            xlabel="Harmonic",
            ylabel="dBFS, 1 V peak = 0",
            ylim=(-140, 12),
        )
        axis.grid(alpha=0.25)
    axes[0].legend()
    fig.savefig(DEST / "harmonics.png", dpi=160)
    plt.close(fig)
    print(
        "static",
        len(levels),
        result["max_abs_fundamental_level_difference"],
        flush=True,
    )


def witnesses():
    rows = []
    cases = {}

    def add(case, label):
        key = tuple(case[f] for f in ["sample_rate", "k", "amplitude", "gain", "tone"])
        cases.setdefault(key, set()).add(label)

    for rate in P["sample_rates"]:
        for kind in ["product", "dense"]:
            for name in ["ordinary4", "adaa2_4", "adaa2_8"]:
                source = read(OLD / f"{kind}-{rate}-{name}.json.gz")
                add(
                    max(source, key=lambda r: r["alias_dbc"]),
                    "M1b " + kind + " " + name + " worst",
                )
        for tone in [0.0, 1.0]:
            add(
                dict(
                    sample_rate=rate,
                    k=bin_for(5000, rate),
                    amplitude=0.5,
                    gain=1.0,
                    tone=tone,
                ),
                "requested 5 kHz / 0.5 V",
            )
    for rate, f, amplitude, gain, tone in [
        (44100, 4871.88720703125, 0.5, 1.0, 1.0),
        (48000, 7248.046875, 0.5, 0.6, 0.0),
        (48000, 6626.953125, 2.0, 0.5, 0.0),
        (44100, 7498.93798828125, 4.5, 0.5, 0.0),
    ]:
        add(
            dict(
                sample_rate=rate,
                k=bin_for(f, rate),
                amplitude=amplitude,
                gain=gain,
                tone=tone,
            ),
            "M1b explicit witness",
        )
    for key, labels in cases.items():
        rate, k, amplitude, gain, tone = key
        case = dict(
            sample_rate=rate,
            k=k,
            fft_length=N,
            input_hz=k * rate / N,
            amplitude=amplitude,
            gain=gain,
            tone=tone,
            output=1.0,
        )
        for name, factor, mode in METHODS:
            for sampler in ["fir", "ideal"]:
                for profile in ["v1", "r2"]:
                    c = Periodic(
                        rate, factor, mode, sampler, profile
                    ).render_coefficients(k, amplitude, gain, tone)
                    rows.append(
                        dict(
                            **case,
                            labels=sorted(labels),
                            profile=profile,
                            method=name,
                            factor=factor,
                            mode=mode,
                            resampler=sampler,
                            **measure(c, rate, k),
                        )
                    )
    write("previous-witnesses.json.gz", rows)
    print("previous witnesses", len(cases), len(rows), flush=True)


def references():
    summary = read(DEST / "summary.json")
    cases = {}
    for group in summary:
        if group["kind"] != "product":
            continue
        for key in ["worst_relative", "worst_absolute", "worst_musical"]:
            for r in group[key][:1]:
                ident = tuple(
                    r[field]
                    for field in ["sample_rate", "k", "amplitude", "gain", "tone"]
                )
                cases[ident] = r
    for r in read(DEST / "previous-witnesses.json.gz"):
        if r["profile"] == "r2" and (
            "requested 5 kHz / 0.5 V" in r["labels"]
            or "M1b explicit witness" in r["labels"]
        ):
            ident = tuple(
                r[field] for field in ["sample_rate", "k", "amplitude", "gain", "tone"]
            )
            cases[ident] = r
    ref = reference_module("r2")
    rows = []
    for r in cases.values():
        case = {
            k: r[k]
            for k in [
                "sample_rate",
                "k",
                "fft_length",
                "input_hz",
                "amplitude",
                "gain",
                "tone",
                "output",
            ]
        }
        order = max(64, int(math.ceil(40000 / r["input_hz"])))
        low, high = ref.reference(case, order), ref.reference(case, order * 2)
        change = float(np.max(abs(high["coefficients"] - low["coefficients"])))
        assert change < 1e-9, change
        comparisons = []
        for name, factor, mode in METHODS:
            for sampler in ["fir", "ideal"]:
                c = Periodic(
                    r["sample_rate"], factor, mode, sampler
                ).render_coefficients(r["k"], r["amplitude"], r["gain"], r["tone"])
                delay = (
                    {4: 40.0, 8: 44.0}[factor] if sampler == "fir" else 0.0
                ) + mode / factor
                intended = c[high["harmonic"] * r["k"]] * np.exp(
                    2j * np.pi * high["harmonic"] * r["k"] * delay / N
                )
                reference = high["coefficients"]
                comparisons.append(
                    dict(
                        method=name,
                        resampler=sampler,
                        delay_removed_host_samples=delay,
                        fundamental_magnitude_error_db=db(
                            abs(intended[0] / reference[0])
                        ),
                        intended_harmonic_residual_dbfs_sine_equivalent=db(
                            np.linalg.norm(intended - reference)
                        ),
                        **measure(c, r["sample_rate"], r["k"]),
                    )
                )
        rows.append(
            dict(
                **case,
                orders=[order, order * 2],
                max_coefficient_change_volts=change,
                convergence_dbfs=db(change),
                periodic_state_error_volts=high["periodic_state_error"],
                intervals=high["intervals"],
                s2_knee_roots=high["second_stage_knee_roots"],
                harmonic=high["harmonic"].tolist(),
                reference_real=high["coefficients"].real.tolist(),
                reference_imag=high["coefficients"].imag.tolist(),
                comparisons=comparisons,
            )
        )
    write("continuous-reference-checks.json", rows)
    print(
        "CT references",
        len(rows),
        "max coefficient delta",
        max(r["max_coefficient_change_volts"] for r in rows),
        flush=True,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["static", "witnesses", "references"])
    args = parser.parse_args()
    globals()[args.action]()
