// Offline M1 measurements of software only. No AH hardware data or alias project.
#include "../../../../dsp/JRockettAHBoostProcessor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>
using P=holdsworth::dsp::JRockettAHBoostProcessor;
using C=holdsworth::dsp::JRockettAHBoostControls;
C controls(std::size_t mode,double db=0.)
{
  return {db,static_cast<holdsworth::dsp::JRockettAHBoostType>(mode/2),
    static_cast<holdsworth::dsp::JRockettAHEmphasis>(mode%2)};
}
std::complex<double> dft(const std::vector<double>& x,double hz,double rate)
{
  std::complex<double> sum{},phase{1.,0.};const auto step=std::polar(1.,-2.*std::numbers::pi*hz/rate);
  for(double v:x){sum+=v*phase;phase*=step;}return sum;
}
std::complex<double> analytic(double hz,double rate,std::size_t mode)
{
  constexpr std::array<std::array<double,2>,6> db{{{6.,0.},{3.,3.},{3.,0.},{0.,3.},{-3.,3.},{-6.,6.}}};
  std::complex<double> result{1.,0.};
  for(std::size_t i=0;i<2;++i)
  {
    const double a=std::pow(10.,db[mode][i]/20.);
    const std::complex<double> s{0.,std::tan(std::numbers::pi*hz/rate)/std::tan(std::numbers::pi*(i?2500.:250.)/rate)};
    result*=i?a*(s+1./std::sqrt(a))/(s+std::sqrt(a)):(s+std::sqrt(a))/(s+1./std::sqrt(a));
  }return result;
}
int main(int argc,char**argv)
{
  if(argc!=2)return 2;
  std::ofstream csv(std::string(argv[1])+"/responses.csv");
  if(!csv)return 2;
  csv<<"sample_rate_hz,mode,frequency_hz,measured_db,target_db,error_db,phase_deg\n"<<std::setprecision(12);
  constexpr std::array<double,6> rates{44100.,48000.,88200.,96000.,176400.,192000.};
  constexpr std::array<const char*,6> names{"F/L","F/H","C/L","C/H","T/L","T/H"};
  double maxDbError=0.,maxComplexError=0.,maxHarmonic=0.,maxTransitionError=0.;
  for(double rate:rates)for(std::size_t mode=0;mode<6;++mode)
  {
    const auto n=static_cast<std::size_t>(rate*.1);std::vector<double>x(n),y(n);x[0]=1.;
    P p;p.setControls(controls(mode));p.prepare(rate,n);p.processBlock(x,y);
    for(double hz:{20.,40.,80.,125.,250.,500.,1000.,2500.,5000.,8000.,10000.})
    {
      const auto measured=dft(y,hz,rate),target=analytic(hz,rate,mode);
      const double db=20.*std::log10(std::abs(measured)),expected=20.*std::log10(std::abs(target));
      maxDbError=std::max(maxDbError,std::abs(db-expected));maxComplexError=std::max(maxComplexError,std::abs(measured-target));
      csv<<rate<<','<<names[mode]<<','<<hz<<','<<db<<','<<expected<<','<<db-expected<<','<<std::arg(measured)*180./std::numbers::pi<<'\n';
    }
    p.reset();for(std::size_t i=0;i<n;++i)x[i]=.7*std::sin(2.*std::numbers::pi*1000.*static_cast<double>(i)/rate);
    p.processBlock(x,y);p.processBlock(x,y);const double fundamental=std::abs(dft(y,1000.,rate));
    for(int h=2;h<=10;++h)maxHarmonic=std::max(maxHarmonic,std::abs(dft(y,1000.*h,rate))/fundamental);
  }
  for(double rate:rates)
  {
    const auto warm=static_cast<std::size_t>(rate*.04),fade=static_cast<std::size_t>(std::ceil(rate*.01)),n=warm+fade+10;
    for(int stimulus=0;stimulus<3;++stimulus)
    {
      std::vector<double>x(n),y(n);std::array<std::vector<double>,6>ref;
      for(std::size_t i=0;i<n;++i)x[i]=stimulus==0?1.:stimulus==1?std::sin(2.*std::numbers::pi*83.*static_cast<double>(i)/rate):std::sin(.027*static_cast<double>(i))+.5*std::cos(.173*static_cast<double>(i));
      for(std::size_t mode=0;mode<6;++mode){P p;p.setControls(controls(mode));p.prepare(rate,n);ref[mode].resize(n);p.processBlock(x,ref[mode]);}
      for(std::size_t from=0;from<6;++from)for(std::size_t to=0;to<6;++to)
      {
        P p;p.setControls(controls(from));p.prepare(rate,n);p.processBlock(std::span{x}.first(warm),std::span{y}.first(warm));p.setControls(controls(to));p.processBlock(std::span{x}.subspan(warm),std::span{y}.subspan(warm));
        for(std::size_t i=warm;i<n;++i){const double t=std::min(1.,static_cast<double>(i-warm)/static_cast<double>(fade-1));maxTransitionError=std::max(maxTransitionError,std::abs(y[i]-((1.-t)*ref[from][i]+t*ref[to][i])));}
      }
    }
  }
  csv.close();
  if(!csv)return 2;
  std::cout<<std::setprecision(12)<<"{\n  \"profile\": \"JROCKETT-AH-BOOST-BEHAVIORAL-V1\",\n  \"hardware_measured\": false,\n  \"response_rows\": 396,\n  \"maximum_db_error\": "<<maxDbError<<",\n  \"maximum_complex_error\": "<<maxComplexError<<",\n  \"maximum_harmonic_dbc\": "<<20.*std::log10(maxHarmonic)<<",\n  \"maximum_warm_crossfade_error\": "<<maxTransitionError<<",\n  \"processor_bytes\": "<<sizeof(P)<<",\n  \"benchmarks\": [\n";
  // Callback timing includes priming; all buffers/objects prepared outside timing.
  double checksum=0.;bool first=true;
  for(double rate:{48000.,192000.,768000.})for(bool changing:{false,true})
  {
    P p;p.prepare(rate,64);std::array<double,64>x{},y{};for(std::size_t i=0;i<64;++i)x[i]=.3*std::sin(.17*static_cast<double>(i));
    for(int i=0;i<300;++i)p.processBlock(x,y);
    std::vector<double>times;times.reserve(20000);double total=0.;
    for(std::size_t i=0;i<20000;++i)
    {
      if(changing)p.setControls(controls(i%6));
      const auto begin=std::chrono::steady_clock::now();p.processBlock(x,y);const auto end=std::chrono::steady_clock::now();
      const double ns=std::chrono::duration<double,std::nano>(end-begin).count();times.push_back(ns);total+=ns;checksum+=y[i%64];
    }
    std::sort(times.begin(),times.end());
    if(!first)std::cout<<",\n";first=false;
    std::cout<<"    {\"rate\": "<<rate<<", \"changing_every_block\": "<<(changing?"true":"false")<<", \"block\": 64, \"mean_ns_per_sample\": "<<total/(20000.*64.)<<", \"p99_callback_us\": "<<times[19800]/1000.<<", \"max_callback_us\": "<<times.back()/1000.<<", \"mean_audio_budget_percent\": "<<total/(20000.*64.)*rate/1e7<<'}';
  }
  std::cout<<"\n  ],\n  \"benchmark_checksum\": "<<checksum<<"\n}\n";
  return maxDbError<.05 && maxHarmonic<1e-10 && maxTransitionError<2e-9 ? 0:1;
}
