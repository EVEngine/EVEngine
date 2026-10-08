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
    void setCharacters(CombatCharacterRuntime& characters) noexcept { characters_ = &characters; }
    void setMelee(MeleeHitRuntime& melee) noexcept { melee_ = &melee; }
    void setWindows(const CombatActionWindowState* windows) noexcept { windows_ = windows; }
    void setGuards(const GuardWindowState* guards) noexcept { guards_ = guards; }
    void setFeel(HitFeelRuntime* feel) noexcept { feel_ = feel; }
    void setTargets(CombatTargetRuntime* targets) noexcept { targets_ = targets; }
    void setCameraFocus(SubjectRef player) noexcept { cameraFocus_ = player; }
    /** @brief Borrow the map of CombatState values mutated by melee hits. */
    void setDamageStates(std::map<std::string, CombatState, std::less<>>* states) noexcept { states_ = states; }
    void setCancelResolver(CombatCancelResolver* cancels) noexcept { cancelResolver_ = cancels; }
    void setCancelSubject(SubjectRef subject) noexcept { cancelSubject_ = subject; }
    void setCancelAbility(LogicalId ability) noexcept { cancelAbility_ = std::move(ability); }
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
