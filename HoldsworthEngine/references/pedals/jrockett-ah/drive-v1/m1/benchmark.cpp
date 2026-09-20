// Incremental isolated Drive timing only, not a whole-plugin deadline guarantee.
#include "../../../../../dsp/JRockettAHDriveProcessor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <span>

int main()
{
  using P=holdsworth::dsp::JRockettAHDriveProcessor;
  using C=holdsworth::dsp::JRockettAHDriveControls;
  double checksum=0.;
  std::cout<<"fs,frames,case,median_us,p99_us,max_us,p99_budget_percent\n"<<std::setprecision(9);
  for (double fs:P::Profile::supportedRates)
    for (std::size_t frames:{1U,2U,4U,8U,32U,64U,128U})
      for (bool moving:{false,true})
      {
        P p;p.setControls({1.,1.,1.,1.});p.prepare(fs,128);
        std::array<double,128>x{},y{};std::array<double,1000>times{};
        for (std::size_t i=0;i<x.size();++i) x[i]=.2*std::sin(.037*static_cast<double>(i))+.05;
        for (int i=0;i<100;++i) p.processBlock(x,y);
        for (std::size_t i=0;i<times.size();++i)
        {
          if (moving) p.setControls(i%2?C{1.,0.,1.,1.}:C{0.,1.,0.,0.});
          const auto start=std::chrono::steady_clock::now();
          p.processBlock(std::span{x}.first(frames),std::span{y}.first(frames));
          const auto end=std::chrono::steady_clock::now();
          times[i]=std::chrono::duration<double,std::micro>(end-start).count();
          checksum+=y[i%frames];
        }
        std::sort(times.begin(),times.end());
        const double budget=1e6*static_cast<double>(frames)/fs;
        std::cout<<fs<<','<<frames<<','<<(moving?"control_ramps":"settled")<<','<<times[500]<<','
                 <<times[990]<<','<<times.back()<<','<<100.*times[990]/budget<<'\n';
      }
  std::cerr<<"Processor bytes: "<<sizeof(P)<<"; checksum: "<<checksum<<'\n';
}
