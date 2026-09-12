"""Independent continuous-time periodic reference for the exact frozen cascade.

No time sampling of the clipped waveform: locate saturation boundaries and
integrate each smooth interval. Gauss-Legendre order convergence is recorded.
This is an offline numerical quadrature reference, never a realtime model or LUT.
"""

import numpy as np
from scipy.optimize import brentq
from numpy.polynomial.legendre import leggauss

H = 2.5
A = -470 / 22
EXPONENT = 3.321928094887362
HP1 = 1 / (2 * np.pi * 22000 * 47e-9)


def saturation(x):
    q = np.abs(x) / H
    u = (q - 0.9) / 0.2
    return np.where(
        q <= 0.9,
        x,
        np.where(
            q >= 1.1,
            np.copysign(H, x),
            np.copysign(H * (0.9 + 0.2 * u - 0.1 * u * u), x),
        ),
    )


def reference(case, order=64):
    frequency = case["input_hz"]
    gain = case["gain"] ** EXPONENT
    first_transfer = A * (1j * frequency) / (HP1 + 1j * frequency)
    amplitude = case["amplitude"] * abs(first_transfer)
    phase = np.angle(first_transfer)
    lam = 154 / frequency
    nodes, weights = leggauss(order)
    boundaries = [0.0, np.pi / 2, np.pi, 3 * np.pi / 2, 2 * np.pi]
    for value in [0.9 * H, 1.1 * H]:
        if amplitude > value:
            t = np.arcsin(value / amplitude)
            boundaries.extend([t, np.pi - t, np.pi + t, 2 * np.pi - t])
    boundaries = np.array(sorted(set(boundaries)))

    def s1(theta):
        return saturation(amplitude * np.sin(theta))

    def coupling_integral(lo, hi):
        t = lo + (hi - lo) * (nodes + 1) / 2
        return lam * (hi - lo) / 2 * np.sum(weights * np.exp(-lam * (hi - t)) * s1(t))

    # Odd half-cycle symmetry gives a well-conditioned exact periodic state.
    half = 0.0
    for lo, hi in zip(boundaries[:-1], boundaries[1:]):
        if hi <= np.pi:
            half = np.exp(-lam * (hi - lo)) * half + coupling_integral(lo, hi)
    initial = -half / (1 + np.exp(-lam * np.pi))
    states = [initial]
    for lo, hi in zip(boundaries[:-1], boundaries[1:]):
        states.append(np.exp(-lam * (hi - lo)) * states[-1] + coupling_integral(lo, hi))

    def lowpass(theta):
        original = np.asarray(theta)
        points = np.atleast_1d(original).astype(float)
        result = np.empty(points.size)
        index = np.clip(
            np.searchsorted(boundaries, points, side="right") - 1,
            0,
            len(boundaries) - 2,
        )
        for j in np.unique(index):
            selected = index == j
            end = points[selected]
            lo = boundaries[j]
            length = end - lo
            t = lo + length[:, None] * (nodes + 1) / 2
            integral = (
                lam
                * length
                / 2
                * np.sum(weights * np.exp(-lam * (end[:, None] - t)) * s1(t), axis=1)
            )
            result[selected] = np.exp(-lam * length) * states[j] + integral
        return result[0] if original.ndim == 0 else result

    def drive2(theta):
        return A * gain * (s1(theta) - lowpass(theta))

    split = list(boundaries)
    roots = []
    for threshold in [-1.1 * H, -0.9 * H, 0.9 * H, 1.1 * H]:
        for lo, hi in zip(boundaries[:-1], boundaries[1:]):
            grid = np.linspace(lo, hi, 33)
            values = drive2(grid) - threshold
            for left, right, fl, fr in zip(
                grid[:-1], grid[1:], values[:-1], values[1:]
            ):
                if fl * fr < 0:
                    root = brentq(
                        lambda t: drive2(t) - threshold, left, right, xtol=1e-14
                    )
                    roots.append(root)
                    split.append(root)
    split = sorted(set(split))
    harmonic = np.arange(1, int(20000 / frequency) + 1)
    coefficients = np.zeros(harmonic.size, complex)
    for lo, hi in zip(split[:-1], split[1:]):
        theta = lo + (hi - lo) * (nodes + 1) / 2
        y = saturation(drive2(theta))
        coefficients += (
            (hi - lo)
            / 2
            * (np.exp(-1j * harmonic[:, None] * theta) @ (weights * y))
            / np.pi
        )
    hz = harmonic * frequency
    cutoff = 500 * 16 ** case["tone"]
    coefficients *= (
        np.exp(1j * harmonic * phase)
        * (cutoff / (cutoff + 1j * hz))
        * (1j * hz / (10 + 1j * hz))
        * case.get("output", 1.0) ** EXPONENT
    )
    return dict(
        harmonic=harmonic,
        coefficients=coefficients,
        periodic_state_error=abs(states[-1] - initial),
        second_stage_knee_roots=roots,
        quadrature_order=order,
        intervals=len(split) - 1,
    )
