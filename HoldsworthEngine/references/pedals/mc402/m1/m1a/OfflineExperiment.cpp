// M1a OFFLINE EXPERIMENT ONLY. Never enrolled in a plugin or production target.
// Reuses the frozen numerical profile and FIR taps without modifying either.
#include "../../../../../dsp/MC402ProvisionalProfile.h"
#include "../../../../../dsp/MC402HalfBandCoefficients.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>

namespace mc402_m1a
{
using P = holdsworth::dsp::MC402ProvisionalProfile;
constexpr double a = P::kneeStart, b = P::kneeEnd, w = P::kneeWidth;
constexpr double c = P::kneeQuadratic / (w * w);

// Normalized exact static transfer. Ordinary mode uses the production arithmetic.
double shape(double x)
{
  const double q = std::abs(x);
  if (q <= a)
    return x;
  if (q >= b)
    return std::copysign(1., x);
  const double u = (q - a) / w;
  return std::copysign(a + w * u - P::kneeQuadratic * u * u, x);
}
double clip(double x, double h)
{
  const double q = std::abs(x);
  if (q <= a * h)
    return x;
  if (q >= b * h)
    return std::copysign(h, x);
  const double u = (q / h - a) / w;
  return std::copysign(h * (a + w * u - P::kneeQuadratic * u * u), x);
}
// F1'=shape, F2'=F1. F1 is even, F2 odd, both zero at zero.
// Tail expressions avoid subtracting large nearly equal powers.
double primitive1(double x)
{
  const double q = std::abs(x), d = std::max(q - a, 0.);
  const double fb = b * b / 2. - c * w * w * w / 3.;
  return q <= b ? q * q / 2. - c * d * d * d / 3. : fb + (q - b);
}
double primitive2(double x)
{
  const double q = std::abs(x), d = std::max(q - a, 0.);
  const double fb = b * b / 2. - c * w * w * w / 3.;
  const double gb = b * b * b / 6. - c * w * w * w * w / 12.;
  const double t = q - b;
  return std::copysign(q <= b ? q * q * q / 6. - c * d * d * d * d / 12. : gb + fb * t + t * t / 2., x);
}
struct Polynomial
{
  double f, derivative, curvature;
};
Polynomial polynomial(double x)
{
  const double q = std::abs(x);
  if (q >= b)
    return {std::copysign(1., x), 0., 0.};
  if (q <= a)
    return {x, 1., 0.};
  return {shape(x), 1. - 2. * c * (q - a), -std::copysign(2. * c, x)};
}
// Exact integral of shape(x)*linear_weight(x), split at the four knees.
// Each local polynomial is integrated about its midpoint. This is the
// algebraically evaluated difference of primitives, not numerical quadrature
// or a lookup table. Avoids cancellation of global F1/F2 at close arguments.
// Endpoint weights are bounded [0,1]; output is the interval AVERAGE integral.
double weightedAverage(double lo, double hi, double weightLo, double weightHi)
{
  const double width = hi - lo;
  if (width == 0.)
    return shape(lo) * (weightLo + weightHi) / 2.;
  const std::array<double, 5> ends{-b, -a, a, b, hi};
  double left = lo, result = 0.;
  for (const double boundary : ends)
  {
    const double right = std::min(boundary, hi);
    if (right <= left)
      continue;
    const double length = right - left, mid = left + length / 2.;
    const auto p = polynomial(mid);
    const double weight = weightLo + ((left - lo) / width + .5 * (length / width)) * (weightHi - weightLo);
    result += (length / width)
              * (p.f * weight
                 + (p.curvature * .5 * weight * length * length
                    + p.derivative * (weightHi - weightLo) * (length / width) * length)
                     / 12.);
    left = right;
    if (left == hi)
      break;
  }
  return result;
}
double first(double x, double y)
{
  if (x > y)
    std::swap(x, y);
  return weightedAverage(x, y, 1., 1.);
}
// Second divided difference: 2*F2[x,y,z]. The triangular B-spline
// integral below is an exact, symmetric, repeated-node-safe evaluation.
double second(double x, double y, double z)
{
  if (x > y)
    std::swap(x, y);
  if (y > z)
    std::swap(y, z);
  if (x > y)
    std::swap(x, y);
  if (x == z)
    return shape(x);
  return 2.
         * (((y - x) / (z - x)) * weightedAverage(x, y, 0., 1.) + ((z - y) / (z - x)) * weightedAverage(y, z, 1., 0.));
}
// Parker 2016 triangular TIME kernel, distinct from second divided differences.
// Integrate (1-t)*f(current+(previous-current)*t) across each adjoining segment.
double timeWeight(double x, double y, double wx, double wy)
{
  return x <= y ? weightedAverage(x, y, wx, wy) : weightedAverage(y, x, wy, wx);
}
struct Antialias
{
  double previous = 0., older = 0.;
  int mode = 0; // 0 ordinary, 1 ADAA1, 2 ADAA2 divided difference, 3 triangular time kernel
  double tick(double value, double h)
  {
    if (mode == 0)
      return clip(value, h);
    const double q = value / h;
    double result = 0.;
    if (mode == 1)
      result = first(q, previous);
    if (mode == 2)
      result = second(q, previous, older);
    if (mode == 3)
      result = timeWeight(q, previous, 0., 1.) + timeWeight(previous, older, 1., 0.);
    older = previous;
    previous = q;
    return h * result;
  }
};
struct Filter
{
  double state = 0., coefficient = 0.;
  void prepare(double hz, double rate)
  {
    const double g = std::tan(std::numbers::pi * hz / rate);
    coefficient = g / (1. + g);
  }
  double low(double x)
  {
    const double v = (x - state) * coefficient, y = v + state;
    state = y + v;
    if (std::abs(state) < P::stateSilenceFloor)
      state = 0.;
    return y;
  }
  double high(double x) { return x - low(x); }
};
struct Core
{
  Filter hp1, hp2, tone, hp3;
  Antialias s1, s2;
  double gain = 0., output = 0.;
  int stages = 3;
  bool toneOn = true, wire = false;
  void prepare(double rate, double g, double t, double o, int mode1, int mode2, int active, bool toneEnabled)
  {
    hp1.prepare(P::inputHighPassHz, rate);
    hp2.prepare(P::interstageHighPassHz, rate);
    tone.prepare(P::toneDarkHz * std::pow(P::toneFrequencyRatio, t), rate);
    hp3.prepare(P::outputHighPassHz, rate);
    gain = std::pow(g, P::attenuationExponent);
    output = std::pow(o, P::attenuationExponent);
    s1.mode = mode1;
    s2.mode = mode2;
    stages = active;
    toneOn = toneEnabled;
  }
  double tick(double x)
  {
    if (wire)
      return x;
    double y = P::stage1Gain * hp1.high(x);
    if (stages & 1)
      y = s1.tick(y, P::stage1SwingVolts);
    y = P::stage2Gain * hp2.high(y * gain);
    if (stages & 2)
      y = s2.tick(y, P::stage2SwingVolts);
    if (toneOn)
      y = tone.low(y);
    return hp3.high(y) * output;
  }
};
struct Fir
{
  std::array<double, 130> history{};
  const double* coefficients = nullptr;
  std::size_t length = 0, cursor = 0;
  double tick(double x)
  {
    history[cursor] = history[cursor + length] = x;
    const double* recent = history.data() + cursor + length;
    const auto middle = length / 2;
    double result = coefficients[middle] * recent[-static_cast<std::ptrdiff_t>(middle)];
    for (std::size_t tap = 1; tap < middle; tap += 2)
      result += coefficients[tap]
                * (recent[-static_cast<std::ptrdiff_t>(tap)] + recent[-static_cast<std::ptrdiff_t>(length - 1 - tap)]);
    if (++cursor == length)
      cursor = 0;
    return result;
  }
};
struct Experiment
{
  Core core;
  std::array<Fir, 6> up{}, down{};
  unsigned stages = 0;
  double latency = 0.;
  void prepare(double rate, unsigned factor, double g, double t, double o, int mode1, int mode2, int active,
               bool toneOn, bool wire)
  {
    core.prepare(rate * factor, g, t, o, mode1, mode2, active, toneOn);
    core.wire = wire;
    for (unsigned divisor = 2; divisor <= factor; divisor *= 2)
    {
      const auto length = P::firLengths[stages];
      const double* h = length == 65 ? holdsworth::dsp::mc402_detail::kHalfBand65.data()
                                     : holdsworth::dsp::mc402_detail::kHalfBand33.data();
      up[stages].length = down[stages].length = length;
      up[stages].coefficients = down[stages].coefficients = h;
      latency += static_cast<double>(length - 1) / divisor;
      ++stages;
    }
  }
  double tick(double x, unsigned stage = 0)
  {
    if (stage == stages)
      return core.tick(x);
    const double even = tick(up[stage].tick(x) * 2., stage + 1);
    const double result = down[stage].tick(even);
    const double odd = tick(up[stage].tick(0.) * 2., stage + 1);
    (void)down[stage].tick(odd);
    return result;
  }
};
} // namespace mc402_m1a

