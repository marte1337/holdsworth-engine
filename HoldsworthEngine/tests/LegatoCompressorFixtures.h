#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace holdsworth::test::legato
{
inline constexpr std::array<double, 6> rates{44100.,48000.,88200.,96000.,176400.,192000.};
struct Note { std::size_t start = 0, end = 0; double levelDb = 0.; bool picked = false; };
struct Fixture { std::string name; std::vector<double> input; std::vector<Note> notes; };
inline double amplitude(double db) { return std::pow(10.,db/20.); }
inline std::size_t frames(double seconds, double rate) { return static_cast<std::size_t>(std::llround(seconds*rate)); }
inline void addNote(Fixture& f, double rate, double seconds, double levelDb, bool picked, double hz)
{
  const auto start = f.input.size(), count = frames(seconds,rate);
  f.notes.push_back({start,start+count,levelDb,picked});
  constexpr std::array<double,5> harmonics{1.,.35,.2,.1,.07};
  const double normalization = 1./std::sqrt((1.+.35*.35+.2*.2+.1*.1+.07*.07)*.5);
  for (std::size_t i = 0; i < count; ++i)
  {
    const double t = static_cast<double>(i)/rate;
    const double onset = std::min(1.,t/.002);
    const double transient = picked ? 1.+.4*std::exp(-t/.008) : 1.;
    double carrier = 0.;
    for (std::size_t h = 0; h < harmonics.size(); ++h)
      carrier += harmonics[h]*std::sin(2.*std::numbers::pi*hz*static_cast<double>(h+1)*t+.2*static_cast<double>(h));
    f.input.push_back(.775*amplitude(levelDb)*normalization*carrier*onset*transient);
  }
}
inline std::vector<Fixture> fixtures(double rate)
{
  std::vector<Fixture> all;
  Fixture single{"pick-weak-150ms",{}, {}};
  addNote(single,rate,.300,-12.,true,110.); addNote(single,rate,.150,-24.,false,146.832);
  all.push_back(std::move(single));
  Fixture multiple{"pick-multiple",{}, {}};
  addNote(multiple,rate,.300,-12.,true,110.);
  for (int i=0;i<3;++i) addNote(multiple,rate,.150,-12.-std::array<double,3>{8.,12.,16.}[static_cast<std::size_t>(i)],false,146.832+30.*i);
  all.push_back(std::move(multiple));
  Fixture alternating{"alternating",{}, {}};
  for (int i=0;i<8;++i) addNote(alternating,rate,.100,i%2 ? -24. : -12.,i%2==0,110.+20.*i);
  all.push_back(std::move(alternating));
  for (int spacing : {20,40,60,80,120})
  {
    Fixture run{"rapid-"+std::to_string(spacing)+"ms",{}, {}};
    addNote(run,rate,.300,-12.,true,110.);
    for (int i=0;i<6;++i)
      addNote(run,rate,.001*spacing,-12.-std::array<double,6>{8.,12.,16.,12.,8.,12.}[static_cast<std::size_t>(i)],false,146.832+23.*i);
    all.push_back(std::move(run));
  }
  Fixture decay{"sustained-decay",{}, {}};
  addNote(decay,rate,2.,-12.,true,110.);
  for (std::size_t i=0;i<decay.input.size();++i) decay.input[i] *= std::exp(-static_cast<double>(i)/(rate*.45));
  all.push_back(std::move(decay));
  Fixture silence{"silence-note",std::vector<double>(frames(.250,rate),0.), {}};
  addNote(silence,rate,.300,-24.,false,110.); all.push_back(std::move(silence));
  Fixture noise{"noise-hum",{}, {}};
  std::uint64_t state = 0x3c791faULL;
  for (std::size_t i=0;i<frames(1.,rate);++i)
  {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    const double white = static_cast<double>(state >> 11)*0x1p-53*2.-1.;
    noise.input.push_back(.775*amplitude(-66.)*(white+.5*std::sin(2.*std::numbers::pi*50.*static_cast<double>(i)/rate)));
  }
  all.push_back(std::move(noise));
  Fixture equal{"equal-level",{}, {}};
  for (double hz : {82.4069,110.,220.,440.,1000.}) addNote(equal,rate,.400,-12.,false,hz);
  all.push_back(std::move(equal));
  return all;
}
inline double rmsDb(const std::vector<double>& x, std::size_t start, std::size_t end)
{
  double power = 0.;
  for (auto i=start;i<end;++i) power += x[i]*x[i];
  return 10.*std::log10(std::max(power/static_cast<double>(end-start),1.e-30));
}
} // namespace holdsworth::test::legato
