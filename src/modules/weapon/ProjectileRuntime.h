#pragma once
#include "common/Export.h"


/** @file ProjectileRuntime.h @brief Deterministic pooled projectile simulation. */

#include "common/ECS.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Time.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace eve::weapon {

/** @brief Built-in trajectory integration mode. */
enum class ProjectileMode : std::uint8_t { Linear, Ballistic, Homing };

/** @brief Owning point in projectile simulation space. */
struct ProjectilePoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    /** @brief Operator ==. */
    friend bool operator==(const ProjectilePoint&, const ProjectilePoint&) = default;
};

/** @brief Owning velocity or direction vector in projectile simulation space. */
struct ProjectileVector {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    /** @brief Operator ==. */
    friend bool operator==(const ProjectileVector&, const ProjectileVector&) = default;
};

/** @brief Pool-scoped generation-qualified reference; empty slots at maximum generation retire permanently. */
struct ProjectileHandle {
    std::uint32_t slot       = 0;
    std::uint32_t generation = 0;

    /** @brief Lossless pool-scoped value key; does not identify a world or issue live permission. */
    [[nodiscard]] constexpr std::uint64_t packedKey() const noexcept {
        return (std::uint64_t(generation) << 32) | slot;
    }
    /** @brief Decode value bits only; the owning pool must validate generation and slot correspondence. */
    [[nodiscard]] static constexpr ProjectileHandle fromPackedKey(std::uint64_t key) noexcept {
        return {std::uint32_t(key), std::uint32_t(key >> 32)};
    }
    /** @brief Operator ==. */
    friend bool operator==(const ProjectileHandle&, const ProjectileHandle&) = default;
};

/** @brief Data-driven trajectory and lifetime definition. */
struct ProjectileDefinition {
    LogicalId      id;
    ProjectileMode mode               = ProjectileMode::Linear;
    double         speed              = 0.0;
    double         gravity            = 0.0;
    double         maxTurnRateDegrees = 0.0;
    Duration       lifetime           = Duration::zero();

    /** @brief Validate finite, non-negative trajectory parameters and mode-specific requirements. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> validate() const;
};

/** @brief Owning spawn request; homing requests require a generation-checked target. */
struct ProjectileSpawnRequest {
    ProjectilePoint                  position;
    ProjectileVector                 direction;
    std::optional<ecs::EntityHandle> target;
};

/** @brief Owning observable state for one live projectile. */
struct ProjectileState {
    ProjectileHandle                 handle;
    LogicalId                        definitionId;
    ProjectileMode                   mode = ProjectileMode::Linear;
    ProjectilePoint                  position;
    ProjectileVector                 velocity;
    double                           gravity            = 0.0;
    double                           maxTurnRateDegrees = 0.0;
    Duration                         age;
    Duration                         lifetime;
    std::optional<ecs::EntityHandle> target;
    /** @brief Validate portable trajectory facts without resolving or requiring a process-local homing target.
     * @return Invalid mode, id/handle generation, nonfinite transform/parameters or age/lifetime diagnostics.
     * @remarks The owning adapter separately admits stable relationships and binds native homing targets.
     * @thread Read-only owner-thread inspection; no callbacks, services or mutations.
     */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> validateTrajectory() const;
};

/** @brief Owning result of one atomic projectile simulation step. */
struct ProjectileUpdate {
    std::vector<ProjectileHandle> advanced;
    std::vector<ProjectileHandle> released;
};

/** @brief One pool slot including its generation, retained even while empty. */
struct ProjectileSlotSnapshot {
    std::uint32_t generation = 0;
    std::optional<ProjectileState> state;
};

/** @brief Complete deterministic projectile-pool state for rollback and save games. */
struct ProjectileRuntimeSnapshot {
    std::vector<ProjectileSlotSnapshot> slots;
    std::uint32_t capacity = 0;  ///< Active slot window; slots retain generation history across shrinking/regrowing.
};

/** @brief Optional target-position boundary used only by homing projectiles. */
class IProjectileTargetProvider {
public:
    /** @brief Releases IProjectileTargetProvider resources. */
    virtual ~IProjectileTargetProvider() = default;
    /** @brief Resolve one generation-checked target to an owning simulation-space position. */
    [[nodiscard]] virtual Result<ProjectilePoint> position(ecs::EntityHandle target) const = 0;
};

/**
 * @brief Owner-thread deterministic projectile simulator with a bounded reusable pool.
 *
 * update stages every live state and publishes only after all homing target
 * lookups succeed. It uses injected Duration and no clock or RNG. Floating
 * point replay is deterministic for the same compiler/backend; cross-backend
 * comparison should use a documented positional tolerance.
 */
class EVENGINE_API_WORLD ProjectileRuntime {
public:
    /** @brief Projectile runtime. */
    ProjectileRuntime();

