# Analytic R2 knee and ADAA

This is an **offline candidate**, `MC402-BOUNDED-V1-PROVISIONAL-R2`. It does
not overwrite `MC402-BOUNDED-V1-PROVISIONAL` or assert measured MC402 behavior.
The provisional Gain-minimum mute remains the same named-profile behavior,
not a hardware fact. All base constants come from the frozen
[production V1 profile](../../rejected-overdrive/dsp/MC402ProvisionalProfile.h).

Let `q = |z|/H`, `a = 9/10`, `b = 11/10`, `w = b-a = 1/5`, and
`t = (q-a)/w`. For the positive normalized transition, start with

\[
P(t)=c_0+c_1t+c_2t^2+c_3t^3+c_4t^4+c_5t^5.
\]

Matching the linear region at `t=0` imposes `c0=a`, `c1=w`, `c2=0`.
Matching the constant region at `t=1` imposes

\[
\begin{aligned}
c_3+c_4+c_5&=-1/10,\\
3c_3+4c_4+5c_5&=-1/5,\\
6c_3+12c_4+20c_5&=0.
\end{aligned}
\]

The unique solution of the degree-at-most-five Hermite problem is
`c3=-1/5`, `c4=1/10`, **`c5=0`**. Thus the minimal polynomial is quartic:

\[
s_2(q)=\begin{cases}
q,&q\le0.9,\\
0.9+0.2t-0.2t^3+0.1t^4,&0.9<q<1.1,\\
1,&q\ge1.1,
\end{cases}
\qquad f_2(z)=\operatorname{sgn}(z)H s_2(|z|/H).
\]

A cubic cannot meet both zero-curvature joins while changing its slope:
its second derivative would be a linear function with two distinct zeros,
therefore identically zero. There is no fitted coefficient or shifted boundary.

Inside the knee,

\[
s_2'=1-3t^2+2t^3=(1-t)^2(1+2t),\quad
s_2''=\frac{6t(t-1)}{w}.
\]

The slope decreases from 1 to 0 without an overshoot; curvature tends to zero
at both joins. The odd extension is C² everywhere and exactly linear at zero.
It is not C³ at the joins. Curvature reaches `-7.5` at the midpoint, whereas
V1 has curvature `-5` throughout its knee with jumps to zero at its boundaries.

For V1, `s1 = 0.9+0.2t-0.1t²`. Their difference is exactly

\[
s_2-s_1=\tfrac1{10}t^2(1-t)^2.
\]

Its maximum is `1/160 = 0.00625` at `t=1/2`, `q=1`. At `H=2.5 V`, that is
`1/64 V = 15.625 mV`, 0.625% of H. At that point V1/R2 return
2.4375/2.453125 V. Negative inputs have the opposite signed difference.
The transfer is exactly identical outside the two knees. The largest slope
difference is `sqrt(3)/18 = 0.096225044865...`, at
`t=(3±sqrt(3))/6`. C² smoothness does not imply that a narrow, heavily driven
knee is adequately resolved by a particular internal sample rate.

## Exact antiderivatives

Use the odd normalized shape `s(x)`. Let `q=|x|` and `d=q-a` inside the knee.
Choose `F1(0)=F2(0)=0`, `F1'=s`, `F2'=F1`. On the positive axis:

| Region | F1(q) | F2(q) |
| --- | --- | --- |
| `q <= a` | `q²/2` | `q³/6` |
| `a < q < b` | `q²/2 − d⁴/(4w²) + d⁵/(10w³)` | `q³/6 − d⁵/(20w²) + d⁶/(60w³)` |
| `q >= b`, `r=q-b` | `599/1000 + r` | `6647/30000 + (599/1000)r + r²/2` |

Extend F1 evenly and F2 oddly. In volts, the antiderivatives are
`H² F1(z/H)` and `H³ F2(z/H)`. Normalized ADAA1 is `F1[x0,x1]`;
ADAA2 is `2 F2[x0,x1,x2]`, using divided-difference notation. The output
in either case is multiplied by H. Repeated nodes use the analytic limits.

The implementation avoids subtraction of nearly equal primitives: it evaluates
the equivalent normalized uniform (ADAA1) or triangular B-spline (ADAA2)
integral of the exact piecewise polynomial. It splits at `-b,-a,a,b`.
For a segment of length L, midpoint m, midpoint weight W, and endpoint weight
difference ΔW, the exact integral divided by L is

\[
W\left(s(m)+\frac{s''(m)L^2}{24}+\frac{s^{(4)}(m)L^4}{1920}\right)
+\Delta W\left(\frac{s'(m)L}{12}+\frac{s'''(m)L^3}{480}\right).
\]

These finite polynomial moments are analytic, not quadrature or a lookup table.
Constant tails skip unused powers. In a normalized x-coordinate, the positive
knee has `s'''=(12t-6)/w²` and `s''''=12/w³`; derivative symmetry supplies
the negative knee. Independent adaptive integration and 90-digit primitive
divided differences validate this implementation, including close/repeated
nodes, boundaries and large finite tails. See [kernel checks](kernel-checks.json).

ADAA changes the dynamic numerical approximation, as it did in M1a/M1b; it
does not change the DC static curve. In the exactly linear region, one stage
of ADAA1 has kernel `(1+z^-1)/2`, and ADAA2 has kernel
`(1+z^-1+z^-2)/3`. Two stages therefore add `1/F` or `2/F` host samples of
small-signal group delay. Their associated magnitude response is measured
separately and is not removed from the processor to make it qualify.
