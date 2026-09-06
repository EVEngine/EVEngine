#pragma once

/**
 * @file PlayHost.h
 * @brief Versioned Play Host protocol for Agent pause / step / observe / capture.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {

/**
 * @brief Injected runtime used by Play Host. Tests supply a fake; MCP uses the
 *        engine adapter. Implementations must not retain request pointers and
 *        must not invoke unknown callbacks.
 * @thread Owner simulation / game-loop thread only.
 */
class IPlayHostRuntime {
public:
    virtual ~IPlayHostRuntime() = default;

    /** @brief Whether the host clock is paused. */
    [[nodiscard]] virtual bool paused() const = 0;
    /** @brief Monotonic host-frame counter owned by this runtime (0 if unknown). */
    [[nodiscard]] virtual std::uint64_t hostFrame() const = 0;
    /** @brief Pause the host clock. */
    virtual void pause() = 0;
    /** @brief Resume free-running host clock. */
    virtual void play() = 0;
    /**
     * @brief Request `count` host frames then pause.
     * @param count Frames to run; callers must pass a positive value.
     */
    [[nodiscard]] virtual Result<void> stepFrames(std::int64_t count) = 0;
    /**
     * @brief Load `game.agent.json` as an owning Value object.
     * @return The contract object, or NotFound / ParseError / Failed.
     */
    [[nodiscard]] virtual Result<Value> loadContract() const = 0;
    /**
     * @brief Project one marked script root without evaluating arbitrary code.
     * @param root Root table slot declared in the contract `path`.
     * @param fields Dotted field paths; empty means the whole root object.
     */
    [[nodiscard]] virtual Result<Value> observeScriptRoot(std::string_view root,
                                                          const std::vector<std::string>& fields) const = 0;
    /**
     * @brief Capture the last presented frame through IRenderCapture.
     * @param path Destination PNG path.
     * @return Object with path/width/height, or Unsupported when capture is absent.
     */
    [[nodiscard]] virtual Result<Value> capturePng(std::string path) = 0;
    /** @brief Capture marked script snapshot JSON. */
    [[nodiscard]] virtual Result<std::string> captureCheckpoint() = 0;
    /**
     * @brief Restore a previously captured checkpoint.
     * @param json Snapshot JSON produced by captureCheckpoint.
     */
    [[nodiscard]] virtual Result<void> restoreCheckpoint(std::string_view json) = 0;
    /** @brief Published gameplay-control domain names, sorted. */
    [[nodiscard]] virtual std::vector<std::string> gameplayDomains() const = 0;
    /**
     * @brief Run one contract-mapped script snippet. Not an open eval surface.
     * @param id Action id declared in `game.agent.json`.
     * @param source Exact snippet from the contract map; implementations must not
     *        substitute caller-supplied script.
     */
    [[nodiscard]] virtual Result<Value> invokeScriptAction(std::string_view id,
                                                           std::string_view source) = 0;
};

/**
 * @brief Execute one versioned Play request against an injected runtime.
 * @param request Object with schema id `evengine.play-request` and version 1.
 * @param runtime Host clock, contract, observation and capture backend.
 * @return Owning response object, or structured parse/routing/runtime diagnostics.
 * @remarks Unknown root fields are rejected. Nested `batch` is rejected.
 */
[[nodiscard]] EVENGINE_API Result<Value> executePlayRequest(const Value& request, IPlayHostRuntime& runtime);

/**
 * @brief Execute one Play request against the live engine runtime.
 * @param request Object with schema id `evengine.play-request` and version 1.
 */
[[nodiscard]] EVENGINE_API Result<Value> executePlayRequest(const Value& request);

/**
 * @brief Parse, execute and serialize one Play JSON request against the live engine.
 * @param requestJson Strict UTF-8 JSON request.
 */
[[nodiscard]] EVENGINE_API Result<std::string> executePlayJson(std::string_view requestJson);

}  // namespace eve::dev
