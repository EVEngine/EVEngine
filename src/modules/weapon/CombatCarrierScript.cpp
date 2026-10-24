#include "weapon/CombatCarrierScript.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "common/Time.h"
#include "common/Value.h"
#include "common/WorldSphereSweep.h"
#include "weapon/CarrierRecipeCodec.h"
#include "weapon/CombatCarrier.h"
#include "weapon/SpellFragmentCompiler.h"
#include "weapon/Weapon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eve::weapon {
namespace {

Result<Value> nullRuntime() {
    return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                    "combat carrier runtime must not be null", "runtime", {},
                                                    "weapon.carrier.squirrel"));
}

class ScriptTargetProvider final : public IProjectileTargetProvider {
public:
    [[nodiscard]] Result<ProjectilePoint> position(ecs::EntityHandle target) const override {
        const auto key = keyOf(target);
        auto       it  = positions_.find(key);
        if (it == positions_.end())
            return Result<ProjectilePoint>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "homing target position is not registered", "target", {},
                                  "weapon.carrier.squirrel"));
        return Result<ProjectilePoint>::success(it->second);
    }

    void set(ecs::EntityHandle target, ProjectilePoint point) { positions_[keyOf(target)] = point; }
    void clear() { positions_.clear(); }
    [[nodiscard]] bool empty() const noexcept { return positions_.empty(); }

private:
    [[nodiscard]] static std::uint64_t keyOf(ecs::EntityHandle target) {
        return (static_cast<std::uint64_t>(target.generation) << 32) | target.id;
    }

    std::map<std::uint64_t, ProjectilePoint> positions_;
};

class ScriptWorldHitProbe final : public ICarrierHitProbe {
public:
    explicit ScriptWorldHitProbe(std::unique_ptr<IWorldSphereSweep> source) : source_(std::move(source)) {}
    Result<std::vector<Contact>> query(CarrierHandle, const CarrierMotion& previous,
                                       const CarrierMotion& current) const override {
        auto hits = source_->query(previous.position.x, previous.position.y, previous.position.z, current.position.x,
                                   current.position.y, current.position.z);
        if (!hits) return Result<std::vector<Contact>>::failure(hits.status());
        std::vector<Contact> contacts;
        for (const auto& hit : hits.value()) {
            Contact c;
            c.point        = {hit.x, hit.y, hit.z};
            c.normal       = {hit.nx, hit.ny, hit.nz};
            c.fraction     = hit.fraction;
            c.worldContact = hit;
            contacts.push_back(c);
        }
        return Result<std::vector<Contact>>::success(std::move(contacts));
    }

private:
    std::unique_ptr<IWorldSphereSweep> source_;
};

class ScriptCircleHitProbe final : public ICarrierHitProbe {
public:
    struct Target {
        ecs::EntityHandle id{};
        ProjectilePoint   center{};
        double            radius = 0.5;
    };

    [[nodiscard]] Result<std::vector<Contact>> query(CarrierHandle, const CarrierMotion& previous,
                                                     const CarrierMotion& current) const override {
        std::vector<Contact> contacts;
        for (const auto& target : targets_) {
            const double ax = previous.position.x - target.center.x;
            const double ay = previous.position.y - target.center.y;
            const double az = previous.position.z - target.center.z;
            const double bx = current.position.x - target.center.x;
            const double by = current.position.y - target.center.y;
            const double bz = current.position.z - target.center.z;
            const double abx = bx - ax;
            const double aby = by - ay;
            const double abz = bz - az;
            const double ab2 = abx * abx + aby * aby + abz * abz;
            const double radius2       = target.radius * target.radius;
            const double startDistance = ax * ax + ay * ay + az * az;
            const double projection    = ax * abx + ay * aby + az * abz;
            if (ab2 <= 1e-12) continue;
            double t = 0.0;
            if (startDistance >= radius2 - 1e-9) {
                // At a surface while departing: do not bounce again on the next frame.
                if (projection >= 0.0) continue;
                const double discriminant = projection * projection - ab2 * (startDistance - radius2);
                if (discriminant < 0.0) continue;
                t = (-projection - std::sqrt(discriminant)) / ab2;
                if (t < -1e-9 || t > 1.0) continue;
                t = std::max(0.0, t);
            }
            const double cx = ax + abx * t;
            const double cy = ay + aby * t;
            const double cz = az + abz * t;
            const double dist2 = cx * cx + cy * cy + cz * cz;
            if (dist2 <= radius2 + 1e-9 * std::max(1.0, radius2)) {
                Contact contact;
                contact.fraction = t;
                contact.target = target.id;
                contact.point  = {target.center.x + cx, target.center.y + cy, target.center.z + cz};
                const double len = std::sqrt(dist2);
                if (len > 1e-9) {
                    contact.normal = {cx / len, cy / len, cz / len};
                } else {
                    contact.normal = {-1.0, 0.0, 0.0};
                }
                contacts.push_back(contact);
            }
        }
        return Result<std::vector<Contact>>::success(std::move(contacts));
    }

    void add(Target target) { targets_.push_back(target); }
    void clear() { targets_.clear(); }
    [[nodiscard]] bool empty() const noexcept { return targets_.empty(); }

private:
    std::vector<Target> targets_;
};

