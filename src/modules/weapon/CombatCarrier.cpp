// eve-hot-path
#include "weapon/CombatCarrier.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <utility>

namespace eve::weapon {
namespace {

constexpr std::uint32_t kDefaultCapacity = 128;
constexpr std::uint32_t kMaximumCapacity = 1024 * 1024;
constexpr double        kPi              = 3.14159265358979323846;

bool sameTarget(ecs::EntityHandle a, ecs::EntityHandle b) {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}

bool finite(const ProjectilePoint& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const ProjectileVector& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double length(const ProjectileVector& value) { return std::hypot(value.x, value.y, value.z); }

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
    // Rotate through the exact angular budget, including antiparallel directions.
    ProjectileVector tangent{desired.x - dot * current.x, desired.y - dot * current.y, desired.z - dot * current.z};
    if (length(tangent) <= 1e-12) {
        const ProjectileVector basis =
            std::fabs(current.y) < 0.9 ? ProjectileVector{0.0, 1.0, 0.0} : ProjectileVector{1.0, 0.0, 0.0};
        const double projection = basis.x * current.x + basis.y * current.y + basis.z * current.z;
        tangent                 = {basis.x - projection * current.x, basis.y - projection * current.y,
                                   basis.z - projection * current.z};
    }
    tangent = normalized(tangent);
    return {(current.x * std::cos(maxRadians) + tangent.x * std::sin(maxRadians)) * speed,
            (current.y * std::cos(maxRadians) + tangent.y * std::sin(maxRadians)) * speed,
            (current.z * std::cos(maxRadians) + tangent.z * std::sin(maxRadians)) * speed};
}

ProjectileVector reflect(ProjectileVector velocity, ProjectileVector normal) {
    const double nLen = length(normal);
    if (nLen <= 1e-12) return velocity;
    normal               = {normal.x / nLen, normal.y / nLen, normal.z / nLen};
    const double into    = velocity.x * normal.x + velocity.y * normal.y + velocity.z * normal.z;
    return {velocity.x - 2.0 * into * normal.x, velocity.y - 2.0 * into * normal.y,
            velocity.z - 2.0 * into * normal.z};
}

ProjectileVector cross(const ProjectileVector& a, const ProjectileVector& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

ProjectileVector rotateAroundAxis(ProjectileVector vector, ProjectileVector axis, double radians) {
    axis                    = normalized(axis);
    const double cosA       = std::cos(radians);
    const double sinA       = std::sin(radians);
    const auto   axisCrossV = cross(axis, vector);
    const double axisDotV   = axis.x * vector.x + axis.y * vector.y + axis.z * vector.z;
    return {vector.x * cosA + axisCrossV.x * sinA + axis.x * axisDotV * (1.0 - cosA),
            vector.y * cosA + axisCrossV.y * sinA + axis.y * axisDotV * (1.0 - cosA),
            vector.z * cosA + axisCrossV.z * sinA + axis.z * axisDotV * (1.0 - cosA)};
}

ProjectileVector lateralBasis(const ProjectileVector& forward) {
    ProjectileVector up{0.0, 1.0, 0.0};
    auto             side = cross(forward, up);
    if (length(side) <= 1e-12) {
        up   = {1.0, 0.0, 0.0};
        side = cross(forward, up);
    }
    return normalized(side);
}

double randomSample(std::uint32_t seed, CarrierHandle handle, std::uint32_t tick, std::uint32_t channel) {
    std::uint32_t value = seed ^ (handle.slot * 0x9e3779b9U) ^ handle.generation ^ (tick * 0x85ebca6bU) ^ channel;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return static_cast<double>(value) / 4294967295.0;
}

bool crossed(Duration previousAge, Duration nextAge, Duration mark) {
    return previousAge < mark && nextAge >= mark;
}

std::int64_t intervalIndex(Duration age, Duration interval) {
    if (interval <= Duration::zero()) return 0;
    return age.nanoseconds() / interval.nanoseconds();
}

Result<void> validateVolley(const CarrierVolleySpec& volley) {
    if (volley.count <= 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Volley count must be positive", "count"));
    if (volley.count > 256)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Volley count must be <= 256", "count"));
    if (volley.pattern == CarrierVolleyPattern::Fan) {
        if (!std::isfinite(volley.spreadDegrees) || volley.spreadDegrees < 0.0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Fan volley spreadDegrees must be finite and non-negative", "spreadDegrees"));
    }
    return Result<void>::success();
}

Result<std::vector<ProjectileVector>> volleyDirections(const ProjectileVector& base,
                                                       const CarrierVolleySpec& volley) {
    auto valid = validateVolley(volley);
    if (!valid) return Result<std::vector<ProjectileVector>>::failure(valid.status());
    if (length(base) <= 1e-12)
        return Result<std::vector<ProjectileVector>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Volley base direction must be non-zero", "direction"));

    const auto forward = normalized(base);
    ProjectileVector yawAxis{0.0, 1.0, 0.0};
    if (std::fabs(forward.x * yawAxis.x + forward.y * yawAxis.y + forward.z * yawAxis.z) > 0.999)
        yawAxis = lateralBasis(forward);

    std::vector<ProjectileVector> directions;
    directions.reserve(static_cast<std::size_t>(volley.count));
    if (volley.count == 1) {
        directions.push_back(forward);
        return Result<std::vector<ProjectileVector>>::success(std::move(directions));
    }
    if (volley.pattern == CarrierVolleyPattern::Ring) {
        for (int i = 0; i < volley.count; ++i) {
            const double radians = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(volley.count);
            directions.push_back(normalized(rotateAroundAxis(forward, yawAxis, radians)));
        }
        return Result<std::vector<ProjectileVector>>::success(std::move(directions));
    }
    const double total = volley.spreadDegrees * kPi / 180.0;
    for (int i = 0; i < volley.count; ++i) {
        const double t       = static_cast<double>(i) / static_cast<double>(volley.count - 1);
        const double radians = -0.5 * total + t * total;
        directions.push_back(normalized(rotateAroundAxis(forward, yawAxis, radians)));
    }
    return Result<std::vector<ProjectileVector>>::success(std::move(directions));
}

}  // namespace

Result<void> CarrierRecipe::validate() const {
    if (!id.isValid()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier recipe id must not be empty", "id"));
    if (lifetime <= Duration::zero())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier lifetime must be positive", "lifetime"));
    if (!std::isfinite(speed) || speed <= 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier speed must be finite and positive", "speed"));
    if (motionOps.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier recipe requires at least one motion op", "motionOps"));

    if (motionOps.size() > 64 || triggers.size() > 64 || impacts.size() > 256)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Carrier recipe exceeds operator/trigger/impact budgets (64/64/256)", "recipe"));
    bool hasIntegrate = false;
    bool curveTail    = false;
    for (std::size_t i = 0; i < motionOps.size(); ++i) {
        const auto& op   = motionOps[i];
        const auto  path = "motionOps[" + std::to_string(i) + "]";
        const bool  curve = op.kind == CarrierMotionOpKind::CurveSway || op.kind == CarrierMotionOpKind::CurveHelix;
        if (curveTail && !curve && op.kind != CarrierMotionOpKind::IntegrateLinear)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "Curve modifiers must be the final operators before integration", path));
        curveTail = curveTail || curve;
        switch (op.kind) {
            case CarrierMotionOpKind::SteerPredictive:
                if (!std::isfinite(op.predictionSeconds) || op.predictionSeconds <= 0.0)
                    return Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "Prediction horizon must be finite and positive", path));
                [[fallthrough]];
            case CarrierMotionOpKind::SteerHoming:
                if (!std::isfinite(op.maxTurnRateDegrees) || op.maxTurnRateDegrees <= 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Homing turn rate must be finite and positive", path + ".maxTurnRateDegrees"));
                break;
            case CarrierMotionOpKind::SteerAvoidBody:
                if (!std::isfinite(op.avoidLookAhead) || op.avoidLookAhead <= 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Body-avoid look-ahead must be finite and positive", path + ".avoidLookAhead"));
                if (!std::isfinite(op.avoidStrength) || op.avoidStrength < 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Body-avoid strength must be finite and non-negative", path + ".avoidStrength"));
                if (!std::isfinite(op.avoidRadiusPadding) || op.avoidRadiusPadding < 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Body-avoid radius padding must be finite and non-negative", path + ".avoidRadiusPadding"));
                if (!std::isfinite(op.maxTurnRateDegrees) || op.maxTurnRateDegrees <= 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Body-avoid turn rate must be finite and positive", path + ".maxTurnRateDegrees"));
                break;
            case CarrierMotionOpKind::ApplyGravity:
                if (!std::isfinite(op.gravity) || op.gravity < 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Gravity must be finite and non-negative", path + ".gravity"));
                break;
            case CarrierMotionOpKind::Accelerate:
                if (!std::isfinite(op.acceleration))
                    return Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "Acceleration must be finite", path + ".acceleration"));
                break;
            case CarrierMotionOpKind::RotateVelocity:
            case CarrierMotionOpKind::SteerOrbit:
                if (!finite(op.axis) || length(op.axis) <= 1e-12 || !std::isfinite(length(op.axis)) ||
                    !std::isfinite(op.angularRateDegrees) ||
                    (op.kind == CarrierMotionOpKind::SteerOrbit &&
                     (!std::isfinite(op.orbitRadius) || op.orbitRadius <= 0.0 ||
                      !std::isfinite(op.maxTurnRateDegrees) || op.maxTurnRateDegrees <= 0.0)))
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument,
                                          "Invalid rotation axis, angular rate, orbit radius or turn rate", path));
                break;
            case CarrierMotionOpKind::ApplyDrag:
                if (!std::isfinite(op.drag) || op.drag < 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                   "Drag must be finite and non-negative", path));
                break;
            case CarrierMotionOpKind::SteerWander:
                if (op.randomSeed < 0 || !std::isfinite(op.maxTurnRateDegrees) || op.maxTurnRateDegrees <= 0.0)
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument,
                                          "Wander requires nonnegative seed and positive turn rate", path));
                [[fallthrough]];
            case CarrierMotionOpKind::SpeedOscillate:
            case CarrierMotionOpKind::CurveSway:
            case CarrierMotionOpKind::CurveHelix:
                if (!std::isfinite(op.curveAmplitude) || op.curveAmplitude < 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Curve amplitude must be finite and non-negative", path + ".curveAmplitude"));
                if (!std::isfinite(op.curveFrequencyHz) || op.curveFrequencyHz <= 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Curve frequency must be finite and positive", path + ".curveFrequencyHz"));
                break;
            case CarrierMotionOpKind::IntegrateLinear:
                if (hasIntegrate || i + 1 != motionOps.size())
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument,
                                          "Exactly one integrator must be last in the motion stack", path));
                hasIntegrate = true;
                break;
            default:
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown carrier motion operator", path));
        }
    }
    if (!hasIntegrate)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier recipe must include IntegrateLinear so position advances", "motionOps"));

    if (triggers.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier recipe requires at least one trigger", "triggers"));

    for (std::size_t i = 0; i < triggers.size(); ++i) {
        const auto& trigger = triggers[i];
        const auto  path    = "triggers[" + std::to_string(i) + "]";
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (triggers[previous].kind == trigger.kind)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                      "Trigger kinds must be unique because impacts bind by kind", path));
        }
        switch (trigger.kind) {
            case CarrierTriggerKind::OnFuse:
                if (trigger.fuse <= Duration::zero())
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Fuse trigger requires positive fuse", path + ".fuse"));
                break;
            case CarrierTriggerKind::OnInterval:
                if (trigger.interval <= Duration::zero())
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Interval trigger requires positive interval", path + ".interval"));
                break;
            case CarrierTriggerKind::OnProximity:
                if (!std::isfinite(trigger.proximityRadius) || trigger.proximityRadius <= 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Proximity trigger requires positive radius", path + ".proximityRadius"));
                break;
            case CarrierTriggerKind::OnDistance:
            case CarrierTriggerKind::OnSpeedBelow:
                if (!std::isfinite(trigger.threshold) || trigger.threshold <= 0.0)
                    return Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "Trigger threshold must be finite and positive", path));
                break;
            case CarrierTriggerKind::OnApex:
            case CarrierTriggerKind::OnExpire:
            case CarrierTriggerKind::OnHit:
                break;
            default:
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown carrier trigger", path));
        }
    }

    for (std::size_t i = 0; i < impacts.size(); ++i) {
        const auto& impact = impacts[i];
        const auto  path   = "impacts[" + std::to_string(i) + "]";
        if (impact.kind > CarrierImpactKind::EmitTrigger ||
            std::none_of(triggers.begin(), triggers.end(),
                         [&](const CarrierTrigger& trigger) { return trigger.kind == impact.on; }))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "Impact kind is unknown or has no matching trigger", path));
        if (!std::isfinite(impact.damage) || impact.damage < 0.0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Impact damage must be finite and non-negative", path + ".damage"));
        if (!std::isfinite(impact.splashRadius) || impact.splashRadius < 0.0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Splash radius must be finite and non-negative", path + ".splashRadius"));
        if (!std::isfinite(impact.restitution) || impact.restitution < 0.0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Bounce restitution must be finite and non-negative", path + ".restitution"));
        if (!std::isfinite(impact.bounceFriction) || impact.bounceFriction < 0.0 || impact.bounceFriction > 1.0 ||
            !std::isfinite(impact.bounceSpreadDegrees) || impact.bounceSpreadDegrees < 0.0 ||
            impact.bounceSpreadDegrees > 90.0 || impact.randomSeed < 0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "Bounce friction/spread/seed must be in [0,1]/[0,90]/nonnegative", path));
        if (impact.pierceCount < 0 || impact.bounceCount < 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Pierce/bounce budgets must be non-negative", path));
        if (impact.kind == CarrierImpactKind::SpawnChild && !impact.childRecipeId.isValid())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "SpawnChild requires childRecipeId", path + ".childRecipeId"));
        if (impact.kind == CarrierImpactKind::Splash && impact.splashRadius <= 0.0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Splash impact requires positive splashRadius", path + ".splashRadius"));
    }
    return Result<void>::success();
}

