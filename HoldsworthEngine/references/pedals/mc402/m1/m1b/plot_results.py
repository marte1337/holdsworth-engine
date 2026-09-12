"""Static scientific figure: measured guitar-band maxima, no sound changes."""

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from evaluate import P, DEST, all_rows

rows = all_rows("product") + all_rows("dense")
fig, axes = plt.subplots(2, 2, figsize=(12, 7.4), sharex=True)
colors = ["#6b7280", "#d97706", "#2563eb"]
for col, rate in enumerate([44100, 48000]):
    subset = [
        r
        for r in rows
        if r["sample_rate"] == rate
        and 2000 <= r["input_hz"] <= 8000
        and 0.25 <= r["amplitude"] <= 2
    ]
    for (method, _, _), color in zip(P["methods"], colors):
        group = [r for r in subset if r["method"] == method]
        frequencies = sorted(set(r["input_hz"] for r in group))
        for ax, metric in [
            (axes[0, col], "alias_dbc"),
            (axes[1, col], "total_alias_dbfs_sine_equivalent"),
        ]:
            values = [
                max(r[metric] for r in group if r["input_hz"] == f) for f in frequencies
            ]
            ax.plot(
                np.array(frequencies) / 1000,
                values,
                color=color,
                lw=1.3,
                marker=".",
                ms=3,
                label={
                    "ordinary4": "Ordinary 4x",
                    "adaa2_4": "ADAA2 4x",
                    "adaa2_8": "ADAA2 8x",
                }[method],
            )
            ax.grid(alpha=0.18)
    axes[0, col].axhline(-70, color="black", ls="--", lw=1)
    axes[1, col].axhline(-80, color="black", ls="--", lw=1)
    axes[0, col].set_title(f"{rate / 1000:g} kHz host")
    axes[1, col].set_xlabel("Input sine frequency (kHz)")
    axes[0, col].set_ylim(-115, 0)
    axes[1, col].set_ylim(-105, 5)
axes[0, 0].set_ylabel("Strongest identified alias (dBc)")
axes[1, 0].set_ylabel("Total identified alias (dBFS, sine-equivalent)")
axes[0, 0].legend(loc="lower left", fontsize=9)
fig.suptitle("MC402 M1b: musical challenge subset, 0.25–2 V peak", fontsize=16)
fig.text(
    0.5,
    0.015,
    "Maximum over measured amplitudes/Gain/Tone at each frequency. Output 1, Boost off. "
    "Dashed: proposed fidelity limits.\n"
    "Finite grids; unseparated aliases on intended harmonics are excluded. "
    "MC402-BOUNDED-V1-PROVISIONAL remains unchanged.",
    ha="center",
    fontsize=9,
)
fig.tight_layout(rect=(0, 0.065, 1, 0.95))
fig.savefig(DEST / "product-aliasing.png", dpi=180)
