#pragma once
#include "common/Export.h"


/** @file CombatLocomotion.h @brief Deterministic player/navigation locomotion adapter for combat subjects. */

#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eve::combat {

/** @brief Finite position or direction in the horizontal combat plane. */
struct CombatVector2 {
    double x = 0.0;
    double z = 0.0;
};

/** @brief Immutable registration data for one locomotion subject. */
struct CombatLocomotionDefinition {
    SubjectRef    subject;
    std::string   ownerId;
    CombatVector2 initialPosition;
    double        maximumSpeed = 0.0;
    double        acceleration = 0.0;

    /** @brief Validate identity and finite positive movement limits. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning navigation goal retained by the locomotion authority. */
struct CombatNavigationGoal {
    CombatVector2 position;
    double        acceptanceRadius = 0.0;
};

/** @brief Owning read-only projection of one registered combat subject. */
struct CombatLocomotionState {
    SubjectRef                          subject;
    std::string                         ownerId;
    CombatVector2                       position;
    CombatVector2                       velocity;
    CombatVector2                       facing{1.0, 0.0};
    CombatVector2                       moveDirection;
    double                              moveSpeedFraction = 0.0;
    double                              maximumSpeed = 0.0;
    double                              acceleration = 0.0;
    std::optional<CombatNavigationGoal> navigationGoal;
};

/** @brief Navigation steering status for the current deterministic step. */
enum class CombatNavigationPhase : std::uint8_t { Moving, Arrived };

/** @brief Owning output produced by an optional navigation adapter. */
struct CombatNavigationSteering {
    CombatNavigationPhase phase = CombatNavigationPhase::Moving;
    CombatVector2         direction;
    double                speedFraction = 1.0;
};

/**
 * @brief Optional synchronous navigation boundary consumed by combat locomotion.
 *
 * Implementations borrow state and goal only for the call, retain neither,
 * invoke no locomotion callbacks, and use the supplied simulation tick rather
 * than a clock. The provider owner must outlive the runtime while installed.
 */
class ICombatNavigationProvider {
public:
    virtual ~ICombatNavigationProvider() = default;

    /**
     * @brief Resolve steering for one subject without mutating locomotion state.
     * @param state Current candidate state borrowed for this call.
     * @param goal Owning runtime goal borrowed for this call.
     * @param tick Deterministic tick being prepared.
     * @return Valid steering or a structured failure that aborts the whole locomotion frame.
     */
    [[nodiscard]] virtual Result<CombatNavigationSteering> steer(const CombatLocomotionState& state,
                                                                  const CombatNavigationGoal& goal,
                                                                  SimulationTick tick) = 0;
};

/** @brief Straight-line obstacle-free provider useful for arenas and deterministic tests. */
class EVENGINE_API_BACKENDS DirectCombatNavigationProvider final : public ICombatNavigationProvider {
public:
    /** @copydoc ICombatNavigationProvider::steer */
    [[nodiscard]] Result<CombatNavigationSteering> steer(const CombatLocomotionState& state,
                                                          const CombatNavigationGoal& goal,
                                                          SimulationTick tick) override;
};

/** @brief Observable committed locomotion event. */
enum class CombatLocomotionEventKind : std::uint8_t { Moved, Arrived };

/** @brief Owning event emitted after a successful complete locomotion frame. */
struct CombatLocomotionEvent {
    SubjectRef                   subject;
    CombatLocomotionEventKind    kind = CombatLocomotionEventKind::Moved;
    CombatVector2                previousPosition;
    CombatVector2                position;
    SimulationTick               tick = SimulationTick::zero();
};

/** @brief Complete owning result of one committed locomotion frame. */
struct CombatLocomotionAdvance {
    SimulationTick                      tick = SimulationTick::zero();
    std::vector<CombatLocomotionEvent> events;
};

/**
 * @brief Owner-thread combat locomotion authority shared by player and navigation commands.
 *
 * The runtime owns every mutable movement fact. Navigation is optional and
 * borrowed; advance prepares all states before one publication, so a provider
 * failure cannot leave a partially moved arena. No method reads a clock or RNG.
 */
class EVENGINE_API_BACKENDS CombatLocomotionRuntime {
public:
    /** @brief Construct without an installed navigation provider. */
    CombatLocomotionRuntime() = default;
    /** @brief Construct borrowing a provider that must outlive this runtime or be cleared. */
    explicit CombatLocomotionRuntime(ICombatNavigationProvider& provider) : navigation_(&provider) {}

    /** @brief Install a borrowed provider; active goals remain owned by this runtime. */
    void setNavigationProvider(ICombatNavigationProvider& provider) noexcept { navigation_ = &provider; }
    /** @brief Remove the borrowed provider; advancing an active goal then fails observably. */
    void clearNavigationProvider() noexcept { navigation_ = nullptr; }
    /** @brief Register one unique subject from validated owning definition data. */
    [[nodiscard]] Result<void> registerSubject(CombatLocomotionDefinition definition);
    /** @brief Remove one subject; missing subjects are rejected. */
    [[nodiscard]] Result<void> unregisterSubject(SubjectRef subject);
    /** @brief Return an owning state snapshot, or NotFound for a stale subject. */
    [[nodiscard]] Result<CombatLocomotionState> state(SubjectRef subject) const;
    /**
     * @brief Set normalized player/AI movement and clear any navigation goal.
     * @param subject Registered subject.
     * @param direction Finite direction; non-unit input is normalized.
     * @param speedFraction Requested fraction in [0,1].
     */
    [[nodiscard]] Result<void> setMoveIntent(SubjectRef subject, CombatVector2 direction,
                                              double speedFraction);
    /** @brief Install a navigation goal after provider and value validation. */
    [[nodiscard]] Result<void> navigateTo(SubjectRef subject, CombatNavigationGoal goal);
    /** @brief Clear a navigation goal and desired movement for one subject. */
    [[nodiscard]] Result<void> stop(SubjectRef subject);
    /** @brief Atomically advance every subject using only the supplied deterministic step. */
    [[nodiscard]] Result<CombatLocomotionAdvance> advance(const SimulationStep& step);
    /** @brief Return owning states in stable subject-id order. */
    [[nodiscard]] std::vector<CombatLocomotionState> states() const;

private:
    std::map<std::string, CombatLocomotionState, std::less<>> states_;
    ICombatNavigationProvider*                               navigation_ = nullptr;
    SimulationTick                                           lastTick_ = SimulationTick::zero();
};

}  // namespace eve::combat
