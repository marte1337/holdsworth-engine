// Incremental pre-NAM AH path timing, not a guarantee for arbitrary loaded NAMs.
#include "../../../../integration/DevelopmentPreNAMSelector.h"
#include "../../../../integration/DevelopmentJRockettAHControls.h"
#include <chrono>
#include <algorithm>
#include <array>
#include <vector>
#include <iostream>
#include <iomanip>
struct Unselected
{
  bool isPrepared() const { return true; }
  std::size_t maximumBlockSize() const { return 100000; }
  void reset() {}
  void processBlock(std::span<const double>,std::span<double>) { std::abort(); }
};
int main()
{
  using namespace holdsworth;
  double checksum=0.;
  std::cout<<"rate_hz,frames,case,median_us,p99_us,max_us,median_budget_percent,p99_budget_percent\n"<<std::setprecision(9);
  for(double rate:integration::kDevelopmentAHRealtimeRates)
    for(std::size_t frames:{1U,2U,4U,7U,8U,16U,32U,64U,128U})
      for(bool onset:{false,true})
      {
        dsp::JRockettAHBoostProcessor ah;integration::DevelopmentPreNAMSelector selector;Unselected tc,mc;
        const auto warmCount=static_cast<std::size_t>(std::ceil(rate*.03));
        ah.prepare(rate,warmCount);std::vector<double>warm(warmCount,.1);std::array<double,128>x{};
        selector.beginBlock(integration::DevelopmentPreNAMProcessor::jRockettAH,frames,tc,mc,ah);
        std::array<double,400> times{};
        for(std::size_t iteration=0;iteration<times.size();++iteration)
        {
          // Complete prior fades and fill history before every timed onset.
          warm.assign(warmCount,.1);ah.processBlock(warm,warm);
          if(onset)ah.setControls(integration::ahControlsFromDevelopmentUI(.5,iteration%2?0.:1.,1.));
          x.fill(.1);
          const auto begin=std::chrono::steady_clock::now();
          selector.beginBlock(integration::DevelopmentPreNAMProcessor::jRockettAH,frames,tc,mc,ah);
          const double pre=selector.inputGain(1.,2.);
          for(std::size_t j=0;j<frames;++j)x[j]*=pre;
          selector.processSelected(std::span{x}.first(frames),.5,tc,mc,ah);
          const auto end=std::chrono::steady_clock::now();
          times[iteration]=std::chrono::duration<double,std::micro>(end-begin).count();
          checksum+=x[iteration%frames];
        }
        std::sort(times.begin(),times.end());
        const double budget=1e6*static_cast<double>(frames)/rate;
        std::cout<<rate<<','<<frames<<','<<(onset?"transition_onset":"settled")<<','<<times[200]<<','<<times[396]<<','<<times[399]<<','<<100.*times[200]/budget<<','<<100.*times[396]/budget<<'\n';
      }
  std::cerr<<"Checksum: "<<checksum<<'\n';
}
