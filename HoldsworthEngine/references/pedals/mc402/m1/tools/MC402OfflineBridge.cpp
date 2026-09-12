// Offline C ABI for numerical validation of the actual compiled processor.
// Allocation, I/O and diagnostics belong here, never in the realtime processor.
#include "../../../../../tests/MC402TestAccess.h"
#include "../../../../../dsp/MC402HalfBandCoefficients.h"

using Processor = holdsworth::dsp::MC402BoostOverdriveProcessor;
using Access = holdsworth::test::MC402TestAccess;

extern "C" {
void* mc402_create(double rate, unsigned factor, double gain, double tone, double output, double boost)
{
  auto* p = new Processor;
  p->setControls({boost != 0.0, true, boost, gain, tone, output});
  try
  {
    Access::prepare(*p, rate, 1U << 24, factor);
  }
  catch (...)
  {
    delete p;
    return nullptr;
  }
  return p;
}
void mc402_destroy(void* handle)
{
  delete static_cast<Processor*>(handle);
}
void mc402_process(void* handle, const double* input, double* output, std::size_t count, int wire)
{
  auto& p = *static_cast<Processor*>(handle);
  if (wire)
    for (std::size_t i = 0; i < count; ++i)
      output[i] = Access::wireSample(p, input[i]);
  else
    p.processBlock({input, count}, {output, count});
}
std::size_t mc402_latency(void* handle)
{
  return static_cast<Processor*>(handle)->latencySamples();
}
unsigned mc402_default_factor(double rate)
{
  Processor p;
  p.prepare(rate, 1);
  return p.oversamplingFactor();
}
double mc402_clip(double value)
{
  return Access::saturate(value, Processor::Profile::stage1SwingVolts);
}
}
