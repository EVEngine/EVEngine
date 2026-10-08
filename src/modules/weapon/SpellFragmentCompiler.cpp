#include "weapon/SpellFragmentCompiler.h"

#include "common/Diagnostic.h"
#include "common/Time.h"
#include "weapon/CarrierRecipeCodec.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::weapon {
namespace {

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

Result<std::optional<double>> optionalDouble(const Value::Object& object, const char* name, std::string_view path) {
    const auto* value = field(object, name);
    if (!value) return Result<std::optional<double>>::success(std::nullopt);
    if (const auto* number = value->getIf<double>()) {
        if (!std::isfinite(*number))
            return Result<std::optional<double>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Expected finite number"), std::string(path), {},
                                                "weapon.spell.fragments"));
        return Result<std::optional<double>>::success(*number);
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        const double converted = static_cast<double>(*integer);
        if (!std::isfinite(converted))
            return Result<std::optional<double>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Expected finite number"), std::string(path), {},
                                                "weapon.spell.fragments"));
        return Result<std::optional<double>>::success(converted);
    }
    return Result<std::optional<double>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Expected number"), std::string(path), {},
                                                "weapon.spell.fragments"));
}

Result<std::optional<int>> optionalInt(const Value::Object& object, const char* name, std::string_view path) {
    const auto* value = field(object, name);
    if (!value) return Result<std::optional<int>>::success(std::nullopt);
    if (const auto* integer = value->getIf<std::int64_t>()) {
        if (*integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max())
            return Result<std::optional<int>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Integer out of range"), std::string(path), {},
                                                "weapon.spell.fragments"));
        return Result<std::optional<int>>::success(static_cast<int>(*integer));
    }
    if (const auto* number = value->getIf<double>()) {
        if (!std::isfinite(*number) || *number != std::floor(*number))
            return Result<std::optional<int>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Expected integer"), std::string(path), {},
                                                "weapon.spell.fragments"));
        if (*number < static_cast<double>(std::numeric_limits<int>::min()) ||
            *number > static_cast<double>(std::numeric_limits<int>::max()))
            return Result<std::optional<int>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Integer out of range"), std::string(path), {},
                                                "weapon.spell.fragments"));
        return Result<std::optional<int>>::success(static_cast<int>(*number));
    }
    return Result<std::optional<int>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Expected integer"), std::string(path), {},
                                                "weapon.spell.fragments"));
}

enum class ArithKind : std::uint8_t { Add, Multiply };

struct ArithOp {
    ArithKind kind  = ArithKind::Add;
    double    value = 0.0;
};

[[nodiscard]] double applyArith(double base, const std::vector<ArithOp>& ops) {
    double result = base;
    for (const auto& op : ops) {
        if (op.kind == ArithKind::Add)
            result += op.value;
        else
            result *= op.value;
    }
    return result;
}

struct ModifierStack {
    std::vector<ArithOp>             speedOps;
    std::vector<ArithOp>             damageOps;
    std::vector<ArithOp>             lifetimeOps;
    double                           gravity        = 0.0;
    double                           homing         = 0.0;
    double                           accelerate     = 0.0;
    double                           swayAmplitude  = 0.0;
    double                           swayFrequency  = 0.0;
    double                           helixAmplitude = 0.0;
    double                           helixFrequency = 0.0;
    int                              pierce         = 0;
    int                              bounce         = 0;
    double                           restitution    = 1.0;
    double                           splash         = 0.0;
    double                           fuse           = 0.0;
    std::string                      element;
    std::string                      damageType;
    std::optional<CarrierVolleySpec> volley;
};

