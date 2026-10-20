#include "../integration/DevelopmentLegatoCompressorControls.h"
#include "../integration/DevelopmentAHOuterTransition.h"
#include "../dsp/TCBLDCleanBoostProcessor.h"
#include "../dsp/MC402CleanBoostProcessor.h"
#include "../dsp/JRockettAHPedal.h"
#include "LegatoCompressorFixtures.h"
#include "TestHarness.h"
#include <array>
#include <bit>
#include <limits>

namespace holdsworth::test
{
namespace
{
using P=dsp::LegatoCompressorProcessor;
using TC=dsp::TCBLDCleanBoostProcessor;
bool calibration()
{
  for(double rate:legato::rates)
    for(double modelCalibration:{-6.,6.,12.,18.}) for(bool pedalOff:{false,true})
    {
      auto f=legato::fixtures(rate).front();P reference;dsp::LegatoCompressorControls c;c.enabled=true;
      reference.setControls(c);reference.prepare(rate,f.input.size());reference.beginBlock({true,1.});
      std::vector<dsp::LegatoCompressorDiagnostics> trace;trace.reserve(f.input.size());
      for(double volts:f.input){(void)reference.processSample(volts);trace.push_back(reference.diagnostics());}
      for(double hostCalibration:{-6.,0.,6.,12.,18.,24.})
      {
        P p;p.setControls(c);p.prepare(rate,f.input.size());
        const double trim=legato::amplitude(3.),host=trim*TC::fullScalePeakVolts(hostCalibration);
        const double stock=legato::amplitude(3.+hostCalibration-modelCalibration);
        const double selected=pedalOff ? stock : host;
        p.beginBlock(integration::legatoDetectorContext(true,host,selected));
        for(std::size_t i=0;i<f.input.size();++i)
        {
          // Same physical waveform, including exactly the same input trim.
          const double normalized=f.input[i]/host;
          const double native=normalized*selected;
          (void)p.processSample(native);const auto d=p.diagnostics();
          if(std::abs(d.levelDb-trace[i].levelDb)>2e-11||std::abs(d.appliedGain-trace[i].appliedGain)>2e-13
             ||std::abs(d.power-trace[i].power)>2e-13)return false;
        }
      }
    }
  return true;
}
bool mappingsAndUnits()
{
  using C=integration::LegatoControl;
  for(auto kind:{C::calibratedThreshold,C::normalizedThreshold,C::attack,C::release,C::makeup})
    for(double p:{0.,.25,.5,.75,1.})
      if(!expectNear("Legato UI mapping roundtrip",integration::legatoPosition(kind,integration::legatoValue(kind,p)),p,1e-14))return false;
  P p;dsp::LegatoCompressorControls c;c.calibratedThresholdDb=-36.;c.normalizedThresholdDb=0.;
  p.setControls(c);p.prepare(48000.,64);p.beginBlock(integration::legatoDetectorContext(false,400.,1.));
  for(int i=0;i<24000;++i)(void)p.processSample(.1);
  if(p.diagnostics().calibrated||p.diagnostics().reductionDb!=0.)return false;
  integration::LegatoMeterTransport meter;meter.publish(p.diagnostics());const auto m=meter.snapshot();
  if(m.calibrated||std::abs(m.levelDb+20.)>.001)return false;
  p.beginBlock({true,1.});for(int i=0;i<24000;++i)(void)p.processSample(.1);
  meter.publish(p.diagnostics());return meter.snapshot().calibrated&&p.diagnostics().reductionDb>5.;
}
bool domainBypass()
{
  for(double rate:legato::rates)
  {
    dsp::TCBLDCleanBoostProcessor tcA,tcB;dsp::MC402CleanBoostProcessor mcA,mcB;dsp::JRockettAHPedal ahA,ahB;
    for(auto* p:{&tcA,&tcB})p->prepare(rate,64);for(auto* p:{&mcA,&mcB})p->prepare(rate,64);
    for(auto* p:{&ahA,&ahB}){p->setSections(true,true);p->prepare(rate,64);}
    integration::DevelopmentAHOuterTransition a,b;a.prepare(rate);b.prepare(rate);P comp;comp.prepare(rate,64);
    for(unsigned choice:{0U,1U,2U,3U,0U})for(int block=0;block<50;++block)
    {
      const auto ch=static_cast<integration::DevelopmentPreNAMProcessor>(choice);
      a.beginBlock(ch,64,tcA,mcA,ahA);b.beginBlock(ch,64,tcB,mcB,ahB);
      std::array<double,64>x{},y{};
      for(std::size_t i=0;i<x.size();++i)x[i]=y[i]=.1*std::sin(.173*static_cast<double>(i+64*static_cast<std::size_t>(block)))*a.inputGain(1.,2.);
      comp.processBlock(x,x,integration::legatoDetectorContext(true,2.,a.inputGain(1.,2.)));
      a.processSelected(x,.5,tcA,mcA,ahA);b.processSelected(y,.5,tcB,mcB,ahB);
      if(!expectSamplesBitExact("Legato bypass leaves all pedal arithmetic",x,y))return false;
      if(a.latencySamples()!=32||b.latencySamples()!=32)return false;
    }
  }
  return true;
}
bool historyAndModeChanges()
{
  for(double rate:legato::rates)
  {
    P regular,history;dsp::LegatoCompressorControls c;c.enabled=true;
    for(auto* p:{&regular,&history}){p->setControls(c);p->prepare(rate,64);}
    std::array<double,64>x{},y{};x.fill(.2);
    for(int block=0;block<1000;++block)
    {
      const dsp::LegatoDetectorContext context{block%31<16,block%7==0 ? .5 : 1.};
      regular.processBlock(x,y,context);history.beginBlock(context);
      for(double v:x)(void)history.processSample(v);
      if(regular.diagnostics().appliedGain!=history.diagnostics().appliedGain||regular.diagnostics().power!=history.diagnostics().power)return false;
    }
  }
  return true;
}
}
TestSuite legatoCompressorIntegrationTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Legato physical calibration equivalence",calibration},TestCase{"Legato UI mappings and explicit units",mappingsAndUnits},
    TestCase{"Legato pedal domain exact bypass regression",domainBypass},TestCase{"Legato host history and unit changes",historyAndModeChanges}};
  return tests;
}
}
