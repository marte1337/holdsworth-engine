"""Curves and endpoint measurements from the isolated compiled provisional DSP."""

import argparse
import json
from pathlib import Path
import numpy as np
from validate_numerics import Bridge, RATES, analog_response


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    bridge = Bridge(args.library)
    response, endpoints, levels = [], [], []
    for rate in RATES:
        factor = bridge.lib.mc402_default_factor(rate)
        count = int(rate)
        phase = np.arange(count) * (2 * np.pi * 1000 / rate)
        for gain in [0.0, 0.1, 0.5, 1.0]:
            for tone in [0.0, 0.5, 1.0]:
                for output in [0.0, 0.5, 1.0]:
                    y, _ = bridge.render(
                        0.12 * np.sin(phase), rate, factor, gain, tone, output
                    )
                    steady = y[count // 2 :]
                    endpoints.append(
                        dict(
                            sample_rate=rate,
                            gain=gain,
                            tone=tone,
                            output=output,
                            input_hz=1000.0,
                            input_peak_volts=0.12,
                            rms_volts=float(np.sqrt(np.mean(steady**2))),
                            peak_volts=float(np.max(np.abs(steady))),
                        )
                    )
        impulse = np.zeros(32768)
        impulse[0] = 1e-5
        for tone in [0.0, 0.5, 1.0]:
            y, _ = bridge.render(impulse, rate, factor, 0.5, tone, 1.0)
            f = np.fft.rfftfreq(len(y), 1 / rate)
            spectrum = np.fft.rfft(y) / 1e-5
            for requested in [80.0, 154.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0]:
                index = np.argmin(np.abs(f - requested))
                expected = analog_response(f[index], 0.5, tone, 1.0)
                response.append(
                    dict(
                        sample_rate=rate,
                        tone=tone,
                        frequency_hz=float(f[index]),
                        measured_db=float(20 * np.log10(abs(spectrum[index]))),
                        analog_db=float(20 * np.log10(abs(expected))),
                    )
                )
        if rate == 48000:
            for gain in [0.1, 0.5, 1.0]:
                for amplitude in np.geomspace(1e-5, 10.0, 41):
                    y, _ = bridge.render(
                        amplitude * np.sin(phase), rate, factor, gain, 1.0, 1.0
                    )
                    levels.append(
                        dict(
                            gain=gain,
                            input_peak_volts=float(amplitude),
                            output_rms_volts=float(
                                np.sqrt(np.mean(y[count // 2 :] ** 2))
                            ),
                        )
                    )
    report = dict(
        profile="MC402-BOUNDED-V1-PROVISIONAL",
        small_signal_points=response,
        endpoints=endpoints,
        level_sweep_48k_tone1_output1=levels,
        all_gain_zero_endpoints_exact_silence=all(
            r["rms_volts"] == 0.0 for r in endpoints if r["gain"] == 0.0
        ),
        all_output_zero_endpoints_exact_silence=all(
            r["rms_volts"] == 0.0 for r in endpoints if r["output"] == 0.0
        ),
    )
    Path(args.output).write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
