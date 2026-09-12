// Standalone offline safety/regression checks; not part of any plugin target.
#include "OfflineExperiment.cpp"
#include <cassert>
#include <iostream>
#include <vector>
int main()
{
  using namespace mc402_m1a;
  for (double x : {-1000., -1.1, -.9, -1e-300, 0., 1e-300, .9, 1.1, 1000.})
  {
    assert(std::abs(first(x, x) - shape(x)) < 1e-14);
    assert(std::abs(second(x, x, x) - shape(x)) < 1e-14);
    for (double d : {0., std::numeric_limits<double>::denorm_min(), 1e-300, 1e-15, 1e-9})
      for (double y : {x + d, x - d})
      {
        assert(std::isfinite(first(x, y)));
        assert(std::isfinite(second(x, y, x)));
        assert(std::abs(first(x, y)) <= 1. + 1e-12);
        assert(std::abs(second(x, y, x)) <= 1. + 1e-12);
      }
  }
  std::vector<double> input(16384), out(input.size()), split(input.size());
  for (std::size_t i = 0; i < input.size(); ++i)
    input[i] = 10. * std::sin(1.234 * static_cast<double>(i));
  for (double rate : P::supportedSampleRates)
    for (unsigned factor : {1U, 2U, 4U, 8U})
      for (int mode : {0, 1, 2, 3})
      {
        void* whole = m1a_create(rate, factor, 1., 1., 1., mode, mode, 3, 1, 0);
        void* parts = m1a_create(rate, factor, 1., 1., 1., mode, mode, 3, 1, 0);
        m1a_process(whole, input.data(), out.data(), out.size());
        for (std::size_t offset = 0; offset < input.size(); offset += 7)
          m1a_process(
            parts, input.data() + offset, split.data() + offset, std::min(std::size_t{7}, input.size() - offset));
        for (std::size_t i = 0; i < out.size(); ++i)
          assert(std::isfinite(out[i]) && std::abs(out[i]) < 100. && out[i] == split[i]);
        m1a_destroy(whole);
        m1a_destroy(parts);
      }
  std::cout << "Offline repeated-node/denormal, finite-output and partition checks pass\n";
}