Value encodeHandle(CarrierHandle handle) {
    Value::Object object;
    object.emplace("slot", Value(static_cast<std::int64_t>(handle.slot)));
    object.emplace("generation", Value(static_cast<std::int64_t>(handle.generation)));
    return Value(std::move(object));
}

const char* triggerKindName(CarrierTriggerKind kind) {
    switch (kind) {
        case CarrierTriggerKind::OnDistance: return "onDistance";
        case CarrierTriggerKind::OnSpeedBelow: return "onSpeedBelow";
        case CarrierTriggerKind::OnApex: return "onApex";
        case CarrierTriggerKind::OnExpire: return "onExpire";
        case CarrierTriggerKind::OnFuse: return "onFuse";
        case CarrierTriggerKind::OnInterval: return "onInterval";
        case CarrierTriggerKind::OnHit: return "onHit";
        case CarrierTriggerKind::OnProximity: return "onProximity";
    }
    return "onExpire";
}

const char* impactKindName(CarrierImpactKind kind) {
    switch (kind) {
        case CarrierImpactKind::EmitHit: return "emitHit";
        case CarrierImpactKind::Splash: return "splash";
        case CarrierImpactKind::Pierce: return "pierce";
        case CarrierImpactKind::Bounce: return "bounce";
        case CarrierImpactKind::Release: return "release";
        case CarrierImpactKind::SpawnChild: return "spawnChild";
        case CarrierImpactKind::EmitTrigger: return "emitTrigger";
    }
    return "release";
}

Value encodeEvent(const CarrierEvent& event) {
    Value::Object object;
    object.emplace("slot", Value(static_cast<std::int64_t>(event.carrier.slot)));
    object.emplace("generation", Value(static_cast<std::int64_t>(event.carrier.generation)));
    object.emplace("recipeId", Value(event.recipeId.format()));
    object.emplace("trigger", Value(std::string(triggerKindName(event.trigger))));
    object.emplace("impact", Value(std::string(impactKindName(event.impact))));
    object.emplace("age", Value(event.age.seconds()));
    if (event.worldContact) {
        const auto& hit = *event.worldContact;
        object.emplace("bodyId", Value(static_cast<std::int64_t>(hit.bodyId)));
        object.emplace("shapeId", Value(static_cast<std::int64_t>(hit.shapeId)));
        object.emplace("bodyIndex", Value(static_cast<std::int64_t>(hit.identity.body & 0xffffffffu)));
        object.emplace("bodyGeneration", Value(static_cast<std::int64_t>(hit.identity.body >> 32)));
        object.emplace("worldIndex", Value(static_cast<std::int64_t>(hit.identity.world & 0xffffffffu)));
        object.emplace("worldGeneration", Value(static_cast<std::int64_t>(hit.identity.world >> 32)));
        object.emplace("fraction", Value(hit.fraction));
    }
    object.emplace("damage", Value(event.damage));
    object.emplace("splashRadius", Value(event.splashRadius));
    object.emplace("damageType", Value(event.damageType));
    object.emplace("element", Value(event.element));
    object.emplace("x", Value(event.position.x));
    object.emplace("y", Value(event.position.y));
    object.emplace("z", Value(event.position.z));
    object.emplace("nx", Value(event.normal.x));
    object.emplace("ny", Value(event.normal.y));
    object.emplace("nz", Value(event.normal.z));
    object.emplace("sourceId", Value(static_cast<std::int64_t>(event.source.id)));
    object.emplace("sourceGeneration", Value(static_cast<std::int64_t>(event.source.generation)));
    object.emplace("targetId", Value(static_cast<std::int64_t>(event.target.id)));
    object.emplace("targetGeneration", Value(static_cast<std::int64_t>(event.target.generation)));
    return Value(std::move(object));
}

Value encodeFrame(const CarrierFrame& frame) {
    Value::Object object;
    Value::Array advanced;
    for (const auto& handle : frame.advanced) advanced.push_back(encodeHandle(handle));
    Value::Array released;
    for (const auto& handle : frame.released) released.push_back(encodeHandle(handle));
    Value::Array events;
    for (const auto& event : frame.events) events.push_back(encodeEvent(event));
    object.emplace("advanced", Value(std::move(advanced)));
    object.emplace("released", Value(std::move(released)));
    object.emplace("events", Value(std::move(events)));
    object.emplace("eventCount", Value(static_cast<std::int64_t>(frame.events.size())));
    object.emplace("activeAdvanced", Value(static_cast<std::int64_t>(frame.advanced.size())));
    return Value(std::move(object));
}

Value encodePlan(const SpellCastPlan& plan) {
    Value::Object object;
    object.emplace("recipeId", Value(plan.recipe.id.format()));
    object.emplace("hasVolley", Value(plan.volley.has_value()));
    if (plan.volley.has_value()) {
        object.emplace("volleyCount", Value(static_cast<std::int64_t>(plan.volley->count)));
        object.emplace("volleyPattern",
                       Value(std::string(plan.volley->pattern == CarrierVolleyPattern::Ring ? "ring" : "fan")));
        object.emplace("volleySpread", Value(plan.volley->spreadDegrees));
    }
    return Value(std::move(object));
}

