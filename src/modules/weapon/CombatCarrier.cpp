#include "weapon/CombatCarrier.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::weapon {
namespace {

constexpr std::uint32_t kDefaultCapacity = 128;
constexpr std::uint32_t kMaximumCapacity = 1024 * 1024;
constexpr double        kPi              = 3.14159265358979323846;

Result<void> carrierError(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <typename T>
Result<T> carrierValueError(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

bool finite(const ProjectilePoint& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const ProjectileVector& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double length(const ProjectileVector& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

ProjectileVector normalized(const ProjectileVector& value) {
    const double magnitude = length(value);
    return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

ProjectileVector directionTo(const ProjectilePoint& from, const ProjectilePoint& to) {
    return {to.x - from.x, to.y - from.y, to.z - from.z};
}

ProjectileVector steer(ProjectileVector velocity, ProjectileVector desired, double maxRadians) {
    const double speed = length(velocity);
    if (speed <= 0.0) return velocity;
    ProjectileVector current = normalized(velocity);
    desired                  = normalized(desired);
    const double dot   = std::clamp(current.x * desired.x + current.y * desired.y + current.z * desired.z, -1.0, 1.0);
    const double angle = std::acos(dot);
    if (angle <= maxRadians || angle <= 1e-12) return {desired.x * speed, desired.y * speed, desired.z * speed};
    const double     alpha = maxRadians / angle;
    ProjectileVector blended{current.x + (desired.x - current.x) * alpha, current.y + (desired.y - current.y) * alpha,
                             current.z + (desired.z - current.z) * alpha};
    blended = normalized(blended);
    return {blended.x * speed, blended.y * speed, blended.z * speed};
}

ProjectileVector reflect(ProjectileVector velocity, ProjectileVector normal) {
    const double nLen = length(normal);
    if (nLen <= 1e-12) return velocity;
    normal               = {normal.x / nLen, normal.y / nLen, normal.z / nLen};
    const double into    = velocity.x * normal.x + velocity.y * normal.y + velocity.z * normal.z;
    return {velocity.x - 2.0 * into * normal.x, velocity.y - 2.0 * into * normal.y,
            velocity.z - 2.0 * into * normal.z};
}

bool crossed(Duration previousAge, Duration nextAge, Duration mark) {
    return previousAge < mark && nextAge >= mark;
}

int intervalIndex(Duration age, Duration interval) {
    if (interval <= Duration::zero()) return 0;
    return static_cast<int>(age.nanoseconds() / interval.nanoseconds());
}

}  // namespace

Result<void> CarrierRecipe::validate() const {
    if (!id.isValid()) return carrierError(DiagnosticCode::InvalidArgument, "Carrier recipe id must not be empty", "id");
    if (lifetime <= Duration::zero())
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier lifetime must be positive", "lifetime");
    if (!std::isfinite(speed) || speed <= 0.0)
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier speed must be finite and positive", "speed");
    if (motionOps.empty())
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier recipe requires at least one motion op",
                            "motionOps");

    bool hasIntegrate = false;
    for (std::size_t i = 0; i < motionOps.size(); ++i) {
        const auto& op   = motionOps[i];
        const auto  path = "motionOps[" + std::to_string(i) + "]";
        switch (op.kind) {
            case CarrierMotionOpKind::SteerHoming:
                if (!std::isfinite(op.maxTurnRateDegrees) || op.maxTurnRateDegrees <= 0.0)
                    return carrierError(DiagnosticCode::InvalidArgument,
                                        "Homing turn rate must be finite and positive", path + ".maxTurnRateDegrees");
                break;
            case CarrierMotionOpKind::ApplyGravity:
                if (!std::isfinite(op.gravity) || op.gravity < 0.0)
                    return carrierError(DiagnosticCode::InvalidArgument,
                                        "Gravity must be finite and non-negative", path + ".gravity");
                break;
            case CarrierMotionOpKind::Accelerate:
                if (!std::isfinite(op.acceleration) || op.acceleration < 0.0)
                    return carrierError(DiagnosticCode::InvalidArgument,
                                        "Acceleration must be finite and non-negative", path + ".acceleration");
                break;
            case CarrierMotionOpKind::IntegrateLinear:
                hasIntegrate = true;
                break;
        }
    }
    if (!hasIntegrate)
        return carrierError(DiagnosticCode::InvalidArgument,
                            "Carrier recipe must include IntegrateLinear so position advances", "motionOps");

    if (triggers.empty())
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier recipe requires at least one trigger",
                            "triggers");

    for (std::size_t i = 0; i < triggers.size(); ++i) {
        const auto& trigger = triggers[i];
        const auto  path    = "triggers[" + std::to_string(i) + "]";
        switch (trigger.kind) {
            case CarrierTriggerKind::OnFuse:
                if (trigger.fuse <= Duration::zero())
                    return carrierError(DiagnosticCode::InvalidArgument, "Fuse trigger requires positive fuse",
                                        path + ".fuse");
                break;
            case CarrierTriggerKind::OnInterval:
                if (trigger.interval <= Duration::zero())
                    return carrierError(DiagnosticCode::InvalidArgument, "Interval trigger requires positive interval",
                                        path + ".interval");
                break;
            case CarrierTriggerKind::OnProximity:
                if (!std::isfinite(trigger.proximityRadius) || trigger.proximityRadius <= 0.0)
                    return carrierError(DiagnosticCode::InvalidArgument,
                                        "Proximity trigger requires positive radius", path + ".proximityRadius");
                break;
            case CarrierTriggerKind::OnExpire:
            case CarrierTriggerKind::OnHit:
                break;
        }
    }

    for (std::size_t i = 0; i < impacts.size(); ++i) {
        const auto& impact = impacts[i];
        const auto  path   = "impacts[" + std::to_string(i) + "]";
        if (!std::isfinite(impact.damage) || impact.damage < 0.0)
            return carrierError(DiagnosticCode::InvalidArgument, "Impact damage must be finite and non-negative",
                                path + ".damage");
        if (!std::isfinite(impact.splashRadius) || impact.splashRadius < 0.0)
            return carrierError(DiagnosticCode::InvalidArgument, "Splash radius must be finite and non-negative",
                                path + ".splashRadius");
        if (!std::isfinite(impact.restitution) || impact.restitution < 0.0)
            return carrierError(DiagnosticCode::InvalidArgument, "Bounce restitution must be finite and non-negative",
                                path + ".restitution");
        if (impact.pierceCount < 0 || impact.bounceCount < 0)
            return carrierError(DiagnosticCode::InvalidArgument, "Pierce/bounce budgets must be non-negative", path);
        if (impact.kind == CarrierImpactKind::SpawnChild && !impact.childRecipeId.isValid())
            return carrierError(DiagnosticCode::InvalidArgument, "SpawnChild requires childRecipeId",
                                path + ".childRecipeId");
        if (impact.kind == CarrierImpactKind::Splash && impact.splashRadius <= 0.0)
            return carrierError(DiagnosticCode::InvalidArgument, "Splash impact requires positive splashRadius",
                                path + ".splashRadius");
    }
    return Result<void>::success();
}

Result<CarrierRecipe> carrierRecipeFromProjectile(const ProjectileDefinition& definition, double damage) {
    auto valid = definition.validate();
    if (!valid) return Result<CarrierRecipe>::failure(valid.status());
    if (!std::isfinite(damage) || damage < 0.0)
        return carrierValueError<CarrierRecipe>(DiagnosticCode::InvalidArgument,
                                                "Bridge damage must be finite and non-negative", "damage");

    CarrierRecipe recipe;
    recipe.id       = definition.id;
    recipe.lifetime = definition.lifetime;
    recipe.speed    = definition.speed;

    switch (definition.mode) {
        case ProjectileMode::Linear:
            recipe.motionOps.push_back({CarrierMotionOpKind::IntegrateLinear});
            break;
        case ProjectileMode::Ballistic:
            recipe.motionOps.push_back({CarrierMotionOpKind::ApplyGravity, definition.gravity});
            recipe.motionOps.push_back({CarrierMotionOpKind::IntegrateLinear});
            break;
        case ProjectileMode::Homing:
            recipe.motionOps.push_back(
                {CarrierMotionOpKind::SteerHoming, 0.0, definition.maxTurnRateDegrees});
            recipe.motionOps.push_back({CarrierMotionOpKind::IntegrateLinear});
            break;
    }

    recipe.triggers.push_back({CarrierTriggerKind::OnExpire});
    recipe.triggers.push_back({CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnHit, damage});
    recipe.impacts.push_back({CarrierImpactKind::Release, CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({CarrierImpactKind::Release, CarrierTriggerKind::OnExpire});

    auto recipeValid = recipe.validate();
    if (!recipeValid) return Result<CarrierRecipe>::failure(recipeValid.status());
    return Result<CarrierRecipe>::success(std::move(recipe));
}

CombatCarrierRuntime::CombatCarrierRuntime() : slots_(kDefaultCapacity) {}

Result<void> CombatCarrierRuntime::configurePool(std::uint32_t capacity) {
    if (activeCount_ != 0)
        return carrierError(DiagnosticCode::Conflict, "Carrier pool cannot be resized while active");
    if (capacity == 0 || capacity > kMaximumCapacity)
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier pool capacity must be in 1..1048576",
                            "capacity");
    slots_.assign(capacity, Slot{});
    return Result<void>::success();
}

Result<CombatCarrierRuntime::LiveRecipe> CombatCarrierRuntime::prepareRecipe(const CarrierRecipe& recipe) {
    auto valid = recipe.validate();
    if (!valid) return Result<LiveRecipe>::failure(valid.status());

    LiveRecipe live;
    live.recipe = recipe;
    for (const auto& op : recipe.motionOps) {
        if (op.kind == CarrierMotionOpKind::SteerHoming) live.needsHoming = true;
    }
    for (const auto& trigger : recipe.triggers) {
        if (trigger.kind == CarrierTriggerKind::OnHit) live.needsHit = true;
        if (trigger.kind == CarrierTriggerKind::OnProximity) live.needsProximity = true;
    }
    for (const auto& impact : recipe.impacts) {
        if (impact.kind == CarrierImpactKind::Pierce)
            live.initialPierce = std::max(live.initialPierce, impact.pierceCount);
        if (impact.kind == CarrierImpactKind::Bounce)
            live.initialBounce = std::max(live.initialBounce, impact.bounceCount);
    }
    return Result<LiveRecipe>::success(std::move(live));
}

Result<void> CombatCarrierRuntime::registerRecipe(const CarrierRecipe& recipe) {
    auto prepared = prepareRecipe(recipe);
    if (!prepared) return Result<void>::failure(prepared.status());
    LiveRecipe live = std::move(prepared).takeValue();
    for (auto& existing : recipes_) {
        if (existing.recipe.id == recipe.id) {
            existing = std::move(live);
            return Result<void>::success();
        }
    }
    recipes_.push_back(std::move(live));
    return Result<void>::success();
}

std::optional<CarrierRecipe> CombatCarrierRuntime::findRecipe(const LogicalId& id) const {
    for (const auto& live : recipes_) {
        if (live.recipe.id == id) return live.recipe;
    }
    return std::nullopt;
}

Result<void> CombatCarrierRuntime::validateSpawn(const CarrierRecipe&       recipe,
                                                 const CarrierSpawnRequest& request) {
    auto valid = recipe.validate();
    if (!valid) return valid;
    if (!finite(request.position) || !finite(request.direction))
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier transform must contain finite values",
                            "request");
    if (length(request.direction) <= 0.0)
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier direction must be non-zero", "direction");

    bool needsTarget = false;
    for (const auto& op : recipe.motionOps) {
        if (op.kind == CarrierMotionOpKind::SteerHoming) needsTarget = true;
    }
    for (const auto& trigger : recipe.triggers) {
        if (trigger.kind == CarrierTriggerKind::OnProximity) needsTarget = true;
    }
    if (needsTarget && !request.target.has_value())
        return carrierError(DiagnosticCode::InvalidArgument, "Homing/proximity carrier requires a target handle",
                            "target");
    return Result<void>::success();
}

Result<CarrierHandle> CombatCarrierRuntime::spawnPrepared(const LiveRecipe&          live,
                                                          const CarrierSpawnRequest& request) {
    auto valid = validateSpawn(live.recipe, request);
    if (!valid) return Result<CarrierHandle>::failure(valid.status());

    for (std::uint32_t slot = 0; slot < slots_.size(); ++slot) {
        if (slots_[slot].state.has_value()) continue;
        Slot& cell = slots_[slot];
        ++cell.generation;
        if (cell.generation == 0) ++cell.generation;

        CarrierState state;
        state.handle          = {slot, cell.generation};
        state.recipeId        = live.recipe.id;
        state.lifetime        = live.recipe.lifetime;
        state.age             = Duration::zero();
        state.pierceRemaining = live.initialPierce;
        state.bounceRemaining = live.initialBounce;
        state.target          = request.target;
        state.source          = request.source;
        const auto dir        = normalized(request.direction);
        state.motion.position = request.position;
        state.motion.velocity = {dir.x * live.recipe.speed, dir.y * live.recipe.speed, dir.z * live.recipe.speed};
        cell.state             = std::move(state);
        ++activeCount_;
        return Result<CarrierHandle>::success(cell.state->handle);
    }
    return carrierValueError<CarrierHandle>(DiagnosticCode::Conflict, "Carrier pool is full", "capacity");
}

Result<CarrierHandle> CombatCarrierRuntime::spawn(const LogicalId& recipeId, const CarrierSpawnRequest& request) {
    for (const auto& live : recipes_) {
        if (live.recipe.id == recipeId) return spawnPrepared(live, request);
    }
    return carrierValueError<CarrierHandle>(DiagnosticCode::NotFound, "Carrier recipe is not registered", "recipeId");
}

Result<CarrierHandle> CombatCarrierRuntime::spawn(const CarrierRecipe& recipe, const CarrierSpawnRequest& request) {
    auto registered = registerRecipe(recipe);
    if (!registered) return Result<CarrierHandle>::failure(registered.status());
    return spawn(recipe.id, request);
}

Result<CarrierFrame> CombatCarrierRuntime::update(Duration                        delta,
                                                  const IProjectileTargetProvider* targets,
                                                  const ICarrierHitProbe*         hits) {
    if (delta < Duration::zero())
        return carrierValueError<CarrierFrame>(DiagnosticCode::InvalidArgument, "Carrier update delta must be >= 0",
                                               "delta");
    if (delta.isZero()) return Result<CarrierFrame>::success({}, Status::success(StatusCode::NoOp));

    struct Candidate {
        CarrierState state;
        bool         release = false;
    };
    struct TriggerFire {
        CarrierTriggerKind                       kind = CarrierTriggerKind::OnExpire;
        std::optional<ICarrierHitProbe::Contact> contact;
    };

    std::vector<Candidate>         candidates;
    std::vector<CarrierEvent>      events;
    std::vector<CarrierChildSpawn> childSpawns;
    candidates.reserve(activeCount_);

    auto lookupLive = [&](const LogicalId& id) -> const LiveRecipe* {
        for (const auto& live : recipes_) {
            if (live.recipe.id == id) return &live;
        }
        return nullptr;
    };

    for (const auto& slot : slots_) {
        if (!slot.state.has_value()) continue;
        const LiveRecipe* live = lookupLive(slot.state->recipeId);
        if (live == nullptr)
            return carrierValueError<CarrierFrame>(DiagnosticCode::NotFound,
                                                   "Live carrier references an unregistered recipe", "recipeId");
        if ((live->needsHoming || live->needsProximity) && targets == nullptr)
            return carrierValueError<CarrierFrame>(DiagnosticCode::Unsupported,
                                                   "Homing/proximity carrier update requires a target provider",
                                                   "targets");
        if (live->needsHit && hits == nullptr)
            return carrierValueError<CarrierFrame>(DiagnosticCode::Unsupported,
                                                   "OnHit carrier update requires a hit probe", "hits");
        candidates.push_back({*slot.state, false});
    }

    for (auto& candidate : candidates) {
        const LiveRecipe* live = lookupLive(candidate.state.recipeId);
        if (live == nullptr)
            return carrierValueError<CarrierFrame>(DiagnosticCode::NotFound,
                                                   "Live carrier references an unregistered recipe", "recipeId");

        const Duration previousAge = candidate.state.age;
        CarrierMotion  previous    = candidate.state.motion;
        CarrierMotion  motion      = previous;

        std::optional<ProjectilePoint> targetPos;
        if (live->needsHoming || live->needsProximity) {
            if (!candidate.state.target.has_value())
                return carrierValueError<CarrierFrame>(DiagnosticCode::InvalidArgument,
                                                       "Homing/proximity carrier lost its target handle", "target");
            auto resolved = targets->position(*candidate.state.target);
            if (!resolved) return Result<CarrierFrame>::failure(resolved.status());
            targetPos = resolved.value();
        }

        const double dtSeconds = delta.seconds();
        for (const auto& op : live->recipe.motionOps) {
            switch (op.kind) {
                case CarrierMotionOpKind::SteerHoming: {
                    const auto desired = directionTo(motion.position, *targetPos);
                    if (length(desired) <= 1e-12) break;
                    const double maxRadians = op.maxTurnRateDegrees * kPi / 180.0 * dtSeconds;
                    motion.velocity         = steer(motion.velocity, desired, maxRadians);
                    break;
                }
                case CarrierMotionOpKind::ApplyGravity:
                    motion.velocity.y -= op.gravity * dtSeconds;
                    break;
                case CarrierMotionOpKind::Accelerate: {
                    const double speed = length(motion.velocity);
                    if (speed <= 1e-12) break;
                    const auto   dir  = normalized(motion.velocity);
                    const double next = speed + op.acceleration * dtSeconds;
                    motion.velocity   = {dir.x * next, dir.y * next, dir.z * next};
                    break;
                }
                case CarrierMotionOpKind::IntegrateLinear:
                    motion.position.x += motion.velocity.x * dtSeconds;
                    motion.position.y += motion.velocity.y * dtSeconds;
                    motion.position.z += motion.velocity.z * dtSeconds;
                    break;
            }
        }

        if (!finite(motion.position) || !finite(motion.velocity))
            return carrierValueError<CarrierFrame>(DiagnosticCode::Failed,
                                                   "Carrier motion produced non-finite values", "motion");

        auto nextAge = previousAge.tryAdd(delta);
        if (!nextAge) return Result<CarrierFrame>::failure(nextAge.status());

        candidate.state.motion = motion;
        candidate.state.age    = std::move(nextAge).takeValue();

        std::vector<TriggerFire> fires;
        for (const auto& trigger : live->recipe.triggers) {
            switch (trigger.kind) {
                case CarrierTriggerKind::OnExpire:
                    if (crossed(previousAge, candidate.state.age, candidate.state.lifetime) ||
                        candidate.state.age >= candidate.state.lifetime)
                        fires.push_back({CarrierTriggerKind::OnExpire, std::nullopt});
                    break;
                case CarrierTriggerKind::OnFuse:
                    if (crossed(previousAge, candidate.state.age, trigger.fuse))
                        fires.push_back({CarrierTriggerKind::OnFuse, std::nullopt});
                    break;
                case CarrierTriggerKind::OnInterval: {
                    const int before = intervalIndex(previousAge, trigger.interval);
                    const int after  = intervalIndex(candidate.state.age, trigger.interval);
                    for (int i = before + 1; i <= after; ++i)
                        fires.push_back({CarrierTriggerKind::OnInterval, std::nullopt});
                    break;
                }
                case CarrierTriggerKind::OnHit: {
                    auto contacts = hits->query(candidate.state.handle, previous, motion);
                    if (!contacts) return Result<CarrierFrame>::failure(contacts.status());
                    for (auto& contact : contacts.value())
                        fires.push_back({CarrierTriggerKind::OnHit, std::move(contact)});
                    break;
                }
                case CarrierTriggerKind::OnProximity: {
                    const auto   deltaPos = directionTo(motion.position, *targetPos);
                    const double dist     = length(deltaPos);
                    if (dist <= trigger.proximityRadius)
                        fires.push_back({CarrierTriggerKind::OnProximity, std::nullopt});
                    break;
                }
            }
        }

        for (const auto& fire : fires) {
            bool suppressRelease = false;
            bool requestRelease  = false;

            for (const auto& impact : live->recipe.impacts) {
                if (impact.on != fire.kind) continue;

                CarrierEvent event;
                event.carrier    = candidate.state.handle;
                event.recipeId   = candidate.state.recipeId;
                event.trigger    = fire.kind;
                event.impact     = impact.kind;
                event.position   = candidate.state.motion.position;
                event.source     = candidate.state.source;
                if (fire.contact.has_value()) {
                    event.target   = fire.contact->target;
                    event.normal   = fire.contact->normal;
                    event.position = fire.contact->point;
                } else if (candidate.state.target.has_value()) {
                    event.target = *candidate.state.target;
                }

                switch (impact.kind) {
                    case CarrierImpactKind::EmitHit:
                        event.damage     = impact.damage;
                        event.damageType = impact.damageType;
                        event.element    = impact.element;
                        events.push_back(std::move(event));
                        break;
                    case CarrierImpactKind::Splash:
                        event.damage       = impact.damage;
                        event.splashRadius = impact.splashRadius;
                        event.damageType   = impact.damageType;
                        event.element      = impact.element;
                        event.position     = candidate.state.motion.position;
                        events.push_back(std::move(event));
                        break;
                    case CarrierImpactKind::Pierce:
                        if (candidate.state.pierceRemaining > 0) {
                            --candidate.state.pierceRemaining;
                            suppressRelease = true;
                        }
                        break;
                    case CarrierImpactKind::Bounce:
                        if (candidate.state.bounceRemaining > 0 && fire.contact.has_value()) {
                            --candidate.state.bounceRemaining;
                            auto reflected = reflect(candidate.state.motion.velocity, fire.contact->normal);
                            reflected.x *= impact.restitution;
                            reflected.y *= impact.restitution;
                            reflected.z *= impact.restitution;
                            candidate.state.motion.velocity = reflected;
                            suppressRelease               = true;
                        }
                        break;
                    case CarrierImpactKind::Release:
                        requestRelease = true;
                        break;
                    case CarrierImpactKind::SpawnChild: {
                        CarrierChildSpawn child;
                        child.recipeId          = impact.childRecipeId;
                        child.parent            = candidate.state.handle;
                        child.request.position  = candidate.state.motion.position;
                        child.request.direction = candidate.state.motion.velocity;
                        if (length(child.request.direction) <= 1e-12) child.request.direction = {1.0, 0.0, 0.0};
                        child.request.target = candidate.state.target;
                        child.request.source = candidate.state.source;
                        childSpawns.push_back(std::move(child));
                        break;
                    }
                }
            }

            if (requestRelease && !suppressRelease) candidate.release = true;
        }

        if (candidate.state.age >= candidate.state.lifetime) {
            bool hasExpireRelease = false;
            for (const auto& impact : live->recipe.impacts) {
                if (impact.kind == CarrierImpactKind::Release && impact.on == CarrierTriggerKind::OnExpire)
                    hasExpireRelease = true;
            }
            if (!hasExpireRelease &&
                std::any_of(fires.begin(), fires.end(),
                            [](const TriggerFire& f) { return f.kind == CarrierTriggerKind::OnExpire; }))
                candidate.release = true;
        }
    }

    std::vector<Slot> staged       = slots_;
    std::size_t       stagedActive = activeCount_;
    for (const auto& candidate : candidates) {
        auto& cell = staged[candidate.state.handle.slot];
        if (cell.generation != candidate.state.handle.generation || !cell.state.has_value())
            return carrierValueError<CarrierFrame>(DiagnosticCode::StaleHandle, "Carrier handle became stale",
                                                   "handle");
        if (candidate.release) {
            cell.state.reset();
            --stagedActive;
        } else {
            cell.state = candidate.state;
        }
    }

    std::vector<CarrierHandle> spawnedChildren;
    spawnedChildren.reserve(childSpawns.size());
    for (const auto& child : childSpawns) {
        const LiveRecipe* childLive = lookupLive(child.recipeId);
        if (childLive == nullptr)
            return carrierValueError<CarrierFrame>(DiagnosticCode::NotFound,
                                                   "Child spawn references an unregistered recipe", "childRecipeId");
        auto valid = validateSpawn(childLive->recipe, child.request);
        if (!valid) return Result<CarrierFrame>::failure(valid.status());

        bool placed = false;
        for (std::uint32_t slot = 0; slot < staged.size(); ++slot) {
            if (staged[slot].state.has_value()) continue;
            Slot& cell = staged[slot];
            ++cell.generation;
            if (cell.generation == 0) ++cell.generation;
            CarrierState state;
            state.handle          = {slot, cell.generation};
            state.recipeId        = childLive->recipe.id;
            state.lifetime        = childLive->recipe.lifetime;
            state.age             = Duration::zero();
            state.pierceRemaining = childLive->initialPierce;
            state.bounceRemaining = childLive->initialBounce;
            state.target          = child.request.target;
            state.source          = child.request.source;
            const auto dir        = normalized(child.request.direction);
            state.motion.position = child.request.position;
            state.motion.velocity = {dir.x * childLive->recipe.speed, dir.y * childLive->recipe.speed,
                                     dir.z * childLive->recipe.speed};
            cell.state             = std::move(state);
            ++stagedActive;
            spawnedChildren.push_back(cell.state->handle);
            placed = true;
            break;
        }
        if (!placed)
            return carrierValueError<CarrierFrame>(DiagnosticCode::Conflict,
                                                   "Carrier pool cannot fit deferred child spawns", "capacity");
    }

    slots_       = std::move(staged);
    activeCount_ = stagedActive;

    CarrierFrame frame;
    for (const auto& candidate : candidates) {
        frame.advanced.push_back(candidate.state.handle);
        if (candidate.release) frame.released.push_back(candidate.state.handle);
    }
    for (const auto& child : spawnedChildren) frame.advanced.push_back(child);
    frame.events      = std::move(events);
    frame.childSpawns = std::move(childSpawns);
    return Result<CarrierFrame>::success(std::move(frame));
}

Result<void> CombatCarrierRuntime::release(CarrierHandle handle) {
    if (handle.slot >= slots_.size())
        return carrierError(DiagnosticCode::StaleHandle, "Carrier handle slot is out of range", "handle");
    Slot& cell = slots_[handle.slot];
    if (!cell.state.has_value() || cell.generation != handle.generation)
        return carrierError(DiagnosticCode::StaleHandle, "Carrier handle is stale", "handle");
    cell.state.reset();
    --activeCount_;
    return Result<void>::success();
}

std::optional<CarrierState> CombatCarrierRuntime::find(CarrierHandle handle) const {
    if (handle.slot >= slots_.size()) return std::nullopt;
    const Slot& cell = slots_[handle.slot];
    if (!cell.state.has_value() || cell.generation != handle.generation) return std::nullopt;
    return cell.state;
}

std::vector<CarrierState> CombatCarrierRuntime::states() const {
    std::vector<CarrierState> result;
    result.reserve(activeCount_);
    for (const auto& slot : slots_) {
        if (slot.state.has_value()) result.push_back(*slot.state);
    }
    return result;
}

CarrierRuntimeSnapshot CombatCarrierRuntime::snapshot() const {
    CarrierRuntimeSnapshot snap;
    snap.slots.reserve(slots_.size());
    for (const auto& slot : slots_) snap.slots.push_back({slot.generation, slot.state});
    return snap;
}

Result<void> CombatCarrierRuntime::restore(const CarrierRuntimeSnapshot& snapshot) {
    if (snapshot.slots.empty() || snapshot.slots.size() > kMaximumCapacity)
        return carrierError(DiagnosticCode::InvalidArgument, "Carrier snapshot capacity must be in 1..1048576",
                            "slots");

    std::vector<Slot> staged;
    staged.reserve(snapshot.slots.size());
    std::size_t active = 0;
    for (const auto& slot : snapshot.slots) {
        if (slot.state.has_value()) {
            if (slot.state->handle.slot >= snapshot.slots.size() || slot.state->handle.generation != slot.generation)
                return carrierError(DiagnosticCode::InvalidArgument,
                                    "Carrier snapshot state handle does not match slot metadata", "slots");
            if (!findRecipe(slot.state->recipeId).has_value())
                return carrierError(DiagnosticCode::NotFound,
                                    "Carrier snapshot references an unregistered recipe", "recipeId");
            ++active;
        }
        staged.push_back({slot.generation, slot.state});
    }
    slots_       = std::move(staged);
    activeCount_ = active;
    return Result<void>::success();
}

}  // namespace eve::weapon
