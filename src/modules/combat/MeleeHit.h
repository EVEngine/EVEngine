#pragma once
#include "common/Export.h"

/** @file MeleeHit.h @brief Deterministic bone-agnostic melee hitbox/hurtbox sweep. */

#include "action/Action.h"
#include "combat/Damage.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eve::combat {

/** @brief Finite world-space point used by melee geometry. */
struct MeleePoint3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/** @brief Primitive shape used by hitboxes and hurtboxes. */
enum class MeleeShapeKind : std::uint8_t { Sphere, Capsule };

/**
 * @brief Owning geometry definition referenced by a stable hitbox or hurtbox id.
 *
 * Capsule axis is local +Y from `center - (0, halfHeight, 0)` to
 * `center + (0, halfHeight, 0)` after the authored world pose is applied.
 */
struct MeleeShape {
    MeleeShapeKind kind      = MeleeShapeKind::Sphere;
    double         radius    = 0.0;
    double         halfHeight = 0.0;

    /** @brief Reject non-finite or non-positive dimensions. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief World pose for one shape sample. */
struct MeleePose {
    MeleePoint3 position;
    /** @brief Yaw radians around world +Y; pitch/roll are unused for v1 capsules. */
    double      yawRadians = 0.0;

    /** @brief Reject non-finite pose values. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Body-part tag used for multipliers and debugging. */
struct MeleeHurtboxDefinition {
    SubjectRef  subject;
    std::string hurtboxId;
    std::string bodyPart;
    MeleeShape  shape;

    /** @brief Validate identity and shape. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Catalog entry that maps a timeline hitbox resource id onto geometry. */
struct MeleeHitboxDefinition {
    std::string hitboxId;
    MeleeShape  shape;
    /** @brief Local offset from the pose provided by the pose source. */
    MeleePoint3 localOffset;
    double      healthDamage = 0.0;
    double      poiseDamage  = 0.0;
    std::string damageType   = "Damage.Physical.Slash";

    /** @brief Validate id, shape and finite damage. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief One confirmed contact produced by a sweep frame. */
struct MeleeHitEvent {
    SubjectRef                               source;
    SubjectRef                               target;
    std::string                              hitboxId;
    std::string                              hurtboxId;
    std::string                              bodyPart;
    MeleePoint3                              contact;
    MeleePoint3                              normal;
    double                                   healthDamage = 0.0;
    double                                   poiseDamage  = 0.0;
    std::string                              damageType;
    std::optional<action::ActionExecutionId> actionExecution;
};

/** @brief Complete owning result of one melee advance. */
struct MeleeHitFrame {
    SimulationTick            tick = SimulationTick::zero();
    std::vector<MeleeHitEvent> hits;
};

/**
 * @brief Optional pose resolver for active hitbox ids.
 *
 * Called synchronously on the owner thread. Implementations must not retain
 * borrowed strings, reenter MeleeHitRuntime, or read a wall clock.
 */
class IMeleePoseSource {
public:
    virtual ~IMeleePoseSource() = default;
    /**
     * @brief Resolve the current world pose for one subject's active hitbox.
     * @param subject Attacking subject.
     * @param hitboxId Timeline resource id (e.g. weapon.main).
     */
    [[nodiscard]] virtual Result<MeleePose> pose(SubjectRef subject, std::string_view hitboxId) const = 0;
};

/**
 * @brief Owner-thread authority for hurtboxes, hitbox catalogs and sweep hits.
 *
 * Active hitboxes are armed by string id while a Montage hitbox window is open.
 * Geometry poses come from an optional pose source; missing poses fail the arm
 * or advance observably. Hits are de-duplicated per (execution, hitbox, target,
 * hurtbox) until the window exits.
 */
class EVENGINE_API_BACKENDS MeleeHitRuntime {
public:
    /** @brief Install a borrowed pose source that must outlive this runtime or be cleared. */
    void setPoseSource(IMeleePoseSource& source) noexcept { poses_ = &source; }
    /** @brief Remove the borrowed pose source. */
    void clearPoseSource() noexcept { poses_ = nullptr; }

    /** @brief Register or replace one hitbox catalog entry after full validation. */
    [[nodiscard]] Result<void> registerHitbox(MeleeHitboxDefinition definition);
    /** @brief Register or replace one hurtbox after full validation. */
    [[nodiscard]] Result<void> registerHurtbox(MeleeHurtboxDefinition definition);
    /** @brief Remove every hurtbox owned by a subject. */
    [[nodiscard]] Result<void> clearHurtboxes(SubjectRef subject);
    /** @brief Update the current world pose of one registered hurtbox. */
    [[nodiscard]] Result<void> setHurtboxPose(SubjectRef subject, std::string_view hurtboxId, MeleePose pose);

    /**
     * @brief Arm one hitbox while a Montage window owns it.
     * @remarks Requires a pose source. The first pose becomes both previous and current.
     */
    [[nodiscard]] Result<void> armHitbox(SubjectRef subject, std::string_view hitboxId,
                                         action::ActionExecutionId execution);
    /** @brief Disarm one hitbox window and forget its per-target hit memory. */
    [[nodiscard]] Result<void> disarmHitbox(SubjectRef subject, std::string_view hitboxId,
                                            action::ActionExecutionId execution);

    /**
     * @brief Sample poses for armed hitboxes, sweep against hurtboxes and emit unique hits.
     * @param tick Deterministic simulation tick; must be non-decreasing.
     */
    [[nodiscard]] Result<MeleeHitFrame> advance(SimulationTick tick);

    /** @brief Apply owning hit events through DamageRuntime using a mutable state map. */
    [[nodiscard]] Result<std::vector<DamageOutcome>> applyHits(
        DamageRuntime& damage, std::map<std::string, CombatState, std::less<>>& states,
        const std::vector<MeleeHitEvent>& hits,
        const std::map<std::string, double, std::less<>>& bodyPartMultipliers = {}) const;

    /** @brief Number of armed hitbox windows. */
    [[nodiscard]] std::size_t armedCount() const noexcept { return armed_.size(); }
    /** @brief Number of registered hurtboxes. */
    [[nodiscard]] std::size_t hurtboxCount() const noexcept { return hurtboxes_.size(); }

private:
    struct HurtboxState {
        MeleeHurtboxDefinition definition;
        MeleePose              pose;
        bool                   hasPose = false;
    };
    struct ArmedHitbox {
        SubjectRef                subject;
        std::string               hitboxId;
        action::ActionExecutionId execution;
        MeleePose                 previous;
        MeleePose                 current;
        bool                      hasPose = false;
    };
    using ArmedKey   = std::tuple<std::string, std::string, std::uint64_t>;
    using HitMemoryKey = std::tuple<std::uint64_t, std::string, std::string, std::string>;

    std::map<std::string, MeleeHitboxDefinition, std::less<>> hitboxes_;
    std::map<std::string, HurtboxState, std::less<>>          hurtboxes_;
    std::map<ArmedKey, ArmedHitbox>                           armed_;
    std::map<HitMemoryKey, bool>                              hitMemory_;
    IMeleePoseSource*                                         poses_   = nullptr;
    SimulationTick                                            lastTick_ = SimulationTick::zero();
};

}  // namespace eve::combat
