#!/usr/bin/env python3
"""Offline timing counterexamples, NOT an iPlug2 repair or a DAW test.

Run with Python 3; no dependencies or generated files. These assertions test
the distinction between exact delay, a continuous transition, and host PDC.
They do not test allocation, concurrency, actual AU rendering, or sound quality.
"""

import json
import math


def main():
    rate = 48000
    pedal_delay = 32
    fade_length = 480
    frequency = rate / (2 * pedal_delay)  # 750 Hz: the taps are opposite in phase.
    amplitude = 0.25

    def signal(n):
        return amplitude * math.sin(2 * math.pi * frequency * n / rate)

    switch_sample = 1040  # Peak, with both histories already available.
    old_last = signal(switch_sample - 1)
    new_first = signal(switch_sample - pedal_delay)
    ordinary_step = abs(signal(switch_sample) - old_last)
    switched_step = abs(new_first - old_last)
    assert ordinary_step < 0.002
    assert switched_step > 0.49

    # At a crossfade midpoint, both delay taps contribute. An impulse response
    # with two nonzero taps cannot equal any single exact integer-sample delay.
    midpoint_impulse = [0.0] * (pedal_delay + 1)
    midpoint_impulse[0] = midpoint_impulse[pedal_delay] = 0.5
    assert sum(value != 0 for value in midpoint_impulse) == 2
    midpoint_peak = abs(0.5 * signal(switch_sample)
                        + 0.5 * signal(switch_sample - pedal_delay))
    assert midpoint_peak < 1e-12

    # A host that continues rendering while deferring compensation recalculation
    # demonstrates why notification return is not a sample-timed PDC commit.
    # This is an allowed scheduling counterexample, not a measured host policy.
    callback_sizes = (1, 2, 4, 8, 32, 64, 128)
    model_latencies = (0, 29, 75)
    counterexamples = 0
    for model in model_latencies:
        for block in callback_sizes:
            old_total, new_total = model, model + pedal_delay
            # Audio-owned adoption plus atomic publication could keep THESE
            # two values equal, including synchronous property queries.
            active_bypass_delay = reported_latency = new_total
            assert active_bypass_delay == reported_latency
            # A property listener can return before the host changes its graph.
            host_compensation = old_total
            rendered_during_deferral = block
            assert rendered_during_deferral > 0
            assert reported_latency - host_compensation == pedal_delay
            # Notification/PDC convergence does not undo samples already emitted.
            host_compensation = reported_latency
            assert host_compensation == active_bypass_delay
            counterexamples += 1

    print(json.dumps({
        "kind": "offline counterexamples, not framework or host validation",
        "rate_hz": rate,
        "tone_hz": frequency,
        "amplitude": amplitude,
        "ordinary_adjacent_sample_step": ordinary_step,
        "exact_0_to_32_switch_step": switched_step,
        "example_crossfade_samples": fade_length,
        "crossfade_midpoint_impulse_nonzero_taps": 2,
        "crossfade_midpoint_tone_peak": midpoint_peak,
        "deferred_host_compensation_counterexamples": counterexamples,
        "assertions": "passed"
    }, indent=2))


if __name__ == "__main__":
    main()