extern "C" {
// Caller validates rate/factor/dimensions. All experiment allocation stays here.
void* m1a_create(double rate, unsigned factor, double gain, double tone, double output, int s1, int s2, int active,
                 int toneOn, int wire)
{
  if (factor == 0 || factor > 64 || (factor & (factor - 1)) || rate <= 0.)
    return nullptr;
  auto* e = new mc402_m1a::Experiment;
  e->prepare(rate, factor, gain, tone, output, s1, s2, active, toneOn != 0, wire != 0);
  return e;
}
void m1a_destroy(void* e)
{
  delete static_cast<mc402_m1a::Experiment*>(e);
}
void m1a_process(void* e, const double* x, double* y, std::size_t count)
{
  auto& p = *static_cast<mc402_m1a::Experiment*>(e);
  for (std::size_t i = 0; i < count; ++i)
    y[i] = p.tick(x[i]);
}
void m1a_periodic(void* e, const double* x, double* y, std::size_t count, unsigned warmupPeriods)
{
  auto& p = *static_cast<mc402_m1a::Experiment*>(e);
  for (unsigned period = 0; period < warmupPeriods; ++period)
    for (std::size_t i = 0; i < count; ++i)
      (void)p.tick(x[i]);
  for (std::size_t i = 0; i < count; ++i)
    y[i] = p.tick(x[i]);
}
double m1a_shape(double x, int primitive)
{
  if (primitive == 1)
    return mc402_m1a::primitive1(x);
  if (primitive == 2)
    return mc402_m1a::primitive2(x);
  return mc402_m1a::shape(x);
}
double m1a_adaa(double x, double y, double z, int order)
{
  return order == 1 ? mc402_m1a::first(x, y) : mc402_m1a::second(x, y, z);
}
std::size_t m1a_state_size()
{
  return sizeof(mc402_m1a::Antialias);
}
double m1a_benchmark(double rate, unsigned factor, int s1, int s2, std::size_t count, double* checksum)
{
  mc402_m1a::Experiment p;
  p.prepare(rate, factor, 1., 1., 1., s1, s2, 3, true, false);
  std::array<double, 4096> x{};
  for (std::size_t i = 0; i < x.size(); ++i)
    x[i] = .5 * std::sin(2. * std::numbers::pi * 997. * static_cast<double>(i) / rate);
  for (std::size_t i = 0; i < 65536; ++i)
    (void)p.tick(x[i % x.size()]);
  const auto start = std::chrono::steady_clock::now();
  double sum = 0.;
  for (std::size_t i = 0; i < count; ++i)
    sum += p.tick(x[i % x.size()]);
  *checksum = sum;
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
}
