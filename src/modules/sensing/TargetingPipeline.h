#pragma once
#include "common/Export.h"


/**
 * @file TargetingPipeline.h
 * @brief Ordered Select/Filter/Sort task pipeline over SensingWorld candidates.
 *
 * Presets compose registered ITargetingTask instances. Tasks never assign a
 * primary target; consumers retain selection policy. Built-in tasks are named
 * `sensing.select.*`, `sensing.filter.*`, and `sensing.sort.*`.
 */

#include "sensing/Sensing.h"
#include "sensing/Targeting.h"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::sensing {

/** @brief Task role inside a TargetingPreset. */
enum class TargetingTaskKind : std::uint8_t {
    Select,
    Filter,
    Sort,
};

/**
 * @brief Per-step parameters for a preset entry.
 *
 * Unused fields are ignored by tasks that do not read them. Cone angles are
 * radians; facing comes from TargetingSourceContext.
 */
struct TargetingTaskStep {
    std::string taskId;
    QuerySpec   querySpec{};
    float       coneHalfAngle = 0.f;
    float       coneRange     = 0.f;
    std::uint32_t truncateCount = std::numeric_limits<std::uint32_t>::max();
};

/** @brief Named ordered list of targeting tasks. */
struct TargetingPreset {
    std::string                     id;
    std::vector<TargetingTaskStep>  steps;
};

/**
 * @brief Runtime source for one pipeline execution.
 *
 * @ownership `world` is non-owning and must outlive execute().
 * @thread Call on the same simulation thread as the bound SensingWorld.
 */
struct TargetingSourceContext {
    SensingWorld* world = nullptr;
    QueryOrigin   origin{};
    float         dirX = 1.f;
    float         dirY = 0.f;
};

/**
 * @brief Pluggable Select/Filter/Sort task.
 *
 * Implementations must not retain inout candidates or context references after
 * execute() returns. Missing optional capabilities return Unsupported.
 */
class ITargetingTask {
public:
    virtual ~ITargetingTask() = default;

    /** @brief Stable task id used by presets (e.g. "sensing.filter.cone"). */
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    /** @brief Task kind used for diagnostics. */
    [[nodiscard]] virtual TargetingTaskKind kind() const noexcept = 0;
    /**
     * @brief Runs the task against the working candidate list.
     * @return Success or a structured failure; on failure inout is unspecified.
     */
    [[nodiscard]] virtual Result<void> execute(TargetingSourceContext& context, std::vector<RankedCandidate>& inout,
                                               const TargetingTaskStep& step) const = 0;
};

/**
 * @brief In-memory task + preset registry and synchronous executor.
 *
 * @thread Not synchronized; use on one simulation thread.
 * @reentrancy Tasks must not re-enter executePreset on the same pipeline.
 */
class EVENGINE_API_PLATFORM TargetingPipeline {
public:
    /** @brief Creates an empty pipeline. */
    TargetingPipeline() = default;
    // Class-level dllexport instantiates every member; `tasks_` is a map of
    // unique_ptr, so the implicit copy assignment would be a hard C2280.
    TargetingPipeline(const TargetingPipeline&)            = delete;
    TargetingPipeline& operator=(const TargetingPipeline&) = delete;
    TargetingPipeline(TargetingPipeline&&)                 = default;
    TargetingPipeline& operator=(TargetingPipeline&&)      = default;

    /** @brief Creates a pipeline with built-in sensing tasks and one coneSelect preset. */
    [[nodiscard]] static TargetingPipeline withBuiltins();

    /**
     * @brief Process-local builtins pipeline used by SensingWorld::executePreset.
     * @ownership Process lifetime; callers must not delete it.
     */
    [[nodiscard]] static TargetingPipeline& sharedBuiltins();

    /** @brief Registers a unique task; duplicate ids fail with AlreadyExists. */
    [[nodiscard]] Result<void> registerTask(std::unique_ptr<ITargetingTask> task);
    /** @brief Registers or replaces a preset after validating task ids exist. */
    [[nodiscard]] Result<void> registerPreset(TargetingPreset preset);
    /** @brief Returns whether a preset id is registered. */
    [[nodiscard]] bool hasPreset(std::string_view presetId) const noexcept;

    /**
     * @brief Executes a preset against a source context.
     * @return Owning ranked candidates (no primary), or a structured failure.
     */
    [[nodiscard]] Result<CandidateQueryResult> execute(TargetingSourceContext& context,
                                                       const TargetingPreset&  preset) const;
    /**
     * @brief Looks up a preset by id and executes it.
     * @return Owning ranked candidates, or NotFound/structured failure.
     */
    [[nodiscard]] Result<CandidateQueryResult> executePreset(TargetingSourceContext& context,
                                                             std::string_view         presetId) const;

private:
    std::map<std::string, std::unique_ptr<ITargetingTask>, std::less<>> tasks_;
    std::map<std::string, TargetingPreset, std::less<>>                 presets_;
};

}  // namespace eve::sensing
