#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace holdsworth::integration
{
struct TunerCaptureBufferTestAccess;

// Fixed SPSC transport: one lifecycle/audio producer and one main-thread
// consumer. prepare() needs exclusion from capture, but not from the consumer.
// Control setters have one main-thread owner. Queue indices are never reset;
// generation metadata invalidates old data without changing either ownership.
class TunerCaptureBuffer final
{
public:
  static constexpr std::size_t kChunkSamples = 256;
  static constexpr std::size_t kQueueCapacity = 256;
  static constexpr std::size_t kStorageSlots = kQueueCapacity + 1;
  static constexpr std::uint32_t kEnabledMask = 1;
  static constexpr std::uint32_t kEditorOpenMask = 2;

  struct Chunk final
  {
    std::array<float, kChunkSamples> samples{};
    std::uint32_t requestToken = 0;
    std::uint32_t epoch = 0;
    std::uint32_t sequence = 0;
    double sampleRate = 0.;
  };

  static_assert(std::is_trivially_copyable_v<Chunk>);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

  TunerCaptureBuffer() noexcept = default;
  TunerCaptureBuffer(const TunerCaptureBuffer&) = delete;
  TunerCaptureBuffer& operator=(const TunerCaptureBuffer&) = delete;

  void setEnabled(bool enabled) noexcept { setControlFlag(kEnabledMask, enabled); }
  void setEditorOpen(bool open) noexcept { setControlFlag(kEditorOpenMask, open); }
  [[nodiscard]] bool requestedEnabled() const noexcept { return (requestToken() & kEnabledMask) != 0; }
  [[nodiscard]] bool editorOpen() const noexcept { return (requestToken() & kEditorOpenMask) != 0; }
  [[nodiscard]] std::uint32_t requestToken() const noexcept { return mRequest.load(std::memory_order_acquire); }

  // Only producer-local state changes here. The consumer learns about every
  // reset, including a same-rate reset, through the published epoch.
  void prepare(double hostRate) noexcept
  {
    mSampleRate = std::isfinite(hostRate) && hostRate > 0. ? hostRate : 0.;
    mPrepared = mSampleRate > 0.;
    mBlockActive = false;
    mPartialCount = 0;
    mPartialInvalid = false;
    mInGap = false;
    advanceEpoch();
  }

  // Call once before pushSample calls. A revision changes even if audio never
  // observed an intervening Off/closed state, so rapid toggles cannot join old
  // and new samples into one pitch window.
  [[nodiscard]] bool beginBlock() noexcept
  {
    const auto request = mRequest.load(std::memory_order_acquire);
    if (request != mObservedRequest)
    {
      mObservedRequest = request;
      mPartialCount = 0;
      mPartialInvalid = false;
      mInGap = false;
      advanceEpoch();
    }
    mBlockActive = mPrepared && (request & (kEnabledMask | kEditorOpenMask))
      == (kEnabledMask | kEditorOpenMask);
    return mBlockActive;
  }

  void pushSample(double cleanMono) noexcept
  {
    if (!mBlockActive) return;
    const bool finite = std::isfinite(cleanMono)
      && std::abs(cleanMono) <= static_cast<double>(std::numeric_limits<float>::max());
    mAssembly.samples[mPartialCount++] = finite ? static_cast<float>(cleanMono) : 0.F;
    mPartialInvalid |= !finite;
    if (mPartialCount != kChunkSamples) return;

    ++mSequence; // Modulo arithmetic is intentional; never reset on prepare.
    if (mPartialInvalid)
    {
      mInvalidChunks.fetch_add(1, std::memory_order_relaxed);
      startGap();
    }
    else
    {
      const auto write = mWrite.load(std::memory_order_relaxed);
      const auto next = increment(write);
      if (next == mRead.load(std::memory_order_acquire))
      {
        mOverrunChunks.fetch_add(1, std::memory_order_relaxed);
        startGap();
      }
      else
      {
        mAssembly.requestToken = mObservedRequest;
        mAssembly.epoch = mEpoch;
        mAssembly.sequence = mSequence;
        mAssembly.sampleRate = mSampleRate;
        mStorage[write] = mAssembly;
        mWrite.store(next, std::memory_order_release);
        mInGap = false;
      }
    }
    mPartialCount = 0;
    mPartialInvalid = false;
    publishCursor(); // Progress also advances when this complete chunk dropped.
  }

  // Match the application's existing input fold before trim/calibration: APP
  // sums connected inputs, plug-ins average them. Input memory is read-only.
  // A zero-channel block captures silence. Inactive calls never inspect inputs.
  void captureBlock(const double* const* inputs, std::size_t frames,
                    std::size_t channels, bool averageChannels) noexcept
  {
    if (!beginBlock()) return;
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
      double mono = channels == 0 ? 0. : inputs[0][frame];
      for (std::size_t channel = 1; channel < channels; ++channel)
        mono += inputs[channel][frame];
      if (averageChannels && channels > 1) mono /= static_cast<double>(channels);
      pushSample(mono);
    }
  }

  // Consumer calls available() once, then pops at most that many chunks. This
  // remains bounded even if the producer continues writing during the drain.
  [[nodiscard]] std::size_t available() const noexcept
  {
    const auto write = mWrite.load(std::memory_order_acquire);
    const auto read = mRead.load(std::memory_order_relaxed);
    return write >= read ? write - read : kStorageSlots - (read - write);
  }
  [[nodiscard]] bool pop(Chunk& chunk) noexcept
  {
    const auto read = mRead.load(std::memory_order_relaxed);
    if (read == mWrite.load(std::memory_order_acquire)) return false;
    chunk = mStorage[read];
    mRead.store(increment(read), std::memory_order_release);
    return true;
  }

  // Epoch and sequence share one atomic publication; no cursor can combine
  // samples from one lifecycle with metadata from another. A chunk may be just
  // ahead of a consumer's cursor snapshot when production overlaps draining.
  [[nodiscard]] std::uint64_t latestCursor() const noexcept
  { return mCursor.load(std::memory_order_acquire); }
  [[nodiscard]] static constexpr std::uint32_t cursorEpoch(std::uint64_t cursor) noexcept
  { return static_cast<std::uint32_t>(cursor >> 32); }
  [[nodiscard]] static constexpr std::uint32_t cursorSequence(std::uint64_t cursor) noexcept
  { return static_cast<std::uint32_t>(cursor); }
  [[nodiscard]] static constexpr std::uint64_t makeCursor(std::uint32_t epoch, std::uint32_t sequence) noexcept
  { return (static_cast<std::uint64_t>(epoch) << 32) | sequence; }
  // Valid for bounded queue age (< 2^31 chunks). Ahead-of-snapshot chunks are
  // fresh, while unsigned subtraction naturally handles sequence wrap.
  [[nodiscard]] static constexpr std::uint32_t chunksBehind(std::uint32_t latest,
                                                           std::uint32_t sequence) noexcept
  {
    const auto difference = latest - sequence;
    return difference < 0x80000000U ? difference : 0U;
  }
  [[nodiscard]] std::uint64_t overrunChunks() const noexcept
  { return mOverrunChunks.load(std::memory_order_relaxed); }
  [[nodiscard]] std::uint64_t invalidChunks() const noexcept
  { return mInvalidChunks.load(std::memory_order_relaxed); }

private:
  friend struct TunerCaptureBufferTestAccess;
  static constexpr std::uint32_t kStateMask = kEnabledMask | kEditorOpenMask;
  static constexpr std::uint32_t increment(std::uint32_t index) noexcept
  { return index + 1 == kStorageSlots ? 0 : index + 1; }
  void setControlFlag(std::uint32_t flag, bool state) noexcept
  {
    const auto previous = mRequest.load(std::memory_order_relaxed);
    const auto next = state ? previous | flag : previous & ~flag;
    if (next != previous)
      mRequest.store(((previous & ~kStateMask) + 4U) | (next & kStateMask),
                     std::memory_order_release);
  }
  void advanceEpoch() noexcept
  {
    ++mEpoch;
    publishCursor();
  }
  void startGap() noexcept
  {
    if (!mInGap)
    {
      mInGap = true;
      advanceEpoch();
    }
  }
  void publishCursor() noexcept
  { mCursor.store(makeCursor(mEpoch, mSequence), std::memory_order_release); }

  std::array<Chunk, kStorageSlots> mStorage{};
  alignas(64) std::atomic<std::uint32_t> mWrite{0};
  alignas(64) std::atomic<std::uint32_t> mRead{0};
  std::atomic<std::uint32_t> mRequest{0};
  std::atomic<std::uint64_t> mCursor{makeCursor(1, 0)};
  std::atomic<std::uint64_t> mOverrunChunks{0}, mInvalidChunks{0};

  Chunk mAssembly{};
  std::size_t mPartialCount = 0;
  std::uint32_t mObservedRequest = 0, mEpoch = 1, mSequence = 0;
  double mSampleRate = 0.;
  bool mPrepared = false, mBlockActive = false, mPartialInvalid = false, mInGap = false;
};
} // namespace holdsworth::integration
