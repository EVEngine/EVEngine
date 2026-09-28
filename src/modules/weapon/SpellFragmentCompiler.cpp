#include "weapon/SpellFragmentCompiler.h"

#include "common/Diagnostic.h"
#include "common/Time.h"
#include "weapon/CarrierRecipeCodec.h"

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace eve::weapon {
namespace {

Result<void> fail(DiagnosticCode code, std::string_view message, std::string_view path) {
    return Result<void>::failure(Diagnostic::error(code, std::string(message), std::string(path), {},
                                                   "weapon.spell.fragments"));
}

template <class T>
Result<T> failT(DiagnosticCode code, std::string_view message, std::string_view path) {
    return Result<T>::failure(Diagnostic::error(code, std::string(message), std::string(path), {},
                                                "weapon.spell.fragments"));
}

const Value* field(const Value::Object& object, const char* name) {
    const auto it = object.find(name);
    return it == object.end() ? nullptr : &it->second;
}

bool readString(const Value::Object& object, const char* name, std::string& output) {
    const auto* value = field(object, name);
    const auto* text  = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    output = *text;
    return true;
}

bool readDouble(const Value::Object& object, const char* name, double& output) {
    const auto* value = field(object, name);
    if (const auto* number = value ? value->getIf<double>() : nullptr) {
        output = *number;
        return std::isfinite(output);
    }
    if (const auto* integer = value ? value->getIf<std::int64_t>() : nullptr) {
        output = static_cast<double>(*integer);
        return std::isfinite(output);
    }
    return false;
}

bool readInt(const Value::Object& object, const char* name, int& output) {
    const auto* value = field(object, name);
    if (const auto* integer = value ? value->getIf<std::int64_t>() : nullptr) {
        if (*integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max()) return false;
        output = static_cast<int>(*integer);
        return true;
    }
    if (const auto* number = value ? value->getIf<double>() : nullptr) {
        if (!std::isfinite(*number) || *number != std::floor(*number)) return false;
        if (*number < static_cast<double>(std::numeric_limits<int>::min()) ||
            *number > static_cast<double>(std::numeric_limits<int>::max()))
            return false;
        output = static_cast<int>(*number);
        return true;
    }
    return false;
}

struct ModifierStack {
    double speedMultiply     = 1.0;
    double speedAdd          = 0.0;
    double damageMultiply    = 1.0;
    double damageAdd         = 0.0;
    double lifetimeMultiply  = 1.0;
    double lifetimeAdd       = 0.0;
    double gravity           = 0.0;
    double homing            = 0.0;
    double accelerate        = 0.0;
    double swayAmplitude     = 0.0;
    double swayFrequency     = 0.0;
    double helixAmplitude    = 0.0;
    double helixFrequency    = 0.0;
    int    pierce            = 0;
    int    bounce            = 0;
    double restitution       = 1.0;
    double splash            = 0.0;
    double fuse              = 0.0;
    std::string element;
    std::string damageType;
    std::optional<CarrierVolleySpec> volley;
};

Result<void> applyModifier(ModifierStack& stack, const Value::Object& object, std::string_view kind,
                           std::string path) {
    if (kind == "homing") {
        double turn = 90.0;
        if (!readDouble(object, "turnRate", turn) && !readDouble(object, "maxTurnRateDegrees", turn))
            return fail(DiagnosticCode::InvalidArgument, "homing requires turnRate", path);
        if (turn <= 0.0) return fail(DiagnosticCode::InvalidArgument, "homing turnRate must be positive", path);
        stack.homing = turn;
        return Result<void>::success();
    }
    if (kind == "gravity") {
        double gravity = 9.8;
        (void)readDouble(object, "gravity", gravity);
        if (gravity < 0.0) return fail(DiagnosticCode::InvalidArgument, "gravity must be non-negative", path);
        stack.gravity = gravity;
        return Result<void>::success();
    }
    if (kind == "accelerate") {
        double acceleration = 0.0;
        if (!readDouble(object, "acceleration", acceleration) || acceleration < 0.0)
            return fail(DiagnosticCode::InvalidArgument, "accelerate requires non-negative acceleration", path);
        stack.accelerate = acceleration;
        return Result<void>::success();
    }
    if (kind == "sway") {
        if (!readDouble(object, "amplitude", stack.swayAmplitude) &&
            !readDouble(object, "curveAmplitude", stack.swayAmplitude))
            return fail(DiagnosticCode::InvalidArgument, "sway requires amplitude", path);
        if (!readDouble(object, "frequency", stack.swayFrequency) &&
            !readDouble(object, "curveFrequencyHz", stack.swayFrequency))
            stack.swayFrequency = 2.0;
        return Result<void>::success();
    }
    if (kind == "helix") {
        if (!readDouble(object, "amplitude", stack.helixAmplitude) &&
            !readDouble(object, "curveAmplitude", stack.helixAmplitude))
            return fail(DiagnosticCode::InvalidArgument, "helix requires amplitude", path);
        if (!readDouble(object, "frequency", stack.helixFrequency) &&
            !readDouble(object, "curveFrequencyHz", stack.helixFrequency))
            stack.helixFrequency = 2.0;
        return Result<void>::success();
    }
    if (kind == "pierce") {
        int count = 1;
        (void)readInt(object, "count", count);
        if (count <= 0) return fail(DiagnosticCode::InvalidArgument, "pierce count must be positive", path);
        stack.pierce = count;
        return Result<void>::success();
    }
    if (kind == "bounce") {
        int count = 1;
        (void)readInt(object, "count", count);
        if (count <= 0) return fail(DiagnosticCode::InvalidArgument, "bounce count must be positive", path);
        stack.bounce = count;
        (void)readDouble(object, "restitution", stack.restitution);
        return Result<void>::success();
    }
    if (kind == "damage") {
        double add = 0.0;
        double multiply = 1.0;
        const bool hasAdd = readDouble(object, "add", add);
        const bool hasMul = readDouble(object, "multiply", multiply);
        if (!hasAdd && !hasMul)
            return fail(DiagnosticCode::InvalidArgument, "damage requires add and/or multiply", path);
        if (hasAdd) stack.damageAdd += add;
        if (hasMul) stack.damageMultiply *= multiply;
        return Result<void>::success();
    }
    if (kind == "speed") {
        double add = 0.0;
        double multiply = 1.0;
        const bool hasAdd = readDouble(object, "add", add);
        const bool hasMul = readDouble(object, "multiply", multiply);
        if (!hasAdd && !hasMul)
            return fail(DiagnosticCode::InvalidArgument, "speed requires add and/or multiply", path);
        if (hasAdd) stack.speedAdd += add;
        if (hasMul) stack.speedMultiply *= multiply;
        return Result<void>::success();
    }
    if (kind == "lifetime") {
        double add = 0.0;
        double multiply = 1.0;
        const bool hasAdd = readDouble(object, "add", add);
        const bool hasMul = readDouble(object, "multiply", multiply);
        if (!hasAdd && !hasMul)
            return fail(DiagnosticCode::InvalidArgument, "lifetime requires add and/or multiply", path);
        if (hasAdd) stack.lifetimeAdd += add;
        if (hasMul) stack.lifetimeMultiply *= multiply;
        return Result<void>::success();
    }
    if (kind == "element") {
        if (!readString(object, "value", stack.element) && !readString(object, "element", stack.element))
            return fail(DiagnosticCode::InvalidArgument, "element requires value", path);
        return Result<void>::success();
    }
    if (kind == "damageType") {
        if (!readString(object, "value", stack.damageType) && !readString(object, "damageType", stack.damageType))
            return fail(DiagnosticCode::InvalidArgument, "damageType requires value", path);
        return Result<void>::success();
    }
    if (kind == "splash") {
        double radius = 0.0;
        if (!readDouble(object, "radius", radius) && !readDouble(object, "splash", radius))
            return fail(DiagnosticCode::InvalidArgument, "splash requires radius", path);
        if (radius <= 0.0) return fail(DiagnosticCode::InvalidArgument, "splash radius must be positive", path);
        stack.splash = radius;
        return Result<void>::success();
    }
    if (kind == "fuse") {
        double seconds = 0.0;
        if (!readDouble(object, "seconds", seconds) && !readDouble(object, "fuse", seconds))
            return fail(DiagnosticCode::InvalidArgument, "fuse requires seconds", path);
        if (seconds <= 0.0) return fail(DiagnosticCode::InvalidArgument, "fuse seconds must be positive", path);
        stack.fuse = seconds;
        double splash = 0.0;
        if (readDouble(object, "splash", splash) || readDouble(object, "radius", splash)) stack.splash = splash;
        return Result<void>::success();
    }
    if (kind == "fan" || kind == "multicast" || kind == "ring") {
        CarrierVolleySpec volley;
        volley.pattern = (kind == "ring") ? CarrierVolleyPattern::Ring : CarrierVolleyPattern::Fan;
        if (!readInt(object, "count", volley.count) || volley.count < 1)
            return fail(DiagnosticCode::InvalidArgument, "volley requires positive count", path);
        (void)readDouble(object, "spread", volley.spreadDegrees);
        if (volley.pattern == CarrierVolleyPattern::Fan) (void)readDouble(object, "spreadDegrees", volley.spreadDegrees);
        stack.volley = volley;
        return Result<void>::success();
    }
    return fail(DiagnosticCode::InvalidArgument, "Unknown fragment kind", path + ".kind");
}

Result<CarrierRecipe> buildProjectile(const Value::Object& object, const ModifierStack& stack, std::string path) {
    Value::Object preset = object;
    std::string id;
    if (!readString(object, "id", id) || id.empty())
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "projectile requires id", path + ".id");

    double speed = 10.0;
    (void)readDouble(object, "speed", speed);
    speed = speed * stack.speedMultiply + stack.speedAdd;
    if (!(speed > 0.0) || !std::isfinite(speed))
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "projectile speed must be positive", path + ".speed");

    double lifetime = 2.0;
    (void)readDouble(object, "lifetime", lifetime);
    lifetime = lifetime * stack.lifetimeMultiply + stack.lifetimeAdd;
    if (!(lifetime > 0.0) || !std::isfinite(lifetime))
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "projectile lifetime must be positive",
                                    path + ".lifetime");

    double damage = 0.0;
    (void)readDouble(object, "damage", damage);
    damage = damage * stack.damageMultiply + stack.damageAdd;

    preset["id"] = Value(id);
    preset["speed"] = Value(speed);
    preset["lifetime"] = Value(lifetime);
    preset["damage"] = Value(damage);
    if (stack.gravity > 0.0) preset["gravity"] = Value(stack.gravity);
    if (stack.homing > 0.0) preset["homing"] = Value(stack.homing);
    if (stack.accelerate > 0.0) preset["accelerate"] = Value(stack.accelerate);
    if (stack.pierce > 0) preset["pierce"] = Value(static_cast<std::int64_t>(stack.pierce));
    if (stack.bounce > 0) preset["bounce"] = Value(static_cast<std::int64_t>(stack.bounce));
    if (stack.fuse > 0.0) preset["fuse"] = Value(stack.fuse);
    if (stack.splash > 0.0) preset["splash"] = Value(stack.splash);

    std::string element;
    std::string damageType;
    (void)readString(object, "element", element);
    (void)readString(object, "damageType", damageType);
    if (!stack.element.empty()) element = stack.element;
    if (!stack.damageType.empty()) damageType = stack.damageType;
    if (!element.empty()) preset["element"] = Value(element);
    if (!damageType.empty()) preset["damageType"] = Value(damageType);

    auto recipe = decodeCarrierRecipe(Value(std::move(preset)));
    if (!recipe.ok()) return recipe;

    // Overlay curve motion ops that the preset form does not express.
    CarrierRecipe built = std::move(recipe).takeValue();
    std::vector<CarrierMotionOp> motion;
    if (stack.homing > 0.0) {
        CarrierMotionOp op;
        op.kind               = CarrierMotionOpKind::SteerHoming;
        op.maxTurnRateDegrees = stack.homing;
        motion.push_back(op);
    }
    if (stack.gravity > 0.0) {
        CarrierMotionOp op;
        op.kind    = CarrierMotionOpKind::ApplyGravity;
        op.gravity = stack.gravity;
        motion.push_back(op);
    }
    if (stack.accelerate > 0.0) {
        CarrierMotionOp op;
        op.kind         = CarrierMotionOpKind::Accelerate;
        op.acceleration = stack.accelerate;
        motion.push_back(op);
    }
    if (stack.swayAmplitude > 0.0) {
        CarrierMotionOp op;
        op.kind             = CarrierMotionOpKind::CurveSway;
        op.curveAmplitude   = stack.swayAmplitude;
        op.curveFrequencyHz = stack.swayFrequency > 0.0 ? stack.swayFrequency : 2.0;
        motion.push_back(op);
    }
    if (stack.helixAmplitude > 0.0) {
        CarrierMotionOp op;
        op.kind             = CarrierMotionOpKind::CurveHelix;
        op.curveAmplitude   = stack.helixAmplitude;
        op.curveFrequencyHz = stack.helixFrequency > 0.0 ? stack.helixFrequency : 2.0;
        motion.push_back(op);
    }
    motion.push_back({CarrierMotionOpKind::IntegrateLinear});
    built.motionOps = std::move(motion);

    if (stack.bounce > 0) {
        for (auto& impact : built.impacts) {
            if (impact.kind == CarrierImpactKind::Bounce) impact.restitution = stack.restitution;
        }
    }

    auto valid = built.validate();
    if (!valid) return Result<CarrierRecipe>::failure(valid.status());
    return Result<CarrierRecipe>::success(std::move(built));
}

}  // namespace

