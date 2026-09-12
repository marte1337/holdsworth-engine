"""Full-cascade harmonic comparisons from the retained isolated old/R2 renders."""

import numpy as np
from evaluate import DEST, read, write, db


def main():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    metadata = read(DEST / "static-comparison.json")["isolated_renders"]
    rows = []
    for row in metadata:
        data = np.load(DEST / row["file"])
        old, new = data["old_volts"], data["r2_volts"]
        n, rate = old.size, float(data["sample_rate"])
        k = round(row["input_hz"] * n / rate)
        harmonics = np.arange(1, int(20000 / row["input_hz"]) + 1)
        spectra = [np.fft.rfft(y) * 2 / n for y in [old, new]]
        rows.append(
            dict(
                **row,
                harmonic=harmonics.tolist(),
                old_harmonic_dbfs=[db(abs(v)) for v in spectra[0][harmonics * k]],
                r2_harmonic_dbfs=[db(abs(v)) for v in spectra[1][harmonics * k]],
                harmonic_difference_dbfs=[
                    db(abs(v)) for v in (spectra[1] - spectra[0])[harmonics * k]
                ],
            )
        )
    write("render-spectra.json", rows)
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.8), constrained_layout=True)
    for axis, row in zip(axes, rows[:3]):
        for name in ["old", "r2"]:
            axis.plot(
                row["harmonic"],
                np.maximum(row[name + "_harmonic_dbfs"], -160),
                "o-",
                label=name,
            )
        axis.set(
            title=f'{row["amplitude"]} V peak, Gain {row["gain"]}, Tone {row["tone"]}',
            xlabel="Intended harmonic",
            ylabel="Provisional dBFS",
            ylim=(-165, 15),
        )
        axis.grid(alpha=0.25)
    axes[0].legend()
    fig.suptitle(
        "Isolated cascade, 1001.953125 Hz / 48 kHz, ADAA2 8x FIR\nDisplay floor −160 dBFS; raw numerical values retained"
    )
    fig.savefig(DEST / "render-spectra.png", dpi=160)
    plt.close(fig)
    print("Full-cascade spectra from six retained matched renders")


if __name__ == "__main__":
    main()
