"""80-digit independent primitive/divided-difference checks, including Hermite nodes."""

from decimal import Decimal as D, localcontext
import numpy as np
from experiment import Experiment, write


def f(x):
    q = abs(x)
    if q <= D(".9"):
        return x
    if q >= D("1.1"):
        return D(1) if x >= 0 else -D(1)
    return (q - D("2.5") * (q - D(".9")) ** 2) * (1 if x >= 0 else -1)


def F(x, order):
    q = abs(x)
    a = D(".9")
    b = D("1.1")
    w = D(".2")
    c = D("2.5")
    d = max(q - a, D(0))
    fb = b * b / 2 - c * w**3 / 3
    gb = b**3 / 6 - c * w**4 / 12
    if order == 1:
        return q * q / 2 - c * d**3 / 3 if q <= b else fb + q - b
    result = (
        q**3 / 6 - c * d**4 / 12 if q <= b else gb + fb * (q - b) + (q - b) ** 2 / 2
    )
    return result if x >= 0 else -result


def expected(values, order):
    x, y, z = sorted(D(float(v)) for v in values)
    if order == 1:
        x, y = D(float(values[0])), D(float(values[1]))
        return f(x) if x == y else (F(x, 1) - F(y, 1)) / (x - y)
    if x == z:
        return f(x)
    left = F(x, 1) if x == y else (F(y, 2) - F(x, 2)) / (y - x)
    right = F(z, 1) if y == z else (F(z, 2) - F(y, 2)) / (z - y)
    return 2 * (right - left) / (z - x)


def main():
    e = Experiment()
    rng = np.random.default_rng(40280)
    cases = list(rng.uniform(-100, 100, (1000, 3)))
    for x in [-100.0, -1.1, -0.9, 0.0, 0.9, 1.1, 100.0]:
        for delta in [0.0, 1e-15, 1e-12, 1e-9, 1e-6]:
            cases.extend([(x, x + delta, x - delta), (x, x + delta, x), (x, 0.0, x)])
    maxima = {1: 0.0, 2: 0.0}
    with localcontext() as context:
        context.prec = 80
        for values in cases:
            for order in [1, 2]:
                actual = e.lib.m1a_adaa(*values, order)
                error = abs(actual - float(expected(values, order)))
                maxima[order] = max(maxima[order], error)
                assert error < 2e-13, (values, order, error)
    write(
        "high-precision-checks.json",
        dict(decimal_digits=80, triples=len(cases), max_abs_error=maxima, passed=True),
    )
    print("80-digit antiderivative/Hermite comparisons pass", maxima)


if __name__ == "__main__":
    main()
