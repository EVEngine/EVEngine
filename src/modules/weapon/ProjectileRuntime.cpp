#include "weapon/ProjectileRuntime.h"

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

Result<void> projectileError(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <typename T>
Result<T> projectileValueError(DiagnosticCode code, std::string message, std::string path = {}) {
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

}  // namespace

Result<void> ProjectileDefinition::validate() const {
    if (!id.isValid()) return projectileError(DiagnosticCode::InvalidArgument, "Projectile id must not be empty", "id");
    if (!std::isfinite(speed) || speed <= 0.0)
        return projectileError(DiagnosticCode::InvalidArgument, "Projectile speed must be finite and positive",
                               "speed");
    if (!std::isfinite(gravity) || gravity < 0.0)
        return projectileError(DiagnosticCode::InvalidArgument, "Projectile gravity must be finite and non-negative",
                               "gravity");
    if (!std::isfinite(maxTurnRateDegrees) || maxTurnRateDegrees < 0.0)
        return projectileError(DiagnosticCode::InvalidArgument, "Projectile turn rate must be finite and non-negative",
                               "maxTurnRateDegrees");
    if (lifetime <= Duration::zero())
        return projectileError(DiagnosticCode::InvalidArgument, "Projectile lifetime must be positive", "lifetime");
    if (mode == ProjectileMode::Homing && maxTurnRateDegrees <= 0.0)
        return projectileError(DiagnosticCode::InvalidArgument, "Homing projectile turn rate must be positive",
                               "maxTurnRateDegrees");
    return Result<void>::success();
}

ProjectileRuntime::ProjectileRuntime() : slots_(kDefaultCapacity) {}

Result<void> ProjectileRuntime::configurePool(std::uint32_t capacity) {
    if (activeCount_ != 0)
        return projectileError(DiagnosticCode::Conflict, "Projectile pool cannot be resized while active");
    if (capacity == 0 || capacity > kMaximumCapacity)
        return projectileError(DiagnosticCode::InvalidArgument, "Projectile pool capacity must be in 1..1048576",
                               "capacity");
    slots_.assign(capacity, Slot{});
    return Result<void>::success();
}

Result<void> ProjectileRuntime::validateSpawn(const ProjectileDefinition&   definition,
                                              const ProjectileSpawnRequest& request) {
    auto valid = definition.validate();
    if (!valid) return valid;
    if (!finite(request.position) || !finite(request.direction))
        return projectileError(DiagnosticCode::InvalidArgument, "Projectile transform must contain finite values",
                               "request");
    if (length(request.direction) <= 1e-12)
        return projectileError(DiagnosticCode::InvalidArgument, "Projectile direction must be non-zero", "direction");
    if (definition.mode == ProjectileMode::Homing && !request.target)
        return projectileError(DiagnosticCode::InvalidArgument, "Homing projectile requires a target", "target");
    return Result<void>::success();
}

Result<ProjectileHandle> ProjectileRuntime::spawn(const ProjectileDefinition&   definition,
                                                  const ProjectileSpawnRequest& request) {
    auto valid = validateSpawn(definition, request);
    if (!valid) return Result<ProjectileHandle>::failure(valid.status());
    auto free = std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return !slot.state.has_value(); });
    if (free == slots_.end())
        return projectileValueError<ProjectileHandle>(DiagnosticCode::Conflict, "Projectile pool is exhausted");
    const std::size_t index = static_cast<std::size_t>(std::distance(slots_.begin(), free));
    ++free->generation;
    if (free->generation == 0) ++free->generation;
    const ProjectileVector direction = normalized(request.direction);
    ProjectileState        state;
    state.handle       = {static_cast<std::uint32_t>(index), free->generation};
    state.definitionId = definition.id;
    state.mode         = definition.mode;
    state.position     = request.position;
    state.velocity = {direction.x * definition.speed, direction.y * definition.speed, direction.z * definition.speed};
    state.gravity  = definition.gravity;
    state.maxTurnRateDegrees = definition.maxTurnRateDegrees;
    state.lifetime           = definition.lifetime;
    state.target             = request.target;
    free->state              = state;
    ++activeCount_;
    return Result<ProjectileHandle>::success(state.handle);
}

