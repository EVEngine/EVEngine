#pragma once
#include "common/Export.h"

/** @file CombatCharacter.h @brief 3D action-combat character controller. */

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

/** @brief Finite 3D combat vector. */
struct CombatVector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/** @brief High-level locomotion mode owned by the character controller. */
enum class CombatCharacterMode : std::uint8_t { Grounded, Airborne, Dodging, Attacking, Stunned, Dead };

/** @brief Lock-relative dodge direction on the XZ plane. */
enum class CombatDodgeRelative : std::uint8_t { Forward, Back, Left, Right };

/** @brief Immutable registration for one action-combat character. */
struct CombatCharacterDefinition {
    SubjectRef    subject;
    std::string   ownerId;
    CombatVector3 initialPosition;
    double        maximumSpeed  = 5.0;
    double        acceleration  = 18.0;
    double        jumpSpeed     = 6.0;
    double        gravity       = 20.0;
    double        dodgeSpeed    = 10.0;
    double        dodgeDuration = 0.28;
    std::uint32_t maxAirJumps   = 1;
    double        capsuleRadius = 0.35;

    /** @brief Validate identity and finite positive movement limits. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning read-only projection of one character. */
struct CombatCharacterState {
    SubjectRef                   subject;
    std::string                  ownerId;
    CombatVector3                position;
    CombatVector3                velocity;
    CombatVector3                facing{1.0, 0.0, 0.0};
    CombatVector3                moveDirection;
    double                       moveSpeedFraction = 0.0;
    double                       maximumSpeed      = 0.0;
    double                       acceleration      = 0.0;
    double                       jumpSpeed         = 0.0;
    double                       gravity           = 0.0;
    double                       dodgeSpeed        = 0.0;
    double                       dodgeDuration     = 0.0;
    double                       capsuleRadius     = 0.35;
    std::uint32_t                maxAirJumps       = 0;
    std::uint32_t                airJumpsUsed      = 0;
    CombatCharacterMode          mode              = CombatCharacterMode::Grounded;
    double                       modeTimeRemaining = 0.0;
    bool                         invulnerable      = false;
    bool                         timeFrozen        = false;
    std::optional<CombatVector3> rootMotionDelta;
};

/** @brief Observable character event after a committed frame. */
enum class CombatCharacterEventKind : std::uint8_t { Moved, Jumped, Landed, DodgeStarted, DodgeEnded, ModeChanged };

/** @brief Owning event emitted by the character controller. */
struct CombatCharacterEvent {
    SubjectRef               subject;
    CombatCharacterEventKind kind = CombatCharacterEventKind::Moved;
    CombatCharacterMode      mode = CombatCharacterMode::Grounded;
    CombatVector3            position;
    SimulationTick           tick = SimulationTick::zero();
};

/** @brief Complete owning result of one character advance. */
struct CombatCharacterAdvance {
    SimulationTick                    tick = SimulationTick::zero();
    std::vector<CombatCharacterEvent> events;
};

/**
 * @brief Optional ground-height sampler consumed by the character controller.
 *
 * Implementations borrow coordinates only for the call, retain nothing, and use
 * no wall clock. Missing providers keep the v1 plane y = 0.
 */
class ICombatGroundProvider {
public:
    virtual ~ICombatGroundProvider() = default;
    /** @brief Return finite ground height at the supplied world XZ. */
    [[nodiscard]] virtual Result<double> sampleHeight(double x, double z) const = 0;
};

/**
 * @brief Optional capsule move resolver for walls and overlapping bodies.
 *
 * Return the corrected destination. Failures abort the whole character frame.
 */
class ICombatMoveProbe {
public:
    virtual ~ICombatMoveProbe() = default;
    /**
     * @brief Resolve a candidate translation for one capsule.
     * @param from Current committed position.
     * @param to Unconstrained destination.
     * @param radius Finite positive capsule radius.
     */
    [[nodiscard]] virtual Result<CombatVector3> resolve(const CombatVector3& from, const CombatVector3& to,
                                                        double radius) const = 0;
};

/**
 * @brief Owner-thread 3D character authority for action combat.
 *
 * Horizontal intent, jump, dodge i-frames and optional root-motion deltas are
 * owned here. Ground is the plane y = 0 unless an `ICombatGroundProvider` is
 * borrowed; capsule collisions are optional via `ICombatMoveProbe`. Projects
 * project the resulting pose into Scene/Physics after each advance.
 */
class EVENGINE_API_BACKENDS CombatCharacterRuntime {
public:
    /** @brief Register one unique character from validated owning definition data. */
    [[nodiscard]] Result<void> registerSubject(CombatCharacterDefinition definition);
    /** @brief Remove one character; missing subjects are rejected. */
    [[nodiscard]] Result<void> unregisterSubject(SubjectRef subject);
    /** @brief Return an owning state snapshot, or NotFound for a stale subject. */
    [[nodiscard]] Result<CombatCharacterState> state(SubjectRef subject) const;