Result<void> applyModifier(ModifierStack& stack, const Value::Object& object, std::string_view kind,
                           std::string path) {
    if (kind == "homing") {
        auto turnRate = optionalDouble(object, "turnRate", path + ".turnRate");
        if (!turnRate.ok()) return Result<void>::failure(turnRate.status());
        auto alt = optionalDouble(object, "maxTurnRateDegrees", path + ".maxTurnRateDegrees");
        if (!alt.ok()) return Result<void>::failure(alt.status());
        const double turn = turnRate.value().value_or(alt.value().value_or(90.0));
        if (!(turn > 0.0)) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("homing turnRate must be positive"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        stack.homing = turn;
        return Result<void>::success();
    }
    if (kind == "gravity") {
        auto gravity = optionalDouble(object, "gravity", path + ".gravity");
        if (!gravity.ok()) return Result<void>::failure(gravity.status());
        const double value = gravity.value().value_or(9.8);
        if (value < 0.0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("gravity must be non-negative"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        stack.gravity = value;
        return Result<void>::success();
    }
    if (kind == "accelerate") {
        auto acceleration = optionalDouble(object, "acceleration", path + ".acceleration");
        if (!acceleration.ok()) return Result<void>::failure(acceleration.status());
        if (!acceleration.value().has_value() || *acceleration.value() < 0.0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("accelerate requires non-negative acceleration"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        stack.accelerate = *acceleration.value();
        return Result<void>::success();
    }
    if (kind == "sway" || kind == "helix") {
        auto amplitude = optionalDouble(object, "amplitude", path + ".amplitude");
        if (!amplitude.ok()) return Result<void>::failure(amplitude.status());
        auto altAmp = optionalDouble(object, "curveAmplitude", path + ".curveAmplitude");
        if (!altAmp.ok()) return Result<void>::failure(altAmp.status());
        const auto amp = amplitude.value().has_value() ? amplitude.value() : altAmp.value();
        if (!amp.has_value() || *amp < 0.0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("curve fragment requires non-negative amplitude"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        auto frequency = optionalDouble(object, "frequency", path + ".frequency");
        if (!frequency.ok()) return Result<void>::failure(frequency.status());
        auto altFreq = optionalDouble(object, "curveFrequencyHz", path + ".curveFrequencyHz");
        if (!altFreq.ok()) return Result<void>::failure(altFreq.status());
        const double freq = frequency.value().value_or(altFreq.value().value_or(2.0));
        if (!(freq > 0.0)) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("curve frequency must be positive"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        if (kind == "sway") {
            stack.swayAmplitude = *amp;
            stack.swayFrequency = freq;
        } else {
            stack.helixAmplitude = *amp;
            stack.helixFrequency = freq;
        }
        return Result<void>::success();
    }
    if (kind == "pierce" || kind == "bounce") {
        auto count = optionalInt(object, "count", path + ".count");
        if (!count.ok()) return Result<void>::failure(count.status());
        const int value = count.value().value_or(1);
        if (value <= 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("count must be positive"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        if (kind == "pierce") {
            stack.pierce = value;
        } else {
            stack.bounce     = value;
            auto restitution = optionalDouble(object, "restitution", path + ".restitution");
            if (!restitution.ok()) return Result<void>::failure(restitution.status());
            if (restitution.value().has_value()) {
                if (*restitution.value() < 0.0)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("restitution must be non-negative"), std::string(path), {},
                                                   "weapon.spell.fragments"));
                stack.restitution = *restitution.value();
            }
        }
        return Result<void>::success();
    }
    if (kind == "damage" || kind == "speed" || kind == "lifetime") {
        auto add = optionalDouble(object, "add", path + ".add");
        if (!add.ok()) return Result<void>::failure(add.status());
        auto multiply = optionalDouble(object, "multiply", path + ".multiply");
        if (!multiply.ok()) return Result<void>::failure(multiply.status());
        if (!add.value().has_value() && !multiply.value().has_value())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("modifier requires add and/or multiply"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        auto& ops = (kind == "damage") ? stack.damageOps : (kind == "speed") ? stack.speedOps : stack.lifetimeOps;
        // Preserve fragment order: add keys then multiply keys as written is ambiguous in objects,
        // so apply add before multiply when both are present in one fragment; separate fragments
        // retain left-to-right order across the sequence.
        if (add.value().has_value()) ops.push_back({ArithKind::Add, *add.value()});
        if (multiply.value().has_value()) ops.push_back({ArithKind::Multiply, *multiply.value()});
        return Result<void>::success();
    }
    if (kind == "element") {
        if (!readString(object, "value", stack.element) && !readString(object, "element", stack.element))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("element requires value"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        return Result<void>::success();
    }
    if (kind == "damageType") {
        if (!readString(object, "value", stack.damageType) && !readString(object, "damageType", stack.damageType))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("damageType requires value"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        return Result<void>::success();
    }
    if (kind == "splash") {
        auto radius = optionalDouble(object, "radius", path + ".radius");
        if (!radius.ok()) return Result<void>::failure(radius.status());
        auto alt = optionalDouble(object, "splash", path + ".splash");
        if (!alt.ok()) return Result<void>::failure(alt.status());
        const auto value = radius.value().has_value() ? radius.value() : alt.value();
        if (!value.has_value() || !(*value > 0.0))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("splash requires positive radius"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        stack.splash = *value;
        return Result<void>::success();
    }
    if (kind == "fuse") {
        auto seconds = optionalDouble(object, "seconds", path + ".seconds");
        if (!seconds.ok()) return Result<void>::failure(seconds.status());
        auto alt = optionalDouble(object, "fuse", path + ".fuse");
        if (!alt.ok()) return Result<void>::failure(alt.status());
        const auto value = seconds.value().has_value() ? seconds.value() : alt.value();
        if (!value.has_value() || !(*value > 0.0))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("fuse requires positive seconds"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        stack.fuse  = *value;
        auto splash = optionalDouble(object, "splash", path + ".splash");
        if (!splash.ok()) return Result<void>::failure(splash.status());
        auto splashAlt = optionalDouble(object, "radius", path + ".radius");
        if (!splashAlt.ok()) return Result<void>::failure(splashAlt.status());
        if (splash.value().has_value())
            stack.splash = *splash.value();
        else if (splashAlt.value().has_value())
            stack.splash = *splashAlt.value();
        return Result<void>::success();
    }
    if (kind == "fan" || kind == "multicast" || kind == "ring") {
        CarrierVolleySpec volley;
        volley.pattern = (kind == "ring") ? CarrierVolleyPattern::Ring : CarrierVolleyPattern::Fan;
        auto count     = optionalInt(object, "count", path + ".count");
        if (!count.ok()) return Result<void>::failure(count.status());
        if (!count.value().has_value() || *count.value() < 1)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("volley requires positive count"), std::string(path), {},
                                                   "weapon.spell.fragments"));
        volley.count = *count.value();
        auto spread  = optionalDouble(object, "spread", path + ".spread");
        if (!spread.ok()) return Result<void>::failure(spread.status());
        auto spreadAlt = optionalDouble(object, "spreadDegrees", path + ".spreadDegrees");
        if (!spreadAlt.ok()) return Result<void>::failure(spreadAlt.status());
        volley.spreadDegrees = spread.value().value_or(spreadAlt.value().value_or(0.0));
        stack.volley         = volley;
        return Result<void>::success();
    }
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Unknown fragment kind"), std::string(path + ".kind"), {},
                                                   "weapon.spell.fragments"));
}

Result<CarrierRecipe> buildProjectile(const Value::Object& object, const ModifierStack& stack, std::string path) {
    if (field(object, "triggers") || field(object, "impacts") || field(object, "motionOps") ||
        (field(object, "motion") && field(object, "motion")->isArray())) {
        return Result<CarrierRecipe>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::string("Projectile fragments must use preset fields, not full-form motion/triggers/impacts"), std::string(path), {},
                                                "weapon.spell.fragments"));
    }

    std::string id;
    if (!readString(object, "id", id) || id.empty())
        return Result<CarrierRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("projectile requires id"), std::string(path + ".id"), {},
                                                "weapon.spell.fragments"));

    auto speedField = optionalDouble(object, "speed", path + ".speed");
    if (!speedField.ok()) return Result<CarrierRecipe>::failure(speedField.status());
    double speed = applyArith(speedField.value().value_or(10.0), stack.speedOps);
    if (!(speed > 0.0) || !std::isfinite(speed))
        return Result<CarrierRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("projectile speed must be positive"), std::string(path + ".speed"), {},
                                                "weapon.spell.fragments"));

    auto lifetimeField = optionalDouble(object, "lifetime", path + ".lifetime");
    if (!lifetimeField.ok()) return Result<CarrierRecipe>::failure(lifetimeField.status());
    double lifetime = applyArith(lifetimeField.value().value_or(2.0), stack.lifetimeOps);
    if (!(lifetime > 0.0) || !std::isfinite(lifetime))
        return Result<CarrierRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("projectile lifetime must be positive"), std::string(path + ".lifetime"), {},
                                                "weapon.spell.fragments"));

    auto damageField = optionalDouble(object, "damage", path + ".damage");
    if (!damageField.ok()) return Result<CarrierRecipe>::failure(damageField.status());
    double damage = applyArith(damageField.value().value_or(0.0), stack.damageOps);

    ModifierStack merged     = stack;
    auto          seedDouble = [&](const char* name, double& dest) -> Result<void> {
        auto value = optionalDouble(object, name, std::string(path) + "." + name);
        if (!value.ok()) return Result<void>::failure(value.status());
        if (dest <= 0.0 && value.value().has_value()) dest = *value.value();
        return Result<void>::success();
    };
    if (auto seeded = seedDouble("homing", merged.homing); !seeded)
        return Result<CarrierRecipe>::failure(seeded.status());
    if (auto seeded = seedDouble("maxTurnRateDegrees", merged.homing); !seeded)
        return Result<CarrierRecipe>::failure(seeded.status());
    if (auto seeded = seedDouble("gravity", merged.gravity); !seeded)
        return Result<CarrierRecipe>::failure(seeded.status());
    if (auto seeded = seedDouble("accelerate", merged.accelerate); !seeded)
        return Result<CarrierRecipe>::failure(seeded.status());

    std::string motionPreset;
    (void)readString(object, "motion", motionPreset);
    if (motionPreset == "homing" && merged.homing <= 0.0) merged.homing = 90.0;
    if (motionPreset == "ballistic" && merged.gravity <= 0.0) merged.gravity = 9.8;

    auto pierceField = optionalInt(object, "pierce", path + ".pierce");
    if (!pierceField.ok()) return Result<CarrierRecipe>::failure(pierceField.status());
    if (merged.pierce <= 0 && pierceField.value().has_value()) merged.pierce = *pierceField.value();
    auto bounceField = optionalInt(object, "bounce", path + ".bounce");
    if (!bounceField.ok()) return Result<CarrierRecipe>::failure(bounceField.status());
    if (merged.bounce <= 0 && bounceField.value().has_value()) merged.bounce = *bounceField.value();

    Value::Object preset;
    preset["id"]       = Value(id);
    preset["speed"]    = Value(speed);
    preset["lifetime"] = Value(lifetime);
    preset["damage"]   = Value(damage);
    if (merged.gravity > 0.0) preset["gravity"] = Value(merged.gravity);
    if (merged.homing > 0.0) preset["homing"] = Value(merged.homing);
    if (merged.accelerate > 0.0) preset["accelerate"] = Value(merged.accelerate);
    if (merged.pierce > 0) preset["pierce"] = Value(static_cast<std::int64_t>(merged.pierce));
    if (merged.bounce > 0) preset["bounce"] = Value(static_cast<std::int64_t>(merged.bounce));
    if (merged.fuse > 0.0) preset["fuse"] = Value(merged.fuse);
    if (merged.splash > 0.0) preset["splash"] = Value(merged.splash);

    std::string element;
    std::string damageType;
    (void)readString(object, "element", element);
    (void)readString(object, "damageType", damageType);
    if (!merged.element.empty()) element = merged.element;
    if (!merged.damageType.empty()) damageType = merged.damageType;
    if (!element.empty()) preset["element"] = Value(element);
    if (!damageType.empty()) preset["damageType"] = Value(damageType);

    auto recipe = decodeCarrierRecipe(Value(std::move(preset)));
    if (!recipe.ok()) return recipe;
    CarrierRecipe built = std::move(recipe).takeValue();

    // Preserve preset motion from decode; only inject curve ops that presets cannot express.
    if (merged.swayAmplitude > 0.0 || merged.helixAmplitude > 0.0) {
        std::vector<CarrierMotionOp> motion = built.motionOps;
        // Insert curve ops before the final IntegrateLinear.
        if (!motion.empty() && motion.back().kind == CarrierMotionOpKind::IntegrateLinear) motion.pop_back();
        if (merged.swayAmplitude > 0.0) {
            CarrierMotionOp op;
            op.kind             = CarrierMotionOpKind::CurveSway;
            op.curveAmplitude   = merged.swayAmplitude;
            op.curveFrequencyHz = merged.swayFrequency > 0.0 ? merged.swayFrequency : 2.0;
            motion.push_back(op);
        }
        if (merged.helixAmplitude > 0.0) {
            CarrierMotionOp op;
            op.kind             = CarrierMotionOpKind::CurveHelix;
            op.curveAmplitude   = merged.helixAmplitude;
            op.curveFrequencyHz = merged.helixFrequency > 0.0 ? merged.helixFrequency : 2.0;
            motion.push_back(op);
        }
        motion.push_back({CarrierMotionOpKind::IntegrateLinear});
        built.motionOps = std::move(motion);
    }

    if (merged.bounce > 0) {
        for (auto& impact : built.impacts) {
            if (impact.kind == CarrierImpactKind::Bounce) impact.restitution = merged.restitution;
        }
    }

    auto valid = built.validate();
    if (!valid) return Result<CarrierRecipe>::failure(valid.status());
    return Result<CarrierRecipe>::success(std::move(built));
}

}  // namespace