Result<ProjectileUpdate> ProjectileRuntime::update(Duration delta, const IProjectileTargetProvider* targets) {
    if (delta < Duration::zero())
        return projectileValueError<ProjectileUpdate>(DiagnosticCode::InvalidArgument,
                                                      "Projectile delta must be non-negative", "delta");
    if (delta.isZero()) return Result<ProjectileUpdate>::success({}, Status::success(StatusCode::NoOp));

    std::vector<Slot> staged = slots_;
    ProjectileUpdate  update;
    const double      seconds = delta.seconds();
    for (Slot& slot : staged) {
        if (!slot.state) continue;
        ProjectileState& state   = *slot.state;
        auto             nextAge = state.age.tryAdd(delta);
        if (!nextAge) return Result<ProjectileUpdate>::failure(nextAge.status());
        state.age = std::move(nextAge).takeValue();
        if (state.age >= state.lifetime) {
            update.released.push_back(state.handle);
            slot.state.reset();
            continue;
        }
        if (state.mode == ProjectileMode::Homing) {
            if (!targets)
                return projectileValueError<ProjectileUpdate>(DiagnosticCode::Unsupported,
                                                              "Homing projectile target provider is unavailable");
            auto target = targets->position(*state.target);
            if (!target) return Result<ProjectileUpdate>::failure(target.status());
            ProjectileVector desired = directionTo(state.position, target.value());
            if (length(desired) <= 1e-12)
                return projectileValueError<ProjectileUpdate>(DiagnosticCode::Conflict,
                                                              "Homing projectile overlaps its target");
            state.velocity = steer(state.velocity, desired, state.maxTurnRateDegrees * kPi / 180.0 * seconds);
        }
        if (state.mode == ProjectileMode::Ballistic) state.velocity.y -= state.gravity * seconds;
        state.position.x += state.velocity.x * seconds;
        state.position.y += state.velocity.y * seconds;
        state.position.z += state.velocity.z * seconds;
        update.advanced.push_back(state.handle);
    }
    slots_ = std::move(staged);
    activeCount_ -= update.released.size();
    return Result<ProjectileUpdate>::success(std::move(update));
}

Result<void> ProjectileRuntime::release(ProjectileHandle handle) {
    if (handle.slot >= slots_.size() || !slots_[handle.slot].state ||
        slots_[handle.slot].generation != handle.generation)
        return projectileError(DiagnosticCode::StaleHandle, "Projectile handle is stale");
    slots_[handle.slot].state.reset();
    --activeCount_;
    return Result<void>::success();
}

std::optional<ProjectileState> ProjectileRuntime::find(ProjectileHandle handle) const {
    if (handle.slot >= slots_.size() || !slots_[handle.slot].state ||
        slots_[handle.slot].generation != handle.generation)
        return std::nullopt;
    return slots_[handle.slot].state;
}

std::vector<ProjectileState> ProjectileRuntime::states() const {
    std::vector<ProjectileState> result;
    result.reserve(activeCount_);
    for (const Slot& slot : slots_)
        if (slot.state) result.push_back(*slot.state);
    return result;
}

ProjectileRuntimeSnapshot ProjectileRuntime::snapshot() const {
    ProjectileRuntimeSnapshot result;
    result.slots.reserve(slots_.size());
    for (const Slot& slot : slots_) result.slots.push_back({slot.generation, slot.state});
    return result;
}

Result<void> ProjectileRuntime::restore(const ProjectileRuntimeSnapshot& snapshot) {
    if (snapshot.slots.empty() || snapshot.slots.size() > 1048576)
        return projectileError(DiagnosticCode::InvalidArgument,
                               "Projectile snapshot pool capacity is outside 1..1048576");
    std::vector<Slot> staged(snapshot.slots.size());
    std::size_t active = 0;
    for (std::size_t index = 0; index < snapshot.slots.size(); ++index) {
        const auto& input = snapshot.slots[index];
        staged[index].generation = input.generation;
        if (!input.state) continue;
        const auto& state = *input.state;
        const auto finitePoint = [](const ProjectilePoint& point) {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        };
        const auto finiteVector = [](const ProjectileVector& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };
        if (state.handle.slot != index || state.handle.generation != input.generation ||
            !state.definitionId.isValid() || !finitePoint(state.position) || !finiteVector(state.velocity) ||
            !std::isfinite(state.gravity) || state.gravity < 0.0 ||
            !std::isfinite(state.maxTurnRateDegrees) || state.maxTurnRateDegrees < 0.0 ||
            state.age.nanoseconds() < 0 || state.lifetime.nanoseconds() <= 0 || state.age >= state.lifetime ||
            (state.mode == ProjectileMode::Homing && !state.target))
            return projectileError(DiagnosticCode::InvalidArgument,
                                   "Projectile snapshot contains an invalid live slot");
        staged[index].state = state;
        ++active;
    }
    slots_ = std::move(staged);
    activeCount_ = active;
    return Result<void>::success();
}

}  // namespace eve::weapon
