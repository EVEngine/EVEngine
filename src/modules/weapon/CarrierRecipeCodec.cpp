#include "weapon/CarrierRecipeCodec.h"

#include "common/Diagnostic.h"
#include "common/Time.h"

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace eve::weapon {
namespace {

Result<void> fail(DiagnosticCode code, std::string_view message, std::string_view path) {
    return Result<void>::failure(Diagnostic::error(code, std::string(message), std::string(path), {},
                                                   "weapon.carrier.recipe"));
}

template <class T>
Result<T> failT(DiagnosticCode code, std::string_view message, std::string_view path) {
    return Result<T>::failure(Diagnostic::error(code, std::string(message), std::string(path), {},
                                                "weapon.carrier.recipe"));
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

Result<Duration> readSeconds(const Value::Object& object, const char* name, std::string_view path) {
    double seconds = 0.0;
    if (!readDouble(object, name, seconds) || seconds <= 0.0)
        return failT<Duration>(DiagnosticCode::InvalidArgument, "Expected positive seconds", path);
    auto duration = Duration::fromSeconds(seconds);
    if (!duration.ok()) return Result<Duration>::failure(duration.status());
    return Result<Duration>::success(duration.value());
}

Result<LogicalId> readLogicalId(const Value::Object& object, const char* name, std::string_view path) {
    std::string text;
    if (!readString(object, name, text) || text.empty())
        return failT<LogicalId>(DiagnosticCode::InvalidArgument, "Expected non-empty logical id", path);
    auto parsed = LogicalId::parse(text);
    if (!parsed.has_value())
        return failT<LogicalId>(DiagnosticCode::InvalidArgument, "Logical id must be namespace:name", path);
    return Result<LogicalId>::success(std::move(*parsed));
}

Result<CarrierMotionOpKind> parseMotionKind(std::string_view text, std::string_view path) {
    if (text == "linear" || text == "IntegrateLinear") return Result<CarrierMotionOpKind>::success(CarrierMotionOpKind::IntegrateLinear);
    if (text == "gravity" || text == "ApplyGravity") return Result<CarrierMotionOpKind>::success(CarrierMotionOpKind::ApplyGravity);
    if (text == "homing" || text == "SteerHoming") return Result<CarrierMotionOpKind>::success(CarrierMotionOpKind::SteerHoming);
    if (text == "accelerate" || text == "Accelerate") return Result<CarrierMotionOpKind>::success(CarrierMotionOpKind::Accelerate);
    if (text == "sway" || text == "CurveSway") return Result<CarrierMotionOpKind>::success(CarrierMotionOpKind::CurveSway);
    if (text == "helix" || text == "CurveHelix") return Result<CarrierMotionOpKind>::success(CarrierMotionOpKind::CurveHelix);
    if (text == "avoidBody" || text == "SteerAvoidBody") return Result<CarrierMotionOpKind>::success(CarrierMotionOpKind::SteerAvoidBody);
    return failT<CarrierMotionOpKind>(DiagnosticCode::InvalidArgument, "Unknown motion kind", path);
}

Result<CarrierTriggerKind> parseTriggerKind(std::string_view text, std::string_view path) {
    if (text == "onExpire" || text == "OnExpire") return Result<CarrierTriggerKind>::success(CarrierTriggerKind::OnExpire);
    if (text == "onFuse" || text == "OnFuse") return Result<CarrierTriggerKind>::success(CarrierTriggerKind::OnFuse);
    if (text == "onInterval" || text == "OnInterval") return Result<CarrierTriggerKind>::success(CarrierTriggerKind::OnInterval);
    if (text == "onHit" || text == "OnHit") return Result<CarrierTriggerKind>::success(CarrierTriggerKind::OnHit);
    if (text == "onProximity" || text == "OnProximity") return Result<CarrierTriggerKind>::success(CarrierTriggerKind::OnProximity);
    return failT<CarrierTriggerKind>(DiagnosticCode::InvalidArgument, "Unknown trigger kind", path);
}

Result<CarrierImpactKind> parseImpactKind(std::string_view text, std::string_view path) {
    if (text == "emitHit" || text == "hit" || text == "EmitHit") return Result<CarrierImpactKind>::success(CarrierImpactKind::EmitHit);
    if (text == "splash" || text == "Splash") return Result<CarrierImpactKind>::success(CarrierImpactKind::Splash);
    if (text == "pierce" || text == "Pierce") return Result<CarrierImpactKind>::success(CarrierImpactKind::Pierce);
    if (text == "bounce" || text == "Bounce") return Result<CarrierImpactKind>::success(CarrierImpactKind::Bounce);
    if (text == "release" || text == "Release") return Result<CarrierImpactKind>::success(CarrierImpactKind::Release);
    if (text == "spawnChild" || text == "SpawnChild") return Result<CarrierImpactKind>::success(CarrierImpactKind::SpawnChild);
    return failT<CarrierImpactKind>(DiagnosticCode::InvalidArgument, "Unknown impact kind", path);
}

const char* motionKindName(CarrierMotionOpKind kind) {
    switch (kind) {
        case CarrierMotionOpKind::SteerHoming: return "homing";
        case CarrierMotionOpKind::SteerAvoidBody: return "avoidBody";
        case CarrierMotionOpKind::ApplyGravity: return "gravity";
        case CarrierMotionOpKind::Accelerate: return "accelerate";
        case CarrierMotionOpKind::CurveSway: return "sway";
        case CarrierMotionOpKind::CurveHelix: return "helix";
        case CarrierMotionOpKind::IntegrateLinear: return "linear";
    }
    return "linear";
}

const char* triggerKindName(CarrierTriggerKind kind) {
    switch (kind) {
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
    }
    return "release";
}

Result<CarrierMotionOp> decodeMotionOp(const Value& value, std::string path) {
    CarrierMotionOp op;
    if (const auto* text = value.getIf<std::string>()) {
        auto kind = parseMotionKind(*text, path);
        if (!kind.ok()) return Result<CarrierMotionOp>::failure(kind.status());
        op.kind = kind.value();
        return Result<CarrierMotionOp>::success(op);
    }
    const auto* object = value.getIf<Value::Object>();
    if (!object) return failT<CarrierMotionOp>(DiagnosticCode::InvalidArgument, "Motion op must be string or object", path);
    std::string kindText;
    if (!readString(*object, "kind", kindText))
        return failT<CarrierMotionOp>(DiagnosticCode::InvalidArgument, "Motion op requires kind", path + ".kind");
    auto kind = parseMotionKind(kindText, path + ".kind");
    if (!kind.ok()) return Result<CarrierMotionOp>::failure(kind.status());
    op.kind = kind.value();
    (void)readDouble(*object, "gravity", op.gravity);
    (void)readDouble(*object, "maxTurnRateDegrees", op.maxTurnRateDegrees);
    if (op.maxTurnRateDegrees == 0.0) (void)readDouble(*object, "turnRate", op.maxTurnRateDegrees);
    (void)readDouble(*object, "acceleration", op.acceleration);
    (void)readDouble(*object, "avoidLookAhead", op.avoidLookAhead);
    (void)readDouble(*object, "avoidStrength", op.avoidStrength);
    (void)readDouble(*object, "avoidRadiusPadding", op.avoidRadiusPadding);
    (void)readDouble(*object, "curveAmplitude", op.curveAmplitude);
    if (op.curveAmplitude == 0.0) (void)readDouble(*object, "amplitude", op.curveAmplitude);
    (void)readDouble(*object, "curveFrequencyHz", op.curveFrequencyHz);
    if (op.curveFrequencyHz == 0.0) (void)readDouble(*object, "frequency", op.curveFrequencyHz);
    return Result<CarrierMotionOp>::success(op);
}

Result<CarrierTrigger> decodeTrigger(const Value& value, std::string path) {
    CarrierTrigger trigger;
    if (const auto* text = value.getIf<std::string>()) {
        auto kind = parseTriggerKind(*text, path);
        if (!kind.ok()) return Result<CarrierTrigger>::failure(kind.status());
        trigger.kind = kind.value();
        return Result<CarrierTrigger>::success(trigger);
    }
    const auto* object = value.getIf<Value::Object>();
    if (!object) return failT<CarrierTrigger>(DiagnosticCode::InvalidArgument, "Trigger must be string or object", path);
    std::string kindText;
    if (!readString(*object, "kind", kindText))
        return failT<CarrierTrigger>(DiagnosticCode::InvalidArgument, "Trigger requires kind", path + ".kind");
    auto kind = parseTriggerKind(kindText, path + ".kind");
    if (!kind.ok()) return Result<CarrierTrigger>::failure(kind.status());
    trigger.kind = kind.value();
    double fuse = 0.0;
    if (readDouble(*object, "fuse", fuse) || readDouble(*object, "seconds", fuse)) {
        auto duration = Duration::fromSeconds(fuse);
        if (!duration.ok()) return Result<CarrierTrigger>::failure(duration.status());
        trigger.fuse = duration.value();
    }
    double interval = 0.0;
    if (readDouble(*object, "interval", interval)) {
        auto duration = Duration::fromSeconds(interval);
        if (!duration.ok()) return Result<CarrierTrigger>::failure(duration.status());
        trigger.interval = duration.value();
    }
    (void)readDouble(*object, "proximityRadius", trigger.proximityRadius);
    if (trigger.proximityRadius == 0.0) (void)readDouble(*object, "radius", trigger.proximityRadius);
    return Result<CarrierTrigger>::success(trigger);
}

Result<CarrierImpact> decodeImpact(const Value& value, std::string path) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return failT<CarrierImpact>(DiagnosticCode::InvalidArgument, "Impact must be an object", path);
    CarrierImpact impact;
    std::string kindText;
    if (!readString(*object, "kind", kindText))
        return failT<CarrierImpact>(DiagnosticCode::InvalidArgument, "Impact requires kind", path + ".kind");
    auto kind = parseImpactKind(kindText, path + ".kind");
    if (!kind.ok()) return Result<CarrierImpact>::failure(kind.status());
    impact.kind = kind.value();
    std::string onText;
    if (readString(*object, "on", onText)) {
        auto on = parseTriggerKind(onText, path + ".on");
        if (!on.ok()) return Result<CarrierImpact>::failure(on.status());
        impact.on = on.value();
    }
    (void)readDouble(*object, "damage", impact.damage);
    (void)readDouble(*object, "splashRadius", impact.splashRadius);
    if (impact.splashRadius == 0.0) (void)readDouble(*object, "radius", impact.splashRadius);
    (void)readInt(*object, "pierceCount", impact.pierceCount);
    if (impact.pierceCount == 0) (void)readInt(*object, "count", impact.pierceCount);
    (void)readInt(*object, "bounceCount", impact.bounceCount);
    (void)readDouble(*object, "restitution", impact.restitution);
    (void)readString(*object, "damageType", impact.damageType);
    (void)readString(*object, "element", impact.element);
    if (const auto* child = field(*object, "childRecipeId")) {
        if (const auto* text = child->getIf<std::string>()) {
            auto parsed = LogicalId::parse(*text);
            if (!parsed.has_value())
                return failT<CarrierImpact>(DiagnosticCode::InvalidArgument, "Invalid childRecipeId",
                                            path + ".childRecipeId");
            impact.childRecipeId = std::move(*parsed);
        }
    }
    return Result<CarrierImpact>::success(impact);
}

Result<CarrierRecipe> decodePreset(const Value::Object& object) {
    CarrierRecipe recipe;
    auto id = readLogicalId(object, "id", "id");
    if (!id.ok()) return Result<CarrierRecipe>::failure(id.status());
    recipe.id = id.value();

    auto lifetime = readSeconds(object, "lifetime", "lifetime");
    if (!lifetime.ok()) return Result<CarrierRecipe>::failure(lifetime.status());
    recipe.lifetime = lifetime.value();

    if (!readDouble(object, "speed", recipe.speed) || recipe.speed <= 0.0)
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "Preset requires positive speed", "speed");

    double damage = 0.0;
    (void)readDouble(object, "damage", damage);
    std::string damageType;
    std::string element;
    (void)readString(object, "damageType", damageType);
    (void)readString(object, "element", element);

    double gravity = 0.0;
    double homing  = 0.0;
    double accelerate = 0.0;
    (void)readDouble(object, "gravity", gravity);
    (void)readDouble(object, "homing", homing);
    if (homing == 0.0) (void)readDouble(object, "maxTurnRateDegrees", homing);
    (void)readDouble(object, "accelerate", accelerate);

    int pierce = 0;
    int bounce = 0;
    (void)readInt(object, "pierce", pierce);
    (void)readInt(object, "bounce", bounce);

    double fuse = 0.0;
    double splash = 0.0;
    (void)readDouble(object, "fuse", fuse);
    (void)readDouble(object, "splash", splash);

    std::string motionPreset = "linear";
    (void)readString(object, "motion", motionPreset);

    if (homing > 0.0 || motionPreset == "homing") {
        CarrierMotionOp op;
        op.kind               = CarrierMotionOpKind::SteerHoming;
        op.maxTurnRateDegrees = homing > 0.0 ? homing : 90.0;
        recipe.motionOps.push_back(op);
    }
    if (gravity > 0.0 || motionPreset == "ballistic") {
        CarrierMotionOp op;
        op.kind    = CarrierMotionOpKind::ApplyGravity;
        op.gravity = gravity > 0.0 ? gravity : 9.8;
        recipe.motionOps.push_back(op);
    }
    if (accelerate > 0.0) {
        CarrierMotionOp op;
        op.kind         = CarrierMotionOpKind::Accelerate;
        op.acceleration = accelerate;
        recipe.motionOps.push_back(op);
    }
    recipe.motionOps.push_back({CarrierMotionOpKind::IntegrateLinear});

    recipe.triggers.push_back({CarrierTriggerKind::OnExpire});
    recipe.triggers.push_back({CarrierTriggerKind::OnHit});
    if (fuse > 0.0) {
        CarrierTrigger trigger;
        trigger.kind = CarrierTriggerKind::OnFuse;
        auto duration = Duration::fromSeconds(fuse);
        if (!duration.ok()) return Result<CarrierRecipe>::failure(duration.status());
        trigger.fuse = duration.value();
        recipe.triggers.push_back(trigger);
    }

    if (damage > 0.0 || !element.empty() || !damageType.empty()) {
        CarrierImpact hit;
        hit.kind       = CarrierImpactKind::EmitHit;
        hit.on         = CarrierTriggerKind::OnHit;
        hit.damage     = damage;
        hit.damageType = damageType;
        hit.element    = element;
        recipe.impacts.push_back(hit);
    }
    if (pierce > 0) {
        CarrierImpact impact;
        impact.kind        = CarrierImpactKind::Pierce;
        impact.on          = CarrierTriggerKind::OnHit;
        impact.pierceCount = pierce;
        recipe.impacts.push_back(impact);
    }
    if (bounce > 0) {
        CarrierImpact impact;
        impact.kind        = CarrierImpactKind::Bounce;
        impact.on          = CarrierTriggerKind::OnHit;
        impact.bounceCount = bounce;
        recipe.impacts.push_back(impact);
    }
    recipe.impacts.push_back({CarrierImpactKind::Release, CarrierTriggerKind::OnHit});

    if (fuse > 0.0) {
        CarrierImpact splashImpact;
        splashImpact.kind         = splash > 0.0 ? CarrierImpactKind::Splash : CarrierImpactKind::EmitHit;
        splashImpact.on           = CarrierTriggerKind::OnFuse;
        splashImpact.damage       = damage;
        splashImpact.splashRadius = splash;
        splashImpact.damageType   = damageType;
        splashImpact.element      = element;
        recipe.impacts.push_back(splashImpact);
        recipe.impacts.push_back({CarrierImpactKind::Release, CarrierTriggerKind::OnFuse});
    }
    if (splash > 0.0 && fuse <= 0.0) {
        CarrierImpact splashImpact;
        splashImpact.kind         = CarrierImpactKind::Splash;
        splashImpact.on           = CarrierTriggerKind::OnHit;
        splashImpact.damage       = damage;
        splashImpact.splashRadius = splash;
        splashImpact.damageType   = damageType;
        splashImpact.element      = element;
        recipe.impacts.push_back(splashImpact);
    }

    recipe.impacts.push_back({CarrierImpactKind::Release, CarrierTriggerKind::OnExpire});

    auto valid = recipe.validate();
    if (!valid) return Result<CarrierRecipe>::failure(valid.status());
    return Result<CarrierRecipe>::success(std::move(recipe));
}

Result<CarrierRecipe> decodeFull(const Value::Object& object) {
    CarrierRecipe recipe;
    auto id = readLogicalId(object, "id", "id");
    if (!id.ok()) return Result<CarrierRecipe>::failure(id.status());
    recipe.id = id.value();

    auto lifetime = readSeconds(object, "lifetime", "lifetime");
    if (!lifetime.ok()) return Result<CarrierRecipe>::failure(lifetime.status());
    recipe.lifetime = lifetime.value();

    if (!readDouble(object, "speed", recipe.speed) || recipe.speed <= 0.0)
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "Recipe requires positive speed", "speed");

    const auto* motion = field(object, "motion");
    if (!motion) motion = field(object, "motionOps");
    if (!motion || !motion->isArray())
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "Full recipe requires motion array", "motion");
    for (std::size_t i = 0; i < motion->arraySize(); ++i) {
        auto op = decodeMotionOp(motion->at(i), "motion[" + std::to_string(i) + "]");
        if (!op.ok()) return Result<CarrierRecipe>::failure(op.status());
        recipe.motionOps.push_back(op.value());
    }

    const auto* triggers = field(object, "triggers");
    if (!triggers || !triggers->isArray())
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "Full recipe requires triggers array", "triggers");
    for (std::size_t i = 0; i < triggers->arraySize(); ++i) {
        auto trigger = decodeTrigger(triggers->at(i), "triggers[" + std::to_string(i) + "]");
        if (!trigger.ok()) return Result<CarrierRecipe>::failure(trigger.status());
        recipe.triggers.push_back(trigger.value());
    }

    const auto* impacts = field(object, "impacts");
    if (!impacts || !impacts->isArray())
        return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "Full recipe requires impacts array", "impacts");
    for (std::size_t i = 0; i < impacts->arraySize(); ++i) {
        auto impact = decodeImpact(impacts->at(i), "impacts[" + std::to_string(i) + "]");
        if (!impact.ok()) return Result<CarrierRecipe>::failure(impact.status());
        recipe.impacts.push_back(impact.value());
    }

    auto valid = recipe.validate();
    if (!valid) return Result<CarrierRecipe>::failure(valid.status());
    return Result<CarrierRecipe>::success(std::move(recipe));
}

}  // namespace

