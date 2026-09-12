// Unmodified V1 controls/core for independent equivalence and A/B comparisons.
#include "../m1a/OfflineExperiment.cpp"
#include "PeriodicStage.h"
extern "C" void m1c_stage(const double* x, double* y, std::size_t n, double h, int mode)
{
  periodicStage<mc402_m1a::Antialias>(x, y, n, h, mode);
}
