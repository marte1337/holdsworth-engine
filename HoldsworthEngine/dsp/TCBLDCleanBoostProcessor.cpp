#include "TCBLDCleanBoostProcessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wdeprecated-anon-enum-enum-conversion"
  #pragma clang diagnostic ignored "-Wsign-conversion"
#endif
#include "../../eigen/Eigen/Eigenvalues"

namespace holdsworth::dsp
{
namespace
{

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

constexpr std::size_t kNodeCount = 33;
constexpr std::size_t kVariableResistanceCount = 5;
constexpr std::size_t kUnknownCount = kNodeCount + kVariableResistanceCount;
constexpr std::size_t kStateCount = 24;
constexpr std::size_t kGround = kUnknownCount;

using RealVector = std::array<double, kUnknownCount>;
using RealMatrix = std::array<RealVector, kUnknownCount>;
using StateInjection = std::array<std::array<double, kStateCount>, kUnknownCount>;
using StateObservation = std::array<std::array<double, kUnknownCount>, kStateCount>;
using StateMatrix = std::array<std::array<double, kStateCount>, kStateCount>;
using ComplexStateVector = std::array<std::complex<double>, kStateCount>;
using ComplexStateMatrix = std::array<ComplexStateVector, kStateCount>;
using EigenRealMatrix = Eigen::Matrix<double, kStateCount, kStateCount>;
using EigenRealVector = Eigen::Matrix<double, kStateCount, 1>;
using EigenRealRowVector = Eigen::Matrix<double, 1, kStateCount>;
using EigenComplexVector = Eigen::Matrix<std::complex<double>, kStateCount, 1>;

constexpr Eigen::Index kFitPointCount = 128;
constexpr Eigen::Index kFitCoefficientCount =
  static_cast<Eigen::Index>(kStateCount + 1);
using FitMatrix = Eigen::Matrix<double, 2 * kFitPointCount, kFitCoefficientCount>;
using FitTarget = Eigen::Matrix<double, 2 * kFitPointCount, 1>;
using FitCoefficients = Eigen::Matrix<double, kFitCoefficientCount, 1>;

enum Node : std::size_t
{
  inputTip,
  inputBias,
  ic1Pin5,
  ic1Pin6,
  o1,
  o1Internal,
  asymmetricClamp,
  postClip,
  r23C15,
  ic1Pin9,
  og,
  ogInternal,
  nrefAudio,
  vref,
  q1Drain,
  toneInput,
  p2Left,
  p2Wiper,
  p2Right,
  toneShape,
  p3Left,
  p3Wiper,
  p3Right,
  ic1Pin13,
  q2Drain,
  out14,
  out14Internal,
  nreturn,
  r35R34,
  control2,
  c26R16,
  outputCoupled,
  outputHot
};

enum VariableResistance : std::size_t
{
  p1Current = kNodeCount,
  p2aCurrent,
  p2bCurrent,
  p3aCurrent,
  p3bCurrent
};

struct DynamicBranch final
{
  std::size_t positive;
  std::size_t negative;
  double storage;
  bool isOpAmpPole = false;
};

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSqrtTwoTimesPoint775 = 1.0960155108391487;
constexpr double kLogTaperGamma = 3.321928094887362347870319429489390176;
constexpr double kOpAmpOpenLoopGain = 50'000.0;
constexpr double kOpAmpUnityGainBandwidthHz = 3.5e6;
constexpr double kOpAmpOutputResistanceOhm = 300.0;
constexpr double kOpAmpPoleTimeSeconds =
  kOpAmpOpenLoopGain / (2.0 * kPi * kOpAmpUnityGainBandwidthHz);

constexpr std::array<DynamicBranch, kStateCount> kDynamicBranches{{
  {vref, kGround, 22.0e-6},
  {inputTip, inputBias, 47.0e-9},
  {ic1Pin5, kGround, 220.0e-12},
  // C10 and C11 are parallel in the selected Boost follower state.
  {ic1Pin6, o1, 1.0e-6 + 220.0e-12},
  {postClip, vref, 10.0e-9},
  {r23C15, ic1Pin9, 10.0e-6},
  {o1, q1Drain, 4.7e-6},
  {q1Drain, nrefAudio, 5.0e-12},
  {q1Drain, kGround, 220.0e-12},
  {og, toneInput, 2.2e-6},
  {p2Left, p2Wiper, 100.0e-9},
  {p2Wiper, p2Right, 100.0e-9},
  {p3Left, p3Right, 1.0e-9},
  {p3Wiper, toneShape, 6.8e-9},
  {ic1Pin13, q2Drain, 1.0e-6},
  {q2Drain, out14, 5.0e-12},
  {og, nreturn, 10.0e-9},
  {control2, kGround, 10.0e-9},
  {c26R16, out14, 10.0e-9},
  {out14, outputCoupled, 22.0e-6},
  {outputHot, kGround, 100.0e-12},
  {o1Internal, kGround, kOpAmpPoleTimeSeconds, true},
  {ogInternal, kGround, kOpAmpPoleTimeSeconds, true},
  {out14Internal, kGround, kOpAmpPoleTimeSeconds, true},
}};

void addConductance(RealMatrix& matrix,
                    const std::size_t first,
                    const std::size_t second,
                    const double conductance) noexcept
{
  if (first != kGround)
    matrix[first][first] += conductance;
  if (second != kGround)
    matrix[second][second] += conductance;
  if (first != kGround && second != kGround)
  {
    matrix[first][second] -= conductance;
    matrix[second][first] -= conductance;
  }
}

void addResistance(RealMatrix& matrix,
                   const std::size_t first,
                   const std::size_t second,
                   const double resistanceOhm) noexcept
{
  addConductance(matrix, first, second, 1.0 / resistanceOhm);
}

void addVariableResistance(RealMatrix& matrix,
                           const std::size_t first,
                           const std::size_t second,
                           const std::size_t current,
                           const double resistanceOhm) noexcept
{
  if (first != kGround)
  {
    matrix[first][current] += 1.0;
    matrix[current][first] += 1.0;
  }
  if (second != kGround)
  {
    matrix[second][current] -= 1.0;
    matrix[current][second] -= 1.0;
  }
  matrix[current][current] -= resistanceOhm;
}

// The op-amp's ideal dependent source owns its internal node, so Rout is
// stamped only into the externally driven output-node KCL row.
void addOpAmp(RealMatrix& matrix,
              const std::size_t output,
              const std::size_t noninverting,
              const std::size_t inverting,
              const std::size_t internal) noexcept
{
  const double outputConductance = 1.0 / kOpAmpOutputResistanceOhm;
  matrix[output][output] += outputConductance;
  matrix[output][internal] -= outputConductance;

  matrix[internal][internal] += 1.0;
  matrix[internal][noninverting] -= kOpAmpOpenLoopGain;
  matrix[internal][inverting] += kOpAmpOpenLoopGain;
}

struct LuFactorization final
{
  RealMatrix lu{};
  std::array<std::size_t, kUnknownCount> pivotRows{};
};

[[nodiscard]] bool factorize(const RealMatrix& source,
                             LuFactorization& result) noexcept
{
  result.lu = source;
  for (std::size_t column = 0; column < kUnknownCount; ++column)
  {
    std::size_t pivotRow = column;
    double pivotMagnitude = std::abs(result.lu[column][column]);
    for (std::size_t row = column + 1; row < kUnknownCount; ++row)
    {
      const double candidate = std::abs(result.lu[row][column]);
      if (candidate > pivotMagnitude)
      {
        pivotMagnitude = candidate;
        pivotRow = row;
      }
    }
    if (!std::isfinite(pivotMagnitude) || pivotMagnitude < 1.0e-18)
      return false;

    result.pivotRows[column] = pivotRow;
    if (pivotRow != column)
      std::swap(result.lu[column], result.lu[pivotRow]);

    const double diagonal = result.lu[column][column];
    for (std::size_t row = column + 1; row < kUnknownCount; ++row)
    {
      const double multiplier = result.lu[row][column] / diagonal;
      result.lu[row][column] = multiplier;
      for (std::size_t inner = column + 1; inner < kUnknownCount; ++inner)
        result.lu[row][inner] -= multiplier * result.lu[column][inner];
    }
  }
  return true;
}

[[nodiscard]] bool solve(const LuFactorization& factorization,
                         const RealVector& rightHandSide,
                         RealVector& solution) noexcept
{
  solution = rightHandSide;
  for (std::size_t row = 0; row < kUnknownCount; ++row)
  {
    const std::size_t pivotRow = factorization.pivotRows[row];
    if (pivotRow != row)
      std::swap(solution[row], solution[pivotRow]);
    for (std::size_t column = 0; column < row; ++column)
      solution[row] -= factorization.lu[row][column] * solution[column];
  }

  for (std::size_t reverse = 0; reverse < kUnknownCount; ++reverse)
  {
    const std::size_t row = kUnknownCount - 1 - reverse;
    for (std::size_t column = row + 1; column < kUnknownCount; ++column)
      solution[row] -= factorization.lu[row][column] * solution[column];
    solution[row] /= factorization.lu[row][row];
    if (!std::isfinite(solution[row]))
      return false;
  }
  return true;
}

[[nodiscard]] bool solveComplex(ComplexStateMatrix matrix,
                                ComplexStateVector rightHandSide,
                                ComplexStateVector& solution) noexcept
{
  for (std::size_t column = 0; column < kStateCount; ++column)
  {
    std::size_t pivotRow = column;
    double pivotMagnitude = std::abs(matrix[column][column]);
    for (std::size_t row = column + 1; row < kStateCount; ++row)
    {
      const double candidate = std::abs(matrix[row][column]);
      if (candidate > pivotMagnitude)
      {
        pivotMagnitude = candidate;
        pivotRow = row;
      }
    }
    if (!std::isfinite(pivotMagnitude) || pivotMagnitude < 1.0e-18)
      return false;
    if (pivotRow != column)
    {
      std::swap(matrix[column], matrix[pivotRow]);
      std::swap(rightHandSide[column], rightHandSide[pivotRow]);
    }

    const std::complex<double> diagonal = matrix[column][column];
    for (std::size_t row = column + 1; row < kStateCount; ++row)
    {
      const std::complex<double> multiplier = matrix[row][column] / diagonal;
      matrix[row][column] = 0.0;
      for (std::size_t inner = column + 1; inner < kStateCount; ++inner)
        matrix[row][inner] -= multiplier * matrix[column][inner];
      rightHandSide[row] -= multiplier * rightHandSide[column];
    }
  }

  for (std::size_t reverse = 0; reverse < kStateCount; ++reverse)
  {
    const std::size_t row = kStateCount - 1 - reverse;
    std::complex<double> remainder = rightHandSide[row];
    for (std::size_t column = row + 1; column < kStateCount; ++column)
      remainder -= matrix[row][column] * solution[column];
    solution[row] = remainder / matrix[row][row];
    if (!std::isfinite(solution[row].real()) || !std::isfinite(solution[row].imag()))
      return false;
  }
  return true;
}

[[nodiscard]] double clampControl(const double value, const double fallback) noexcept
{
  return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
}

[[nodiscard]] bool solveRealSchurSystem(
  const EigenRealMatrix& schur,
  const EigenRealVector& rightHandSide,
  const std::complex<double> z,
  EigenComplexVector& solution) noexcept
{
  solution.setZero();
  Eigen::Index row = static_cast<Eigen::Index>(kStateCount) - 1;
  while (row >= 0)
  {
    const bool isTwoStateBlock =
      row > 0 && std::abs(schur(row, row - 1)) > 1.0e-12;
    if (isTwoStateBlock)
    {
      std::complex<double> firstRemainder = rightHandSide(row - 1);
      std::complex<double> secondRemainder = rightHandSide(row);
      for (Eigen::Index column = row + 1;
           column < static_cast<Eigen::Index>(kStateCount);
           ++column)
      {
        firstRemainder += schur(row - 1, column) * solution(column);
        secondRemainder += schur(row, column) * solution(column);
      }

      const std::complex<double> firstDiagonal = z - schur(row - 1, row - 1);
      const std::complex<double> secondDiagonal = z - schur(row, row);
      const double upper = schur(row - 1, row);
      const double lower = schur(row, row - 1);
      const std::complex<double> determinant =
        firstDiagonal * secondDiagonal - upper * lower;
      if (std::abs(determinant) < 1.0e-18)
        return false;
      solution(row - 1) =
        (secondDiagonal * firstRemainder + upper * secondRemainder) / determinant;
      solution(row) =
        (lower * firstRemainder + firstDiagonal * secondRemainder) / determinant;
      row -= 2;
    }
    else
    {
      std::complex<double> remainder = rightHandSide(row);
      for (Eigen::Index column = row + 1;
           column < static_cast<Eigen::Index>(kStateCount);
           ++column)
      {
        remainder += schur(row, column) * solution(column);
      }
      const std::complex<double> denominator = z - schur(row, row);
      if (std::abs(denominator) < 1.0e-18)
        return false;
      solution(row) = remainder / denominator;
      --row;
    }
  }

  for (Eigen::Index index = 0;
       index < static_cast<Eigen::Index>(kStateCount);
       ++index)
  {
    if (!std::isfinite(solution(index).real())
        || !std::isfinite(solution(index).imag()))
      return false;
  }
  return true;
}

} // namespace

void TCBLDCleanBoostProcessor::prepare(const double sampleRate,
                                       const std::size_t maximumBlockSize)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || maximumBlockSize == 0)
    throw std::invalid_argument("TCBLDCleanBoostProcessor requires a positive sample rate and block size");

  mSampleRate = sampleRate;
  mMaximumBlockSize = maximumBlockSize;
  Coefficients coefficients;
  if (!buildCoefficients(mControls, coefficients))
  {
    mSampleRate = 0.0;
    mMaximumBlockSize = 0;
    throw std::runtime_error("TCBLDCleanBoostProcessor could not factor the M1 nominal circuit");
  }
  mCoefficients = coefficients;
  for (auto& slot : mCoefficientQueue)
  {
    slot.sequence.store(0, std::memory_order_relaxed);
    slot.status.store(kCoefficientSlotFree, std::memory_order_relaxed);
  }
  mNextCoefficientSequence = 1;
  mPrepared = true;
  reset();
}