Result<CarrierRecipe> carrierRecipeFromProjectile(const ProjectileDefinition& definition, double damage) {
    auto valid = definition.validate();
    if (!valid) return Result<CarrierRecipe>::failure(valid.status());
    if (!std::isfinite(damage) || damage < 0.0)
        return Result<CarrierRecipe>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Bridge damage must be finite and non-negative", "damage"));

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

Result<void> CarrierSimulationSettings::validate() const {
    if (maxStep < Duration::zero() || maxIterations == 0 || maxIterations > 65536 || maxCollisions == 0 ||
        maxCollisions > 65536 || maxTriggerFirings == 0 || maxTriggerFirings > 65536)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Simulation requires nonnegative maxStep and budgets in 1..65536", "simulation"));
    return Result<void>::success();
}

Result<void> CombatCarrierRuntime::configureSimulation(const CarrierSimulationSettings& settings) {
    if (updating_ || activeCount_ != 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Configure simulation only while empty and outside callbacks", "simulation"));
    auto valid = settings.validate();
    if (!valid) return valid;
    simulation_ = settings;
    return Result<void>::success();
}

Result<void> CombatCarrierRuntime::configurePool(std::uint32_t capacity) {
    if (updating_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    if (activeCount_ != 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Carrier pool cannot be resized while active", {}));
    if (capacity == 0 || capacity > kMaximumCapacity)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier pool capacity must be in 1..1048576", "capacity"));
    slots_.assign(capacity, Slot{});
    return Result<void>::success();
}

Result<CombatCarrierRuntime::LiveRecipe> CombatCarrierRuntime::prepareRecipe(const CarrierRecipe& recipe) {
    auto valid = recipe.validate();
    if (!valid) return Result<LiveRecipe>::failure(valid.status());

    LiveRecipe live;
    live.recipe = recipe;
    for (const auto& op : recipe.motionOps) {
        if ((op.kind == CarrierMotionOpKind::SteerHoming || op.kind == CarrierMotionOpKind::SteerOrbit ||
             op.kind == CarrierMotionOpKind::SteerPredictive))
            live.needsHoming = true;
        if (op.kind == CarrierMotionOpKind::SteerAvoidBody) live.needsBodyAvoid = true;
    }
    for (const auto& trigger : recipe.triggers) {
        if (trigger.kind == CarrierTriggerKind::OnHit) live.needsHit = recipe.collisionEnabled;
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
    if (updating_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    auto prepared = prepareRecipe(recipe);
    if (!prepared) return Result<void>::failure(prepared.status());
    LiveRecipe live = std::move(prepared).takeValue();
    for (auto& existing : recipes_) {
        if (existing.recipe.id == recipe.id) {
            if (existing.recipe == recipe) return Result<void>::success(Status::success(StatusCode::NoOp));
            if (std::any_of(slots_.begin(), slots_.end(),
                            [&](const Slot& slot) { return slot.state && slot.state->recipeId == recipe.id; }))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict, "Release live carriers before replacing their recipe", "recipeId"));
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
    if (!finite(request.position) || !finite(request.direction) || !std::isfinite(length(request.direction)))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier transform must contain finite values", "request"));
    if (length(request.direction) <= 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier direction must be non-zero", "direction"));

    bool needsTarget = false;
    for (const auto& op : recipe.motionOps) {
        if ((op.kind == CarrierMotionOpKind::SteerHoming || op.kind == CarrierMotionOpKind::SteerOrbit ||
             op.kind == CarrierMotionOpKind::SteerPredictive))
            needsTarget = true;
    }
    for (const auto& trigger : recipe.triggers) {
        if (trigger.kind == CarrierTriggerKind::OnProximity) needsTarget = true;
    }
    if (needsTarget && !request.target.has_value())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Homing/proximity carrier requires a target handle", "target"));
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
        state.heading          = dir;
        state.motion.position = request.position;
        state.motion.velocity = {dir.x * live.recipe.speed, dir.y * live.recipe.speed, dir.z * live.recipe.speed};
        cell.state             = std::move(state);
        ++activeCount_;
        return Result<CarrierHandle>::success(cell.state->handle);
    }
    return Result<CarrierHandle>::failure(
        Diagnostic::error(DiagnosticCode::Conflict, "Carrier pool is full", "capacity"));
}

Result<CarrierHandle> CombatCarrierRuntime::spawn(const LogicalId& recipeId, const CarrierSpawnRequest& request) {
    if (updating_)
        return Result<CarrierHandle>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    for (const auto& live : recipes_) {
        if (live.recipe.id == recipeId) return spawnPrepared(live, request);
    }
    return Result<CarrierHandle>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "Carrier recipe is not registered", "recipeId"));
}

Result<CarrierHandle> CombatCarrierRuntime::spawn(const CarrierRecipe& recipe, const CarrierSpawnRequest& request) {
    if (updating_)
        return Result<CarrierHandle>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    auto valid = validateSpawn(recipe, request);
    if (!valid) return Result<CarrierHandle>::failure(valid.status());
    if (activeCount_ >= slots_.size())
        return Result<CarrierHandle>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Carrier pool is full", "capacity"));
    auto registered = registerRecipe(recipe);
    if (!registered) return Result<CarrierHandle>::failure(registered.status());
    return spawn(recipe.id, request);
}

Result<std::vector<CarrierHandle>> CombatCarrierRuntime::spawnVolley(const LogicalId&           recipeId,
                                                                     const CarrierSpawnRequest& request,
                                                                     const CarrierVolleySpec&   volley) {
    if (updating_)
        return Result<std::vector<CarrierHandle>>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    auto dirs = volleyDirections(request.direction, volley);
    if (!dirs) return Result<std::vector<CarrierHandle>>::failure(dirs.status());
    if (activeCount_ + dirs.value().size() > slots_.size())
        return Result<std::vector<CarrierHandle>>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Carrier pool cannot fit the full volley", "capacity"));

    std::vector<CarrierHandle> handles;
    handles.reserve(dirs.value().size());
    const std::size_t activeBefore = activeCount_;
    const auto        slotsBefore  = slots_;
    for (const auto& direction : dirs.value()) {
        CarrierSpawnRequest one = request;
        one.direction           = direction;
        auto spawned            = spawn(recipeId, one);
        if (!spawned) {
            slots_       = slotsBefore;
            activeCount_ = activeBefore;
            return Result<std::vector<CarrierHandle>>::failure(spawned.status());
        }
        handles.push_back(spawned.value());
    }
    return Result<std::vector<CarrierHandle>>::success(std::move(handles));
}

Result<std::vector<CarrierHandle>> CombatCarrierRuntime::spawnVolley(const CarrierRecipe&       recipe,
                                                                     const CarrierSpawnRequest& request,
                                                                     const CarrierVolleySpec&   volley) {
    if (updating_)
        return Result<std::vector<CarrierHandle>>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    auto valid = validateSpawn(recipe, request);
    if (!valid) return Result<std::vector<CarrierHandle>>::failure(valid.status());
    auto directions = volleyDirections(request.direction, volley);
    if (!directions) return Result<std::vector<CarrierHandle>>::failure(directions.status());
    if (activeCount_ + directions.value().size() > slots_.size())
        return Result<std::vector<CarrierHandle>>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Carrier pool cannot fit the full volley", "capacity"));
    auto registered = registerRecipe(recipe);
    if (!registered) return Result<std::vector<CarrierHandle>>::failure(registered.status());
    return spawnVolley(recipe.id, request, volley);
}

Result<CarrierFrame> CombatCarrierRuntime::update(Duration delta, const IProjectileTargetProvider* targets,
                                                  const ICarrierHitProbe*            hits,
                                                  const ICarrierBodyDefenseProvider* bodies,
                                                  const ICarrierHitProbe*            worldSweep) {
    return updateImpl(delta, targets, hits, bodies, nullptr, worldSweep);
}

Result<CarrierFrame> CombatCarrierRuntime::updateWithMotion(Duration delta, const ICarrierMotionFunction& motion,
                                                            const IProjectileTargetProvider*   targets,
                                                            const ICarrierHitProbe*            hits,
                                                            const ICarrierBodyDefenseProvider* bodies,
                                                            const ICarrierHitProbe*            worldSweep) {
    return updateImpl(delta, targets, hits, bodies, &motion, worldSweep);
}

Result<CarrierFrame> CombatCarrierRuntime::updateImpl(Duration delta, const IProjectileTargetProvider* targets,
                                                      const ICarrierHitProbe*            hits,
                                                      const ICarrierBodyDefenseProvider* bodies,
                                                      const ICarrierMotionFunction*      customMotion,
                                                      const ICarrierHitProbe*            worldSweep) {
    if (updating_)
        return Result<CarrierFrame>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    struct UpdateGuard {
        bool& active;
        explicit UpdateGuard(bool& flag) : active(flag) { active = true; }
        ~UpdateGuard() { active = false; }
    } guard(updating_);

    if (delta < Duration::zero())
        return Result<CarrierFrame>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier update delta must be >= 0", "delta"));
    if (delta.isZero()) return Result<CarrierFrame>::success({}, Status::success(StatusCode::NoOp));

    struct Candidate {
        CarrierState state;
        bool         release = false;
    };
    struct TriggerFire {
        CarrierTriggerKind                       kind = CarrierTriggerKind::OnExpire;
        std::optional<ICarrierHitProbe::Contact> contact;
        Duration                                 at;
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
            return Result<CarrierFrame>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "Live carrier references an unregistered recipe", "recipeId"));
        if ((live->needsHoming || live->needsProximity) && targets == nullptr)
            return Result<CarrierFrame>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "Homing/proximity carrier update requires a target provider", "targets"));
        if (live->needsHit && (live->recipe.physicsWorldSweep ? worldSweep : hits) == nullptr)
            return Result<CarrierFrame>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "Enabled OnHit collision requires its selected probe",
                                  live->recipe.physicsWorldSweep ? "worldSweep" : "hits"));
        if (live->needsBodyAvoid && bodies == nullptr)
            return Result<CarrierFrame>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported,
                                  "SteerAvoidBody carrier update requires a body-defense provider", "bodies"));
        candidates.push_back({*slot.state, false});
    }

    for (auto& candidate : candidates) {
        const LiveRecipe* live = lookupLive(candidate.state.recipeId);
        if (live == nullptr)
            return Result<CarrierFrame>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "Live carrier references an unregistered recipe", "recipeId"));

        std::optional<ProjectilePoint> targetPos;
        if (live->needsHoming || live->needsProximity) {
            if (!candidate.state.target.has_value())
                return Result<CarrierFrame>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "Homing/proximity carrier lost its target handle", "target"));
            auto resolved = targets->position(*candidate.state.target);
            if (!resolved) return Result<CarrierFrame>::failure(resolved.status());
            targetPos = resolved.value();
            if (!finite(*targetPos))
                return Result<CarrierFrame>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Target position must be finite", "targets"));
        }

        const Duration   sampleAge = candidate.state.age;
        ProjectileVector targetVelocity{};
        if (targetPos && candidate.state.previousTargetPosition &&
            sampleAge > candidate.state.previousTargetSampleAge) {
            const double sampleDt =
                static_cast<double>(sampleAge.nanoseconds() - candidate.state.previousTargetSampleAge.nanoseconds()) *
                1e-9;
            const auto displacement = directionTo(*candidate.state.previousTargetPosition, *targetPos);
            targetVelocity          = {displacement.x / sampleDt, displacement.y / sampleDt, displacement.z / sampleDt};
        }
        std::int64_t remaining =
            std::min(delta.nanoseconds(), candidate.state.lifetime.nanoseconds() - candidate.state.age.nanoseconds());
        std::uint32_t iterations = 0, collisions = 0, triggerFirings = 0;
        while (remaining > 0 && !candidate.release) {
            if (++iterations > simulation_.maxIterations)
                return Result<CarrierFrame>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict,
                    "Carrier simulation iteration budget exhausted; retry with smaller dt or a larger budget",
                    "simulation"));
            const CarrierState segmentStart     = candidate.state;
            const Duration     previousAge      = segmentStart.age;
            const double       previousDistance = segmentStart.distanceTravelled;
            CarrierMotion      previous         = segmentStart.motion;
            CarrierMotion      motion           = previous;
            const auto         oldCurve         = segmentStart.curveVelocity;
            motion.velocity                     = {motion.velocity.x - oldCurve.x, motion.velocity.y - oldCurve.y,
                                                   motion.velocity.z - oldCurve.z};
            ProjectileVector curveVelocity{};
            const Duration   step = Duration::fromNanoseconds(
                simulation_.maxStep.isZero() ? remaining : std::min(remaining, simulation_.maxStep.nanoseconds()));
            const double dtSeconds  = step.seconds();
            const double ageSeconds = candidate.state.age.seconds();
            for (const auto& op : live->recipe.motionOps) {
                switch (op.kind) {
                    case CarrierMotionOpKind::SteerPredictive: {
                        auto predicted = *targetPos;
                        if (candidate.state.previousTargetPosition) {
                            const double speed = length(motion.velocity);
                            const double horizon =
                                speed > 1e-12 ? std::min(op.predictionSeconds,
                                                         length(directionTo(motion.position, *targetPos)) / speed)
                                              : 0.0;
                            predicted.x += targetVelocity.x * horizon;
                            predicted.y += targetVelocity.y * horizon;
                            predicted.z += targetVelocity.z * horizon;
                        }
                        const auto desired = directionTo(motion.position, predicted);
                        if (length(desired) > 1e-12)
                            motion.velocity =
                                steer(motion.velocity, desired, op.maxTurnRateDegrees * kPi / 180.0 * dtSeconds);
                        break;
                    }
                    case CarrierMotionOpKind::SteerHoming: {
                        const auto desired = directionTo(motion.position, *targetPos);
                        if (length(desired) <= 1e-12) break;
                        const double maxRadians = op.maxTurnRateDegrees * kPi / 180.0 * dtSeconds;
                        motion.velocity         = steer(motion.velocity, desired, maxRadians);
                        break;
                    }
                    case CarrierMotionOpKind::SteerAvoidBody: {
                        ecs::EntityHandle targetHandle{};
                        if (candidate.state.target.has_value()) targetHandle = *candidate.state.target;
                        auto samples = bodies->sample(candidate.state.handle, targetHandle, motion);
                        if (!samples) return Result<CarrierFrame>::failure(samples.status());
                        ProjectileVector avoid{0.0, 0.0, 0.0};
                        bool             any = false;
                        const auto       fwd = length(motion.velocity) > 1e-12 ? normalized(motion.velocity)
                                                                               : ProjectileVector{1.0, 0.0, 0.0};
                        for (const auto& body : samples.value()) {
                            if (!std::isfinite(body.radius) || body.radius < 0.0) {
                                return Result<CarrierFrame>::failure(
                                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "Body defense radius must be finite and non-negative", "bodies"));
                            }
                            const auto   toCenter = directionTo(motion.position, body.center);
                            const double dist     = length(toCenter);
                            const double blockAt  = body.radius + op.avoidRadiusPadding + op.avoidLookAhead;
                            if (dist <= 1e-12 || dist > blockAt) continue;
                            if (length(body.facing) > 1e-12 && body.frontConeDegrees < 180.0) {
                                const auto approach =
                                    normalized(ProjectileVector{-toCenter.x, -toCenter.y, -toCenter.z});
                                const auto   face = normalized(body.facing);
                                const double dot  = std::clamp(
                                    approach.x * face.x + approach.y * face.y + approach.z * face.z, -1.0, 1.0);
                                const double angleDeg = std::acos(dot) * 180.0 / kPi;
                                if (angleDeg > body.frontConeDegrees) continue;
                            }
                            auto lateral = cross(fwd, toCenter);
                            if (length(lateral) <= 1e-12) lateral = lateralBasis(fwd);
                            lateral = normalized(lateral);
                            if (targetPos.has_value()) {
                                const auto   around     = directionTo(body.center, *targetPos);
                                const auto   left       = lateral;
                                const auto   right      = ProjectileVector{-lateral.x, -lateral.y, -lateral.z};
                                const double leftScore  = left.x * around.x + left.y * around.y + left.z * around.z;
                                const double rightScore = right.x * around.x + right.y * around.y + right.z * around.z;
                                if (rightScore > leftScore) lateral = right;
                            }
                            const double weight = op.avoidStrength * (1.0 - dist / blockAt);
                            avoid.x += lateral.x * weight;
                            avoid.y += lateral.y * weight;
                            avoid.z += lateral.z * weight;
                            any = true;
                        }
                        if (any && length(avoid) > 1e-12) {
                            const auto   desired = ProjectileVector{fwd.x + avoid.x, fwd.y + avoid.y, fwd.z + avoid.z};
                            const double maxRadians = op.maxTurnRateDegrees * kPi / 180.0 * dtSeconds;
                            motion.velocity         = steer(motion.velocity, desired, maxRadians);
                        }
                        break;
                    }
                    case CarrierMotionOpKind::RotateVelocity:
                        motion.velocity =
                            rotateAroundAxis(motion.velocity, op.axis, op.angularRateDegrees * kPi / 180.0 * dtSeconds);
                        break;
                    case CarrierMotionOpKind::SteerOrbit: {
                        const auto   axis   = normalized(op.axis);
                        auto         radial = directionTo(*targetPos, motion.position);
                        const double height = radial.x * axis.x + radial.y * axis.y + radial.z * axis.z;
                        radial = {radial.x - height * axis.x, radial.y - height * axis.y, radial.z - height * axis.z};
                        const double radius     = length(radial);
                        radial                  = radius > 1e-12 ? normalized(radial) : lateralBasis(axis);
                        const auto   tangent    = cross(axis, radial);
                        const double angular    = op.angularRateDegrees * kPi / 180.0;
                        const double correction = (op.orbitRadius - radius) / op.orbitRadius;
                        const auto   desired    = ProjectileVector{tangent.x * angular + radial.x * correction,
                                                              tangent.y * angular + radial.y * correction,
                                                              tangent.z * angular + radial.z * correction};
                        if (length(desired) > 1e-12)
                            motion.velocity =
                                steer(motion.velocity, desired, op.maxTurnRateDegrees * kPi / 180.0 * dtSeconds);
                        break;
                    }
                    case CarrierMotionOpKind::ApplyDrag: {
                        const double factor = std::exp(-op.drag * dtSeconds);
                        motion.velocity     = {motion.velocity.x * factor, motion.velocity.y * factor,
                                               motion.velocity.z * factor};
                        break;
                    }
                    case CarrierMotionOpKind::SpeedOscillate: {
                        const double speed = length(motion.velocity);
                        const auto   dir   = speed > 1e-12 ? normalized(motion.velocity) : candidate.state.heading;
                        const double next  = std::max(
                            0.0, live->recipe.speed + op.curveAmplitude * std::sin(2.0 * kPi * op.curveFrequencyHz *
                                                                                    (ageSeconds + dtSeconds)));
                        motion.velocity = {dir.x * next, dir.y * next, dir.z * next};
                        break;
                    }
                    case CarrierMotionOpKind::SteerWander: {
                        // Absolute-age hashed samples: replay needs no global random state.
                        const double phase = ageSeconds * op.curveFrequencyHz;
                        if (!std::isfinite(phase) || phase > 4294967294.0)
                            return Result<CarrierFrame>::failure(
                                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Wander sample index exceeds supported range", "motion"));
                        const auto   index  = static_cast<std::uint32_t>(phase);
                        const double blend  = (phase - index) * (phase - index) * (3.0 - 2.0 * (phase - index));
                        auto         sample = [&](std::uint32_t tick, std::uint32_t channel) {
                            return randomSample(static_cast<std::uint32_t>(op.randomSeed), candidate.state.handle, tick,
                                                        channel) *
                                       2.0 -
                                   1.0;
                        };
                        auto smooth = [&](std::uint32_t channel) {
                            const double first = sample(index, channel);
                            return first + (sample(index + 1, channel) - first) * blend;
                        };
                        const double speed = length(motion.velocity);
                        if (speed <= 1e-12) break;
                        const auto             fwd      = normalized(motion.velocity);
                        const auto             side     = lateralBasis(fwd);
                        const auto             up       = cross(side, fwd);
                        const double           lateral  = smooth(0x12345678U) * op.curveAmplitude;
                        const double           vertical = smooth(0xabcdef01U) * op.curveAmplitude;
                        const ProjectileVector desired{fwd.x + side.x * lateral + up.x * vertical,
                                                       fwd.y + side.y * lateral + up.y * vertical,
                                                       fwd.z + side.z * lateral + up.z * vertical};
                        motion.velocity =
                            steer(motion.velocity, desired, op.maxTurnRateDegrees * kPi / 180.0 * dtSeconds);
                        break;
                    }
                    case CarrierMotionOpKind::ApplyGravity: motion.velocity.y -= op.gravity * dtSeconds; break;
                    case CarrierMotionOpKind::Accelerate: {
                        const double speed = length(motion.velocity);
                        const auto   dir   = speed > 1e-12 ? normalized(motion.velocity) : candidate.state.heading;
                        const double next  = std::max(0.0, speed + op.acceleration * dtSeconds);
                        motion.velocity    = {dir.x * next, dir.y * next, dir.z * next};
                        break;
                    }
                    case CarrierMotionOpKind::CurveSway:
                    case CarrierMotionOpKind::CurveHelix: {
                        const ProjectileVector base{motion.velocity.x - curveVelocity.x,
                                                    motion.velocity.y - curveVelocity.y,
                                                    motion.velocity.z - curveVelocity.z};
                        if (length(base) <= 1e-12) break;
                        const auto   fwd     = normalized(base);
                        const auto   side    = lateralBasis(fwd);
                        const auto   up      = normalized(cross(side, fwd));
                        const double omega   = 2.0 * kPi * op.curveFrequencyHz;
                        const double before  = omega * ageSeconds;
                        const double after   = omega * (ageSeconds + dtSeconds);
                        const double lateral = op.curveAmplitude * (std::sin(after) - std::sin(before)) / dtSeconds;
                        const double vertical =
                            op.kind == CarrierMotionOpKind::CurveHelix
                                ? op.curveAmplitude * (std::cos(before) - std::cos(after)) / dtSeconds
                                : 0.0;
                        const ProjectileVector modifier{side.x * lateral + up.x * vertical,
                                                        side.y * lateral + up.y * vertical,
                                                        side.z * lateral + up.z * vertical};
                        motion.velocity = {motion.velocity.x + modifier.x, motion.velocity.y + modifier.y,
                                           motion.velocity.z + modifier.z};
                        curveVelocity   = {curveVelocity.x + modifier.x, curveVelocity.y + modifier.y,
                                           curveVelocity.z + modifier.z};
                        break;
                    }
                    case CarrierMotionOpKind::IntegrateLinear:
                        candidate.state.distanceTravelled += length(motion.velocity) * dtSeconds;
                        motion.position.x += motion.velocity.x * dtSeconds;
                        motion.position.y += motion.velocity.y * dtSeconds;
                        motion.position.z += motion.velocity.z * dtSeconds;
                        break;
                }
            }

            if (customMotion != nullptr) {
                const auto& before = segmentStart;
                auto        custom = [&]() -> Result<CarrierMotion> {
                    try {
                        return customMotion->evaluate(before, motion, step);
                    } catch (const std::exception& error) {
                        return Result<CarrierMotion>::failure(
                            Diagnostic::error(DiagnosticCode::Failed, error.what(), "motionFunction"));
                    } catch (...) {
                        return Result<CarrierMotion>::failure(Diagnostic::error(
                            DiagnosticCode::Failed, "Custom motion function threw", "motionFunction"));
                    }
                }();
                if (!custom) return Result<CarrierFrame>::failure(custom.status());
                const auto& output = custom.value();
                const bool changed = output.position.x != motion.position.x || output.position.y != motion.position.y ||
                                     output.position.z != motion.position.z || output.velocity.x != motion.velocity.x ||
                                     output.velocity.y != motion.velocity.y || output.velocity.z != motion.velocity.z;
                if (changed) {
                    candidate.state.distanceTravelled =
                        before.distanceTravelled + length(directionTo(previous.position, output.position));
                    curveVelocity = {};
                    motion        = output;
                }
            }

            if (!finite(motion.position) || !finite(motion.velocity) || !std::isfinite(length(motion.velocity)) ||
                !std::isfinite(candidate.state.distanceTravelled))
                return Result<CarrierFrame>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "Carrier motion produced non-finite values", "motion"));

            auto nextAge = previousAge.tryAdd(step);
            if (!nextAge) return Result<CarrierFrame>::failure(nextAge.status());

            candidate.state.curveVelocity = curveVelocity;
            const ProjectileVector baseVelocity{motion.velocity.x - curveVelocity.x,
                                                motion.velocity.y - curveVelocity.y,
                                                motion.velocity.z - curveVelocity.z};
            if (length(baseVelocity) > 1e-12) candidate.state.heading = normalized(baseVelocity);
            candidate.state.motion = motion;
            candidate.state.age    = std::move(nextAge).takeValue();

            std::vector<TriggerFire> fires;
            for (const auto& trigger : live->recipe.triggers) {
                switch (trigger.kind) {
                    case CarrierTriggerKind::OnExpire:
                        if (crossed(previousAge, candidate.state.age, candidate.state.lifetime) ||
                            candidate.state.age >= candidate.state.lifetime)
                            fires.push_back({CarrierTriggerKind::OnExpire, std::nullopt, candidate.state.lifetime});
                        break;
                    case CarrierTriggerKind::OnFuse:
                        if (crossed(previousAge, candidate.state.age, trigger.fuse))
                            fires.push_back({CarrierTriggerKind::OnFuse, std::nullopt, trigger.fuse});
                        break;
                    case CarrierTriggerKind::OnInterval: {
                        const auto before = intervalIndex(previousAge, trigger.interval);
                        const auto after  = intervalIndex(candidate.state.age, trigger.interval);
                        if (after - before > simulation_.maxTriggerFirings)
                            return Result<CarrierFrame>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument,
                                "Interval trigger exceeds the configured firing budget; use smaller steps",
                                "interval"));
                        for (auto i = before; i < after; ++i)
                            fires.push_back({CarrierTriggerKind::OnInterval, std::nullopt,
                                             Duration::fromNanoseconds((i + 1) * trigger.interval.nanoseconds())});
                        break;
                    }
                    case CarrierTriggerKind::OnHit: {
                        if (!live->recipe.collisionEnabled) break;
                        const auto* probe    = live->recipe.physicsWorldSweep ? worldSweep : hits;
                        auto        contacts = probe->query(candidate.state.handle, previous, motion);
                        if (!contacts) return Result<CarrierFrame>::failure(contacts.status());
                        auto&        ordered       = contacts.value();
                        const auto   displacement  = directionTo(previous.position, motion.position);
                        const double segmentLength = length(displacement);
                        for (auto& contact : ordered) {
                            if (contact.worldContact &&
                                ((contact.worldContact->identity.world >> 32) == 0 ||
                                 (contact.worldContact->identity.body >> 32) == 0 ||
                                 (contact.worldContact->identity.world & 0xffffffffu) == 0xffffffffu ||
                                 (contact.worldContact->identity.body & 0xffffffffu) == 0xffffffffu))
                                return Result<CarrierFrame>::failure(
                                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "Invalid physics contact identity", "hits.worldContact"));
                            if (!finite(contact.point) || !finite(contact.normal) ||
                                !std::isfinite(length(contact.normal)) || length(contact.normal) <= 1e-12)
                                return Result<CarrierFrame>::failure(
                                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "Contact requires finite point and nonzero normal", "hits"));
                            if (!contact.fraction) {
                                const auto direction =
                                    segmentLength > 1e-12 ? normalized(displacement) : ProjectileVector{};
                                const auto offset = directionTo(previous.position, contact.point);
                                contact.fraction  = segmentLength > 1e-12
                                                        ? std::clamp((offset.x * direction.x + offset.y * direction.y +
                                                                     offset.z * direction.z) /
                                                                         segmentLength,
                                                                     0.0, 1.0)
                                                        : 0.0;
                            }
                            if (!std::isfinite(*contact.fraction) || *contact.fraction < 0.0 || *contact.fraction > 1.0)
                                return Result<CarrierFrame>::failure(
                                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "Contact fraction must be finite and in [0,1]", "hits.fraction"));
                        }
                        std::stable_sort(ordered.begin(), ordered.end(),
                                         [](const auto& a, const auto& b) { return *a.fraction < *b.fraction; });
                        for (auto& contact : ordered) {
                            if (contact.worldContact &&
                                std::find(candidate.state.piercedWorldBodies.begin(),
                                          candidate.state.piercedWorldBodies.end(),
                                          contact.worldContact->identity) != candidate.state.piercedWorldBodies.end())
                                continue;
                            if (contact.target.id != 0 &&
                                std::find_if(candidate.state.piercedTargets.begin(),
                                             candidate.state.piercedTargets.end(), [&](const auto& target) {
                                                 return sameTarget(target, contact.target);
                                             }) != candidate.state.piercedTargets.end())
                                continue;
                            const double fraction = *contact.fraction;
                            // A contact at the start while moving away must not consume another bounce.
                            if (fraction <= 1e-12 && displacement.x * contact.normal.x +
                                                             displacement.y * contact.normal.y +
                                                             displacement.z * contact.normal.z >=
                                                         0.0)
                                continue;
                            auto offset = step.scaled(fraction);
                            if (!offset) return Result<CarrierFrame>::failure(offset.status());
                            auto at = previousAge.tryAdd(offset.value());
                            if (!at) return Result<CarrierFrame>::failure(at.status());
                            fires.push_back({CarrierTriggerKind::OnHit, std::move(contact), at.value()});
                        }
                        break;
                    }
                    case CarrierTriggerKind::OnDistance: {
                        const double before = previousDistance;
                        if (before < trigger.threshold && candidate.state.distanceTravelled >= trigger.threshold) {
                            const double fraction =
                                (trigger.threshold - before) / (candidate.state.distanceTravelled - before);
                            auto offset = step.scaled(std::clamp(fraction, 0.0, 1.0));
                            if (!offset) return Result<CarrierFrame>::failure(offset.status());
                            auto at = previousAge.tryAdd(offset.value());
                            if (!at) return Result<CarrierFrame>::failure(at.status());
                            fires.push_back({trigger.kind, std::nullopt, at.value()});
                        }
                        break;
                    }
                    case CarrierTriggerKind::OnSpeedBelow:
                    case CarrierTriggerKind::OnApex: {
                        const double before = trigger.kind == CarrierTriggerKind::OnApex ? previous.velocity.y
                                                                                         : length(previous.velocity);
                        const double after =
                            trigger.kind == CarrierTriggerKind::OnApex ? motion.velocity.y : length(motion.velocity);
                        const double threshold = trigger.kind == CarrierTriggerKind::OnApex ? 0.0 : trigger.threshold;
                        if (before > threshold && after <= threshold) {
                            auto offset = step.scaled(std::clamp((before - threshold) / (before - after), 0.0, 1.0));
                            if (!offset) return Result<CarrierFrame>::failure(offset.status());
                            auto at = previousAge.tryAdd(offset.value());
                            if (!at) return Result<CarrierFrame>::failure(at.status());
                            fires.push_back({trigger.kind, std::nullopt, at.value()});
                        }
                        break;
                    }
                    case CarrierTriggerKind::OnProximity: {
                        const auto   deltaPos = directionTo(motion.position, *targetPos);
                        const double dist     = length(deltaPos);
                        if (dist <= trigger.proximityRadius)
                            fires.push_back({CarrierTriggerKind::OnProximity, std::nullopt, candidate.state.age});
                        break;
                    }
                }
            }

            std::stable_sort(fires.begin(), fires.end(), [](const TriggerFire& a, const TriggerFire& b) {
                if (a.at != b.at) return a.at < b.at;
                return a.kind != CarrierTriggerKind::OnExpire && b.kind == CarrierTriggerKind::OnExpire;
            });
            std::optional<Duration> bouncedAt;
            for (std::size_t fireIndex = 0; fireIndex < fires.size(); ++fireIndex) {
                const auto fire = fires[fireIndex];
                if (bouncedAt && fire.at > *bouncedAt) break;
                if (fire.contact && fire.contact->worldContact &&
                    std::find(candidate.state.piercedWorldBodies.begin(), candidate.state.piercedWorldBodies.end(),
                              fire.contact->worldContact->identity) != candidate.state.piercedWorldBodies.end())
                    continue;
                if (fire.contact && fire.contact->target.id != 0 &&
                    std::find_if(candidate.state.piercedTargets.begin(), candidate.state.piercedTargets.end(),
                                 [&](const auto& target) { return sameTarget(target, fire.contact->target); }) !=
                        candidate.state.piercedTargets.end())
                    continue;
                if (bouncedAt && fire.contact &&
                    candidate.state.motion.velocity.x * fire.contact->normal.x +
                            candidate.state.motion.velocity.y * fire.contact->normal.y +
                            candidate.state.motion.velocity.z * fire.contact->normal.z >=
                        0.0)
                    continue;
                if (++triggerFirings > simulation_.maxTriggerFirings)
                    return Result<CarrierFrame>::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "Carrier trigger budget exhausted", "simulation"));
                if (fire.kind == CarrierTriggerKind::OnHit && ++collisions > simulation_.maxCollisions)
                    return Result<CarrierFrame>::failure(Diagnostic::error(
                        DiagnosticCode::Conflict, "Carrier collision budget exhausted", "simulation"));
                bool         suppressRelease = false;
                bool         requestRelease  = false;
                const double fraction =
                    std::clamp(static_cast<double>(fire.at.nanoseconds() - previousAge.nanoseconds()) /
                                   static_cast<double>(step.nanoseconds()),
                               0.0, 1.0);
                const ProjectilePoint eventPosition =
                    fire.contact
                        ? fire.contact->point
                        : ProjectilePoint{previous.position.x + (motion.position.x - previous.position.x) * fraction,
                                          previous.position.y + (motion.position.y - previous.position.y) * fraction,
                                          previous.position.z + (motion.position.z - previous.position.z) * fraction};

                for (const auto& impact : live->recipe.impacts) {
                    if (impact.on != fire.kind) continue;

                    CarrierEvent event;
                    event.age      = fire.at;
                    event.carrier  = candidate.state.handle;
                    event.recipeId = candidate.state.recipeId;
                    event.trigger  = fire.kind;
                    event.impact   = impact.kind;
                    event.position = eventPosition;
                    event.source   = candidate.state.source;
                    if (fire.contact.has_value()) {
                        event.worldContact = fire.contact->worldContact;
                        event.target       = fire.contact->target;
                        event.normal       = fire.contact->normal;
                        event.position     = fire.contact->point;
                    } else if (candidate.state.target.has_value()) {
                        event.target = *candidate.state.target;
                    }

                    switch (impact.kind) {
                        case CarrierImpactKind::EmitTrigger: events.push_back(std::move(event)); break;
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
                            event.position     = eventPosition;
                            events.push_back(std::move(event));
                            break;
                        case CarrierImpactKind::Pierce:
                            if (candidate.state.pierceRemaining > 0) {
                                if (fire.contact && fire.contact->worldContact) {
                                    if (candidate.state.piercedWorldBodies.size() >= 65536)
                                        return Result<CarrierFrame>::failure(Diagnostic::error(
                                            DiagnosticCode::Conflict, "Pierced world body budget exhausted", "hits"));
                                    candidate.state.piercedWorldBodies.push_back(fire.contact->worldContact->identity);
                                }
                                if (fire.contact && fire.contact->target.id != 0) {
                                    if (candidate.state.piercedTargets.size() >= 65536)
                                        return Result<CarrierFrame>::failure(
                                            Diagnostic::error(DiagnosticCode::Conflict,
                                                              "Carrier pierced-target budget exhausted", "hits"));
                                    candidate.state.piercedTargets.push_back(fire.contact->target);
                                }
                                --candidate.state.pierceRemaining;
                                suppressRelease = true;
                            }
                            break;
                        case CarrierImpactKind::Bounce:
                            if (candidate.state.bounceRemaining > 0 && fire.contact.has_value()) {
                                if (!finite(fire.contact->point) || !finite(fire.contact->normal) ||
                                    length(fire.contact->normal) <= 1e-12)
                                    return Result<CarrierFrame>::failure(Diagnostic::error(
                                        DiagnosticCode::InvalidArgument,
                                        "Bounce requires finite contact point and nonzero normal", "hits"));
                                --candidate.state.bounceRemaining;
                                candidate.state.motion.position = fire.contact->point;
                                candidate.state.distanceTravelled =
                                    previousDistance + length(directionTo(previous.position, fire.contact->point));
                                const auto impactVelocity =
                                    bouncedAt
                                        ? candidate.state.motion.velocity
                                        : ProjectileVector{previous.velocity.x +
                                                               (motion.velocity.x - previous.velocity.x) * fraction,
                                                           previous.velocity.y +
                                                               (motion.velocity.y - previous.velocity.y) * fraction,
                                                           previous.velocity.z +
                                                               (motion.velocity.z - previous.velocity.z) * fraction};
                                auto         reflected = reflect(impactVelocity, fire.contact->normal);
                                const auto   normal    = normalized(fire.contact->normal);
                                const double normalSpeed =
                                    reflected.x * normal.x + reflected.y * normal.y + reflected.z * normal.z;
                                const double retained = 1.0 - impact.bounceFriction;
                                reflected             = {
                                    (normal.x * normalSpeed + (reflected.x - normal.x * normalSpeed) * retained) *
                                        impact.restitution,
                                    (normal.y * normalSpeed + (reflected.y - normal.y * normalSpeed) * retained) *
                                        impact.restitution,
                                    (normal.z * normalSpeed + (reflected.z - normal.z * normalSpeed) * retained) *
                                        impact.restitution};
                                const double speed = length(reflected);
                                if (impact.bounceSpreadDegrees > 0.0 && speed > 1e-12 && std::isfinite(speed)) {
                                    const auto   forward = normalized(reflected);
                                    const auto   side    = lateralBasis(forward);
                                    const auto   up      = cross(side, forward);
                                    const auto   seed    = static_cast<std::uint32_t>(impact.randomSeed);
                                    const auto   tick    = static_cast<std::uint32_t>(candidate.state.bounceRemaining);
                                    const double cosine =
                                        1.0 - randomSample(seed, candidate.state.handle, tick, 0xb01U) *
                                                  (1.0 - std::cos(impact.bounceSpreadDegrees * kPi / 180.0));
                                    const double sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
                                    const double azimuth =
                                        2.0 * kPi * randomSample(seed, candidate.state.handle, tick, 0xb02U);
                                    const double a = sine * std::cos(azimuth), b = sine * std::sin(azimuth);
                                    reflected = {(forward.x * cosine + side.x * a + up.x * b) * speed,
                                                 (forward.y * cosine + side.y * a + up.y * b) * speed,
                                                 (forward.z * cosine + side.z * a + up.z * b) * speed};
                                    if (reflected.x * normal.x + reflected.y * normal.y + reflected.z * normal.z < 0.0)
                                        reflected = reflect(reflected, normal);
                                }
                                if (!finite(reflected) || !std::isfinite(length(reflected)))
                                    return Result<CarrierFrame>::failure(
                                        Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                          "Bounce restitution overflowed velocity", "restitution"));
                                for (const auto& trigger : live->recipe.triggers) {
                                    const bool speedCrossed = trigger.kind == CarrierTriggerKind::OnSpeedBelow &&
                                                              length(impactVelocity) > trigger.threshold &&
                                                              speed <= trigger.threshold;
                                    const bool apexCrossed = trigger.kind == CarrierTriggerKind::OnApex &&
                                                             impactVelocity.y > 0.0 && reflected.y <= 0.0;
                                    if (speedCrossed || apexCrossed)
                                        fires.insert(fires.begin() + static_cast<std::ptrdiff_t>(fireIndex + 1),
                                                     {trigger.kind, std::nullopt, fire.at});
                                }
                                candidate.state.motion.velocity = reflected;
                                if (length(reflected) > 1e-12) candidate.state.heading = normalized(reflected);
                                candidate.state.curveVelocity = {};
                                bouncedAt                     = fire.at;
                                suppressRelease               = true;
                            }
                            break;
                        case CarrierImpactKind::Release: requestRelease = true; break;
                        case CarrierImpactKind::SpawnChild: {
                            CarrierChildSpawn child;
                            child.recipeId         = impact.childRecipeId;
                            child.parent           = candidate.state.handle;
                            child.request.position = eventPosition;
                            child.request.direction =
                                bouncedAt
                                    ? candidate.state.motion.velocity
                                    : ProjectileVector{
                                          previous.velocity.x + (motion.velocity.x - previous.velocity.x) * fraction,
                                          previous.velocity.y + (motion.velocity.y - previous.velocity.y) * fraction,
                                          previous.velocity.z + (motion.velocity.z - previous.velocity.z) * fraction};
                            if (length(child.request.direction) <= 1e-12) child.request.direction = {1.0, 0.0, 0.0};
                            child.request.target = candidate.state.target;
                            child.request.source = candidate.state.source;
                            childSpawns.push_back(std::move(child));
                            break;
                        }
                    }
                }

                if (requestRelease && !suppressRelease) {
                    candidate.release = true;
                    break;
                }
            }

            if (bouncedAt) {
                const auto consumed = bouncedAt->nanoseconds() - previousAge.nanoseconds();
                candidate.state.age = *bouncedAt;
                remaining -= consumed;
            } else {
                remaining -= step.nanoseconds();
            }
            if (candidate.state.age >= candidate.state.lifetime) candidate.release = true;
        }  // all segments for this carrier
        candidate.state.previousTargetPosition  = targetPos;
        candidate.state.previousTargetSampleAge = sampleAge;
    }

    std::vector<Slot> staged       = slots_;
    std::size_t       stagedActive = activeCount_;
    for (const auto& candidate : candidates) {
        auto& cell = staged[candidate.state.handle.slot];
        if (cell.generation != candidate.state.handle.generation || !cell.state.has_value())
            return Result<CarrierFrame>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "Carrier handle became stale", "handle"));
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
            return Result<CarrierFrame>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "Child spawn references an unregistered recipe", "childRecipeId"));
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
            state.heading          = dir;
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
            return Result<CarrierFrame>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "Carrier pool cannot fit deferred child spawns", "capacity"));
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
    if (updating_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    if (handle.slot >= slots_.size())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Carrier handle slot is out of range", "handle"));
    Slot& cell = slots_[handle.slot];
    if (!cell.state.has_value() || cell.generation != handle.generation)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Carrier handle is stale", "handle"));
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
    snap.simulation = simulation_;
    for (const auto& live : recipes_) snap.recipes.push_back(live.recipe);
    snap.slots.reserve(slots_.size());
    for (const auto& slot : slots_) snap.slots.push_back({slot.generation, slot.state});
    return snap;
}

