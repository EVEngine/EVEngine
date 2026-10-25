#include <Box2D/Box2D.h>
#include <cmath>
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/snapshot/WorldSnapshotCodec.h"

namespace eve::physics::snapshot_detail {
namespace {
Value defaults(float density) {
    return Value(Value::Object{{"density", Value(density)},
                               {"friction", Value(0.2f)},
                               {"restitution", Value(0.f)},
                               {"sensor", Value(false)},
                               {"category", Value("1")},
                               {"mask", Value("65535")},
                               {"group", Value("0")},
                               {"tag", Value("")}});
}
Result<void> invalid(const char* message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, message, "payload.bodies.fixtures.material"));
}
// Validate the backend's single-precision mass accumulation before its assertion-bearing ResetMassData.
Result<void> validateMassChange(Fixture& changed, float density) {
    const auto* body = changed.getBody()->raw();
    if (body->GetType() != b2_dynamicBody) return Result<void>::success();
    float  mass = 0, inertia = 0;
    b2Vec2 center(0, 0);
    for (const auto* fixture = body->GetFixtureList(); fixture; fixture = fixture->GetNext()) {
        const float candidateDensity = fixture == changed.raw() ? density : fixture->GetDensity();
        if (candidateDensity == 0) continue;
        b2MassData data;
        fixture->GetShape()->ComputeMass(&data, candidateDensity);
        mass += data.mass;
        inertia += data.I;
        center += data.mass * data.center;
        if (!std::isfinite(mass) || !std::isfinite(inertia) || !center.IsValid() || data.mass < 0 || data.I < 0)
            return invalid("Fixture density and geometry overflow body mass properties");
    }
    if (mass > 0) {
        const float inverse = 1.f / mass;
        if (!std::isfinite(inverse)) return invalid("Body inverse mass is not representable");
        center *= inverse;
    }
    if (inertia > 0 && !body->IsFixedRotation()) {
        const float centered = inertia - mass * b2Dot(center, center);
        if (!std::isfinite(centered) || centered <= 0 || !std::isfinite(1.f / centered))
            return invalid("Body rotational inertia is not representable");
    }
    return Result<void>::success();
}
}  // namespace
Value fixtureMaterialValue(const b2Fixture& fixture) {
    auto result = defaults(fixture.GetDensity());
    result.set("friction", Value(fixture.GetFriction()));
    result.set("restitution", Value(fixture.GetRestitution()));
    result.set("sensor", Value(fixture.IsSensor()));
    const auto filter = fixture.GetFilterData();
    result.set("category", Value(std::to_string(filter.categoryBits)));
    result.set("mask", Value(std::to_string(filter.maskBits)));
    result.set("group", Value(std::to_string(filter.groupIndex)));
    const auto* wrapper = static_cast<const Fixture*>(fixture.GetUserData());
    if (wrapper) result.set("tag", Value(wrapper->getTag()));
    return result;
}
Result<void> restoreFixtureMaterial(Fixture& fixture, const Value& value) {
    const auto* o = value.getIf<Value::Object>();
    if (!o || !hasExactFields(*o, {"density", "friction", "restitution", "sensor", "category", "mask", "group", "tag"}))
        return invalid("Fixture material has unknown or missing fields");
    auto density = readFloat(*o, "density");
    if (!density) return Result<void>::failure(density.status());
    auto friction = readFloat(*o, "friction");
    if (!friction) return Result<void>::failure(friction.status());
    auto restitution = readFloat(*o, "restitution");
    if (!restitution) return Result<void>::failure(restitution.status());
    auto sensor = readBool(*o, "sensor");
    if (!sensor) return Result<void>::failure(sensor.status());
    auto category = readInt64(*o, "category");
    if (!category) return Result<void>::failure(category.status());
    auto mask = readInt64(*o, "mask");
    if (!mask) return Result<void>::failure(mask.status());
    auto group = readInt64(*o, "group");
    if (!group) return Result<void>::failure(group.status());
    auto tag = readString(*o, "tag");
    if (!tag) return Result<void>::failure(tag.status());
    if (density.value() < 0 || friction.value() < 0 || restitution.value() < 0 || category.value() < 0 ||
        category.value() > 65535 || mask.value() < 0 || mask.value() > 65535 || group.value() < -32768 ||
        group.value() > 32767)
        return invalid("Fixture material/filter value is outside its valid range");
    auto massCheck = validateMassChange(fixture, density.value());
    if (!massCheck) return massCheck;
    fixture.setDensity(density.value());
    fixture.setFriction(friction.value());
    fixture.setRestitution(restitution.value());
    fixture.setSensor(sensor.value());
    fixture.setCategoryBits(static_cast<int>(category.value()));
    fixture.setMaskBits(static_cast<int>(mask.value()));
    fixture.setGroupIndex(static_cast<int>(group.value()));
    fixture.setTag(tag.value());
    return Result<void>::success();
}
Result<void> migratePlanarV2(Value& payload) {
    auto* o = payload.getIf<Value::Object>();
    if (!o || !hasExactFields(*o, {"bodies", "gravityX", "gravityY", "meter", "tick", "revision", "stepCount",
                                   "simulatedDurationNs", "lastDeltaSeconds"}))
        return invalid("Invalid v2 world fields");
    auto* bodies = o->at("bodies").getIf<Value::Array>();
    if (!bodies) return invalid("Invalid v2 body array");
    for (auto& body : *bodies) {
        auto* b = body.getIf<Value::Object>();
        if (!b || b->contains("dynamics")) return invalid("Invalid v2 body fields");
        auto* fixtures = body.find("fixtures");
        auto* items    = fixtures ? fixtures->getIf<Value::Array>() : nullptr;
        if (!items) return invalid("Invalid v2 fixture array");
        for (auto& fixture : *items) {
            auto* f = fixture.getIf<Value::Object>();
            if (!f || !hasExactFields(*f, {"geometry", "radius", "type"})) return invalid("Invalid v2 fixture fields");
            auto type = readInt64(*f, "type");
            if (!type) return Result<void>::failure(type.status());
            f->emplace("material", defaults(type.value() == b2Shape::e_chain ? 0.f : 1.f));
        }
        b->emplace("dynamics", Value(Value::Object{{"linearDamping", Value(0.f)},
                                                   {"angularDamping", Value(0.f)},
                                                   {"gravityScale", Value(1.f)},
                                                   {"allowSleep", Value(true)}}));
    }
    o->emplace("allowSleep", Value(false));
    o->emplace("joints", Value(Value::Array{}));
    return Result<void>::success();
}
}  // namespace eve::physics::snapshot_detail
