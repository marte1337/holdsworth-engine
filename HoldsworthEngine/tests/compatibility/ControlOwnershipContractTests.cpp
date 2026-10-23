// M0 isolated counterexamples, not production control coordination.
// Threads, waits and allocation in this file are test-only.
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

namespace
{
void awaitStage(const std::atomic<unsigned>& stage, unsigned expected)
{
  while (stage.load(std::memory_order_acquire) < expected)
    std::this_thread::yield();
}

bool capturedPayloadSurvivesSharedParameterOverwrite()
{
  std::atomic<double> parameter{0.0};
  std::atomic<unsigned> stage{0};
  constexpr double uiPayload = 0.25;
  constexpr double hostPayload = 0.75;
  double callbackReread = 0.0;

  std::thread ui([&] {
    parameter.store(uiPayload, std::memory_order_relaxed);
    stage.store(1, std::memory_order_release);
    awaitStage(stage, 2);
    callbackReread = parameter.load(std::memory_order_relaxed);
  });
  std::thread host([&] {
    awaitStage(stage, 1);
    parameter.store(hostPayload, std::memory_order_relaxed);
    stage.store(2, std::memory_order_release);
  });
  ui.join();
  host.join();

  // Source identity alone cannot recover the original value after this schedule.
  return callbackReread == hostPayload && callbackReread != uiPayload
      && uiPayload == 0.25;
}

constexpr std::uint64_t encodeTuple(std::uint32_t a, std::uint32_t b)
{
  return (static_cast<std::uint64_t>(a) << 32U) | b;
}

bool independentTupleWritersLoseCapturedUpdate()
{
  std::atomic<std::uint32_t> requestedA{0};
  std::atomic<std::uint32_t> requestedB{0};
  std::atomic<std::uint64_t> published{encodeTuple(0, 0)};
  std::atomic<unsigned> stage{0};

  std::thread ui([&] {
    const auto staleB = requestedB.load(std::memory_order_relaxed);
    stage.store(1, std::memory_order_release);
    awaitStage(stage, 2);
    requestedA.store(1, std::memory_order_relaxed);
    published.store(encodeTuple(1, staleB), std::memory_order_relaxed);
    stage.store(3, std::memory_order_release);
  });
  std::thread host([&] {
    awaitStage(stage, 1);
    const auto staleA = requestedA.load(std::memory_order_relaxed);
    stage.store(2, std::memory_order_release);
    awaitStage(stage, 3);
    requestedB.store(2, std::memory_order_relaxed);
    published.store(encodeTuple(staleA, 2), std::memory_order_relaxed);
  });
  ui.join();
  host.join();

  // The separate request fields contain both edits, but the complete published
  // tuple lost A. Atomic whole-tuple publication alone does not merge writers.
  const bool lost = requestedA.load() == 1 && requestedB.load() == 2
                 && published.load() == encodeTuple(0, 2);

  // A local single-owner application of captured field patches preserves both.
  // This is a test illustration, not an ingress/coordinator implementation.
  std::uint32_t ownedA = 0, ownedB = 0;
  ownedA = 1;
  ownedB = 2;
  return lost && encodeTuple(ownedA, ownedB) == encodeTuple(1, 2);
}
} // namespace

int main()
{
  constexpr unsigned schedules = 128;
  for (unsigned iteration = 0; iteration < schedules; ++iteration)
  {
    if (!capturedPayloadSurvivesSharedParameterOverwrite())
    {
      std::cerr << "FAIL: payload overwrite counterexample\n";
      return 1;
    }
    if (!independentTupleWritersLoseCapturedUpdate())
    {
      std::cerr << "FAIL: multi-writer tuple counterexample\n";
      return 1;
    }
  }
  std::cout << "PASS: 2 ownership counterexamples, " << schedules
            << " controlled schedules each\n";
}
