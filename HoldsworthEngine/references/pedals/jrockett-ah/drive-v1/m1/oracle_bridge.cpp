// Offline Python bridge only; never enrolled in a product target.
#include "../../../../../dsp/JRockettAHDriveProcessor.h"
#include <algorithm>
#include <span>

extern "C" int ah_drive_render(double fs, double gain, double bass, double treble,
                                double volume, const double* x, double* y,
                                std::size_t count, std::size_t block)
{
  try
  {
    holdsworth::dsp::JRockettAHDriveProcessor p;
    p.setControls({gain,bass,treble,volume});p.prepare(fs,block);
    for (std::size_t i=0;i<count;i+=block)
    {
      const auto n=std::min(block,count-i);
      p.processBlock(std::span{x+i,n},std::span{y+i,n});
    }
    return static_cast<int>(p.latencySamples());
  }
  catch (...) { return -1; }
}