void TCBLDCleanBoostProcessor::reset() noexcept
{
  mState.fill(0.0);
}

TCBLDCleanBoostControls TCBLDCleanBoostProcessor::sanitizeControls(
  const TCBLDCleanBoostControls& controls) noexcept
{
  const TCBLDCleanBoostControls defaults;
  return {
    clampControl(controls.gain, defaults.gain),
    clampControl(controls.bass, defaults.bass),
    clampControl(controls.treble, defaults.treble),
  };
}

bool TCBLDCleanBoostProcessor::setControls(
  const TCBLDCleanBoostControls& requestedControls) noexcept
{
  const TCBLDCleanBoostControls controls = sanitizeControls(requestedControls);
  if (controls.gain == mControls.gain && controls.bass == mControls.bass
      && controls.treble == mControls.treble)
    return true;

  if (mPrepared)
  {
    Coefficients candidate;
    if (!buildCoefficients(controls, candidate))
      return false;

    std::size_t destination = kCoefficientQueueCapacity;
    for (std::size_t index = 0; index < kCoefficientQueueCapacity; ++index)
    {
      std::uint32_t expected = kCoefficientSlotFree;
      if (mCoefficientQueue[index].status.compare_exchange_strong(
            expected,
            kCoefficientSlotWriting,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
      {
        destination = index;
        break;
      }
    }
    // When audio is stopped, coalesce by reclaiming an unread complete slot.
    // A slot claimed by the audio thread is never overwritten.
    if (destination == kCoefficientQueueCapacity)
    {
      for (std::size_t index = 0; index < kCoefficientQueueCapacity; ++index)
      {
        std::uint32_t expected = kCoefficientSlotReady;
        if (mCoefficientQueue[index].status.compare_exchange_strong(
              expected,
              kCoefficientSlotWriting,
              std::memory_order_acq_rel,
              std::memory_order_acquire))
        {
          destination = index;
          break;
        }
      }
    }
    if (destination == kCoefficientQueueCapacity)
      return false;

    auto& slot = mCoefficientQueue[destination];
    slot.coefficients = candidate;
    slot.sequence.store(mNextCoefficientSequence++, std::memory_order_relaxed);
    slot.status.store(kCoefficientSlotReady, std::memory_order_release);
  }
  mControls = controls;
  return true;
}

bool TCBLDCleanBoostProcessor::buildCoefficients(
  const TCBLDCleanBoostControls& controls,
  Coefficients& destination) const noexcept
{
  if (!std::isfinite(mSampleRate) || mSampleRate <= 0.0)
    return false;

  RealMatrix matrix{};
  RealVector inputInjection{};

  // Selected M1 source fixture and input/Boost follower.
  addResistance(matrix, inputTip, kGround, 1.0e3);
  inputInjection[inputTip] += 1.0 / 1.0e3;
  addResistance(matrix, inputBias, vref, 1.0e6); // R13
  addResistance(matrix, inputBias, ic1Pin5, 2.2e3); // R14
  addResistance(matrix, ic1Pin6, o1, 470.0e3); // R15
  addOpAmp(matrix, o1, ic1Pin5, ic1Pin6, o1Internal);

  // Boost-mode retained distortion-branch loading; D4/DG1 are open in M1.
  addResistance(matrix, o1, asymmetricClamp, 4.7e3); // R21
  addResistance(matrix, asymmetricClamp, postClip, 10.0e3); // R22
  addResistance(matrix, postClip, o1, 10.0e3); // R20

  // P1 active Gain and its exact generic LOG mechanical law.
  addResistance(matrix, o1, r23C15, 1.5e3); // R23
  const double p1Resistance = 47.0e3 * std::pow(controls.gain, kLogTaperGamma);
  addVariableResistance(matrix, ic1Pin9, og, p1Current, p1Resistance);
  addOpAmp(matrix, og, nrefAudio, ic1Pin9, ogInternal);

  // Finite VREF and M1's generic Q4 small-signal base-load reduction.
  addResistance(matrix, vref, kGround, 47.0e3); // R2 to AC-grounded VPLUS
  addResistance(matrix, vref, kGround, 47.0e3); // R3
  addResistance(matrix, nrefAudio, vref, 10.0e3); // R18
  constexpr double beta = 200.0;
  constexpr double vbe = 0.65;
  constexpr double thermalVoltage = 0.02585;
  constexpr double r17 = 33.0e3;
  constexpr double r2 = 47.0e3;
  constexpr double r3 = 47.0e3;
  constexpr double r18 = 10.0e3;
  constexpr double q4Resistance = (beta + 1.0) * r17;
  constexpr double a11 = 1.0 / r2 + 1.0 / r3 + 1.0 / r18;
  constexpr double a12 = -1.0 / r18;
  constexpr double a21 = -1.0 / r18;
  constexpr double a22 = 1.0 / r18 + 1.0 / q4Resistance;
  constexpr double b1 = 9.0 / r2;
  constexpr double b2 = vbe / q4Resistance;
  constexpr double determinant = a11 * a22 - a12 * a21;
  constexpr double nominalNref = (a11 * b2 - a21 * b1) / determinant;
  constexpr double emitterCurrent = (nominalNref - vbe) / r17;
  constexpr double collectorCurrent = emitterCurrent * beta / (beta + 1.0);
  constexpr double rPi = beta * thermalVoltage / collectorCurrent;
  addResistance(matrix, nrefAudio, kGround, rPi + q4Resistance);

  // Complete Q1 audio branch with the frozen off-state envelope.
  addResistance(matrix, q1Drain, nrefAudio, 3.3e6); // R19
  addResistance(matrix, q1Drain, nrefAudio, 1.0e9); // Q1 off Rds

  // The physically coupled Bass/Treble network. The five variable resistor
  // branches keep exact zero-ohm endpoints without epsilon substitutions.
  addResistance(matrix, toneInput, p2Left, 1.5e3); // R42
  addVariableResistance(matrix, p2Left, p2Wiper, p2aCurrent, 22.0e3 * controls.bass);
  addVariableResistance(matrix, p2Wiper, p2Right, p2bCurrent, 22.0e3 * (1.0 - controls.bass));
  addResistance(matrix, p2Right, nrefAudio, 1.5e3); // R41
  addResistance(matrix, p2Wiper, toneShape, 10.0e3); // R45

  addResistance(matrix, toneInput, p3Left, 2.2e3); // R43
  addVariableResistance(matrix, p3Left, p3Wiper, p3aCurrent, 100.0e3 * controls.treble);
  addVariableResistance(matrix, p3Wiper, p3Right, p3bCurrent, 100.0e3 * (1.0 - controls.treble));
  addResistance(matrix, p3Right, nrefAudio, 1.0e3); // R44
  addResistance(matrix, toneShape, ic1Pin13, 10.0e3); // R46

  // Frozen full-level Q2 feedback and final 4741 line driver.
  addResistance(matrix, q2Drain, out14, 1.0e6); // R38
  addResistance(matrix, q2Drain, out14, 1.0e9); // Q2 off Rds
  addOpAmp(matrix, out14, nrefAudio, ic1Pin13, out14Internal);

  // Known passive suppressor/control-side loading retained by M1.
  addResistance(matrix, nreturn, r35R34, 680.0e3); // R34
  addResistance(matrix, r35R34, out14, 2.2e6); // R35 maximum profile
  addResistance(matrix, nreturn, control2, 6.8e6); // R33
  addResistance(matrix, control2, out14, 6.8e6); // R36
  addResistance(matrix, control2, c26R16, 220.0); // R16

  // Output coupling/source resistor and selected 1 MOhm load fixture.
  addResistance(matrix, outputCoupled, kGround, 10.0e3); // R39
  addResistance(matrix, outputCoupled, outputHot, 47.0); // R40
  addResistance(matrix, outputHot, kGround, 1.0e6);

  StateInjection historyInjection{};
  StateObservation branchObservation{};
  std::array<double, kStateCount> companionConductance{};
  for (std::size_t state = 0; state < kStateCount; ++state)
  {
    const DynamicBranch& branch = kDynamicBranches[state];
    const double conductance = 2.0 * mSampleRate * branch.storage;
    companionConductance[state] = conductance;

    if (branch.isOpAmpPole)
    {
      matrix[branch.positive][branch.positive] += conductance;
      historyInjection[branch.positive][state] += 1.0;
      branchObservation[state][branch.positive] += 1.0;
    }
    else
    {
      addConductance(matrix, branch.positive, branch.negative, conductance);
      if (branch.positive != kGround)
      {
        historyInjection[branch.positive][state] += 1.0;
        branchObservation[state][branch.positive] += 1.0;
      }
      if (branch.negative != kGround)
      {
        historyInjection[branch.negative][state] -= 1.0;
        branchObservation[state][branch.negative] -= 1.0;
      }
    }
  }

  LuFactorization factorization;
  if (!factorize(matrix, factorization))
    return false;

  RealVector inputSolution{};
  if (!solve(factorization, inputInjection, inputSolution))
    return false;

  std::array<RealVector, kStateCount> historySolutions{};
  for (std::size_t state = 0; state < kStateCount; ++state)
  {
    RealVector injection{};
    for (std::size_t unknown = 0; unknown < kUnknownCount; ++unknown)
      injection[unknown] = historyInjection[unknown][state];
    if (!solve(factorization, injection, historySolutions[state]))
      return false;
  }

  StateMatrix branchFromHistory{};
  std::array<double, kStateCount> branchFromInput{};
  for (std::size_t branch = 0; branch < kStateCount; ++branch)
  {
    for (std::size_t unknown = 0; unknown < kUnknownCount; ++unknown)
      branchFromInput[branch] += branchObservation[branch][unknown] * inputSolution[unknown];
    for (std::size_t history = 0; history < kStateCount; ++history)
    {
      for (std::size_t unknown = 0; unknown < kUnknownCount; ++unknown)
      {
        branchFromHistory[branch][history] +=
          branchObservation[branch][unknown] * historySolutions[history][unknown];
      }
    }
  }

  Coefficients oracleCoefficients{};
  oracleCoefficients.direct = inputSolution[outputHot];
  for (std::size_t state = 0; state < kStateCount; ++state)
  {
    oracleCoefficients.input[state] =
      2.0 * companionConductance[state] * branchFromInput[state];
    oracleCoefficients.output[state] = historySolutions[state][outputHot];
    for (std::size_t history = 0; history < kStateCount; ++history)
    {
      oracleCoefficients.transition[state][history] =
        2.0 * companionConductance[state] * branchFromHistory[state][history]
        - (state == history ? 1.0 : 0.0);
      if (!std::isfinite(oracleCoefficients.transition[state][history]))
        return false;
    }
    if (!std::isfinite(oracleCoefficients.input[state])
        || !std::isfinite(oracleCoefficients.output[state]))
      return false;
  }
  return std::isfinite(oracleCoefficients.direct)
         && buildStableApproximation(oracleCoefficients, destination);
}

bool TCBLDCleanBoostProcessor::buildStableApproximation(
  const Coefficients& oracleCoefficients,
  Coefficients& destination) const noexcept
{
  // Some reviewed M1 Gain positions contain right-half-plane small-signal
  // poles. An exact causal discretization would therefore explode even though
  // its AC solution matches the golden curves. Reflect only those poles into
  // the unit circle, apply the corresponding unity-magnitude phase factors,
  // and refit real Schur-state residues over the audition band. This retains
  // M1's coupled magnitude response while making the realtime recurrence
  // unconditionally stable. Its phase cannot equal the unstable oracle at
  // those Gain positions; stable M1 positions retain the complex response.
  EigenRealMatrix oracleTransition;
  EigenRealVector oracleInput;
  EigenRealRowVector oracleOutput;
  for (Eigen::Index row = 0; row < static_cast<Eigen::Index>(kStateCount); ++row)
  {
    oracleInput(row) = oracleCoefficients.input[static_cast<std::size_t>(row)];
    oracleOutput(row) = oracleCoefficients.output[static_cast<std::size_t>(row)];
    for (Eigen::Index column = 0;
         column < static_cast<Eigen::Index>(kStateCount);
         ++column)
    {
      oracleTransition(row, column) = oracleCoefficients.transition[
        static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    }
  }

  const Eigen::RealSchur<EigenRealMatrix> decomposition(oracleTransition, true);
  if (decomposition.info() != Eigen::Success)
    return false;

  const EigenRealMatrix oracleSchurTransition = decomposition.matrixT();
  const EigenRealVector oracleSchurInput =
    decomposition.matrixU().transpose() * oracleInput;
  const EigenRealRowVector oracleSchurOutput =
    oracleOutput * decomposition.matrixU();
  EigenRealMatrix stableTransition = oracleSchurTransition;
  const EigenRealVector stableInput = oracleSchurInput;
  std::array<std::complex<double>, kStateCount> unstablePoles{};
  std::array<std::complex<double>, kStateCount> reflectedPoles{};
  std::size_t unstablePoleCount = 0;
  constexpr double maximumPoleRadius = 0.999999;
  Eigen::Index index = 0;
  while (index < static_cast<Eigen::Index>(kStateCount))
  {
    const bool isTwoStateBlock =
      index + 1 < static_cast<Eigen::Index>(kStateCount)
      && std::abs(stableTransition(index + 1, index)) > 1.0e-12;
    if (isTwoStateBlock)
    {
      const double determinant =
        stableTransition(index, index) * stableTransition(index + 1, index + 1)
        - stableTransition(index, index + 1) * stableTransition(index + 1, index);
      const double radius = std::sqrt(std::abs(determinant));
      if (!std::isfinite(radius) || radius == 0.0)
        return false;
      double scale = 1.0;
      if (radius > 1.0 + 1.0e-7)
      {
        scale = 1.0 / (radius * radius);
        const double trace =
          stableTransition(index, index) + stableTransition(index + 1, index + 1);
        const std::complex<double> discriminant = std::sqrt(
          std::complex<double>(trace * trace - 4.0 * determinant, 0.0));
        const std::complex<double> firstPole = 0.5 * (trace + discriminant);
        const std::complex<double> secondPole = 0.5 * (trace - discriminant);
        unstablePoles[unstablePoleCount] = firstPole;
        reflectedPoles[unstablePoleCount++] = 1.0 / std::conj(firstPole);
        unstablePoles[unstablePoleCount] = secondPole;
        reflectedPoles[unstablePoleCount++] = 1.0 / std::conj(secondPole);
      }
      else if (radius > maximumPoleRadius)
        scale = maximumPoleRadius / radius;
      stableTransition.template block<2, 2>(index, index) *= scale;
      index += 2;
    }
    else
    {
      const double pole = stableTransition(index, index);
      if (!std::isfinite(pole))
        return false;
      if (std::abs(pole) > 1.0 + 1.0e-7)
      {
        stableTransition(index, index) = 1.0 / pole;
        unstablePoles[unstablePoleCount] = pole;
        reflectedPoles[unstablePoleCount++] = 1.0 / pole;
      }
      else if (std::abs(pole) > maximumPoleRadius)
        stableTransition(index, index) = std::copysign(maximumPoleRadius, pole);
      ++index;
    }
  }

  constexpr std::array<double, 6> goldenAnchorsHz{{
    22.0863781529,
    97.5616199823,
    707.106781187,
    5124.96615053,
    10771.3032004,
    17673.3855069,
  }};
  const double maximumFitFrequency = std::min(20000.0, 0.45 * mSampleRate);
  const double minimumFitFrequency = std::min(5.0, 0.01 * maximumFitFrequency);
  if (!(maximumFitFrequency > 0.0) || !(minimumFitFrequency > 0.0))
    return false;

  FitMatrix fitMatrix;
  FitTarget fitTarget;
  fitMatrix.setZero();
  fitTarget.setZero();
  for (Eigen::Index sample = 0; sample < kFitPointCount; ++sample)
  {
    const bool useGoldenAnchor =
      sample < static_cast<Eigen::Index>(goldenAnchorsHz.size())
      && goldenAnchorsHz[static_cast<std::size_t>(sample)] < maximumFitFrequency;
    const double proportion =
      static_cast<double>(sample) / static_cast<double>(kFitPointCount - 1);
    const double frequency =
      useGoldenAnchor
        ? goldenAnchorsHz[static_cast<std::size_t>(sample)]
        : minimumFitFrequency
            * std::pow(maximumFitFrequency / minimumFitFrequency, proportion);

    const std::complex<double> oracleZ =
      std::polar(1.0, 2.0 * kPi * frequency / mSampleRate);
    EigenComplexVector oracleState;
    if (!solveRealSchurSystem(
          oracleSchurTransition, oracleSchurInput, oracleZ, oracleState))
      return false;
    std::complex<double> target =
      oracleCoefficients.direct
      + (oracleSchurOutput.cast<std::complex<double>>() * oracleState)(0, 0);
    for (std::size_t pole = 0; pole < unstablePoleCount; ++pole)
    {
      const std::complex<double> correctionAtDc =
        (1.0 - unstablePoles[pole])
        / (std::abs(unstablePoles[pole]) * (1.0 - reflectedPoles[pole]));
      if (std::abs(correctionAtDc) < 1.0e-18)
        return false;
      const std::complex<double> dcPhaseNormalization =
        std::conj(correctionAtDc) / std::abs(correctionAtDc);
      target *= dcPhaseNormalization * (oracleZ - unstablePoles[pole])
                / (std::abs(unstablePoles[pole])
                   * (oracleZ - reflectedPoles[pole]));
    }
    if (!std::isfinite(target.real()) || !std::isfinite(target.imag()))
      return false;

    const double relativeWeight =
      1.0 / std::max(std::abs(target), 1.0e-8);
    fitTarget(sample) = relativeWeight * target.real();
    fitTarget(sample + kFitPointCount) = relativeWeight * target.imag();
    fitMatrix(sample, 0) = relativeWeight;

    const std::complex<double> outputZ =
      std::polar(1.0, 2.0 * kPi * frequency / mSampleRate);
    EigenComplexVector stableState;
    if (!solveRealSchurSystem(stableTransition, stableInput, outputZ, stableState))
      return false;
    for (Eigen::Index state = 0;
         state < static_cast<Eigen::Index>(kStateCount);
         ++state)
    {
      fitMatrix(sample, state + 1) = relativeWeight * stableState(state).real();
      fitMatrix(sample + kFitPointCount, state + 1) =
        relativeWeight * stableState(state).imag();
    }
  }

  const Eigen::ColPivHouseholderQR<FitMatrix> fitFactorization(fitMatrix);
  if (fitFactorization.rank() == 0)
    return false;
  const FitCoefficients fitted = fitFactorization.solve(fitTarget);
  const FitTarget fittedTarget = fitMatrix * fitted;
  for (Eigen::Index sample = 0; sample < kFitPointCount; ++sample)
  {
    const double relativeComplexError =
      std::hypot(fittedTarget(sample) - fitTarget(sample),
                 fittedTarget(sample + kFitPointCount)
                   - fitTarget(sample + kFitPointCount));
    if (!std::isfinite(relativeComplexError) || relativeComplexError > 1.0e-3)
      return false;
  }

  destination = {};
  destination.direct = fitted(0);
  for (std::size_t state = 0; state < kStateCount; ++state)
  {
    destination.input[state] = stableInput(static_cast<Eigen::Index>(state));
    destination.output[state] = fitted(static_cast<Eigen::Index>(state + 1));
    for (std::size_t history = 0; history < kStateCount; ++history)
    {
      destination.transition[state][history] = stableTransition(
        static_cast<Eigen::Index>(state), static_cast<Eigen::Index>(history));
    }
  }

  if (!std::isfinite(destination.direct))
    return false;
  for (std::size_t index = 0; index < kStateCount; ++index)
  {
    if (!std::isfinite(destination.input[index])
        || !std::isfinite(destination.output[index]))
      return false;
    for (const double value : destination.transition[index])
    {
      if (!std::isfinite(value))
        return false;
    }
  }
  return true;
}

#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

void TCBLDCleanBoostProcessor::processBlock(const std::span<const Sample> input,
                                            const std::span<Sample> output) noexcept
{
  const bool validCall = mPrepared && input.size() == output.size()
                         && input.size() <= mMaximumBlockSize;
  assert(validCall && "TCBLDCleanBoostProcessor::prepare() and its block contract are required");
  if (!validCall)
  {
    if (input.size() == output.size() && input.data() != output.data())
      std::copy(input.begin(), input.end(), output.begin());
    return;
  }

  for (std::size_t attempt = 0; attempt < kCoefficientQueueCapacity; ++attempt)
  {
    std::size_t latest = kCoefficientQueueCapacity;
    std::uint32_t latestSequence = 0;
    for (std::size_t index = 0; index < kCoefficientQueueCapacity; ++index)
    {
      const auto& slot = mCoefficientQueue[index];
      if (slot.status.load(std::memory_order_acquire) == kCoefficientSlotReady)
      {
        const std::uint32_t sequence = slot.sequence.load(std::memory_order_relaxed);
        if (latest == kCoefficientQueueCapacity || sequence > latestSequence)
        {
          latest = index;
          latestSequence = sequence;
        }
      }
    }
    if (latest == kCoefficientQueueCapacity)
      break;

    std::uint32_t expected = kCoefficientSlotReady;
    if (!mCoefficientQueue[latest].status.compare_exchange_strong(
          expected,
          kCoefficientSlotReading,
          std::memory_order_acq_rel,
          std::memory_order_acquire))
      continue;

    latestSequence =
      mCoefficientQueue[latest].sequence.load(std::memory_order_relaxed);
    mCoefficients = mCoefficientQueue[latest].coefficients;
    mCoefficientQueue[latest].status.store(
      kCoefficientSlotFree, std::memory_order_release);
    for (std::size_t index = 0; index < kCoefficientQueueCapacity; ++index)
    {
      auto& slot = mCoefficientQueue[index];
      expected = kCoefficientSlotReady;
      if (slot.status.compare_exchange_strong(
            expected,
            kCoefficientSlotReading,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
      {
        const std::uint32_t sequence = slot.sequence.load(std::memory_order_relaxed);
        slot.status.store(
          sequence <= latestSequence ? kCoefficientSlotFree : kCoefficientSlotReady,
          std::memory_order_release);
      }
    }
    reset();
    break;
  }
  const Coefficients& coefficients = mCoefficients;

  std::array<double, kStateCount> nextState{};
  for (std::size_t frame = 0; frame < input.size(); ++frame)
  {
    // This far-outside-audio numerical guard is not M3 rail clipping.
    const double currentInput = std::isfinite(input[frame]) ? input[frame] : 0.0;
    double currentOutput = coefficients.direct * currentInput;
    for (std::size_t state = 0; state < kStateCount; ++state)
      currentOutput += coefficients.output[state] * mState[state];

    bool finiteState = std::isfinite(currentOutput);
    for (std::size_t state = 0; state < kStateCount; ++state)
    {
      double value = coefficients.input[state] * currentInput;
      for (std::size_t history = 0; history < kStateCount; ++history)
        value += coefficients.transition[state][history] * mState[history];
      nextState[state] = value;
      finiteState = finiteState && std::isfinite(value);
    }

    if (finiteState)
    {
      mState = nextState;
      output[frame] = currentOutput;
    }
    else
    {
      reset();
      output[frame] = 0.0;
    }
  }
}

std::complex<double> TCBLDCleanBoostProcessor::frequencyResponse(
  const double frequencyHz) const noexcept
{
  if (!mPrepared || !std::isfinite(frequencyHz) || frequencyHz < 0.0
      || frequencyHz >= 0.5 * mSampleRate)
    return {std::numeric_limits<double>::quiet_NaN(), 0.0};

  const double angle = 2.0 * kPi * frequencyHz / mSampleRate;
  const std::complex<double> z = std::polar(1.0, angle);
  const Coefficients* coefficients = &mCoefficients;
  std::uint32_t latestSequence = 0;
  for (const auto& slot : mCoefficientQueue)
  {
    if (slot.status.load(std::memory_order_acquire) == kCoefficientSlotReady)
    {
      const std::uint32_t sequence = slot.sequence.load(std::memory_order_relaxed);
      if (sequence > latestSequence)
      {
        latestSequence = sequence;
        coefficients = &slot.coefficients;
      }
    }
  }
  ComplexStateMatrix system{};
  ComplexStateVector rightHandSide{};
  for (std::size_t row = 0; row < kStateCount; ++row)
  {
    rightHandSide[row] = coefficients->input[row];
    for (std::size_t column = 0; column < kStateCount; ++column)
    {
      system[row][column] = -coefficients->transition[row][column];
      if (row == column)
        system[row][column] += z;
    }
  }

  ComplexStateVector state{};
  if (!solveComplex(system, rightHandSide, state))
    return {std::numeric_limits<double>::quiet_NaN(), 0.0};

  std::complex<double> response = coefficients->direct;
  for (std::size_t index = 0; index < kStateCount; ++index)
    response += coefficients->output[index] * state[index];
  return response;
}

double TCBLDCleanBoostProcessor::fullScalePeakVolts(
  const double calibrationDbu) noexcept
{
  if (!std::isfinite(calibrationDbu))
    return 1.0;
  return kSqrtTwoTimesPoint775 * std::pow(10.0, calibrationDbu / 20.0);
}

double TCBLDCleanBoostProcessor::normalizedSampleToVolts(
  const double sample,
  const double calibrationDbu) noexcept
{
  return sample * fullScalePeakVolts(calibrationDbu);
}

double TCBLDCleanBoostProcessor::voltsToNormalizedSample(
  const double volts,
  const double calibrationDbu) noexcept
{
  return volts / fullScalePeakVolts(calibrationDbu);
}

} // namespace holdsworth::dsp
