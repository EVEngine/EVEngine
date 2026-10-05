#pragma once
#include "common/Export.h"

/** @file CombatCharacter.h @brief 3D action-combat character controller. */

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
enum class CombatCharacterMode : std::uint8_t {
    Grounded,
    Airborne,
    Dodging,
    Attacking,
    Stunned,
    Dead
};

/** @brief Immutable registration for one action-combat character. */
struct CombatCharacterDefinition {
    SubjectRef    subject;
    std::string   ownerId;
    CombatVector3 initialPosition;
    double        maximumSpeed   = 5.0;
    double        acceleration   = 18.0;
    double        jumpSpeed      = 6.0;
    double        gravity        = 20.0;
    double        dodgeSpeed     = 10.0;
    double        dodgeDuration  = 0.28;
    std::uint32_t maxAirJumps    = 1;

    /** @brief Validate identity and finite positive movement limits. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning read-only projection of one character. */
struct CombatCharacterState {
    SubjectRef          subject;
    std::string         ownerId;
    CombatVector3       position;
    CombatVector3       velocity;
    CombatVector3       facing{1.0, 0.0, 0.0};
    CombatVector3       moveDirection;
    double              moveSpeedFraction = 0.0;
    double              maximumSpeed      = 0.0;
    double              acceleration      = 0.0;
    double              jumpSpeed         = 0.0;
    double              gravity           = 0.0;
    double              dodgeSpeed        = 0.0;
    double              dodgeDuration     = 0.0;
    std::uint32_t       maxAirJumps       = 0;
    std::uint32_t       airJumpsUsed      = 0;
    CombatCharacterMode mode              = CombatCharacterMode::Grounded;
    double              modeTimeRemaining = 0.0;
    bool                invulnerable      = false;
    std::optional<CombatVector3> rootMotionDelta;
};

/** @brief Observable character event after a committed frame. */
enum class CombatCharacterEventKind : std::uint8_t {
    Moved,
    Jumped,
    Landed,
    DodgeStarted,
    DodgeEnded,
    ModeChanged
};

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
    SimulationTick                     tick = SimulationTick::zero();
    std::vector<CombatCharacterEvent> events;
};

/**
 * @brief Owner-thread 3D character authority for action combat.
 *
 * Horizontal intent, jump, dodge i-frames and optional root-motion deltas are
 * owned here. Ground is the plane y = 0 for v1 arenas; projects can project the
 * resulting pose into Scene/Physics after each advance.
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
    /** @brief Enter attacking mode; root motion may replace horizontal intent. */
    [[nodiscard]] Result<void> beginAttack(SubjectRef subject);
    /** @brief Leave attacking mode when recover completes. */
    [[nodiscard]] Result<void> endAttack(SubjectRef subject);
    /** @brief Apply a stun that blocks jump/dodge/attack for the supplied duration. */
    [[nodiscard]] Result<void> applyStun(SubjectRef subject, Duration duration);
    /** @brief Mark the character dead; further locomotion intents become NoOp. */
    [[nodiscard]] Result<void> kill(SubjectRef subject);
    /**
     * @brief Queue one frame of authored root-motion delta consumed by the next advance.
     * @remarks Only applied while mode is Attacking.
     */
    [[nodiscard]] Result<void> setRootMotionDelta(SubjectRef subject, CombatVector3 delta);
    /** @brief Atomically advance every character using only the supplied deterministic step. */
    [[nodiscard]] Result<CombatCharacterAdvance> advance(const SimulationStep& step);
    /** @brief Return owning states in stable subject-id order. */
    [[nodiscard]] std::vector<CombatCharacterState> states() const;

private:
    std::map<std::string, CombatCharacterState, std::less<>> states_;
    SimulationTick                                           lastTick_ = SimulationTick::zero();
};

}  // namespace eve::combat