Result<SpellCastPlan> compileSpellFragments(const Value& fragments) {
    if (!fragments.isArray())
        return Result<SpellCastPlan>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Fragments must be an array"), std::string(""), {},
                                                "weapon.spell.fragments"));
    if (fragments.arraySize() == 0)
        return Result<SpellCastPlan>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Fragments must not be empty"), std::string(""), {},
                                                "weapon.spell.fragments"));

    ModifierStack stack;
    std::optional<CarrierRecipe> recipe;
    for (std::size_t i = 0; i < fragments.arraySize(); ++i) {
        const std::string path = "[" + std::to_string(i) + "]";
        const auto* object = fragments.at(i).getIf<Value::Object>();
        if (!object)
            return Result<SpellCastPlan>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Fragment must be an object"), std::string(path), {},
                                                "weapon.spell.fragments"));
        std::string kind;
        if (!readString(*object, "kind", kind) || kind.empty())
            return Result<SpellCastPlan>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Fragment requires kind"), std::string(path + ".kind"), {},
                                                "weapon.spell.fragments"));

        if (kind == "projectile" || kind == "bolt" || kind == "grenade" || kind == "missile") {
            if (recipe.has_value())
                return Result<SpellCastPlan>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Only one projectile fragment is supported per cast"), std::string(path), {},
                                                "weapon.spell.fragments"));
            auto built = buildProjectile(*object, stack, path);
            if (!built.ok()) return Result<SpellCastPlan>::failure(built.status());
            recipe = std::move(built).takeValue();
            continue;
        }

        if (recipe.has_value())
            return Result<SpellCastPlan>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Modifiers must appear before the projectile fragment"), std::string(path), {},
                                                "weapon.spell.fragments"));

        auto applied = applyModifier(stack, *object, kind, path);
        if (!applied) return Result<SpellCastPlan>::failure(applied.status());
    }

    if (!recipe.has_value())
        return Result<SpellCastPlan>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("Fragment sequence requires one projectile fragment"), std::string(""), {},
                                                "weapon.spell.fragments"));

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
