#pragma once

/**
 * @file PlayTrace.h
 * @brief Versioned Play Trace recording, digest and replay.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"
#include "devtools/PlayHost.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace eve::dev {

/**
 * @brief Canonical digest of a Play observation state object.
 * @param state Owning observation projection (typically `response.state`).
 * @return `fnv1a64:` plus 16 hex digits, or a serialization failure.
 */
[[nodiscard]] EVENGINE_API Result<std::string> playObservationDigest(const Value& state);

/**
 * @brief Parse and reject unknown fields on a Play Trace recording.
 * @param recording Object with schema id `evengine.play-trace` and version 1.
 */
[[nodiscard]] EVENGINE_API Result<Value> parsePlayTrace(const Value& recording);

/**
 * @brief Replay a parsed recording against a host runtime.
 * @param recording Play Trace object.
 * @param runtime Same runtime used to produce the recording.
 * @return Response with `status` `passed` or a structured mismatch/runtime failure.
 * @remarks Capture pixels are not compared. Observation digests must match exactly.
 */
[[nodiscard]] EVENGINE_API Result<Value> replayPlayTrace(const Value& recording, IPlayHostRuntime& runtime);

/** @brief RAII flag so nested Play requests during replay are not recorded. */
class EVENGINE_API PlayReplayGuard final {
public:
    PlayReplayGuard();
    ~PlayReplayGuard();
    PlayReplayGuard(const PlayReplayGuard&)            = delete;
    PlayReplayGuard& operator=(const PlayReplayGuard&) = delete;
};

/**
 * @brief Process-owned append buffer for `trace=append`.
 *
 * Does not own game state. Replay sets a replaying flag so nested Play requests
 * do not record. Main-thread affinity; no callbacks.
 */
class EVENGINE_API PlayTraceBuffer final {
public:
    static PlayTraceBuffer& instance();

    void clear();
    /** @brief True while `replayPlayTrace` is executing. */
    [[nodiscard]] bool replaying() const { return replaying_; }
    void setReplaying(bool value) { replaying_ = value; }
    void begin(std::string contractId, std::string contractHash, std::int64_t seed,
               std::string startCheckpoint);
    void append(const Value& request, const Value& response, std::uint64_t hostFrame);
    [[nodiscard]] bool recording() const { return begun_; }
    [[nodiscard]] bool empty() const { return stepCount_ == 0; }
    [[nodiscard]] Value exportTrace() const;

private:
    PlayTraceBuffer() = default;

    std::string   contractId_;
    std::string   contractHash_;
    std::int64_t  seed_ = 0;
    std::string   startCheckpoint_;
    Value::Array  stepItems_;
    std::int64_t  stepCount_ = 0;
    bool          begun_ = false;
    bool          replaying_ = false;
};

}  // namespace eve::dev