Result<CarrierRecipe> decodeCarrierRecipe(const Value& value) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return failT<CarrierRecipe>(DiagnosticCode::InvalidArgument, "Carrier recipe must be an object", "");
    if (field(*object, "motionOps") || field(*object, "triggers") || field(*object, "impacts")) {
        if (const auto* motion = field(*object, "motion"); motion && motion->isString()) {
            // Preset with convenience motion string plus optional explicit arrays is ambiguous —
            // prefer full form only when motion is an array or motionOps is present.
        }
        if (field(*object, "motionOps") || (field(*object, "motion") && field(*object, "motion")->isArray()))
            return decodeFull(*object);
    }
    if (field(*object, "motion") && field(*object, "motion")->isArray() && field(*object, "triggers") &&
        field(*object, "impacts"))
        return decodeFull(*object);
    return decodePreset(*object);
}

Result<Value> encodeCarrierRecipe(const CarrierRecipe& recipe) {
    auto valid = recipe.validate();
    if (!valid) return Result<Value>::failure(valid.status());

    Value::Object object;
    object.emplace("id", Value(recipe.id.format()));
    object.emplace("lifetime", Value(recipe.lifetime.seconds()));
    object.emplace("speed", Value(recipe.speed));

    Value::Array motion;
    for (const auto& op : recipe.motionOps) {
        Value::Object entry;
        entry.emplace("kind", Value(std::string(motionKindName(op.kind))));
        if (op.gravity != 0.0) entry.emplace("gravity", Value(op.gravity));
        if (op.maxTurnRateDegrees != 0.0) entry.emplace("maxTurnRateDegrees", Value(op.maxTurnRateDegrees));
        if (op.acceleration != 0.0) entry.emplace("acceleration", Value(op.acceleration));
        if (op.avoidLookAhead != 0.0) entry.emplace("avoidLookAhead", Value(op.avoidLookAhead));
        if (op.avoidStrength != 1.0) entry.emplace("avoidStrength", Value(op.avoidStrength));
        if (op.avoidRadiusPadding != 0.0) entry.emplace("avoidRadiusPadding", Value(op.avoidRadiusPadding));
        if (op.curveAmplitude != 0.0) entry.emplace("curveAmplitude", Value(op.curveAmplitude));
        if (op.curveFrequencyHz != 0.0) entry.emplace("curveFrequencyHz", Value(op.curveFrequencyHz));
        motion.push_back(Value(std::move(entry)));
    }
    object.emplace("motion", Value(std::move(motion)));

    Value::Array triggers;
    for (const auto& trigger : recipe.triggers) {
        Value::Object entry;
        entry.emplace("kind", Value(std::string(triggerKindName(trigger.kind))));
        if (trigger.fuse > Duration::zero()) entry.emplace("fuse", Value(trigger.fuse.seconds()));
        if (trigger.interval > Duration::zero()) entry.emplace("interval", Value(trigger.interval.seconds()));
        if (trigger.proximityRadius > 0.0) entry.emplace("proximityRadius", Value(trigger.proximityRadius));
        triggers.push_back(Value(std::move(entry)));
    }
    object.emplace("triggers", Value(std::move(triggers)));

    Value::Array impacts;
    for (const auto& impact : recipe.impacts) {
        Value::Object entry;
        entry.emplace("kind", Value(std::string(impactKindName(impact.kind))));
        entry.emplace("on", Value(std::string(triggerKindName(impact.on))));
        if (impact.damage != 0.0) entry.emplace("damage", Value(impact.damage));
        if (impact.splashRadius != 0.0) entry.emplace("splashRadius", Value(impact.splashRadius));
        if (impact.pierceCount != 0) entry.emplace("pierceCount", Value(static_cast<std::int64_t>(impact.pierceCount)));
        if (impact.bounceCount != 0) entry.emplace("bounceCount", Value(static_cast<std::int64_t>(impact.bounceCount)));
        if (impact.restitution != 1.0) entry.emplace("restitution", Value(impact.restitution));
        if (!impact.damageType.empty()) entry.emplace("damageType", Value(impact.damageType));
        if (!impact.element.empty()) entry.emplace("element", Value(impact.element));
        if (impact.childRecipeId.isValid()) entry.emplace("childRecipeId", Value(impact.childRecipeId.format()));
        impacts.push_back(Value(std::move(entry)));
    }
    object.emplace("impacts", Value(std::move(impacts)));
    return Result<Value>::success(Value(std::move(object)));
}

}  // namespace eve::weapon