class ScriptMotionFunction final : public ICarrierMotionFunction {
public:
    explicit ScriptMotionFunction(ssq::Function callback) : callback_(std::move(callback)) {}
    Result<CarrierMotion> evaluate(const CarrierState& before, const CarrierMotion& proposed,
                                   Duration step) const override {
        const auto vm  = callback_.getHandle();
        const auto top = sq_gettop(vm);
        struct StackGuard {
            HSQUIRRELVM vm;
            SQInteger   top;
            ~StackGuard() { sq_settop(vm, top); }
        } guard{vm, top};
        auto table = [vm](const CarrierMotion& motion) {
            ssq::Table result(vm);
            result.set("x", static_cast<float>(motion.position.x));
            result.set("y", static_cast<float>(motion.position.y));
            result.set("z", static_cast<float>(motion.position.z));
            result.set("vx", static_cast<float>(motion.velocity.x));
            result.set("vy", static_cast<float>(motion.velocity.y));
            result.set("vz", static_cast<float>(motion.velocity.z));
            return result;
        };
        auto state = table(before.motion);
        state.set("age", static_cast<float>(before.age.seconds()));
        state.set("lifetime", static_cast<float>(before.lifetime.seconds()));
        state.set("distanceTravelled", static_cast<float>(before.distanceTravelled));
        state.set("recipeId", before.recipeId.format());
        state.set("slot", static_cast<std::int64_t>(before.handle.slot));
        state.set("generation", static_cast<std::int64_t>(before.handle.generation));
        if (SQ_FAILED(script::callFunctionResult(vm, callback_.getRaw(), script::rootTable, state, table(proposed),
                                                 static_cast<float>(step.seconds()))))
            return Result<CarrierMotion>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "Custom motion callback threw; see script diagnostic", "motionFunction"));
        if (sq_gettype(vm, -1) == OT_NULL) return Result<CarrierMotion>::success(proposed);
        auto value = script::valueFromSquirrel(vm, -1);
        if (!value) return Result<CarrierMotion>::failure(value.status());
        const auto* object = value.value().getIf<Value::Object>();
        if (!object)
            return Result<CarrierMotion>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "Motion function must return null or a position/velocity table", "motionFunction"));
        CarrierMotion output    = proposed;
        int           positions = 0, velocities = 0;
        for (const auto& [name, field] : *object) {
            double number = 0.0;
            if (const auto* real = field.getIf<double>())
                number = *real;
            else if (const auto* integer = field.getIf<std::int64_t>())
                number = static_cast<double>(*integer);
            else
                return Result<CarrierMotion>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Motion coordinate must be a number", name));
            if (name == "x") {
                output.position.x = number;
                ++positions;
            } else if (name == "y") {
                output.position.y = number;
                ++positions;
            } else if (name == "z") {
                output.position.z = number;
                ++positions;
            } else if (name == "vx") {
                output.velocity.x = number;
                ++velocities;
            } else if (name == "vy") {
                output.velocity.y = number;
                ++velocities;
            } else if (name == "vz") {
                output.velocity.z = number;
                ++velocities;
            } else
                return Result<CarrierMotion>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown motion output field", name));
        }
        if ((positions != 0 && positions != 3) || (velocities != 0 && velocities != 3) ||
            (positions == 0 && velocities == 0))
            return Result<CarrierMotion>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Return complete x/y/z and/or vx/vy/vz groups", "motionFunction"));
        // Returning the projected proposal is an identity operation despite the VM's float precision.
        if (positions == 3 && velocities == 3 && output.position.x == static_cast<float>(proposed.position.x) &&
            output.position.y == static_cast<float>(proposed.position.y) &&
            output.position.z == static_cast<float>(proposed.position.z) &&
            output.velocity.x == static_cast<float>(proposed.velocity.x) &&
            output.velocity.y == static_cast<float>(proposed.velocity.y) &&
            output.velocity.z == static_cast<float>(proposed.velocity.z))
            return Result<CarrierMotion>::success(proposed);
        const double dt = step.seconds();
        if (positions == 0)
            output.position = {before.motion.position.x + output.velocity.x * dt,
                               before.motion.position.y + output.velocity.y * dt,
                               before.motion.position.z + output.velocity.z * dt};
        if (velocities == 0)
            output.velocity = {(output.position.x - before.motion.position.x) / dt,
                               (output.position.y - before.motion.position.y) / dt,
                               (output.position.z - before.motion.position.z) / dt};
        return Result<CarrierMotion>::success(output);
    }

private:
    ssq::Function callback_;
};

class ScriptCombatCarrierRuntime {
public:
    ScriptCombatCarrierRuntime() = default;

