#include "../integration/TunerCaptureBuffer.h"
#include "TestHarness.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <limits>
#include <thread>

namespace holdsworth::integration
{
struct TunerCaptureBufferTestAccess
{
  static std::size_t partial(const TunerCaptureBuffer& capture) { return capture.mPartialCount; }
  static void sequence(TunerCaptureBuffer& capture, std::uint32_t sequence)
  { capture.mSequence = sequence; capture.publishCursor(); }
  static void request(TunerCaptureBuffer& capture, std::uint32_t request)
  { capture.mRequest.store(request, std::memory_order_relaxed); }
};
}

namespace holdsworth::test
{
namespace
{
using Capture = integration::TunerCaptureBuffer;
using Access = integration::TunerCaptureBufferTestAccess;
constexpr std::array rates{44100., 48000., 88200., 96000., 176400., 192000.};

void activate(Capture& capture, double rate = 48000.)
{ capture.prepare(rate); capture.setEnabled(true); capture.setEditorOpen(true); }

void constantChunk(Capture& capture, double value)
{
  if (!capture.beginBlock()) return;
  for (std::size_t i = 0; i < Capture::kChunkSamples; ++i) capture.pushSample(value);
}

bool matches(const Capture::Chunk& chunk, double value)
{
  return std::all_of(chunk.samples.begin(), chunk.samples.end(), [value](float sample) {
    return std::bit_cast<std::uint32_t>(sample)
      == std::bit_cast<std::uint32_t>(static_cast<float>(value));
  });
}

bool inactiveAndRapidToggles()
{
  Capture capture;
  capture.prepare(48000.);
  const auto initial = capture.latestCursor();
  // Invalid pointers and a very large block prove inactive capture does no
  // sample access or work proportional to frame/channel counts.
  capture.captureBlock(nullptr, std::numeric_limits<std::size_t>::max(), 9, true);
  capture.pushSample(std::numeric_limits<double>::infinity());
  if (capture.latestCursor() != initial || capture.available() != 0 || Access::partial(capture) != 0) return false;
  capture.setEnabled(true);
  capture.captureBlock(nullptr, std::numeric_limits<std::size_t>::max(), 9, true);
  if (capture.available() != 0 || capture.invalidChunks() != 0) return false;
  capture.setEditorOpen(true);
  if (!capture.beginBlock()) return false;
  for (unsigned i = 0; i < 128; ++i) capture.pushSample(1.);
  const auto token = capture.requestToken();
  capture.setEnabled(false); capture.setEnabled(true);
  if (capture.requestToken() == token || !capture.beginBlock() || Access::partial(capture) != 0) return false;
  for (unsigned i = 0; i < 128; ++i) capture.pushSample(2.);
  if (capture.available() != 0) return false;
  for (unsigned i = 0; i < 128; ++i) capture.pushSample(2.);
  Capture::Chunk chunk;
  if (!capture.pop(chunk) || !matches(chunk, 2.) || chunk.requestToken != capture.requestToken()) return false;
  const auto openToken = capture.requestToken();
  capture.setEditorOpen(false); capture.setEditorOpen(true);
  if (capture.requestToken() == openToken || !capture.beginBlock()) return false;
  const auto stable = capture.requestToken();
  capture.setEnabled(true); capture.setEditorOpen(true);
  if (capture.requestToken() != stable) return false;
  capture.setEditorOpen(false);
  capture.captureBlock(nullptr, std::numeric_limits<std::size_t>::max(), 2, false);
  return !capture.editorOpen() && capture.requestedEnabled() && capture.available() == 0
    && capture.invalidChunks() == 0 && capture.overrunChunks() == 0 && Access::partial(capture) == 0;
}

bool foldPartitionsAndInputPreservation()
{
  constexpr std::size_t samples = 4 * Capture::kChunkSamples;
  std::array<double, samples> left{}, right{};
  for (std::size_t i = 0; i < samples; ++i)
  {
    left[i] = .2 * std::sin(.113 * static_cast<double>(i));
    right[i] = .15 * std::cos(.071 * static_cast<double>(i));
  }
  left[0] = -0.; right[0] = 0.;
  const auto originalLeft = left, originalRight = right;
  constexpr std::array<std::size_t, 9> sizes{1, 2, 7, 32, 73, 256, 301, 19, 333};
  for (bool average : {false, true})
    for (std::size_t channels : {0U, 1U, 2U})
    {
      Capture whole, split;
      activate(whole); activate(split);
      const std::array<const double*, 2> input{left.data(), right.data()};
      whole.captureBlock(input.data(), samples, channels, average);
      for (std::size_t position = 0, block = 0; position < samples; ++block)
      {
        const auto count = std::min(sizes[block % sizes.size()], samples - position);
        const std::array<const double*, 2> section{left.data() + position, right.data() + position};
        split.captureBlock(section.data(), count, channels, average);
        position += count;
      }
      if (whole.available() != 4 || split.available() != 4) return false;
      for (std::size_t block = 0; block < 4; ++block)
      {
        Capture::Chunk a, b;
        if (!whole.pop(a) || !split.pop(b) || a.sequence != b.sequence || a.epoch != b.epoch) return false;
        for (std::size_t i = 0; i < Capture::kChunkSamples; ++i)
        {
          const auto index = block * Capture::kChunkSamples + i;
          double expected = channels == 0 ? 0. : left[index];
          if (channels == 2) expected += right[index];
          if (average && channels == 2) expected /= 2.;
          const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(expected));
          if (std::bit_cast<std::uint32_t>(a.samples[i]) != bits
              || std::bit_cast<std::uint32_t>(b.samples[i]) != bits) return false;
        }
      }
    }
  return expectSamplesBitExact("tuner capture preserves left host input", left, originalLeft)
    && expectSamplesBitExact("tuner capture preserves right host input", right, originalRight);
}

bool lifecycleAndRates()
{
  Capture capture;
  activate(capture);
  constantChunk(capture, 1.);
  auto cursor = capture.latestCursor();
  for (double rate : rates)
  {
    capture.prepare(rate);
    if (Capture::cursorEpoch(capture.latestCursor()) == Capture::cursorEpoch(cursor)
        || Capture::cursorSequence(capture.latestCursor()) != Capture::cursorSequence(cursor)) return false;
    constantChunk(capture, rate);
    cursor = capture.latestCursor();
  }
  if (capture.available() != rates.size() + 1) return false;
  Capture::Chunk chunk;
  if (!capture.pop(chunk) || !matches(chunk, 1.)) return false;
  std::uint32_t previousEpoch = chunk.epoch;
  for (double rate : rates)
  {
    if (!capture.pop(chunk) || chunk.sampleRate != rate || !matches(chunk, rate)
        || chunk.epoch == previousEpoch || chunk.requestToken != capture.requestToken()) return false;
    previousEpoch = chunk.epoch;
  }
  for (double badRate : {0., -1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
  {
    capture.prepare(badRate);
    capture.captureBlock(nullptr, std::numeric_limits<std::size_t>::max(), 7, false);
    if (capture.available() != 0 || capture.beginBlock()) return false;
  }
  capture.prepare(44100.);
  constantChunk(capture, 3.);
  return capture.pop(chunk) && chunk.sampleRate == 44100. && matches(chunk, 3.);
}

bool overrunAndRecovery()
{
  Capture capture;
  activate(capture);
  for (std::size_t i = 0; i < Capture::kQueueCapacity; ++i) constantChunk(capture, static_cast<double>(i));
  const auto before = capture.latestCursor();
  constantChunk(capture, -1.);
  const auto firstGap = capture.latestCursor();
  constantChunk(capture, -2.);
  if (capture.available() != Capture::kQueueCapacity || capture.overrunChunks() != 2
      || Capture::cursorEpoch(before) == Capture::cursorEpoch(firstGap)
      || Capture::cursorEpoch(firstGap) != Capture::cursorEpoch(capture.latestCursor())
      || Capture::cursorSequence(capture.latestCursor()) != Capture::kQueueCapacity + 2) return false;
  Capture::Chunk chunk;
  const auto snapshot = capture.available();
  for (std::size_t i = 0; i < snapshot; ++i)
    if (!capture.pop(chunk) || !matches(chunk, static_cast<double>(i)) || chunk.epoch != Capture::cursorEpoch(before))
      return false;
  if (capture.pop(chunk)) return false;
  constantChunk(capture, 123.);
  if (!capture.pop(chunk) || !matches(chunk, 123.) || chunk.epoch != Capture::cursorEpoch(firstGap)
      || chunk.sequence != Capture::kQueueCapacity + 3) return false;
  // Successful recovery ends the gap; a later independent overflow invalidates
  // that generation again rather than being hidden by the first overrun.
  for (std::size_t i = 0; i <= Capture::kQueueCapacity; ++i) constantChunk(capture, 2.);
  return capture.overrunChunks() == 3 && Capture::cursorEpoch(capture.latestCursor()) != chunk.epoch;
}

bool nonfiniteAndOverflowRejection()
{
  Capture capture;
  activate(capture);
  constantChunk(capture, 1.);
  const auto valid = capture.latestCursor();
  for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::max()})
  {
    if (!capture.beginBlock()) return false;
    for (std::size_t i = 0; i < Capture::kChunkSamples; ++i) capture.pushSample(i == 23 ? bad : 2.);
    if (capture.available() != 1) return false;
  }
  const auto gap = capture.latestCursor();
  if (capture.invalidChunks() != 4 || capture.overrunChunks() != 0
      || Capture::cursorEpoch(gap) == Capture::cursorEpoch(valid)) return false;
  constantChunk(capture, 3.);
  Capture::Chunk old, fresh;
  return capture.pop(old) && capture.pop(fresh) && matches(old, 1.) && matches(fresh, 3.)
    && fresh.epoch == Capture::cursorEpoch(gap) && fresh.epoch != old.epoch
    && fresh.sequence == 6 && capture.available() == 0;
}

bool modularSequenceAndControlRevision()
{
  Capture capture;
  activate(capture);
  if (!capture.beginBlock()) return false;
  Access::sequence(capture, std::numeric_limits<std::uint32_t>::max() - 1);
  constantChunk(capture, 1.); constantChunk(capture, 2.);
  Capture::Chunk a, b;
  if (!capture.pop(a) || !capture.pop(b) || a.sequence != std::numeric_limits<std::uint32_t>::max()
      || b.sequence != 0 || Capture::cursorSequence(capture.latestCursor()) != 0 || a.epoch != b.epoch) return false;
  if (Capture::chunksBehind(0, a.sequence) != 1 || Capture::chunksBehind(0, 1) != 0
      || Capture::cursorEpoch(Capture::makeCursor(0xabcdef12U, 0x12345678U)) != 0xabcdef12U) return false;
  Access::request(capture, std::numeric_limits<std::uint32_t>::max());
  capture.setEnabled(false);
  if (capture.requestToken() != Capture::kEditorOpenMask || capture.requestedEnabled()) return false;
  capture.setEnabled(true);
  return capture.requestToken() == 7 && capture.editorOpen() && capture.requestedEnabled();
}

bool concurrentOwnership()
{
  Capture capture;
  activate(capture);
  std::atomic<bool> start{false}, done{false};
  constexpr std::uint32_t total = 24000;
  std::thread producer([&] {
    while (!start.load(std::memory_order_acquire)) {}
    for (std::uint32_t number = 1; number <= total; ++number)
    {
      // Producer-side rate resets race with consumer drains without resetting
      // shared queue indices or modifying unread chunk storage.
      if (number % 127 == 0) capture.prepare(number % 2 == 0 ? 48000. : 96000.);
      constantChunk(capture, static_cast<double>(number));
    }
    done.store(true, std::memory_order_release);
  });
  bool valid = true;
  std::uint32_t previousSequence = 0;
  std::size_t consumed = 0;
  start.store(true, std::memory_order_release);
  while (!done.load(std::memory_order_acquire) || capture.available() != 0)
  {
    const auto count = capture.available();
    if (count > Capture::kQueueCapacity) valid = false;
    for (std::size_t i = 0; i < count; ++i)
    {
      Capture::Chunk chunk;
      if (!capture.pop(chunk)) { valid = false; break; }
      valid &= chunk.sequence > previousSequence && chunk.sequence <= total
        && chunk.requestToken == capture.requestToken() && matches(chunk, static_cast<double>(chunk.sequence))
        && (chunk.sampleRate == 48000. || chunk.sampleRate == 96000.);
      previousSequence = chunk.sequence;
      ++consumed;
    }
  }
  producer.join();
  return valid && consumed > 0 && consumed + capture.overrunChunks() == total
    && Capture::cursorSequence(capture.latestCursor()) == total && capture.invalidChunks() == 0;
}

bool fixedStorage()
{
  Capture capture;
  std::array<double, 1024> samples{};
  const std::array<const double*, 2> input{samples.data(), samples.data()};
  beginAllocationTracking();
  activate(capture, 192000.);
  for (unsigned iteration = 0; iteration < 200; ++iteration)
  {
    capture.captureBlock(input.data(), samples.size(), 2, iteration % 2 == 0);
    const auto count = capture.available();
    Capture::Chunk chunk;
    for (std::size_t i = 0; i < count; ++i) (void)capture.pop(chunk);
    if (iteration % 13 == 0) capture.prepare(iteration % 2 == 0 ? 192000. : 44100.);
  }
  capture.setEnabled(false);
  capture.captureBlock(nullptr, std::numeric_limits<std::size_t>::max(), 9, false);
  return endAllocationTracking() == 0 && capture.overrunChunks() == 0 && capture.invalidChunks() == 0;
}
}

TestSuite tunerCaptureBufferTests() noexcept
{
  static constexpr std::array tests{
    TestCase{"Tuner capture inactive and rapid control revisions", inactiveAndRapidToggles},
    TestCase{"Tuner capture input fold partitions and bit preservation", foldPartitionsAndInputPreservation},
    TestCase{"Tuner capture lifecycle and host rates", lifecycleAndRates},
    TestCase{"Tuner capture FIFO overrun and fresh recovery", overrunAndRecovery},
    TestCase{"Tuner capture nonfinite and float overflow rejection", nonfiniteAndOverflowRejection},
    TestCase{"Tuner capture modular sequence and control revision", modularSequenceAndControlRevision},
    TestCase{"Tuner capture concurrent SPSC ownership and resets", concurrentOwnership},
    TestCase{"Tuner capture fixed storage and disabled fast path", fixedStorage}};
  return tests;
}
} // namespace holdsworth::test
