#pragma once

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

    friend bool operator==(const ProjectilePoint&, const ProjectilePoint&) = default;
};

/** @brief Owning velocity or direction vector in projectile simulation space. */
struct ProjectileVector {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const ProjectileVector&, const ProjectileVector&) = default;
};

/** @brief Generation-qualified reference to one pooled projectile slot. */
struct ProjectileHandle {
    std::uint32_t slot       = 0;
    std::uint32_t generation = 0;

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
    [[nodiscard]] Result<void> validate() const;
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
};

/** @brief Owning result of one atomic projectile simulation step. */
struct ProjectileUpdate {
    std::vector<ProjectileHandle> advanced;
    std::vector<ProjectileHandle> released;
};

/** @brief Optional target-position boundary used only by homing projectiles. */
class IProjectileTargetProvider {
public:
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
class ProjectileRuntime {
public:
    ProjectileRuntime();

    /** @brief Resize the pool while empty; capacity must be in 1..1048576. */
    [[nodiscard]] Result<void> configurePool(std::uint32_t capacity);
    /** @brief Spawn one projectile or return a structured capacity/validation failure. */
    [[nodiscard]] Result<ProjectileHandle> spawn(const ProjectileDefinition&   definition,
                                                 const ProjectileSpawnRequest& request);
    /** @brief Atomically advance all live projectiles by supplied deterministic time. */
    [[nodiscard]] Result<ProjectileUpdate> update(Duration delta, const IProjectileTargetProvider* targets = nullptr);
    /** @brief Explicitly release one live handle; stale handles are rejected. */
    [[nodiscard]] Result<void> release(ProjectileHandle handle);
    /** @brief Return an owning state copy, or empty for a stale/released handle. */
    [[nodiscard]] std::optional<ProjectileState> find(ProjectileHandle handle) const;
    /** @brief Return owning live states in stable slot order. */
    [[nodiscard]] std::vector<ProjectileState> states() const;
    /** @brief Number of live projectiles. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return activeCount_; }
    /** @brief Current fixed pool capacity. */
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

private:
    struct Slot {
        std::uint32_t                  generation = 0;
        std::optional<ProjectileState> state;
    };

    [[nodiscard]] static Result<void> validateSpawn(const ProjectileDefinition&   definition,
                                                    const ProjectileSpawnRequest& request);

    std::vector<Slot> slots_;
    std::size_t       activeCount_ = 0;
};

}  // namespace eve::weapon