    [[nodiscard]] Result<Value> configurePool(std::int64_t capacity) {
        if (evaluating_) return mutationConflict();
        if (capacity < 1 || capacity > 1048576)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "pool capacity must be in 1..1048576", "capacity", {},
                                                            "weapon.carrier.squirrel"));
        auto configured = runtime_.configurePool(static_cast<std::uint32_t>(capacity));
        if (!configured) return Result<Value>::failure(configured.status());
        Value::Object object;
        object.emplace("capacity", Value(static_cast<std::int64_t>(runtime_.capacity())));
        return Result<Value>::success(Value(std::move(object)));
    }

    [[nodiscard]] Result<Value> configureSimulation(double maxStepSeconds, std::int64_t maxIterations,
                                                    std::int64_t maxCollisions, std::int64_t maxTriggerFirings) {
        if (evaluating_) return mutationConflict();
        auto duration = Duration::fromSeconds(maxStepSeconds);
        if (!duration) return Result<Value>::failure(duration.status());
        if (maxIterations < 1 || maxIterations > 65536 || maxCollisions < 1 || maxCollisions > 65536 ||
            maxTriggerFirings < 1 || maxTriggerFirings > 65536)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Simulation budgets must be in 1..65536", "simulation"));
        CarrierSimulationSettings settings{duration.value(), static_cast<std::uint32_t>(maxIterations),
                                           static_cast<std::uint32_t>(maxCollisions),
                                           static_cast<std::uint32_t>(maxTriggerFirings)};
        auto                      configured = runtime_.configureSimulation(settings);
        if (!configured) return Result<Value>::failure(configured.status());
        return Result<Value>::success(Value::object({}));
    }

    [[nodiscard]] Result<Value> registerRecipeValue(const Value& value) {
        if (evaluating_) return mutationConflict();
        auto recipe = decodeCarrierRecipe(value);
        if (!recipe.ok()) return Result<Value>::failure(recipe.status());
        CarrierRecipe owned = std::move(recipe).takeValue();
        for (const auto& op : owned.motionOps) {
            if (op.kind == CarrierMotionOpKind::SteerAvoidBody) {
                return Result<Value>::failure(Diagnostic::error(
                    DiagnosticCode::Unsupported,
                    "SteerAvoidBody recipes are not supported by the script carrier runtime; omit avoidBody or use C++",
                    "motion", {}, "weapon.carrier.squirrel"));
            }
        }
        const std::string id = owned.id.format();
        auto registered = runtime_.registerRecipe(owned);
        if (!registered) return Result<Value>::failure(registered.status());
        Value::Object object;
        object.emplace("recipeId", Value(id));
        return Result<Value>::success(Value(std::move(object)));
    }

    [[nodiscard]] Result<Value> registerRecipeJson(const std::string& json) {
        if (evaluating_) return mutationConflict();
        auto parsed = Value::fromJson(json);
        if (!parsed.ok()) return Result<Value>::failure(parsed.status());
        return registerRecipeValue(parsed.value());
    }

    [[nodiscard]] Result<Value> compileFragmentsValue(const Value& fragments) {
        if (evaluating_) return mutationConflict();
        auto plan = compileSpellFragments(fragments);
        if (!plan.ok()) return Result<Value>::failure(plan.status());
        return registerPlan(std::move(plan).takeValue());
    }

    [[nodiscard]] Result<Value> compileFragmentsJson(const std::string& json) {
        if (evaluating_) return mutationConflict();
        auto plan = compileSpellFragmentsJson(json);
        if (!plan.ok()) return Result<Value>::failure(plan.status());
        return registerPlan(std::move(plan).takeValue());
    }

    [[nodiscard]] Result<Value> castValue(const Value& fragments, double x, double y, double z, double dx, double dy,
                                          double dz, std::int64_t targetId, std::int64_t targetGeneration) {
        if (evaluating_) return mutationConflict();
        auto plan = compileSpellFragments(fragments);
        if (!plan.ok()) return Result<Value>::failure(plan.status());
        SpellCastPlan owned      = std::move(plan).takeValue();
        auto          uniquified = uniquifyRecipeId(owned.recipe.id);
        if (!uniquified.ok()) return Result<Value>::failure(uniquified.status());
        owned.recipe.id = uniquified.value();

        bool needsTarget = false;
        for (const auto& op : owned.recipe.motionOps) {
            if ((op.kind == CarrierMotionOpKind::SteerHoming || op.kind == CarrierMotionOpKind::SteerOrbit ||
                 op.kind == CarrierMotionOpKind::SteerPredictive))
                needsTarget = true;
        }
        for (const auto& trigger : owned.recipe.triggers) {
            if (trigger.kind == CarrierTriggerKind::OnProximity) needsTarget = true;
        }
        if (needsTarget && targetId <= 0) {
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "Homing/proximity casts require castAtTarget with a positive target id", "target", {},
                                  "weapon.carrier.squirrel"));
        }

        auto registered = registerPlan(owned);
        if (!registered.ok()) return registered;
        return spawnPlan(owned, x, y, z, dx, dy, dz, targetId, targetGeneration);
    }

    [[nodiscard]] Result<Value> castJson(const std::string& json, double x, double y, double z, double dx, double dy,
                                         double dz, std::int64_t targetId, std::int64_t targetGeneration) {
        if (evaluating_) return mutationConflict();
        auto parsed = Value::fromJson(json);
        if (!parsed.ok()) return Result<Value>::failure(parsed.status());
        return castValue(parsed.value(), x, y, z, dx, dy, dz, targetId, targetGeneration);
    }

    [[nodiscard]] static Result<void> validateHandleParts(std::int64_t id, std::int64_t generation,
                                                          std::string_view path) {
        if (id < 0 || generation < 0 || static_cast<std::uint64_t>(id) > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<std::uint64_t>(generation) > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "handle id/generation must fit in uint32", std::string(path),
                                                           {}, "weapon.carrier.squirrel"));
        }
        return Result<void>::success();
    }

    [[nodiscard]] static Result<void> validatePoint(double x, double y, double z, std::string_view path) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "coordinates must be finite", std::string(path), {},
                                                           "weapon.carrier.squirrel"));
        }
        return Result<void>::success();
    }

    [[nodiscard]] Result<Value> spawn(const std::string& recipeId, double x, double y, double z, double dx, double dy,
                                      double dz, std::int64_t targetId, std::int64_t targetGeneration) {
        if (evaluating_) return mutationConflict();
        auto id = LogicalId::parse(recipeId);
        if (!id.has_value())
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "recipe id must be namespace:name", "recipeId", {},
                                                            "weapon.carrier.squirrel"));
        if (auto point = validatePoint(x, y, z, "position"); !point) return Result<Value>::failure(point.status());
        if (auto point = validatePoint(dx, dy, dz, "direction"); !point) return Result<Value>::failure(point.status());
        CarrierSpawnRequest request;
        request.position  = {x, y, z};
        request.direction = {dx, dy, dz};
        if (targetId > 0) {
            auto handle = validateHandleParts(targetId, targetGeneration, "target");
            if (!handle) return Result<Value>::failure(handle.status());
            ecs::EntityHandle target;
            target.id         = static_cast<std::uint32_t>(targetId);
            target.generation = static_cast<std::uint32_t>(targetGeneration);
            request.target    = target;
        }
        auto spawned = runtime_.spawn(*id, request);
        if (!spawned.ok()) return Result<Value>::failure(spawned.status());
        Value::Object object;
        object.emplace("handle", encodeHandle(spawned.value()));
        object.emplace("activeCount", Value(static_cast<std::int64_t>(runtime_.activeCount())));
        return Result<Value>::success(Value(std::move(object)));
    }

    [[nodiscard]] Result<Value> spawnVolley(const std::string& recipeId, double x, double y, double z, double dx,
                                            double dy, double dz, std::int64_t count, const std::string& pattern,
                                            double spread, std::int64_t targetId, std::int64_t targetGeneration) {
        if (evaluating_) return mutationConflict();
        auto id = LogicalId::parse(recipeId);
        if (!id.has_value())
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "recipe id must be namespace:name", "recipeId", {},
                                                            "weapon.carrier.squirrel"));
        if (auto point = validatePoint(x, y, z, "position"); !point) return Result<Value>::failure(point.status());
        if (auto point = validatePoint(dx, dy, dz, "direction"); !point) return Result<Value>::failure(point.status());
        CarrierVolleySpec volley;
        volley.count         = static_cast<int>(count);
        volley.spreadDegrees = spread;
        volley.pattern       = (pattern == "ring" || pattern == "Ring") ? CarrierVolleyPattern::Ring
                                                                        : CarrierVolleyPattern::Fan;
        CarrierSpawnRequest request;
        request.position  = {x, y, z};
        request.direction = {dx, dy, dz};
        if (targetId > 0) {
            auto handle = validateHandleParts(targetId, targetGeneration, "target");
            if (!handle) return Result<Value>::failure(handle.status());
            ecs::EntityHandle target;
            target.id         = static_cast<std::uint32_t>(targetId);
            target.generation = static_cast<std::uint32_t>(targetGeneration);
            request.target    = target;
        }
        auto spawned = runtime_.spawnVolley(*id, request, volley);
        if (!spawned.ok()) return Result<Value>::failure(spawned.status());
        Value::Array handles;
        for (const auto& handle : spawned.value()) handles.push_back(encodeHandle(handle));
        Value::Object object;
        object.emplace("handles", Value(std::move(handles)));
        object.emplace("count", Value(static_cast<std::int64_t>(spawned.value().size())));
        object.emplace("activeCount", Value(static_cast<std::int64_t>(runtime_.activeCount())));
        return Result<Value>::success(Value(std::move(object)));
    }

    [[nodiscard]] Result<Value> states() const {
        Value::Array values;
        for (const auto& state : runtime_.states()) {
            Value::Object entry;
            entry.emplace("handle", encodeHandle(state.handle));
            entry.emplace("recipeId", Value(state.recipeId.format()));
            entry.emplace("x", Value(state.motion.position.x));
            entry.emplace("y", Value(state.motion.position.y));
            entry.emplace("z", Value(state.motion.position.z));
            entry.emplace("vx", Value(state.motion.velocity.x));
            entry.emplace("vy", Value(state.motion.velocity.y));
            entry.emplace("vz", Value(state.motion.velocity.z));
            entry.emplace("age", Value(state.age.seconds()));
            entry.emplace("distanceTravelled", Value(state.distanceTravelled));
            values.emplace_back(std::move(entry));
        }
        return Result<Value>::success(Value(std::move(values)));
    }

    [[nodiscard]] Result<Value> bindWorld3D(const std::string& token, float radius, std::int64_t category,
                                            std::int64_t mask, std::int64_t ignoredBody) {
        if (evaluating_) return mutationConflict();
        if (runtime_.activeCount() != 0)
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Rebind only with an empty projectile pool", "worldSweep"));
        if (category < -1 || category > UINT32_MAX || mask < -1 || mask > UINT32_MAX || ignoredBody < -1 ||
            ignoredBody > INT32_MAX)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Invalid category, mask or ignored body", "worldSweep"));
        auto* factory = cap::query<IWorldSphereSweepFactory>();
        if (!factory)
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "Physics world sweep provider is unavailable", "worldSweep"));
        auto bound = factory->bind(token, {radius, static_cast<std::uint32_t>(category),
                                           static_cast<std::uint32_t>(mask), static_cast<int>(ignoredBody)});
        if (!bound) return Result<Value>::failure(bound.status());
        worldHits_ = std::make_unique<ScriptWorldHitProbe>(std::move(bound).takeValue());
        return Result<Value>::success(Value{});
    }
    [[nodiscard]] Result<Value> clearWorld3D() {
        if (evaluating_) return mutationConflict();
        if (runtime_.activeCount() != 0)
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Unbind only with an empty projectile pool", "worldSweep"));
        worldHits_.reset();
        return Result<Value>::success(Value{});
    }

    [[nodiscard]] Result<Value> update(double seconds) {
        if (evaluating_) return mutationConflict();
        auto delta = Duration::fromSeconds(seconds);
        if (!delta) return Result<Value>::failure(delta.status());
        return finishFrame(runtime_.update(delta.value(), &targets_, &hits_, nullptr, worldHits_.get()));
    }

    [[nodiscard]] Result<Value> updateWithMotion(double seconds, ssq::Object callback) {
        if (evaluating_) return mutationConflict();
        if (callback.getType() != ssq::Type::CLOSURE && callback.getType() != ssq::Type::NATIVECLOSURE)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Motion function must be callable", "motionFunction"));
        auto delta = Duration::fromSeconds(seconds);
        if (!delta) return Result<Value>::failure(delta.status());
        struct Guard {
            bool& flag;
            ~Guard() { flag = false; }
        } guard{evaluating_};
        evaluating_ = true;
        ScriptMotionFunction function(callback.toFunction());
        return finishFrame(
            runtime_.updateWithMotion(delta.value(), function, &targets_, &hits_, nullptr, worldHits_.get()));
    }

    [[nodiscard]] Result<Value> addHitTarget(std::int64_t id, std::int64_t generation, double x, double y, double z,
                                             double radius) {
        if (evaluating_) return mutationConflict();
        auto handle = validateHandleParts(id, generation, "target");
        if (!handle) return Result<Value>::failure(handle.status());
        auto point = validatePoint(x, y, z, "target");
        if (!point) return Result<Value>::failure(point.status());
        if (!(radius > 0.0) || !std::isfinite(radius))
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "hit target requires positive finite radius", "radius", {},
                                                            "weapon.carrier.squirrel"));
        ScriptCircleHitProbe::Target target;
        target.id.id         = static_cast<std::uint32_t>(id);
        target.id.generation = static_cast<std::uint32_t>(generation);
        target.center        = {x, y, z};
        target.radius        = radius;
        hits_.add(target);
        Value::Object object;
        object.emplace("count", Value(static_cast<std::int64_t>(1)));
        return Result<Value>::success(Value(std::move(object)));
    }

    [[nodiscard]] Result<Value> clearHitTargets() {
        if (evaluating_) return mutationConflict();
        hits_.clear();
        return Result<Value>::success(Value::object({}));
    }

    [[nodiscard]] Result<Value> setTargetPosition(std::int64_t id, std::int64_t generation, double x, double y,
                                                  double z) {
        if (evaluating_) return mutationConflict();
        auto handle = validateHandleParts(id, generation, "target");
        if (!handle) return Result<Value>::failure(handle.status());
        auto point = validatePoint(x, y, z, "target");
        if (!point) return Result<Value>::failure(point.status());
        ecs::EntityHandle target;
        target.id         = static_cast<std::uint32_t>(id);
        target.generation = static_cast<std::uint32_t>(generation);
        targets_.set(target, {x, y, z});
        return Result<Value>::success(Value::object({}));
    }

    [[nodiscard]] Result<Value> clearTargets() {
        if (evaluating_) return mutationConflict();
        targets_.clear();
        return Result<Value>::success(Value::object({}));
    }

    [[nodiscard]] std::int64_t activeCount() const noexcept {
        return static_cast<std::int64_t>(runtime_.activeCount());
    }

    [[nodiscard]] std::int64_t capacity() const noexcept { return static_cast<std::int64_t>(runtime_.capacity()); }

