"""Render standalone figures from recorded numerical data; no DSP tuning."""

import argparse
import json
from pathlib import Path
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", required=True)
    args = parser.parse_args()
    directory = Path(args.directory)
    behavior = json.loads((directory / "behavior-report.json").read_text())
    numerical = json.loads((directory / "numerical-report.json").read_text())
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.8), layout="constrained")
    for tone in [0.0, 0.5, 1.0]:
        rows = [
            r
            for r in behavior["small_signal_points"]
            if r["sample_rate"] == 48000 and r["tone"] == tone
        ]
        axes[0].semilogx(
            [r["frequency_hz"] for r in rows],
            [r["measured_db"] for r in rows],
            "o-",
            label=f"Tone {tone:g}",
        )
    axes[0].set(
        xlabel="Frequency (Hz)",
        ylabel="Small-signal gain (dB)",
        title="48 kHz, Gain 0.5, Output 1",
    )
    for gain in [0.1, 0.5, 1.0]:
        rows = [
            r for r in behavior["level_sweep_48k_tone1_output1"] if r["gain"] == gain
        ]
        axes[1].loglog(
            [r["input_peak_volts"] for r in rows],
            [r["output_rms_volts"] for r in rows],
            label=f"Gain {gain:g}",
        )
    axes[1].set(
        xlabel="Input peak (provisional V)",
        ylabel="Output RMS (provisional V)",
        title="1 kHz, 48 kHz, Tone / Output 1",
    )
    for rate in numerical["method"]["rates"]:
        rows = [r for r in numerical["alias"] if r["sample_rate"] == rate]
        axes[2].semilogx(
            [r["factor"] for r in rows],
            [r["alias_dbc"] for r in rows],
            "o-",
            base=2,
            label=f"{rate / 1000:g} kHz",
        )
    axes[2].axhline(-70, color="black", linestyle="--", label="Acceptance gate")
    axes[2].set(
        xlabel="Oversampling factor",
        ylabel="Worst single-tone alias (dBc)",
        title="No tested factor passes full stress grid",
        xticks=[1, 2, 4, 8, 16, 32, 64],
    )
    for ax in axes:
        ax.grid(True, which="both", alpha=0.2)
        ax.legend(fontsize=8)
    fig.suptitle(
        "MC402-BOUNDED-V1-PROVISIONAL — isolated measurements, not hardware calibration",
        fontsize=12,
    )
    fig.savefig(directory / "measurements.png", dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    main()
