#include "weapon/CombatCarrierScript.h"

#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "common/Time.h"
#include "common/Value.h"
#include "weapon/CarrierRecipeCodec.h"
#include "weapon/CombatCarrier.h"
#include "weapon/SpellFragmentCompiler.h"
#include "weapon/Weapon.h"

#include <cmath>
#include <cstdint>
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
            double t = 0.0;
            if (ab2 > 1e-12) {
                t = -(ax * abx + ay * aby + az * abz) / ab2;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
            }
            const double cx = ax + abx * t;
            const double cy = ay + aby * t;
            const double cz = az + abz * t;
            const double dist2 = cx * cx + cy * cy + cz * cz;
            if (dist2 <= target.radius * target.radius) {
                Contact contact;
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

Value encodeEvent(const CarrierEvent& event) {
    Value::Object object;
    object.emplace("slot", Value(static_cast<std::int64_t>(event.carrier.slot)));
    object.emplace("generation", Value(static_cast<std::int64_t>(event.carrier.generation)));
    object.emplace("recipeId", Value(event.recipeId.format()));
    object.emplace("damage", Value(event.damage));
    object.emplace("splashRadius", Value(event.splashRadius));
    object.emplace("damageType", Value(event.damageType));
    object.emplace("element", Value(event.element));
    object.emplace("x", Value(event.position.x));
    object.emplace("y", Value(event.position.y));
    object.emplace("z", Value(event.position.z));
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

class ScriptCombatCarrierRuntime {
public:
    ScriptCombatCarrierRuntime() = default;

    [[nodiscard]] Result<Value> configurePool(std::int64_t capacity) {
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

    [[nodiscard]] Result<Value> registerRecipeValue(const Value& value) {
        auto recipe = decodeCarrierRecipe(value);
        if (!recipe.ok()) return Result<Value>::failure(recipe.status());
        CarrierRecipe owned = std::move(recipe).takeValue();
        const std::string id = owned.id.format();
        auto registered = runtime_.registerRecipe(owned);
        if (!registered) return Result<Value>::failure(registered.status());
        Value::Object object;
        object.emplace("recipeId", Value(id));
        return Result<Value>::success(Value(std::move(object)));
    }

    [[nodiscard]] Result<Value> registerRecipeJson(const std::string& json) {
        auto parsed = Value::fromJson(json);
        if (!parsed.ok()) return Result<Value>::failure(parsed.status());
        return registerRecipeValue(parsed.value());
    }

    [[nodiscard]] Result<Value> compileFragmentsValue(const Value& fragments) {
        auto plan = compileSpellFragments(fragments);
        if (!plan.ok()) return Result<Value>::failure(plan.status());
        return registerPlan(std::move(plan).takeValue());
    }

    [[nodiscard]] Result<Value> compileFragmentsJson(const std::string& json) {
        auto plan = compileSpellFragmentsJson(json);
        if (!plan.ok()) return Result<Value>::failure(plan.status());
        return registerPlan(std::move(plan).takeValue());
    }

    [[nodiscard]] Result<Value> castValue(const Value& fragments, double x, double y, double z, double dx, double dy,
                                          double dz, std::int64_t targetId, std::int64_t targetGeneration) {
        auto plan = compileSpellFragments(fragments);
        if (!plan.ok()) return Result<Value>::failure(plan.status());
        auto registered = registerPlan(plan.value());
        if (!registered.ok()) return registered;
        return spawnPlan(plan.value(), x, y, z, dx, dy, dz, targetId, targetGeneration);
    }

    [[nodiscard]] Result<Value> castJson(const std::string& json, double x, double y, double z, double dx, double dy,
                                         double dz, std::int64_t targetId, std::int64_t targetGeneration) {
        auto parsed = Value::fromJson(json);
        if (!parsed.ok()) return Result<Value>::failure(parsed.status());
        return castValue(parsed.value(), x, y, z, dx, dy, dz, targetId, targetGeneration);
    }

    [[nodiscard]] Result<Value> spawn(const std::string& recipeId, double x, double y, double z, double dx, double dy,
                                      double dz, std::int64_t targetId, std::int64_t targetGeneration) {
        auto id = LogicalId::parse(recipeId);
        if (!id.has_value())
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "recipe id must be namespace:name", "recipeId", {},
                                                            "weapon.carrier.squirrel"));
        CarrierSpawnRequest request;
        request.position  = {x, y, z};
        request.direction = {dx, dy, dz};
        if (targetId > 0) {
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
        auto id = LogicalId::parse(recipeId);
        if (!id.has_value())
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "recipe id must be namespace:name", "recipeId", {},
                                                            "weapon.carrier.squirrel"));
        CarrierVolleySpec volley;
        volley.count         = static_cast<int>(count);
        volley.spreadDegrees = spread;
        volley.pattern       = (pattern == "ring" || pattern == "Ring") ? CarrierVolleyPattern::Ring
                                                                        : CarrierVolleyPattern::Fan;
        CarrierSpawnRequest request;
        request.position  = {x, y, z};
        request.direction = {dx, dy, dz};
        if (targetId > 0) {
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

    [[nodiscard]] Result<Value> update(double seconds) {
        auto delta = Duration::fromSeconds(seconds);
        if (!delta.ok()) return Result<Value>::failure(delta.status());
        const IProjectileTargetProvider* targets = targets_.empty() ? nullptr : &targets_;
        const ICarrierHitProbe*          hits    = hits_.empty() ? nullptr : &hits_;
        auto frame = runtime_.update(delta.value(), targets, hits, nullptr);
        if (!frame.ok()) return Result<Value>::failure(frame.status());
        lastFrame_ = frame.value();
        Value encoded = encodeFrame(lastFrame_);
        if (auto* object = encoded.getIf<Value::Object>()) {
            object->emplace("activeCount", Value(static_cast<std::int64_t>(runtime_.activeCount())));
        }
        return Result<Value>::success(std::move(encoded));
    }

    [[nodiscard]] Result<Value> addHitTarget(std::int64_t id, std::int64_t generation, double x, double y, double z,
                                             double radius) {
        if (id < 0 || generation < 0 || !(radius > 0.0) || !std::isfinite(radius))
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "hit target requires non-negative ids and positive radius",
                                                            "target", {}, "weapon.carrier.squirrel"));
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
        hits_.clear();
        return Result<Value>::success(Value::object({}));
    }

    [[nodiscard]] Result<Value> setTargetPosition(std::int64_t id, std::int64_t generation, double x, double y,
                                                  double z) {
        if (id < 0 || generation < 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "target ids must be non-negative", "target", {},
                                                            "weapon.carrier.squirrel"));
        ecs::EntityHandle target;
        target.id         = static_cast<std::uint32_t>(id);
        target.generation = static_cast<std::uint32_t>(generation);
        targets_.set(target, {x, y, z});
        return Result<Value>::success(Value::object({}));
    }

    [[nodiscard]] Result<Value> clearTargets() {
        targets_.clear();
        return Result<Value>::success(Value::object({}));
    }

    [[nodiscard]] std::int64_t activeCount() const noexcept {
        return static_cast<std::int64_t>(runtime_.activeCount());
    }

    [[nodiscard]] std::int64_t capacity() const noexcept { return static_cast<std::int64_t>(runtime_.capacity()); }

private:
    [[nodiscard]] Result<Value> registerPlan(SpellCastPlan plan) {
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
        if (plan.volley.has_value()) {
            return spawnVolley(plan.recipe.id.format(), x, y, z, dx, dy, dz, plan.volley->count,
                               plan.volley->pattern == CarrierVolleyPattern::Ring ? "ring" : "fan",
                               plan.volley->spreadDegrees, targetId, targetGeneration);
        }
        return spawn(plan.recipe.id.format(), x, y, z, dx, dy, dz, targetId, targetGeneration);
    }

    CombatCarrierRuntime  runtime_;
    ScriptTargetProvider  targets_;
    ScriptCircleHitProbe  hits_;
    CarrierFrame          lastFrame_{};
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
    runtime.addFunc("update", [vm](ScriptCombatCarrierRuntime* self, float seconds) {
        return project(vm, self ? self->update(seconds) : nullRuntime());
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