private:
    bool                               evaluating_ = false;
    [[nodiscard]] static Result<Value> mutationConflict() {
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Reentrant carrier mutation is forbidden", "runtime"));
    }
    [[nodiscard]] Result<Value> finishFrame(Result<CarrierFrame> frame) {
        if (!frame) return Result<Value>::failure(frame.status());
        lastFrame_    = frame.value();
        Value encoded = encodeFrame(lastFrame_);
        if (auto* object = encoded.getIf<Value::Object>())
            object->emplace("activeCount", Value(static_cast<std::int64_t>(runtime_.activeCount())));
        return Result<Value>::success(std::move(encoded));
    }

    [[nodiscard]] Result<LogicalId> uniquifyRecipeId(const LogicalId& base) {
        ++castSeq_;
        const std::string uniqueName = std::string(base.name()) + "-c" + std::to_string(castSeq_);
        auto              parsed     = LogicalId::fromParts(base.namespaceName(), uniqueName);
        if (!parsed.has_value())
            return Result<LogicalId>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "unable to allocate unique cast recipe id", "recipeId",
                                                                {}, "weapon.carrier.squirrel"));
        return Result<LogicalId>::success(std::move(*parsed));
    }

    [[nodiscard]] Result<Value> registerPlan(SpellCastPlan plan) {
        for (const auto& op : plan.recipe.motionOps) {
            if (op.kind == CarrierMotionOpKind::SteerAvoidBody) {
                return Result<Value>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported,
                                      "SteerAvoidBody recipes are not supported by the script carrier runtime",
                                      "motion", {}, "weapon.carrier.squirrel"));
            }
        }
        for (const auto& dependent : plan.dependentRecipes) {
            auto registered = runtime_.registerRecipe(dependent);
            if (!registered) return Result<Value>::failure(registered.status());
        }
        auto registered = runtime_.registerRecipe(plan.recipe);
        if (!registered) return Result<Value>::failure(registered.status());
        return Result<Value>::success(encodePlan(plan));
    }

    [[nodiscard]] Result<Value> spawnPlan(const SpellCastPlan& plan, double x, double y, double z, double dx, double dy,
                                          double dz, std::int64_t targetId, std::int64_t targetGeneration) {
        auto spawned = plan.volley.has_value()
                           ? spawnVolley(plan.recipe.id.format(), x, y, z, dx, dy, dz, plan.volley->count,
                                         plan.volley->pattern == CarrierVolleyPattern::Ring ? "ring" : "fan",
                                         plan.volley->spreadDegrees, targetId, targetGeneration)
                           : spawn(plan.recipe.id.format(), x, y, z, dx, dy, dz, targetId, targetGeneration);
        if (!spawned.ok()) return spawned;
        Value owned = std::move(spawned).takeValue();
        if (auto* object = owned.getIf<Value::Object>()) {
            object->emplace("recipeId", Value(plan.recipe.id.format()));
        }
        return Result<Value>::success(std::move(owned));
    }

    CombatCarrierRuntime  runtime_;
    ScriptTargetProvider  targets_;
    ScriptCircleHitProbe  hits_;
    std::unique_ptr<ScriptWorldHitProbe> worldHits_;
    CarrierFrame          lastFrame_{};
    std::uint64_t         castSeq_ = 0;
};

ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