Result<SpellCastPlan> compileSpellFragments(const Value& fragments) {
    if (!fragments.isArray())
        return failT<SpellCastPlan>(DiagnosticCode::InvalidArgument, "Fragments must be an array", "");
    if (fragments.arraySize() == 0)
        return failT<SpellCastPlan>(DiagnosticCode::InvalidArgument, "Fragments must not be empty", "");

    ModifierStack stack;
    std::optional<CarrierRecipe> recipe;
    for (std::size_t i = 0; i < fragments.arraySize(); ++i) {
        const std::string path = "[" + std::to_string(i) + "]";
        const auto* object = fragments.at(i).getIf<Value::Object>();
        if (!object)
            return failT<SpellCastPlan>(DiagnosticCode::InvalidArgument, "Fragment must be an object", path);
        std::string kind;
        if (!readString(*object, "kind", kind) || kind.empty())
            return failT<SpellCastPlan>(DiagnosticCode::InvalidArgument, "Fragment requires kind", path + ".kind");

        if (kind == "projectile" || kind == "bolt" || kind == "grenade" || kind == "missile") {
            if (recipe.has_value())
                return failT<SpellCastPlan>(DiagnosticCode::InvalidArgument,
                                            "Only one projectile fragment is supported per cast", path);
            auto built = buildProjectile(*object, stack, path);
            if (!built.ok()) return Result<SpellCastPlan>::failure(built.status());
            recipe = std::move(built).takeValue();
            continue;
        }

        if (recipe.has_value())
            return failT<SpellCastPlan>(DiagnosticCode::InvalidArgument,
                                        "Modifiers must appear before the projectile fragment", path);

        auto applied = applyModifier(stack, *object, kind, path);
        if (!applied) return Result<SpellCastPlan>::failure(applied.status());
    }

    if (!recipe.has_value())
        return failT<SpellCastPlan>(DiagnosticCode::InvalidArgument,
                                    "Fragment sequence requires one projectile fragment", "");

    SpellCastPlan plan;
    plan.recipe = std::move(*recipe);
    plan.volley = stack.volley;
    return Result<SpellCastPlan>::success(std::move(plan));
}

Result<SpellCastPlan> compileSpellFragmentsJson(const std::string& json) {
    auto parsed = Value::fromJson(json);
    if (!parsed.ok()) return Result<SpellCastPlan>::failure(parsed.status());
    return compileSpellFragments(parsed.value());
}

}  // namespace eve::weapon
