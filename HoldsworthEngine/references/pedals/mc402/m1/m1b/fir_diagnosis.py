"""Attribute the bright-Tone 5th-harmonic fold using actual frozen FIR taps."""

import math
import re
import numpy as np
from evaluate import DEST, N, Experiment, bin_for, render, measure, db, write


def main():
    header = (DEST.parents[4] / "dsp" / "MC402HalfBandCoefficients.h").read_text()
    arrays = {}
    for size in [33, 65]:
        body = re.search(
            r"kHalfBand" + str(size) + r"\s*\{([^}]+)\}", header, re.S
        ).group(1)
        arrays[size] = np.array(
            [float(s.strip()) for s in body.split(",") if s.strip()]
        )
        assert len(arrays[size]) == size
    rows = []
    e = Experiment()
    for frequency in [4871.88720703125, 5001.08642578125]:
        rate, factor = 44100, 8
        k = bin_for(frequency, rate)
        frequency = k * rate / N
        x = 0.5 * np.sin(np.arange(N * factor) * (2 * np.pi * k / (N * factor)))
        y = e.render(
            x,
            rate * factor,
            gain=1.0,
            tone=1.0,
            s1=2,
            s2=2,
            warmup=math.ceil(0.5 * rate / N),
        )
        c = np.fft.rfft(y) * 2 / y.size
        fifth_hz = 5 * frequency
        down = 1.0 + 0j
        stages = []
        for index, size in enumerate([65, 33, 33]):
            stage_rate = rate * 2 ** (index + 1)
            h = np.sum(
                arrays[size]
                * np.exp(-2j * np.pi * fifth_hz * np.arange(size) / stage_rate)
            )
            down *= h
            stages.append(
                dict(
                    length=size,
                    stage_rate=stage_rate,
                    fifth_harmonic_attenuation_db=db(abs(h)),
                )
            )
        actual = measure(render(e, rate, k, 0.5, factor, 2, 1.0, 1.0), rate, k)
        rows.append(
            dict(
                sample_rate=rate,
                input_hz=frequency,
                amplitude=0.5,
                gain=1.0,
                tone=1.0,
                output=1.0,
                factor=factor,
                fifth_harmonic_hz=fifth_hz,
                folded_fifth_hz=rate - fifth_hz,
                ideal_input_core_fifth_dbfs=db(abs(c[5 * k])),
                downsampler_attenuation_db=db(abs(down)),
                predicted_fifth_fold_dbfs=db(abs(c[5 * k] * down)),
                actual=actual,
                stages=stages,
                ideal_resampler_diagnostic=measure(y, rate * factor, k),
            )
        )
    write("fir-diagnosis.json", rows)
    print("Frozen FIR transition-band fifth-harmonic fold measured")


if __name__ == "__main__":
    main()