ssq::Table newCarrierRuntime(HSQUIRRELVM vm, std::int64_t capacity) {
    if (capacity < 1) capacity = 256;
    auto runtime    = std::make_unique<ScriptCombatCarrierRuntime>();
    auto configured = runtime->configurePool(capacity);
    if (!configured.ok()) return script::projectStatusResult(vm, configured.status());
    auto object = script::makeOwnedSquirrelInstance<ScriptCombatCarrierRuntime>(vm, std::move(runtime));
    if (!object) return script::projectStatusResult(vm, object.status());
    auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied));
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

Result<Value> valueFromObject(const ssq::Object& object) {
    return script::valueFromSquirrel(object);
}

}  // namespace

void exposeCombatCarrierBindings(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();

    auto runtime = table.addClass<ScriptCombatCarrierRuntime>(
        "CombatCarrierRuntime", std::function<ScriptCombatCarrierRuntime*()>([] { return nullptr; }), true);
    runtime.addFunc("ownership", [](ScriptCombatCarrierRuntime*) { return std::string("owned"); });
    runtime.addFunc("activeCount", [](ScriptCombatCarrierRuntime* self) {
        return self ? self->activeCount() : static_cast<std::int64_t>(0);
    });
    runtime.addFunc("capacity", [](ScriptCombatCarrierRuntime* self) {
        return self ? self->capacity() : static_cast<std::int64_t>(0);
    });
    runtime.addFunc("configurePool", [vm](ScriptCombatCarrierRuntime* self, std::int64_t capacity) {
        return project(vm, self ? self->configurePool(capacity) : nullRuntime());
    });
    runtime.addFunc(
        "configureSimulation", [vm](ScriptCombatCarrierRuntime* self, float maxStepSeconds, std::int64_t maxIterations,
                                    std::int64_t maxCollisions, std::int64_t maxTriggerFirings) {
            return project(
                vm, self ? self->configureSimulation(maxStepSeconds, maxIterations, maxCollisions, maxTriggerFirings)
                         : nullRuntime());
        });
    runtime.addFunc("registerRecipe", [vm](ScriptCombatCarrierRuntime* self, ssq::Object recipe) {
        if (!self) return project(vm, nullRuntime());
        auto value = valueFromObject(recipe);
        if (!value.ok()) return project(vm, Result<Value>::failure(value.status()));
        return project(vm, self->registerRecipeValue(value.value()));
    });
    runtime.addFunc("registerRecipeJson", [vm](ScriptCombatCarrierRuntime* self, const std::string& json) {
        return project(vm, self ? self->registerRecipeJson(json) : nullRuntime());
    });
    runtime.addFunc("compileFragments", [vm](ScriptCombatCarrierRuntime* self, ssq::Object fragments) {
        if (!self) return project(vm, nullRuntime());
        auto value = valueFromObject(fragments);
        if (!value.ok()) return project(vm, Result<Value>::failure(value.status()));
        return project(vm, self->compileFragmentsValue(value.value()));
    });
    runtime.addFunc("compileFragmentsJson", [vm](ScriptCombatCarrierRuntime* self, const std::string& json) {
        return project(vm, self ? self->compileFragmentsJson(json) : nullRuntime());
    });
    runtime.addFunc("cast", [vm](ScriptCombatCarrierRuntime* self, ssq::Object fragments, float x, float y, float z,
                                 float dx, float dy, float dz) {
        if (!self) return project(vm, nullRuntime());
        auto value = valueFromObject(fragments);
        if (!value.ok()) return project(vm, Result<Value>::failure(value.status()));
        return project(vm, self->castValue(value.value(), x, y, z, dx, dy, dz, 0, 0));
    });
    runtime.addFunc("castAtTarget", [vm](ScriptCombatCarrierRuntime* self, ssq::Object fragments, float x, float y,
                                         float z, float dx, float dy, float dz, std::int64_t targetId,
                                         std::int64_t targetGeneration) {
        if (!self) return project(vm, nullRuntime());
        auto value = valueFromObject(fragments);
        if (!value.ok()) return project(vm, Result<Value>::failure(value.status()));
        return project(vm, self->castValue(value.value(), x, y, z, dx, dy, dz, targetId, targetGeneration));
    });
    runtime.addFunc("castJson", [vm](ScriptCombatCarrierRuntime* self, const std::string& json, float x, float y,
                                     float z, float dx, float dy, float dz) {
        return project(vm, self ? self->castJson(json, x, y, z, dx, dy, dz, 0, 0) : nullRuntime());
    });
    runtime.addFunc("spawn", [vm](ScriptCombatCarrierRuntime* self, const std::string& recipeId, float x, float y,
                                  float z, float dx, float dy, float dz) {
        return project(vm, self ? self->spawn(recipeId, x, y, z, dx, dy, dz, 0, 0) : nullRuntime());
    });
    runtime.addFunc("spawnAtTarget", [vm](ScriptCombatCarrierRuntime* self, const std::string& recipeId, float x,
                                          float y, float z, float dx, float dy, float dz, std::int64_t targetId,
                                          std::int64_t targetGeneration) {
        return project(vm, self ? self->spawn(recipeId, x, y, z, dx, dy, dz, targetId, targetGeneration)
                                : nullRuntime());
    });
    runtime.addFunc("spawnVolley", [vm](ScriptCombatCarrierRuntime* self, const std::string& recipeId, float x, float y,
                                        float z, float dx, float dy, float dz, std::int64_t count,
                                        const std::string& pattern, float spread) {
        return project(vm, self ? self->spawnVolley(recipeId, x, y, z, dx, dy, dz, count, pattern, spread, 0, 0)
                                : nullRuntime());
    });
    runtime.addFunc("bindWorld3D", [vm](ScriptCombatCarrierRuntime* self, const std::string& token, float radius,
                                        std::int64_t category, std::int64_t mask, std::int64_t ignoredBody) {
        return project(vm, self ? self->bindWorld3D(token, radius, category, mask, ignoredBody) : nullRuntime());
    });
    runtime.addFunc("clearWorld3D", [vm](ScriptCombatCarrierRuntime* self) {
        return project(vm, self ? self->clearWorld3D() : nullRuntime());
    });
    runtime.addFunc("states", [vm](ScriptCombatCarrierRuntime* self) {
        return project(vm, self ? self->states() : nullRuntime());
    });
    runtime.addFunc("update", [vm](ScriptCombatCarrierRuntime* self, float seconds) {
        return project(vm, self ? self->update(seconds) : nullRuntime());
    });
    runtime.addFunc("updateWithMotion", [vm](ScriptCombatCarrierRuntime* self, float seconds, ssq::Object function) {
        return project(vm, self ? self->updateWithMotion(seconds, std::move(function)) : nullRuntime());
    });
    runtime.addFunc("addHitTarget", [vm](ScriptCombatCarrierRuntime* self, std::int64_t id, std::int64_t generation,
                                         float x, float y, float z, float radius) {
        return project(vm, self ? self->addHitTarget(id, generation, x, y, z, radius) : nullRuntime());
    });
    runtime.addFunc("clearHitTargets", [vm](ScriptCombatCarrierRuntime* self) {
        return project(vm, self ? self->clearHitTargets() : nullRuntime());
    });
    runtime.addFunc("setTargetPosition", [vm](ScriptCombatCarrierRuntime* self, std::int64_t id,
                                              std::int64_t generation, float x, float y, float z) {
        return project(vm, self ? self->setTargetPosition(id, generation, x, y, z) : nullRuntime());
    });
    runtime.addFunc("clearTargets", [vm](ScriptCombatCarrierRuntime* self) {
        return project(vm, self ? self->clearTargets() : nullRuntime());
    });

    // Module methods are registered from Weapon::expose(Class); helpers also on table for discoverability.
    (void)vm;
}

void exposeCombatCarrierModuleMethods(ssq::Class& cls) {
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("newCarrierRuntime", [vm](Weapon*, std::int64_t capacity) { return newCarrierRuntime(vm, capacity); });
    cls.addFunc("newCarrierRuntimeDefault", [vm](Weapon*) { return newCarrierRuntime(vm, 256); });
}

}  // namespace eve::weapon
