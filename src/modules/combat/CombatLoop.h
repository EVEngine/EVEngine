#pragma once
#include "common/Export.h"

/** @file CombatLoop.h @brief One-frame composition of character, melee, guard, feel and camera. */

#include "combat/CombatCamera.h"
#include "combat/CombatCancelResolver.h"
#include "combat/CombatCharacter.h"
#include "combat/CombatEnemyAI.h"
#include "combat/CombatTarget.h"
#include "combat/Damage.h"
#include "combat/GuardWindowState.h"
#include "combat/HitFeel.h"
#include "combat/MeleeHit.h"
#include "common/Identity.h"

#include <map>
#include <string>
#include <vector>

namespace eve::combat {

class CombatActionWindowState;

/** @brief Owning audit that a dodging i-frame negated a melee hit. */
struct CombatPerfectDodge {
    SubjectRef  defender;
    SubjectRef  attacker;
    std::string hitboxId;
};

/** @brief Owning audit of one composed combat frame. */
struct CombatLoopFrame {
    SimulationTick                      tick = SimulationTick::zero();
    CombatCharacterAdvance              locomotion;
    MeleeHitFrame                       melee;
    std::vector<DamageOutcome>          outcomes;
    std::vector<GuardEvaluation>        guards;
    std::vector<CombatPerfectDodge>     perfectDodges;
    std::vector<CombatCancelResolution> cancels;
    std::vector<CombatEnemySteering>    enemySteering;
    CombatCameraView                    camera;
};

/**
 * @brief Owner-thread coordinator that welds combat runtimes for one arena tick.
 *
 * Each collaborator remains the unique owner of its facts. This object only
 * sequences optional enemy steering / cancel resolve → warp → character →
 * hurtbox pose → melee → guard/invuln/damage/feel/reaction → camera framing.
 * It does not introduce a CombatActor root.
 */
class EVENGINE_API_BACKENDS CombatLoopRuntime {
public:
    /** @brief Sets the characters. */
    void setCharacters(CombatCharacterRuntime& characters) noexcept { characters_ = &characters; }
    /** @brief Sets the melee. */
    void setMelee(MeleeHitRuntime& melee) noexcept { melee_ = &melee; }
    /** @brief Sets the windows. */
    void setWindows(const CombatActionWindowState* windows) noexcept { windows_ = windows; }
    /** @brief Sets the guards. */
    void setGuards(const GuardWindowState* guards) noexcept { guards_ = guards; }
    /** @brief Sets the feel. */
    void setFeel(HitFeelRuntime* feel) noexcept { feel_ = feel; }
    /** @brief Sets the targets. */
    void setTargets(CombatTargetRuntime* targets) noexcept { targets_ = targets; }
    /** @brief Sets the camera focus. */
    void setCameraFocus(SubjectRef player) noexcept { cameraFocus_ = player; }
    /** @brief Borrow the map of CombatState values mutated by melee hits. */
    void setDamageStates(std::map<std::string, CombatState, std::less<>>* states) noexcept { states_ = states; }
    /** @brief Sets the cancel resolver. */
    void setCancelResolver(CombatCancelResolver* cancels) noexcept { cancelResolver_ = cancels; }
    /** @brief Sets the cancel subject. */
    void setCancelSubject(SubjectRef subject) noexcept { cancelSubject_ = subject; }
    /** @brief Sets the cancel ability. */
    void setCancelAbility(LogicalId ability) noexcept { cancelAbility_ = std::move(ability); }
    /** @brief Sets the enemy ai. */
    void setEnemyAI(CombatEnemyIntentSource* enemies) noexcept { enemies_ = enemies; }

    /**
     * @brief Advance every wired runtime with the supplied deterministic step.
     * @remarks Frozen (hitstop) subjects skip character integration for this tick.
     */
    [[nodiscard]] Result<CombatLoopFrame> advance(const SimulationStep& step);

private:
    CombatCharacterRuntime*                          characters_     = nullptr;
    MeleeHitRuntime*                                 melee_          = nullptr;
    const CombatActionWindowState*                   windows_        = nullptr;
    const GuardWindowState*                          guards_         = nullptr;
    HitFeelRuntime*                                  feel_           = nullptr;
    CombatTargetRuntime*                             targets_        = nullptr;
    std::map<std::string, CombatState, std::less<>>* states_         = nullptr;
    CombatCancelResolver*                            cancelResolver_ = nullptr;
    CombatEnemyIntentSource*                         enemies_        = nullptr;
    SubjectRef                                       cameraFocus_;
    SubjectRef                                       cancelSubject_;
    LogicalId                                        cancelAbility_;
};

}  // namespace eve::combat
