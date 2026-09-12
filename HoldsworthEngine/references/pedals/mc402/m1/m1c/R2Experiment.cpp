// OFFLINE R2 ONLY. Filter/Core/FIR/Experiment and native ABI are copied from
// the M1a source, with only the namespace changed; audit verifies that suffix.
// The m1a_* ABI names intentionally retain compatibility with its Python wrapper.
#include "CandidateR2.h"
#include "PeriodicStage.h"
#include "../../../../../dsp/MC402HalfBandCoefficients.h"
#include <chrono>
#include <limits>
namespace mc402_m1c
{
using P = holdsworth::dsp::MC402ProvisionalProfile;
using r2::shape;
using r2::primitive1;
using r2::primitive2;
using r2::first;
using r2::second;
using r2::Antialias;
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
} // namespace mc402_m1c

extern "C" {
// Caller validates rate/factor/dimensions. All experiment allocation stays here.
void* m1a_create(double rate, unsigned factor, double gain, double tone, double output, int s1, int s2, int active,
                 int toneOn, int wire)
{
  if (factor == 0 || factor > 64 || (factor & (factor - 1)) || rate <= 0.)
    return nullptr;
  auto* e = new mc402_m1c::Experiment;
  e->prepare(rate, factor, gain, tone, output, s1, s2, active, toneOn != 0, wire != 0);
  return e;
}
void m1a_destroy(void* e)
{
  delete static_cast<mc402_m1c::Experiment*>(e);
}
void m1a_process(void* e, const double* x, double* y, std::size_t count)
{
  auto& p = *static_cast<mc402_m1c::Experiment*>(e);
  for (std::size_t i = 0; i < count; ++i)
    y[i] = p.tick(x[i]);
}
void m1a_periodic(void* e, const double* x, double* y, std::size_t count, unsigned warmupPeriods)
{
  auto& p = *static_cast<mc402_m1c::Experiment*>(e);
  for (unsigned period = 0; period < warmupPeriods; ++period)
    for (std::size_t i = 0; i < count; ++i)
      (void)p.tick(x[i]);
  for (std::size_t i = 0; i < count; ++i)
    y[i] = p.tick(x[i]);
}
double m1a_shape(double x, int primitive)
{
  if (primitive == 1)
    return mc402_m1c::primitive1(x);
  if (primitive == 2)
    return mc402_m1c::primitive2(x);
  return mc402_m1c::shape(x);
}
double m1a_adaa(double x, double y, double z, int order)
{
  return order == 1 ? mc402_m1c::first(x, y) : mc402_m1c::second(x, y, z);
}
std::size_t m1a_state_size()
{
  return sizeof(mc402_m1c::Antialias);
}
double m1a_benchmark(double rate, unsigned factor, int s1, int s2, std::size_t count, double* checksum)
{
  mc402_m1c::Experiment p;
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

extern "C" void m1c_stage(const double* x, double* y, std::size_t n, double h, int mode)
{
  periodicStage<mc402_m1c::Antialias>(x, y, n, h, mode);
}
extern "C" double m1c_derivative(double x, int order)
{
  const auto p = mc402_m1c::r2::polynomial(x);
  return order == 0 ? p.f : order == 1 ? p.derivative : order == 2 ? p.curvature : order == 3 ? p.third : p.fourth;
}
