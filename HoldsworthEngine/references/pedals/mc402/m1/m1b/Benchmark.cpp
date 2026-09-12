// Offline workload benchmark; no production enrollment or code changes.
#include "../m1a/OfflineExperiment.cpp"
extern "C" double m1b_benchmark(double rate, unsigned factor, int mode, double frequency,
                                double amplitude, double gain, double tone,
                                std::size_t count, double* checksum)
{
  mc402_m1a::Experiment p;
  p.prepare(rate, factor, gain, tone, 1., mode, mode, 3, true, false);
  std::array<double, 8192> x{};
  for (std::size_t i = 0; i < x.size(); ++i)
    x[i] = amplitude * std::sin(2. * std::numbers::pi * frequency * static_cast<double>(i) / rate);
  for (std::size_t i = 0; i < 65536; ++i)
    (void)p.tick(x[i % x.size()]);
  const auto start = std::chrono::steady_clock::now();
  double sum = 0.;
  for (std::size_t i = 0; i < count; ++i)
    sum += p.tick(x[i % x.size()]);
  *checksum = sum;
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