Result<void> CombatCarrierRuntime::restore(const CarrierRuntimeSnapshot& snapshot) {
    if (updating_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    if (snapshot.schemaVersion != 4)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Unsupported carrier snapshot schema; migrate to version 4 before restoring",
            "schemaVersion"));
    auto simulationValid = snapshot.simulation.validate();
    if (!simulationValid) return simulationValid;
    if (snapshot.slots.empty() || snapshot.slots.size() > kMaximumCapacity)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier snapshot capacity must be in 1..1048576", "slots"));

    std::vector<Slot> staged;
    staged.reserve(snapshot.slots.size());
    std::size_t active = 0;
    for (const auto& slot : snapshot.slots) {
        if (slot.state.has_value()) {
            if (slot.state->handle.slot != staged.size() || slot.generation == 0 ||
                slot.state->handle.generation != slot.generation || !finite(slot.state->motion.position) ||
                !finite(slot.state->motion.velocity) || !std::isfinite(length(slot.state->motion.velocity)) ||
                !finite(slot.state->curveVelocity) || !finite(slot.state->heading) ||
                std::abs(length(slot.state->heading) - 1.0) > 1e-6 ||
                (slot.state->previousTargetPosition && !finite(*slot.state->previousTargetPosition)) ||
                slot.state->previousTargetSampleAge < Duration::zero() ||
                slot.state->previousTargetSampleAge > slot.state->age ||
                !std::isfinite(slot.state->distanceTravelled) || slot.state->distanceTravelled < 0.0 ||
                slot.state->age < Duration::zero() || slot.state->lifetime <= slot.state->age ||
                slot.state->pierceRemaining < 0 || slot.state->bounceRemaining < 0)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Carrier snapshot state handle does not match slot metadata", "slots"));
            if (slot.state->piercedTargets.size() > 65536)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "Too many pierced targets in snapshot", "slots.piercedTargets"));
            auto worldBodies = slot.state->piercedWorldBodies;
            if (worldBodies.size() > 65536)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "Too many pierced physics bodies", "slots.piercedWorldBodies"));
            std::sort(worldBodies.begin(), worldBodies.end(), [](const auto& a, const auto& b) {
                return a.world < b.world || (a.world == b.world && a.body < b.body);
            });
            if (std::any_of(worldBodies.begin(), worldBodies.end(),
                            [](const auto& id) {
                                return (id.world >> 32) == 0 || (id.body >> 32) == 0 ||
                                       (id.world & 0xffffffffu) == 0xffffffffu ||
                                       (id.body & 0xffffffffu) == 0xffffffffu;
                            }) ||
                std::adjacent_find(worldBodies.begin(), worldBodies.end()) != worldBodies.end())
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "Invalid or duplicate pierced physics identity",
                                                               "slots.piercedWorldBodies"));
            auto pierced = slot.state->piercedTargets;
            std::sort(pierced.begin(), pierced.end(), [](const auto& a, const auto& b) {
                if (a.table != b.table) return std::less<ecs::Table*>{}(a.table, b.table);
                if (a.type != b.type) return a.type < b.type;
                return a.id < b.id || (a.id == b.id && a.generation < b.generation);
            });
            if (std::any_of(pierced.begin(), pierced.end(), [](const auto& target) { return target.id == 0; }) ||
                std::adjacent_find(pierced.begin(), pierced.end(), sameTarget) != pierced.end())
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "Invalid or duplicate pierced target in snapshot",
                                                               "slots.piercedTargets"));
            const auto registered = findRecipe(slot.state->recipeId);
            if (!registered)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "Carrier snapshot references an unregistered recipe", "recipeId"));
            const auto context =
                std::find_if(snapshot.recipes.begin(), snapshot.recipes.end(),
                             [&](const CarrierRecipe& recipe) { return recipe.id == slot.state->recipeId; });
            if (context == snapshot.recipes.end() || *context != *registered)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict, "Carrier snapshot recipe context differs from the runtime", "recipes"));
            CarrierSpawnRequest request{slot.state->motion.position, slot.state->heading, slot.state->target,
                                        slot.state->source};
            auto                valid = validateSpawn(*registered, request);
            if (!valid) return valid;
            if (slot.state->lifetime != registered->lifetime)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "Carrier snapshot lifetime differs from its recipe", "lifetime"));
            ++active;
        }
        staged.push_back({slot.generation, slot.state});
    }
    simulation_  = snapshot.simulation;
    slots_       = std::move(staged);
    activeCount_ = active;
    return Result<void>::success();
}

}  // namespace eve::weapon
