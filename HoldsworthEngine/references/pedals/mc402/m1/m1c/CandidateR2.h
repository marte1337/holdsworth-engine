#pragma once
// OFFLINE CANDIDATE ONLY. The production V1 profile remains unchanged.
#include "../../../../../dsp/MC402ProvisionalProfile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace mc402_m1c::r2
{
using Base = holdsworth::dsp::MC402ProvisionalProfile;
inline constexpr std::string_view id = "MC402-BOUNDED-V1-PROVISIONAL-R2";
inline constexpr double a = Base::kneeStart, b = Base::kneeEnd, w = Base::kneeWidth;
// Unique degree <=5 Hermite solution; degree 5 coefficient is exactly zero.
inline constexpr std::array<double, 6> kneeU{a, w, 0., -w, w / 2., 0.};
inline constexpr double primitiveAtB = 599. / 1000.;
inline constexpr double secondPrimitiveAtB = 6647. / 30000.;

inline double shape(double x)
{
  const double q = std::abs(x);
  if (q <= a)
    return x;
  if (q >= b)
    return std::copysign(1., x);
  const double t = (q - a) / w;
  return std::copysign(a + w * (t + t * t * t * (t / 2. - 1.)), x);
}
inline double clip(double x, double h)
{
  if (std::abs(x) <= a * h)
    return x; // Retain the exact linear arithmetic outside the changed knee.
  if (std::abs(x) >= b * h)
    return std::copysign(h, x);
  return h * shape(x / h);
}
// F1'=shape, F2'=F1. F1 even, F2 odd. Volts primitives scale by H^2/H^3.
inline double primitive1(double x)
{
  const double q = std::abs(x), d = std::max(q - a, 0.);
  if (q >= b)
    return primitiveAtB + q - b;
  const double d2 = d * d;
  return q * q / 2. - d2 * d2 / (4. * w * w) + d2 * d2 * d / (10. * w * w * w);
}
inline double primitive2(double x)
{
  const double q = std::abs(x), d = std::max(q - a, 0.);
  if (q >= b)
  {
    const double t = q - b;
    return std::copysign(secondPrimitiveAtB + primitiveAtB * t + t * t / 2., x);
  }
  const double d2 = d * d, d4 = d2 * d2;
  return std::copysign(q * q * q / 6. - d4 * d / (20. * w * w)
                      + d4 * d2 / (60. * w * w * w), x);
}
struct Polynomial
{
  double f, derivative, curvature, third, fourth;
};
inline Polynomial polynomial(double x)
{
  const double q = std::abs(x), sign = std::copysign(1., x);
  if (q >= b)
    return {sign, 0., 0., 0., 0.};
  if (q <= a)
    return {x, 1., 0., 0., 0.};
  const double t = (q - a) / w;
  return {shape(x), 1. - 3. * t * t + 2. * t * t * t,
          sign * 6. * t * (t - 1.) / w, (12. * t - 6.) / (w * w),
          sign * 12. / (w * w * w)};
}
// Exact local integral of the quartic times a linear weight. No LUT/quadrature.
// Split at all knees; midpoint moments avoid cancellation of nearby primitives.
inline double weightedAverage(double lo, double hi, double weightLo, double weightHi)
{
  const double width = hi - lo;
  if (width == 0.)
    return shape(lo) * (weightLo + weightHi) / 2.;
  const std::array<double, 5> ends{-b, -a, a, b, hi};
  double left = lo, result = 0.;
  for (double boundary : ends)
  {
    const double right = std::min(boundary, hi);
    if (right <= left)
      continue;
    const double length = right - left, mid = left + length / 2.;
    const auto p = polynomial(mid);
    const double fraction = length / width;
    const double weight = weightLo + ((left - lo) / width + fraction / 2.) * (weightHi - weightLo);
    double average = p.f * weight;
    // Constant tails may be huge intervals: never evaluate unused powers there.
    if (p.derivative != 0. || p.curvature != 0. || p.third != 0. || p.fourth != 0.)
    {
      const double l2 = length * length;
      const double deltaWeight = (weightHi - weightLo) * fraction;
      average += weight * (p.curvature * l2 / 24. + p.fourth * l2 * l2 / 1920.)
                 + deltaWeight * (p.derivative * length / 12. + p.third * l2 * length / 480.);
    }
    result += fraction * average;
    left = right;
    if (left == hi)
      break;
  }
  return result;
}
inline double first(double x, double y)
{
  if (x > y)
    std::swap(x, y);
  return weightedAverage(x, y, 1., 1.);
}
inline double second(double x, double y, double z)
{
  if (x > y) std::swap(x, y);
  if (y > z) std::swap(y, z);
  if (x > y) std::swap(x, y);
  if (x == z) return shape(x);
  return 2. * (((y - x) / (z - x)) * weightedAverage(x, y, 0., 1.)
               + ((z - y) / (z - x)) * weightedAverage(y, z, 1., 0.));
}
inline double timeWeight(double x, double y, double wx, double wy)
{
  return x <= y ? weightedAverage(x, y, wx, wy) : weightedAverage(y, x, wy, wx);
}
struct Antialias
{
  double previous = 0., older = 0.;
  int mode = 0;
  double tick(double value, double h)
  {
    if (mode == 0) return clip(value, h);
    const double q = value / h;
    double result = 0.;
    if (mode == 1) result = first(q, previous);
    if (mode == 2) result = second(q, previous, older);
    if (mode == 3) result = timeWeight(q, previous, 0., 1.) + timeWeight(previous, older, 1., 0.);
    older = previous;
    previous = q;
    return h * result;
  }
};
} // namespace mc402_m1c::r2
