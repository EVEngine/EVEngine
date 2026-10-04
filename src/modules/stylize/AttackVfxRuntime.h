#pragma once
#include "common/Export.h"

/**
 * @file AttackVfxRuntime.h
 * @brief Pooled AttackVfx orchestration with optional layer executors.
 *
 * Owns recipe registration, play/signal/advance/stop, and phase enter/exit
 * events. Layer backends register as IAttackVfxLayerExecutor listeners; missing
 * executors emit LayerSkipped without failing the instance.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "stylize/AttackVfxRecipe.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::stylize {

/** @brief Generation-qualified reference to one pooled attack VFX instance. */
struct AttackVfxHandle {
    std::uint32_t slot       = 0;
    std::uint32_t generation = 0;

    friend bool operator==(const AttackVfxHandle&, const AttackVfxHandle&) = default;
};

/** @brief How stop() tears down an instance. */
enum class AttackVfxStopMode : std::uint8_t {
    StopEmitting,     /**< Mark stopping; phases honor layer stopBehavior later. */
    ClearImmediately, /**< Exit every phase and free the slot this call. */
    Cancel            /**< Alias of ClearImmediately for interrupt paths. */
};

/**
 * @brief Owning play request.
 *
 * Phase 1 keeps source/target as opaque generation-qualified ids so combat /
 * scene adapters can bind later without stylize depending on those modules.
 */
struct AttackVfxRequest {
    std::optional<LogicalId> skinOverride;
    std::uint32_t            sourceId         = 0;
    std::uint32_t            sourceGeneration = 0;
    std::uint32_t            targetId         = 0;
    std::uint32_t            targetGeneration = 0;
    std::string              clockName        = "absolute"; /**< Reserved: action|carrier|absolute. */
};

/** @brief Live observation of one phase inside an instance. */
struct AttackVfxPhaseState {
    AttackVfxPhaseKind kind      = AttackVfxPhaseKind::Release;
    bool               armed     = false; /**< Waiting for startCue / offset. */
    bool               active    = false;
    bool               completed = false;
    double             localTime = 0.0;
    std::size_t        layerCount = 0;
};

/** @brief Owning observable state for one live instance. */
struct AttackVfxInstanceState {
    AttackVfxHandle                handle;
    LogicalId                      recipeId;
    std::optional<LogicalId>       activeSkinId;
    double                         age = 0.0;
    bool                           stopping = false;
    AttackVfxRequest               request;
    std::vector<AttackVfxPhaseState> phases;
};

/** @brief Observable frame events emitted by advance/signal/stop. */
struct AttackVfxFrameEvent {
    enum class Kind : std::uint8_t {
        PhaseEnter,
        PhaseExit,
        InstanceStopped,
        CueConsumed,
        CueIgnored,
        LayerStarted,
        LayerSkipped
    };

    Kind               kind  = Kind::CueIgnored;
    AttackVfxHandle    handle{};
    AttackVfxPhaseKind phase = AttackVfxPhaseKind::Release;
    AttackVfxLayerRole role  = AttackVfxLayerRole::Particles;
    std::string        cue;
    std::size_t        layerCount = 0;
    std::size_t        layerIndex = 0;
};

/** @brief Owning result of one atomic orchestration step. */
struct AttackVfxFrame {
    std::vector<AttackVfxHandle>     advanced;
    std::vector<AttackVfxHandle>     stopped;
    std::vector<AttackVfxFrameEvent> events;
};

/**
 * @brief Deterministic pooled runtime for AttackVfxRecipe instances.
 *
 * @ownership Owns live instance slots and registered recipe copies. Layer GPU
 *            resources are owned by IAttackVfxLayerExecutor implementations.
 * @thread Simulation-thread affine. No internal synchronization.
 * @reentrancy Does not invoke user callbacks; may call registered executors.
 */
class EVENGINE_API_WORLD AttackVfxRuntime {
public:
    /** @brief Construct with a default pool capacity of 32. */
    AttackVfxRuntime();
    ~AttackVfxRuntime();

    AttackVfxRuntime(const AttackVfxRuntime&) = delete;
    AttackVfxRuntime& operator=(const AttackVfxRuntime&) = delete;

    /**
     * @brief Resize the instance pool. Fails when any slot is occupied.
     * @param capacity New slot count; must be > 0.
     */
    [[nodiscard]] Result<void> configurePool(std::uint32_t capacity);

    /** @brief Validate and store an immutable recipe copy keyed by recipe.id. */
    [[nodiscard]] Result<void> registerRecipe(const AttackVfxRecipe& recipe);

    /** @brief Validate and store a reusable skin keyed by skin.id. */
    [[nodiscard]] Result<void> registerSkin(const AttackVfxSkin& skin);

    /** @brief Return a copy of a registered recipe when present. */
    [[nodiscard]] std::optional<AttackVfxRecipe> findRecipe(const LogicalId& id) const;

    /** @brief Return a copy of a registered skin when present. */
    [[nodiscard]] std::optional<AttackVfxSkin> findSkin(const LogicalId& id) const;

    /**
     * @brief Spawn one instance from a registered recipe.
     * @return Generation-qualified handle or a structured diagnostic.
     */
    [[nodiscard]] Result<AttackVfxHandle> play(const LogicalId& recipeId, const AttackVfxRequest& request);

    /**
     * @brief Deliver a named cue to one live instance (e.g. \"impact\", \"cancel\").
     * @remarks The reserved cue \"cancel\" finishes the instance immediately.
     * @return Frame events produced by the cue (phase enter/exit, cue consumed/ignored).
     */
    [[nodiscard]] Result<AttackVfxFrame> signal(AttackVfxHandle handle, std::string_view cue);

    /**
     * @brief Advance all live instances by injected simulation seconds.
     * @param dtSeconds Finite, non-negative simulation delta.
     */
    [[nodiscard]] Result<AttackVfxFrame> advance(double dtSeconds);

    /** @brief Stop one instance using the requested teardown mode. */
    [[nodiscard]] Result<AttackVfxFrame> stop(AttackVfxHandle handle, AttackVfxStopMode mode);

    /** @brief Snapshot one live instance; empty when the handle is stale. */
    [[nodiscard]] std::optional<AttackVfxInstanceState> inspect(AttackVfxHandle handle) const;

    /** @brief Number of occupied slots. */
    [[nodiscard]] std::size_t activeCount() const noexcept;

    /** @brief Configured pool capacity. */
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::stylize