    /** @brief Maximum retained slot history and active capacity, without allocation or callbacks. */
    [[nodiscard]] static constexpr std::uint32_t maximumCapacity() noexcept { return 1024 * 1024; }
    /** @brief Resize the active window while empty, preserving generation history; capacity in 1..maximumCapacity.
     * @return Applied or invalid/capacity-in-use failure; allocation failure leaves the pool unchanged.
     * @thread Owner thread, no callbacks.
     * @cost Copies retained slot history once per explicit pool configuration; shrinking does not discard history.
     * @throws std::bad_alloc No capacity or generation changed.
     */
    [[nodiscard]] Result<void> configurePool(std::uint32_t capacity);
    /** @brief Spawn one projectile or return a structured capacity/validation failure. */
    [[nodiscard]] Result<ProjectileHandle> spawn(const ProjectileDefinition&   definition,
                                                 const ProjectileSpawnRequest& request);
    /** @brief Atomically advance all live projectiles by supplied deterministic time.
     * @ownership Borrows provider only during call; it must keep this runtime alive and must not re-enter mutation.
     * @thread Owner thread; target queries execute without locks. */
    [[nodiscard]] Result<ProjectileUpdate> update(Duration delta, const IProjectileTargetProvider* targets = nullptr);
    /** @brief Explicitly release one live handle; stale handles are rejected. */
    [[nodiscard]] Result<void> release(ProjectileHandle handle);
    /** @brief Return an owning state copy, or empty for a stale/released handle. */
    [[nodiscard]] std::optional<ProjectileState> find(ProjectileHandle handle) const;
    /** @brief Return owning live states in stable slot order. */
    [[nodiscard]] std::vector<ProjectileState> states() const;
    /** @brief Capture live trajectories, active capacity and all retained empty-slot generation history.
     * @ownership Owning CPU values; no world or provider retained. @thread Owner thread, no callbacks.
     * @cost Copies every retained slot once per explicit checkpoint.
     */
    [[nodiscard]] ProjectileRuntimeSnapshot snapshot() const;
    /** @brief Atomically replace this pool from a validated snapshot, including active capacity and generation history.
     * @return Invalid slot/state/capacity/native-target failure or Applied; target changes only after full validation.
     * @ownership Borrows snapshot during call; retains copied values only. @thread Owner thread, no callbacks.
     * @cost Copies all retained slots once per explicit restore attempt.
     * @throws std::bad_alloc The original pool remains intact.
     */
    [[nodiscard]] Result<void> restore(const ProjectileRuntimeSnapshot& snapshot);
    /** @brief Number of live projectiles. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return activeCount_; }
    /** @brief Current fixed pool capacity. */
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    struct Slot {
        std::uint32_t                  generation = 0;
        std::optional<ProjectileState> state;
    };

    [[nodiscard]] static Result<void> validateSpawn(const ProjectileDefinition&   definition,
                                                    const ProjectileSpawnRequest& request);

    std::vector<Slot> slots_;
    std::uint32_t     capacity_    = 0;
    std::size_t       activeCount_ = 0;
};

}  // namespace eve::weapon
