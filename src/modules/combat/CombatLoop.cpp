#include "combat/CombatLoop.h"

#include "combat/ActionWindowState.h"
#include "combat/CombatMotionWarp.h"

namespace eve::combat {
namespace {

bool invulnerable(const CombatActionWindowState* windows, const CombatCharacterRuntime* characters,
                  SubjectRef subject) {
    if (windows && windows->isInvulnerable(subject)) return true;
    if (!characters) return false;
    auto state = characters->state(subject);
    return state && state.value().invulnerable;
}

bool dodging(const CombatCharacterRuntime* characters, SubjectRef subject) {
    if (!characters) return false;
    auto state = characters->state(subject);
    return state && state.value().mode == CombatCharacterMode::Dodging && state.value().invulnerable;
}

}  // namespace

Result<CombatLoopFrame> CombatLoopRuntime::advance(const SimulationStep& step) {
    if (!characters_ || !melee_)
        return Result<CombatLoopFrame>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "combat loop requires character and melee runtimes", "loop"));

    CombatLoopFrame frame;
    frame.tick = step.tick;

    if (enemies_) {
        for (const auto& state : characters_->states()) {
            auto posed = enemies_->setPosition(state.subject, state.position.x, state.position.y, state.position.z);
            if (!posed) return Result<CombatLoopFrame>::failure(posed.status());
        }
        auto steering = enemies_->nextSteering(step.tick);
        if (!steering) return Result<CombatLoopFrame>::failure(steering.status());
        frame.enemySteering = steering.value();
        for (const auto& steer : frame.enemySteering) {
            auto moved = characters_->setMoveIntent(steer.subject, steer.moveDirection, steer.speedFraction);
            if (!moved) return Result<CombatLoopFrame>::failure(moved.status());
        }
    }

    if (cancelResolver_ && cancelSubject_.isValid() && !cancelAbility_.format().empty()) {
        CombatCancelResolveRequest request;
        request.subject        = cancelSubject_;
        request.currentAbility = cancelAbility_;
        request.tick           = step.tick;
        auto resolved          = cancelResolver_->resolve(request);
        if (resolved)
            frame.cancels.push_back(std::move(resolved).takeValue());
        else if (resolved.status().code() != StatusCode::NotFound)
            return Result<CombatLoopFrame>::failure(resolved.status());
    }

    if (feel_) {
        for (const auto& state : characters_->states()) {
            auto frozen = characters_->setTimeFrozen(state.subject, feel_->isFrozen(state.subject));
            if (!frozen) return Result<CombatLoopFrame>::failure(frozen.status());
        }
    }

    if (targets_) {
        for (const auto& state : characters_->states()) {
            if (state.mode != CombatCharacterMode::Attacking) continue;
            if (feel_ && feel_->isFrozen(state.subject)) continue;
            auto lock = targets_->state(state.subject);
            if (!lock || !lock.value().target) continue;
            auto target = characters_->state(*lock.value().target);
            if (!target) continue;
            CombatWarpRequest warp;
            warp.attacker        = state.position;
            warp.target          = target.value().position;
            warp.attackerFacing  = state.facing;
            warp.desiredDistance = 1.4;
            warp.maxTranslation  = 0.12;
            warp.remainingBudget = 0.6;
            auto solved          = CombatMotionWarp::solve(warp);
            if (!solved) {
                if (solved.status().code() == StatusCode::Conflict) continue;
                return Result<CombatLoopFrame>::failure(solved.status());
            }
            auto faced = characters_->setFacing(state.subject, solved.value().facing);
            if (!faced) return Result<CombatLoopFrame>::failure(faced.status());
            if (solved.value().translation.x != 0.0 || solved.value().translation.z != 0.0) {
                auto applied = characters_->setRootMotionDelta(state.subject, solved.value().translation);
                if (!applied) return Result<CombatLoopFrame>::failure(applied.status());
            }
        }
    }

    auto locomotion = characters_->advance(step);
    if (!locomotion) return Result<CombatLoopFrame>::failure(locomotion.status());
    frame.locomotion = std::move(locomotion).takeValue();

    for (const auto& state : characters_->states()) {
        auto posed = melee_->setHurtboxPose(state.subject, "torso",
                                            {{state.position.x, state.position.y + 1.0, state.position.z}, 0.0});
        if (!posed && posed.status().code() != StatusCode::NotFound)
            return Result<CombatLoopFrame>::failure(posed.status());
        (void)posed;
    }

    auto melee = melee_->advance(step.tick);
    if (!melee) return Result<CombatLoopFrame>::failure(melee.status());
    frame.melee = std::move(melee).takeValue();

    if (states_) {
        for (const auto& hit : frame.melee.hits) {
            if (dodging(characters_, hit.target)) {
                frame.perfectDodges.push_back({hit.target, hit.source, hit.hitboxId});
                continue;
            }
            if (invulnerable(windows_, characters_, hit.target)) continue;
            DamageRequest request;
            request.source          = hit.source;
            request.target          = hit.target;
            request.actionExecution = hit.actionExecution;
            request.damageType      = hit.damageType;
            request.healthDamage    = hit.healthDamage;
            request.poiseDamage     = hit.poiseDamage;
            request.knockback       = hit.knockback;
            if (guards_) {
                auto guarded = guards_->mitigate(hit.target, request);
                if (!guarded) return Result<CombatLoopFrame>::failure(guarded.status());
                frame.guards.push_back(guarded.value());
                if (guarded.value().result != GuardResult::None) continue;
            }
            const auto found = states_->find(hit.target.format());
            if (found == states_->end())
                return Result<CombatLoopFrame>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "loop damage target missing", hit.target.format()));
            DamageRuntime damage;
            auto          outcome = damage.apply(found->second, request);
            if (!outcome) return Result<CombatLoopFrame>::failure(outcome.status());
            if (feel_) {
                auto felt = feel_->applyFromOutcome(outcome.value());
                if (!felt) return Result<CombatLoopFrame>::failure(felt.status());
            }
            auto reacted = characters_->applyDamageReaction(
                hit.target, outcome.value().reaction,
                feel_ ? feel_->state(hit.target).hitstunRemaining : Duration::zero(), outcome.value().knockback);
            if (!reacted) return Result<CombatLoopFrame>::failure(reacted.status());
            frame.outcomes.push_back(std::move(outcome).takeValue());
        }
    }

    if (feel_) {
        auto felt = feel_->advance(step);
        if (!felt) return Result<CombatLoopFrame>::failure(felt.status());
    }

    if (cameraFocus_.isValid()) {
        auto focus = characters_->state(cameraFocus_);
        if (focus) {
            CombatCameraFramingRequest framing;
            framing.player       = focus.value().position;
            framing.playerFacing = focus.value().facing;
            if (targets_) {
                auto lock = targets_->state(cameraFocus_);
                if (lock && lock.value().target) {
                    auto locked = characters_->state(*lock.value().target);
                    if (locked) framing.lockTarget = locked.value().position;
                }
            }
            auto view = CombatCameraFraming::solve(framing);
            if (!view) return Result<CombatLoopFrame>::failure(view.status());
            frame.camera = view.value();
        }
    }
    return Result<CombatLoopFrame>::success(std::move(frame), Status::success(StatusCode::Applied));
}

}  // namespace eve::combat
