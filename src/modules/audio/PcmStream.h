#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <memory>
#include <span>

namespace ssq {
class Table;
class Class;
}  // namespace ssq

namespace eve::audio {
class Audio;
struct PcmStreamState;

/** @brief Observable stream phase; starvation preserves the intent to play. */
enum class PcmStreamPhase { Idle, Playing, Paused, Starved, Draining, Finished, Closed };

/** @brief Owning snapshot; frame counts include all channels as one sample frame. */
struct PcmStreamInfo {
    PcmStreamPhase phase           = PcmStreamPhase::Idle;
    int            sampleRate      = 0;
    int            channels        = 0;
    int            availableBlocks = 0;
    int            queuedFrames    = 0;
    std::uint64_t  playedFrames    = 0;
};

/**
 * @brief Bounded, caller-fed PCM playback, independent of codecs and synthesis.
 * @ownership Caller owns this object; submitted samples are copied synchronously.
 * @lifetime Audio teardown closes outstanding streams before destroying OpenAL.
 * Destruction of this object releases playback even while Audio remains alive.
 * @thread All operations and destruction run on Audio's owning/main thread.
 * @reentrancy Never invokes scripts or user callbacks. Not concurrently callable.
 * Streams are transient: hot reload must close/recreate them, not serialize them.
 */
class EVENGINE_API_BACKENDS PcmStream {
public:
    /** @brief Maximum frames per submission; stereo has twice as many values. */
    static constexpr int maxBlockFrames = 4096;
    /** @brief Maximum simultaneously queued blocks. */
    static constexpr int blockCount = 8;
    /** @brief Release all backend resources; must run on the owning thread. */
    ~PcmStream();
    PcmStream(const PcmStream&)            = delete;
    PcmStream& operator=(const PcmStream&) = delete;

    /**
     * @brief Copy interleaved normalized samples and queue one block atomically.
     * @param samples Borrowed for this call only; finite values in [-1,1], nonempty,
     * channel-aligned, at most maxBlockFrames frames. Stereo is L,R,L,R.
     * @return Accepted frame count; Conflict on full queue, PreconditionViolation
     * after finishInput/close, InvalidArgument on invalid samples. Rejection queues nothing.
     * @cost Linear in submitted samples, bounded to one block; conversion/copy is
     * amortized per block, never per script callback on the audio thread.
     */
    [[nodiscard]] Result<int> submit(std::span<const float> samples);
    /** @brief Start/resume; empty input becomes Starved and resumes when data arrives. */
    [[nodiscard]] Result<void> play();
    /** @brief Pause without discarding queued data or reopening ended input. */
    [[nodiscard]] Result<void> pause();
    /** @brief End input idempotently; play drains queued data without inserting silence. */
    [[nodiscard]] Result<void> finishInput();
    /** @brief Discard queued audio, stop, clear counters and reopen input. */
    [[nodiscard]] Result<void> reset();
    /** @brief Idempotent final release; subsequent mutating operations return a diagnostic. */
    void close();
    /** @brief Cheap snapshot from the last pump/operation; never restarts playback. */
    [[nodiscard]] PcmStreamInfo getInfo() const;

private:
    friend class Audio;
    explicit PcmStream(std::shared_ptr<PcmStreamState> state);
    static Result<std::unique_ptr<PcmStream>> create(int sampleRate, int channels);
    static void                               pumpState(const std::shared_ptr<PcmStreamState>& state);
    static void                               closeState(const std::shared_ptr<PcmStreamState>& state);
    std::shared_ptr<PcmStreamState>           state_;
};

/** @brief Install VM-bound adapters; borrows both handles only during registration. */
void exposePcmStream(ssq::Table& table, ssq::Class& audioClass);
}  // namespace eve::audio
