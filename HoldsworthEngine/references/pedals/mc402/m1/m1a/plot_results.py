"""Standalone scientific figure from the recorded M1a experiments."""

import json
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from experiment import DEST, Experiment, N


def main():
    summary = json.loads((DEST / "summary.json").read_text())
    reference = json.loads((DEST / "reference-convergence.json").read_text())[0]
    fig, axes = plt.subplots(2, 2, figsize=(13, 9), layout="constrained")
    case = summary["positive_dbc_diagnosis"][0]["case"]
    e = Experiment()
    for tone_on, label in [
        (False, "Tone bypassed (diagnosis)"),
        (True, "Frozen Tone 0"),
    ]:
        y = e.sine(44100, case["k"], 10.0, gain=1.0, tone=0.0, tone_on=tone_on)
        level = 20 * np.log10(np.maximum(np.abs(np.fft.rfft(y)) * 2 / N, 1e-15))
        hz = np.fft.rfftfreq(N, 1 / 44100)
        axes[0, 0].plot(hz, level, label=label, linewidth=0.8)
    axes[0, 0].scatter(
        [case["alias_hz"], case["input_hz"]],
        [case["alias_dbfs"], case["fundamental_dbfs"]],
        color="black",
        zorder=5,
    )
    axes[0, 0].set(
        xlim=(20, 20000),
        ylim=(-100, 15),
        xlabel="Frequency (Hz)",
        ylabel="Peak-sinusoid dBFS",
        title="44.1 kHz / 1x: real +20.59 dBc alias",
    )
    rates = [44100, 48000, 88200, 96000, 176400, 192000]
    for mode, label in [
        (0, "Ordinary 4x"),
        (1, "ADAA1 both stages / 4x"),
        (2, "ADAA2 both stages / 4x"),
    ]:
        rows = [
            r
            for r in summary["mandatory_matrix"]
            if r["mode"] == mode and r["factor"] == 4
        ]
        axes[0, 1].plot(
            np.arange(6), [r["worst"]["alias_dbc"] for r in rows], "o-", label=label
        )
    axes[0, 1].axhline(-70, color="black", linestyle="--", label="Unchanged gate")
    axes[0, 1].set(
        xticks=np.arange(6),
        xticklabels=[str(r / 1000) for r in rates],
        xlabel="Host rate (kHz)",
        ylabel="Worst alias (dBc)",
        title="Full stress grid: every candidate fails",
    )
    for mode, label in [
        (0, "Ordinary direct sampling"),
        (1, "ADAA1 direct sampling, delay aligned"),
    ]:
        rows = [r for r in reference["variants"] if r["s1"] == mode]
        axes[1, 0].semilogx(
            [r["factor"] for r in rows],
            [r["band_error_vs_continuous_db"] for r in rows],
            "o-",
            base=2,
            label=label,
        )
    axes[1, 0].axhline(-90, color="black", linestyle="--", label="Reference margin")
    axes[1, 0].set(
        xlabel="Offline factor",
        ylabel="Audio-band error vs continuous reference (dB)",
        title="Independent event-split quadrature reference",
    )
    hz = np.linspace(80, 8000, 500)
    for factor in [1, 2, 4, 8]:
        omega = 2 * np.pi * hz / (44100 * factor)
        kernel = ((1 + 2 * np.cos(omega)) / 3) ** 2
        axes[1, 1].plot(
            hz,
            20 * np.log10(np.maximum(abs(kernel), 1e-12)),
            label=f"ADAA2 / {factor}x",
        )
    axes[1, 1].set(
        xlabel="Frequency (Hz)",
        ylabel="Additional linear magnitude change (dB)",
        title="Unchanged static curve still adds dynamic filtering",
    )
    for ax in axes.flat:
        ax.grid(alpha=0.2)
        ax.legend(fontsize=8)
    fig.suptitle(
        "MC402 M1a — offline diagnosis only; frozen production transfer unchanged"
    )
    fig.savefig(DEST / "diagnosis.png", dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    main()
