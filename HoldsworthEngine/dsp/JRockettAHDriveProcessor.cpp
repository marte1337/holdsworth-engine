#include "JRockettAHDriveProcessor.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace holdsworth::dsp
{
namespace
{
double normalized(double x, double fallback) noexcept
{
  return std::isfinite(x) ? std::clamp(x, 0., 1.) : fallback;
}
// Only called during prepare. Convergent I0 series for the frozen Kaiser beta.
double besselI0(double x) noexcept
{
  double sum = 1., term = 1.;
  for (unsigned k = 1; k <= 64; ++k)
  {
    term *= x*x / (4.*static_cast<double>(k*k));
    sum += term;
    if (term < sum*1e-17) break;
  }
  return sum;
}
void shelf(double db, double hz, double fs, bool high, double* c) noexcept
{
  if (db == 0.) { c[0]=1.; c[1]=c[2]=0.; return; }
  const double a=std::pow(10.,db/20.), root=std::sqrt(a);
  const double k=std::tan(std::numbers::pi*hz/fs);
  const double pole=high?k*root:k/root, zero=high?k/root:k*root;
  const double scale=high?a:1.;
  c[0]=scale*(1.+zero)/(1.+pole);
  c[1]=scale*(zero-1.)/(1.+pole);
  c[2]=(pole-1.)/(1.+pole);
}
} // namespace

JRockettAHDriveProcessor::JRockettAHDriveProcessor() noexcept
{
  mTarget=makeTarget({});
  mCurrent=mGoal=mTarget.values;
}

double JRockettAHDriveProcessor::volumeAmplitude(double volume) noexcept
{
  static_assert(Profile::volumeExponent==3.0); // cubic implementation and profile stay coupled
  const double v=normalized(volume,Profile::defaultVolume);
  return Profile::maximumVolumeGain*v*v*v;
}

JRockettAHDriveProcessor::Target JRockettAHDriveProcessor::makeTarget(
  const JRockettAHDriveControls& input) const noexcept
{
  Target t;
  auto& c=t.controls;
  c={normalized(input.gain,Profile::defaultGain), normalized(input.bass,Profile::defaultTone),
     normalized(input.treble,Profile::defaultTone), normalized(input.volume,Profile::defaultVolume)};
  auto& v=t.values;
  v[0]=std::pow(10.,Profile::maximumGainDb*c.gain/20.);
  v[1]=std::pow(v[0],Profile::compensationExponent);
  const double range=Profile::maximumToneDb-Profile::minimumToneDb;
  shelf(Profile::minimumToneDb+range*c.bass,Profile::bassHz,mSampleRate,false,v.data()+2);
  shelf(Profile::minimumToneDb+range*c.treble,Profile::trebleHz,mSampleRate,true,v.data()+5);
  v[8]=volumeAmplitude(c.volume);
  return t;
}

void JRockettAHDriveProcessor::setControls(const JRockettAHDriveControls& controls) noexcept
{
  // All coefficient/power work is on the sole producer, never processBlock.
  mMailbox[mWriter]=makeTarget(controls);
  mWriter=mMiddle.exchange(mWriter|kDirty,std::memory_order_acq_rel)&3U;
}

void JRockettAHDriveProcessor::adoptTarget() noexcept
{
  if ((mMiddle.load(std::memory_order_acquire)&kDirty)!=0)
  {
    mReader=mMiddle.exchange(mReader,std::memory_order_acq_rel)&3U;
    mTarget=mMailbox[mReader];
  }
}

void JRockettAHDriveProcessor::makeFIR() noexcept
{
  constexpr double center=static_cast<double>(Profile::firTaps-1)/2.;
  const double norm=besselI0(Profile::firKaiserBeta);
  const double cutoff=2.*Profile::firCutoffBaseRatio/static_cast<double>(Profile::oversampling);
  double sum=0.;
  for (std::size_t i=0;i<Profile::firTaps;++i)
  {
    const double t=static_cast<double>(i)-center;
    const double sinc=t==0.?cutoff:std::sin(std::numbers::pi*cutoff*t)/(std::numbers::pi*t);
    const double position=t/center;
    mFIR[i]=sinc*besselI0(Profile::firKaiserBeta*std::sqrt(std::max(0.,1.-position*position)))/norm;
    sum+=mFIR[i];
  }
  mPhases={};
  for (std::size_t i=0;i<Profile::firTaps;++i)
  {
    mFIR[i]/=sum;
    mPhases[i%Profile::oversampling][i/Profile::oversampling]=mFIR[i]*static_cast<double>(Profile::oversampling);
  }
}

void JRockettAHDriveProcessor::prepare(double fs, std::size_t maximumBlockSize)
{
  if (std::find(Profile::supportedRates.begin(),Profile::supportedRates.end(),fs)==Profile::supportedRates.end()
      || maximumBlockSize==0)
    throw std::invalid_argument("Invalid J. Rockett AH Drive rate/block contract");
  adoptTarget();
  mSampleRate=fs;
  mTarget=makeTarget(mTarget.controls); // rebuild coefficients if re-preparing at a new rate
  mRampSamples=static_cast<std::size_t>(std::ceil(Profile::smoothingSeconds*fs));
  mMaximumBlockSize=maximumBlockSize;
  makeFIR();
  mPrepared=true;
  reset();
}

void JRockettAHDriveProcessor::reset() noexcept
{
  adoptTarget();
  mCurrent=mGoal=mTarget.values;
  mStep={}; mRemaining={};
  mInputHistory={}; mNonlinearHistory={};
  mInputIndex=mNonlinearIndex=0;
  mBassState=mTrebleState=0.;
}

void JRockettAHDriveProcessor::startRamps() noexcept
{
  for (std::size_t group=0;group<mRemaining.size();++group)
  {
    bool changed=false;
    for (std::size_t j=kOffsets[group];j<kOffsets[group+1];++j)
      changed |= mGoal[j]!=mTarget.values[j];
    if (!changed) continue; // duplicate publications do not prolong a ramp
    mRemaining[group]=mRampSamples*(group==0?Profile::oversampling:1);
    for (std::size_t j=kOffsets[group];j<kOffsets[group+1];++j)
    {
      mGoal[j]=mTarget.values[j];
      mStep[j]=(mGoal[j]-mCurrent[j])/static_cast<double>(mRemaining[group]);
    }
  }
}

void JRockettAHDriveProcessor::advance(std::size_t group) noexcept
{
  if (mRemaining[group]==0) return;
  --mRemaining[group];
  for (std::size_t j=kOffsets[group];j<kOffsets[group+1];++j)
    mCurrent[j]=mRemaining[group]==0?mGoal[j]:mCurrent[j]+mStep[j];
}

double JRockettAHDriveProcessor::shelfTick(double x, const double* c, double& state) noexcept
{
  const double y=c[0]*x+state;
  state=c[1]*x-c[2]*y;
  return y;
}

void JRockettAHDriveProcessor::processBlock(std::span<const double> input, std::span<double> output) noexcept
{
  assert(mPrepared && input.size()==output.size() && input.size()<=mMaximumBlockSize);
  if (!mPrepared || input.size()!=output.size() || input.size()>mMaximumBlockSize)
  {
    if (input.data()!=output.data())
      std::copy_n(input.data(),std::min(input.size(),output.size()),output.data());
    return;
  }
  if (input.empty()) return;
  adoptTarget(); startRamps();
  for (std::size_t i=0;i<input.size();++i)
  {
    advance(1); advance(2); advance(3);
    // Numerical double-overflow guard only; far outside even torture inputs.
    constexpr double limit=std::numeric_limits<double>::max()/4096.;
    const double x=std::isfinite(input[i])?std::clamp(input[i],-limit,limit):0.;
    const double bass=shelfTick(x,mCurrent.data()+2,mBassState);
    mInputIndex=mInputIndex==0?kPhaseTaps-1:mInputIndex-1;
    mInputHistory[mInputIndex]=mInputHistory[mInputIndex+kPhaseTaps]=bass;
    double decimated=0.;
    for (std::size_t phase=0;phase<Profile::oversampling;++phase)
    {
      advance(0);
      double up=0.;
      for (std::size_t j=0;j<kPhaseTaps;++j)
        up+=mPhases[phase][j]*mInputHistory[mInputIndex+j];
      const double nonlinear=Profile::referenceVolts*mCurrent[1]*std::asinh(mCurrent[0]*up/Profile::referenceVolts);
      mNonlinearIndex=mNonlinearIndex==0?Profile::firTaps-1:mNonlinearIndex-1;
      mNonlinearHistory[mNonlinearIndex]=mNonlinearHistory[mNonlinearIndex+Profile::firTaps]=nonlinear;
      // Causal decimation observes phase zero, exactly as upfirdn(..., down=4).
      if (phase==0)
        for (std::size_t j=0;j<Profile::firTaps;++j)
          decimated+=mFIR[j]*mNonlinearHistory[mNonlinearIndex+j];
    }
    const double y=shelfTick(decimated,mCurrent.data()+5,mTrebleState);
    output[i]=mCurrent[8]==0.?0.:y*mCurrent[8]; // exact positive-zero mute, states keep running
  }
}
} // namespace holdsworth::dsp