    /**
     * @brief Set normalized planar movement intent.
     * @param direction Finite XZ direction; non-unit input is normalized. Y is ignored.
     * @param speedFraction Requested fraction in [0,1].
     */
    [[nodiscard]] Result<void> setMoveIntent(SubjectRef subject, CombatVector3 direction, double speedFraction);
    /** @brief Request a jump or air jump when the current mode allows it. */
    [[nodiscard]] Result<void> jump(SubjectRef subject);
    /**
     * @brief Begin a dodge that grants invulnerability for dodgeDuration.
     * @param direction Optional dodge direction; empty uses current facing.
     */
    [[nodiscard]] Result<void> dodge(SubjectRef subject, std::optional<CombatVector3> direction = std::nullopt);
    /**
     * @brief Dodge relative to a lock-on target on the XZ plane.
     * @param lockTarget Finite world position of the locked subject.
     * @param relative Forward = toward target, Back = away, Left/Right = strafe.
     */
    [[nodiscard]] Result<void> dodgeRelative(SubjectRef subject, CombatVector3 lockTarget,
                                             CombatDodgeRelative relative);
    /** @brief Enter attacking mode; root motion may replace horizontal intent. */
    [[nodiscard]] Result<void> beginAttack(SubjectRef subject);
    /** @brief Leave attacking mode when recover completes. */
    [[nodiscard]] Result<void> endAttack(SubjectRef subject);
    /** @brief Apply a stun that blocks jump/dodge/attack for the supplied duration. */
    [[nodiscard]] Result<void> applyStun(SubjectRef subject, Duration duration);
    /**
     * @brief Add an instantaneous velocity impulse (knockback / launch).
     * @remarks Dead subjects are NoOp. Positive Y leaves the subject airborne after stun ends.
     */
    [[nodiscard]] Result<void> applyImpulse(SubjectRef subject, CombatVector3 impulse);
    /**
     * @brief Apply death/stun and knockback from one damage outcome.
     * @param stunDuration Hitstun remaining from HitFeelRuntime; ignored for Death.
     */
    [[nodiscard]] Result<void> applyDamageReaction(SubjectRef subject, HitReaction reaction, Duration stunDuration,
                                                   Impulse3 knockback);
    /** @brief Mark the character dead; further locomotion intents become NoOp. */
    [[nodiscard]] Result<void> kill(SubjectRef subject);
    /**
     * @brief Queue one frame of authored root-motion delta consumed by the next advance.
     * @remarks Only applied while mode is Attacking.
     */
    [[nodiscard]] Result<void> setRootMotionDelta(SubjectRef subject, CombatVector3 delta);
    /**
     * @brief Replace planar facing from a finite XZ vector; Y is ignored.
     * @remarks Zero-length input is rejected. Frozen and dead subjects still accept facing.
     */
    [[nodiscard]] Result<void> setFacing(SubjectRef subject, CombatVector3 facing);
    /**
     * @brief Pause integration for hitstop while retaining the committed pose.
     * @remarks HitFeelRuntime remains the duration authority; this flag is only the locomotion gate.
     */
    [[nodiscard]] Result<void> setTimeFrozen(SubjectRef subject, bool frozen);
    /** @brief Borrow a ground sampler; must outlive this runtime or be cleared. */
    void setGroundProvider(ICombatGroundProvider& provider) noexcept { ground_ = &provider; }
    void clearGroundProvider() noexcept { ground_ = nullptr; }
    /** @brief Borrow a capsule move probe; must outlive this runtime or be cleared. */
    void setMoveProbe(ICombatMoveProbe& probe) noexcept { probe_ = &probe; }
    void clearMoveProbe() noexcept { probe_ = nullptr; }
    /** @brief Atomically advance every character using only the supplied deterministic step. */
    [[nodiscard]] Result<CombatCharacterAdvance> advance(const SimulationStep& step);
    /** @brief Return owning states in stable subject-id order. */
    [[nodiscard]] std::vector<CombatCharacterState> states() const;

private:
    std::map<std::string, CombatCharacterState, std::less<>> states_;
    ICombatGroundProvider*                                   ground_   = nullptr;
    ICombatMoveProbe*                                        probe_    = nullptr;
    SimulationTick                                           lastTick_ = SimulationTick::zero();
};

}  // namespace eve::combat
