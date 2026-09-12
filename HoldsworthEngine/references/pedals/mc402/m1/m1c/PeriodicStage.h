#pragma once
#include <cstddef>
// The only accelerated nonlinear operation is the same per-sample kernel;
// seed its two histories with the last inputs of the exact periodic record.
template <typename Stage>
void periodicStage(const double* x, double* y, std::size_t size, double h, int mode)
{
  if (size == 0) return;
  Stage stage;
  stage.mode = mode;
  stage.previous = x[size - 1] / h;
  stage.older = x[size > 1 ? size - 2 : 0] / h;
  for (std::size_t i = 0; i < size; ++i)
    y[i] = stage.tick(x[i], h);
}
