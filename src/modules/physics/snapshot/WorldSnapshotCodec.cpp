#include "physics/World.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/snapshot/Joint3DParameters.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/backend/SimulationBackend.h"

#include <Box2D/Box2D.h>
#include <box3d/box3d.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "physics/snapshot/WorldSnapshotAccess.h"
#include "physics/snapshot/WorldSnapshotCodec.h"

namespace eve::physics::snapshot_detail {
eve::Result<eve::LogicalId> snapshotSchema(std::string_view text) {
    const auto parsed = eve::LogicalId::parse(text);
    if (!parsed)
        return eve::Result<eve::LogicalId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "physics snapshot schema constant is invalid", "schema"));
    return eve::Result<eve::LogicalId>::success(*parsed);
}

eve::Result<void> checkEnvelope(const eve::SnapshotEnvelope& snapshot, std::string_view type, std::string_view schema) {
    if (snapshot.type != type || snapshot.schema.format() != schema)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                   "snapshot type or schema does not belong to this physics world", "snapshot.type"));
    const auto current = type == kWorld2DType ? kSnapshot2DVersion : kSnapshotVersion;
    if (snapshot.schemaVersion.value() != current && snapshot.schemaVersion.value() != current - 1 &&
        !(type == kWorld3DType && snapshot.schemaVersion.value() == 1))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion,
                                   "physics world snapshot schema version is not supported", "snapshot.schemaVersion"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}


eve::Result<ObservationState> parseObservation(const eve::Value::Object&    object,
                                               const eve::SnapshotEnvelope& snapshot) {
    auto tick = readUint64(object, "tick");
    if (!tick) return eve::Result<ObservationState>::failure(tick.status());
    auto revision = readUint64(object, "revision");
    if (!revision) return eve::Result<ObservationState>::failure(revision.status());
    auto stepCount = readUint64(object, "stepCount");
    if (!stepCount) return eve::Result<ObservationState>::failure(stepCount.status());
    auto duration = readInt64(object, "simulatedDurationNs");
    if (!duration) return eve::Result<ObservationState>::failure(duration.status());
    auto lastDelta = readFloat(object, "lastDeltaSeconds");
    if (!lastDelta) return eve::Result<ObservationState>::failure(lastDelta.status());
    if (duration.value() < 0 || revision.value() != stepCount.value() || tick.value() != snapshot.tick.value() ||
        stepCount.value() != snapshot.revision.value()) {
        return eve::Result<ObservationState>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "physics snapshot payload progress disagrees with its envelope",
            "payload.observation"));
    }

    ObservationState result;
    result.value.stepCount         = stepCount.value();
    result.value.lastTick          = eve::SimulationTick(tick.value());
    result.value.simulatedDuration = eve::Duration::fromNanoseconds(duration.value());
    result.value.simulatedSeconds  = result.value.simulatedDuration.seconds();
    result.value.lastDeltaSeconds  = lastDelta.value();
    auto valid = detail::validateSimulationObservation(result.value, "physics.snapshot.observation");
    if (!valid) return eve::Result<ObservationState>::failure(valid.status());
    return eve::Result<ObservationState>::success(std::move(result));
}


bool validBodyType(const std::string& type) { return type == "static" || type == "kinematic" || type == "dynamic"; }

eve::Result<Body2DState> parseBody2D(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"active", "angle", "awake", "bullet", "fixedRotation", "fixtures",
                                             "dynamics", "id", "type", "vx", "vy", "angularVelocity", "x", "y"}))
        return eve::Result<Body2DState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                   "2D physics snapshot body has unknown or missing fields", "payload.bodies"));
    Body2DState state;
    auto        id = readUint64(*object, "id");
    if (!id || id.value() == 0 || id.value() >= static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return eve::Result<Body2DState>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "2D physics snapshot body id is invalid", "payload.bodies.id"));
    state.id  = static_cast<int>(id.value());
    auto type = readString(*object, "type");
    if (!type || !validBodyType(type.value()))
        return eve::Result<Body2DState>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "2D physics snapshot body type is invalid", "payload.bodies.type"));
    state.type = std::move(type).takeValue();
    auto x     = readFloat(*object, "x");
    if (!x) return eve::Result<Body2DState>::failure(x.status());
    state.x = x.value();
    auto y  = readFloat(*object, "y");
    if (!y) return eve::Result<Body2DState>::failure(y.status());
    state.y    = y.value();
    auto angle = readFloat(*object, "angle");
    if (!angle) return eve::Result<Body2DState>::failure(angle.status());
    state.angle = angle.value();
    auto vx     = readFloat(*object, "vx");
    if (!vx) return eve::Result<Body2DState>::failure(vx.status());
    state.vx = vx.value();
    auto vy  = readFloat(*object, "vy");
    if (!vy) return eve::Result<Body2DState>::failure(vy.status());
    state.vy     = vy.value();
    auto angular = readFloat(*object, "angularVelocity");
    if (!angular) return eve::Result<Body2DState>::failure(angular.status());
    state.angularVelocity = angular.value();
    auto active           = readBool(*object, "active");
    if (!active) return eve::Result<Body2DState>::failure(active.status());
    state.active = active.value();
    auto bullet  = readBool(*object, "bullet");
    if (!bullet) return eve::Result<Body2DState>::failure(bullet.status());
    state.bullet = bullet.value();
    auto awake   = readBool(*object, "awake");
    if (!awake) return eve::Result<Body2DState>::failure(awake.status());
    state.awake = awake.value();
    auto fixed  = readBool(*object, "fixedRotation");
    if (!fixed) return eve::Result<Body2DState>::failure(fixed.status());
    state.fixedRotation        = fixed.value();
    const eve::Value* fixtures = field(*object, "fixtures");
    if (!fixtures || !fixtures->isArray())
        return eve::Result<Body2DState>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                        "2D physics snapshot fixtures must be an array",
                                                                        "payload.bodies.fixtures"));
    state.fixtures       = *fixtures;
    const auto* dynamics = field(*object, "dynamics");
    const auto* d        = dynamics ? dynamics->getIf<Value::Object>() : nullptr;
    if (!d || !hasExactFields(*d, {"linearDamping", "angularDamping", "gravityScale", "allowSleep"}))
        return Result<Body2DState>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "Invalid planar body dynamics"));
    auto linear = readFloat(*d, "linearDamping");
    if (!linear) return Result<Body2DState>::failure(linear.status());
    auto angularDamping = readFloat(*d, "angularDamping");
    if (!angularDamping) return Result<Body2DState>::failure(angularDamping.status());
    auto gravity = readFloat(*d, "gravityScale");
    if (!gravity) return Result<Body2DState>::failure(gravity.status());
    auto sleep = readBool(*d, "allowSleep");
    if (!sleep) return Result<Body2DState>::failure(sleep.status());
    if (linear.value() < 0 || angularDamping.value() < 0)
        return Result<Body2DState>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Damping must be nonnegative"));
    state.linearDamping  = linear.value();
    state.angularDamping = angularDamping.value();
    state.gravityScale   = gravity.value();
    state.allowSleep     = sleep.value();
    return eve::Result<Body2DState>::success(std::move(state));
}

eve::Result<Body3DState> parseBody3D(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"active", "awake", "bullet", "dynamics", "fixedRotation",
                                             "id",     "type",  "x",      "y",        "z",
                                             "qx",     "qy",    "qz",     "qw",       "vx",
                                             "vy",     "vz",    "wx",     "wy",       "wz"}))
        return eve::Result<Body3DState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                   "3D physics snapshot body has unknown or missing fields", "payload.bodies"));
    Body3DState state;
    auto        id = readUint64(*object, "id");
    if (!id || id.value() == 0 || id.value() >= static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return eve::Result<Body3DState>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "3D physics snapshot body id is invalid", "payload.bodies.id"));
    state.id  = static_cast<int>(id.value());
    auto type = readString(*object, "type");
    if (!type || !validBodyType(type.value()))
        return eve::Result<Body3DState>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "3D physics snapshot body type is invalid", "payload.bodies.type"));
    state.type = std::move(type).takeValue();
#define EV_READ_BODY3D_FLOAT(name)                                            \
    do {                                                                      \
        auto value = readFloat(*object, #name);                               \
        if (!value) return eve::Result<Body3DState>::failure(value.status()); \
        state.name = value.value();                                           \
    } while (false)
    EV_READ_BODY3D_FLOAT(x);
    EV_READ_BODY3D_FLOAT(y);
    EV_READ_BODY3D_FLOAT(z);
    EV_READ_BODY3D_FLOAT(qx);
    EV_READ_BODY3D_FLOAT(qy);
    EV_READ_BODY3D_FLOAT(qz);
    EV_READ_BODY3D_FLOAT(qw);
    EV_READ_BODY3D_FLOAT(vx);
    EV_READ_BODY3D_FLOAT(vy);
    EV_READ_BODY3D_FLOAT(vz);
    EV_READ_BODY3D_FLOAT(wx);
    EV_READ_BODY3D_FLOAT(wy);
    EV_READ_BODY3D_FLOAT(wz);
#undef EV_READ_BODY3D_FLOAT
    const double quaternionLengthSquared =
        static_cast<double>(state.qx) * state.qx + static_cast<double>(state.qy) * state.qy +
        static_cast<double>(state.qz) * state.qz + static_cast<double>(state.qw) * state.qw;
    if (!std::isfinite(quaternionLengthSquared) || std::abs(quaternionLengthSquared - 1.0) > 1e-4)
        return eve::Result<Body3DState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "3D physics snapshot rotation must be normalized", "payload.bodies.rotation"));
#define EV_READ_BODY3D_BOOL(name)                                             \
    do {                                                                      \
        auto value = readBool(*object, #name);                                \
        if (!value) return eve::Result<Body3DState>::failure(value.status()); \
        state.name = value.value();                                           \
    } while (false)
    EV_READ_BODY3D_BOOL(active);
    EV_READ_BODY3D_BOOL(bullet);
    EV_READ_BODY3D_BOOL(awake);
    EV_READ_BODY3D_BOOL(fixedRotation);
#undef EV_READ_BODY3D_BOOL
    const auto* dynamics = field(*object, "dynamics")->getIf<Value::Object>();
    if (!dynamics ||
        !hasExactFields(*dynamics, {"linearDamping", "angularDamping", "gravityScale", "sleepThreshold", "allowSleep"}))
        return Result<Body3DState>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "Invalid 3D body dynamics fields"));
    for (auto [name, target] : {std::pair{"linearDamping", &state.linearDamping},
                                {"angularDamping", &state.angularDamping},
                                {"gravityScale", &state.gravityScale},
                                {"sleepThreshold", &state.sleepThreshold}}) {
        auto parsed = readFloat(*dynamics, name);
        if (!parsed) return Result<Body3DState>::failure(parsed.status());
        *target = parsed.value();
    }
    auto allowSleep = readBool(*dynamics, "allowSleep");
    if (!allowSleep) return Result<Body3DState>::failure(allowSleep.status());
    state.allowSleep = allowSleep.value();
    if (state.linearDamping < 0 || state.angularDamping < 0 || state.sleepThreshold < 0)
        return Result<Body3DState>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "3D damping and sleep threshold must be nonnegative"));
    return eve::Result<Body3DState>::success(std::move(state));
}

eve::Value bodyValue(const Body& body) {
    eve::Value::Object object;
    object.emplace("active", eve::Value(body.isActive()));
    object.emplace("dynamics", Value(Value::Object{{"linearDamping", Value(body.raw()->GetLinearDamping())},
                                                   {"angularDamping", Value(body.raw()->GetAngularDamping())},
                                                   {"gravityScale", Value(body.raw()->GetGravityScale())},
                                                   {"allowSleep", Value(body.raw()->IsSleepingAllowed())}}));
    object.emplace("angle", eve::Value(body.getAngle()));
    object.emplace("awake", eve::Value(body.isAwake()));
    object.emplace("bullet", eve::Value(body.isBullet()));
    object.emplace("fixedRotation", eve::Value(body.isFixedRotation()));
    object.emplace("id", eve::Value(std::to_string(body.getId())));
    object.emplace("type", eve::Value(body.getType()));
    object.emplace("vx", eve::Value(body.getLinearVelocityX()));
    object.emplace("vy", eve::Value(body.getLinearVelocityY()));
    object.emplace("angularVelocity", eve::Value(body.getAngularVelocity()));
    object.emplace("x", eve::Value(body.getX()));
    object.emplace("y", eve::Value(body.getY()));
    return eve::Value(std::move(object));
}

eve::Value bodyValue(const Body3D& body) {
    eve::Value::Object object;
    object.emplace("dynamics",
                   Value(Value::Object{
                       {"linearDamping", Value(b3Body_GetLinearDamping(eve::physics::box3d::Access::bodyId(body)))},
                       {"angularDamping", Value(b3Body_GetAngularDamping(eve::physics::box3d::Access::bodyId(body)))},
                       {"gravityScale", Value(b3Body_GetGravityScale(eve::physics::box3d::Access::bodyId(body)))},
                       {"sleepThreshold", Value(b3Body_GetSleepThreshold(eve::physics::box3d::Access::bodyId(body)))},
                       {"allowSleep", Value(b3Body_IsSleepEnabled(eve::physics::box3d::Access::bodyId(body)))}}));
    object.emplace("active", eve::Value(body.isActive()));
    object.emplace("awake", eve::Value(body.isAwake()));
    object.emplace("bullet", eve::Value(body.isBullet()));
    object.emplace("fixedRotation", eve::Value(body.isFixedRotation()));
    object.emplace("id", eve::Value(std::to_string(body.getId())));
    object.emplace("type", eve::Value(body.getType()));
    object.emplace("x", eve::Value(body.getX()));
    object.emplace("y", eve::Value(body.getY()));
    object.emplace("z", eve::Value(body.getZ()));
    object.emplace("qx", eve::Value(body.getRotX()));
    object.emplace("qy", eve::Value(body.getRotY()));
    object.emplace("qz", eve::Value(body.getRotZ()));
    object.emplace("qw", eve::Value(body.getRotW()));
    object.emplace("vx", eve::Value(body.getLinearVelocityX()));
    object.emplace("vy", eve::Value(body.getLinearVelocityY()));
    object.emplace("vz", eve::Value(body.getLinearVelocityZ()));
    object.emplace("wx", eve::Value(body.getAngularVelocityX()));
    object.emplace("wy", eve::Value(body.getAngularVelocityY()));
    object.emplace("wz", eve::Value(body.getAngularVelocityZ()));
    return eve::Value(std::move(object));
}

eve::Value fixtureTopologyValue(const Body& body) {
    eve::Value::Array fixtures;
    const b2Body*     rawBody = body.raw();
    for (const b2Fixture* fixture = rawBody ? rawBody->GetFixtureList() : nullptr; fixture;
         fixture                  = fixture->GetNext()) {
        eve::Value::Object object;
        const b2Shape*     shape = fixture->GetShape();
        object.emplace("type", eve::Value(std::to_string(static_cast<int>(shape->GetType()))));
        object.emplace("radius", eve::Value(shape->m_radius));
        object.emplace("material", fixtureMaterialValue(*fixture));
        eve::Value::Array geometry;
        switch (shape->GetType()) {
            case b2Shape::e_circle: {
                const auto* circle = static_cast<const b2CircleShape*>(shape);
                geometry.emplace_back(circle->m_p.x);
                geometry.emplace_back(circle->m_p.y);
                break;
            }
            case b2Shape::e_polygon: {
                const auto* polygon = static_cast<const b2PolygonShape*>(shape);
                for (int index = 0; index < polygon->m_count; ++index) {
                    geometry.emplace_back(polygon->m_vertices[index].x);
                    geometry.emplace_back(polygon->m_vertices[index].y);
                }
                break;
            }
            case b2Shape::e_chain: {
                const auto* chain = static_cast<const b2ChainShape*>(shape);
                for (int index = 0; index < chain->m_count; ++index) {
                    geometry.emplace_back(chain->m_vertices[index].x);
                    geometry.emplace_back(chain->m_vertices[index].y);
                }
                geometry.emplace_back(chain->m_hasPrevVertex);
                geometry.emplace_back(chain->m_hasNextVertex);
                break;
            }
            case b2Shape::e_edge: {
                const auto* edge = static_cast<const b2EdgeShape*>(shape);
                geometry.emplace_back(edge->m_vertex1.x);
                geometry.emplace_back(edge->m_vertex1.y);
                geometry.emplace_back(edge->m_vertex2.x);
                geometry.emplace_back(edge->m_vertex2.y);
                break;
            }
            default: break;
        }
        object.emplace("geometry", eve::Value(std::move(geometry)));
        fixtures.emplace_back(std::move(object));
    }
    return eve::Value(std::move(fixtures));
}

eve::Value shapesTopologyValue(const std::vector<Shape3D*>& shapes) {
    std::vector<Shape3D*> sorted = shapes;
    std::sort(sorted.begin(), sorted.end(),
              [](const Shape3D* left, const Shape3D* right) { return left->getId() < right->getId(); });
    eve::Value::Array values;
    for (const Shape3D* shape : sorted)
        if (shape && shape->isValid()) values.push_back(WorldSnapshotAccess::shapeSource(*shape));
    return eve::Value(std::move(values));
}

eve::Value jointsTopologyValue(const std::vector<Joint3D*>& joints) {
    std::vector<Joint3D*> sorted = joints;
    std::sort(sorted.begin(), sorted.end(),
              [](const Joint3D* left, const Joint3D* right) { return left->getId() < right->getId(); });
    eve::Value::Array values;
    for (const Joint3D* joint : sorted) {
        if (!joint || !joint->isValid()) continue;
        eve::Value::Object object;
        object.emplace("id", eve::Value(std::to_string(joint->getId())));
        object.emplace("kind", eve::Value(joint->getKind()));
        object.emplace("bodyAId", eve::Value(std::to_string(joint->getBodyAId())));
        object.emplace("bodyBId", eve::Value(std::to_string(joint->getBodyBId())));
        const b3Transform frameA = b3Joint_GetLocalFrameA(eve::physics::box3d::Access::jointId(*joint));
        const b3Transform frameB = b3Joint_GetLocalFrameB(eve::physics::box3d::Access::jointId(*joint));
        eve::Value::Array frames;
        for (float component : {static_cast<float>(frameA.p.x), static_cast<float>(frameA.p.y),
                                static_cast<float>(frameA.p.z), frameA.q.v.x, frameA.q.v.y, frameA.q.v.z, frameA.q.s,
                                static_cast<float>(frameB.p.x), static_cast<float>(frameB.p.y),
                                static_cast<float>(frameB.p.z), frameB.q.v.x, frameB.q.v.y, frameB.q.v.z, frameB.q.s})
            frames.emplace_back(component);
        object.emplace("localFrames", eve::Value(std::move(frames)));
        object.emplace("parameters", captureJoint3DParameters(box3d::Access::jointId(*joint)));
        values.emplace_back(std::move(object));
    }
    return eve::Value(std::move(values));
}

eve::Value::Object observationFields(const SimulationObservation& observation, eve::SimulationTick tick) {
    eve::Value::Object object;
    object.emplace("tick", eve::Value(std::to_string(tick.value())));
    object.emplace("revision", eve::Value(std::to_string(observation.stepCount)));
    object.emplace("stepCount", eve::Value(std::to_string(observation.stepCount)));
    object.emplace("simulatedDurationNs", eve::Value(std::to_string(observation.simulatedDuration.nanoseconds())));
    object.emplace("lastDeltaSeconds", eve::Value(observation.lastDeltaSeconds));
    return object;
}

eve::Value make2DPayload(const World& world, const std::vector<Body*>& bodies) {
    const auto         observation = world.simulationObservation();
    eve::Value::Object object      = observationFields(observation, world.simulationTick());
    object.emplace("gravityX", eve::Value(world.getGravityX()));
    object.emplace("gravityY", eve::Value(world.getGravityY()));
    object.emplace("meter", eve::Value(world.getMeter()));
    object.emplace("allowSleep", Value(world.raw()->GetAllowSleeping()));
    std::vector<Body*> sortedBodies = bodies;
    std::sort(sortedBodies.begin(), sortedBodies.end(),
              [](const Body* left, const Body* right) { return left->getId() < right->getId(); });
    eve::Value::Array values;
    values.reserve(sortedBodies.size());
    for (const Body* body : sortedBodies)
        if (body && body->isValid()) {
            eve::Value value = bodyValue(*body);
            value.getIf<eve::Value::Object>()->emplace("fixtures", fixtureTopologyValue(*body));
            values.push_back(std::move(value));
        }
    object.emplace("bodies", eve::Value(std::move(values)));
    return eve::Value(std::move(object));
}

eve::Value make3DPayload(const World3D& world, const std::vector<Body3D*>& bodies, const std::vector<Shape3D*>& shapes,
                         const std::vector<Joint3D*>& joints) {
    const auto         observation = world.simulationObservation();
    eve::Value::Object object      = observationFields(observation, world.simulationTick());
    object.emplace("gravityX", eve::Value(world.getGravityX()));
    object.emplace("gravityY", eve::Value(world.getGravityY()));
    object.emplace("gravityZ", eve::Value(world.getGravityZ()));
    object.emplace("allowSleep", Value(b3World_IsSleepingEnabled(eve::physics::box3d::Access::worldId(world))));
    std::vector<Body3D*> sortedBodies = bodies;
    std::sort(sortedBodies.begin(), sortedBodies.end(),
              [](const Body3D* left, const Body3D* right) { return left->getId() < right->getId(); });
    eve::Value::Array values;
    values.reserve(sortedBodies.size());
    for (const Body3D* body : sortedBodies)
        if (body && body->isValid()) values.push_back(bodyValue(*body));
    object.emplace("bodies", eve::Value(std::move(values)));
    object.emplace("shapes", shapesTopologyValue(shapes));
    object.emplace("joints", jointsTopologyValue(joints));
    return eve::Value(std::move(object));
}

eve::Result<std::vector<Body2DState>> parseBodies2D(const eve::Value::Object& object) {
    const eve::Value* value = field(object, "bodies");
    const auto*       array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array)
        return eve::Result<std::vector<Body2DState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "physics snapshot bodies must be an array", "payload.bodies"));
    std::vector<Body2DState> result;
    result.reserve(array->size());
    for (const eve::Value& entry : *array) {
        auto body = parseBody2D(entry);
        if (!body) return eve::Result<std::vector<Body2DState>>::failure(body.status());
        result.push_back(std::move(body).takeValue());
    }
    return eve::Result<std::vector<Body2DState>>::success(std::move(result));
}

eve::Result<std::vector<Body3DState>> parseBodies3D(const eve::Value::Object& object) {
    const eve::Value* value = field(object, "bodies");
    const auto*       array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array)
        return eve::Result<std::vector<Body3DState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "physics snapshot bodies must be an array", "payload.bodies"));
    std::vector<Body3DState> result;
    result.reserve(array->size());
    for (const eve::Value& entry : *array) {
        auto body = parseBody3D(entry);
        if (!body) return eve::Result<std::vector<Body3DState>>::failure(body.status());
        result.push_back(std::move(body).takeValue());
    }
    return eve::Result<std::vector<Body3DState>>::success(std::move(result));
}

eve::Result<std::unique_ptr<World>> prepareWorld2D(const std::vector<Body2DState>& states, float gravityX,
                                                   float gravityY, float meter,
                                                   const SimulationObservation& observation,
                                                   eve::PersistentId            instanceId) {
    try {
        auto prepared = std::make_unique<World>(gravityX, gravityY, false, meter, instanceId);
        int  maxId    = 0;
        for (const Body2DState& state : states) {
            Body* body = prepared->newBody(state.type, state.x, state.y);
            WorldSnapshotAccess::ownPreparedBody(*prepared, std::unique_ptr<Body>(body));
            WorldSnapshotAccess::setBodyId(*body, state.id);
            maxId                = std::max(maxId, state.id);
            const auto* fixtures = state.fixtures.getIf<eve::Value::Array>();
            for (auto it = fixtures->rbegin(); it != fixtures->rend(); ++it) {
                const auto* fixture = it->getIf<eve::Value::Object>();
                if (!fixture || !hasExactFields(*fixture, {"geometry", "radius", "type", "material"}))
                    return eve::Result<std::unique_ptr<World>>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                               "2D fixture has unknown or missing fields", "payload.bodies.fixtures"));
                auto type = readInt64(*fixture, "type");
                if (!type) return Result<std::unique_ptr<World>>::failure(type.status());
                auto radius = readFloat(*fixture, "radius");
                if (!radius) return Result<std::unique_ptr<World>>::failure(radius.status());
                const eve::Value* geometryValue = field(*fixture, "geometry");
                const auto*       geometry      = geometryValue ? geometryValue->getIf<eve::Value::Array>() : nullptr;
                if (!geometry)
                    return eve::Result<std::unique_ptr<World>>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "2D fixture geometry is malformed",
                                               "payload.bodies.fixtures.geometry"));
                std::vector<float> values;
                values.reserve(geometry->size());
                for (const eve::Value& component : *geometry) {
                    if (!component.isNumeric()) {
                        if (component.isBool()) continue;
                        return eve::Result<std::unique_ptr<World>>::failure(eve::Diagnostic::error(
                            eve::DiagnosticCode::ParseError, "2D fixture coordinate must be numeric",
                            "payload.bodies.fixtures.geometry"));
                    }
                    values.push_back(
                        static_cast<float>(component.isDouble() ? component.asDouble() : component.asInt()) * meter);
                }
                Fixture* created = nullptr;
                if (type.value() == b2Shape::e_circle && values.size() == 2)
                    created = body->newCircleFixture(radius.value() * meter);
                else if (type.value() == b2Shape::e_polygon && values.size() >= 6)
                    created = body->newPolygonFixture(values);
                else if (type.value() == b2Shape::e_chain && values.size() >= 4) {
                    const bool loop = geometry->size() >= 2 && (*geometry)[geometry->size() - 2].isBool() &&
                                      (*geometry)[geometry->size() - 2].asBool() && geometry->back().isBool() &&
                                      geometry->back().asBool();
                    // Box2D stores the closing vertex explicitly; the body factory closes loops itself.
                    if (loop && values.size() >= 8 && values[0] == values[values.size() - 2] &&
                        values[1] == values.back())
                        values.resize(values.size() - 2);
                    created = body->newChainFixture(values, loop);
                }
                if (!created)
                    return eve::Result<std::unique_ptr<World>>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::Unsupported, "2D fixture shape cannot be reconstructed",
                        "payload.bodies.fixtures.type"));
                WorldSnapshotAccess::ownPreparedFixture(*prepared, std::unique_ptr<Fixture>(created));
                auto material = restoreFixtureMaterial(*created, *field(*fixture, "material"));
                if (!material) return Result<std::unique_ptr<World>>::failure(material.status());
            }
            body->raw()->SetLinearDamping(state.linearDamping);
            body->raw()->SetAngularDamping(state.angularDamping);
            body->raw()->SetGravityScale(state.gravityScale);
            body->raw()->SetSleepingAllowed(state.allowSleep);
            body->setFixedRotation(state.fixedRotation);
            body->setBullet(state.bullet);
            body->setActive(state.active);
            body->setAngle(state.angle);
            body->setLinearVelocity(state.vx, state.vy);
            body->setAngularVelocity(state.angularVelocity);
            body->setAwake(state.awake);
        }
        auto restored = WorldSnapshotAccess::finishPrepared(*prepared, maxId + 1, observation);
        if (!restored) return eve::Result<std::unique_ptr<World>>::failure(restored.status());
        return eve::Result<std::unique_ptr<World>>::success(std::move(prepared));
    } catch (const std::exception& error) {
        return eve::Result<std::unique_ptr<World>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, std::string("2D topology preparation failed: ") + error.what(),
            "physics.world.restore.prepare"));
    }
}

eve::Result<std::vector<float>> readFloatArray(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    const auto*       array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array)
        return eve::Result<std::vector<float>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "snapshot field must be an array", std::string(name)));
    std::vector<float> result;
    result.reserve(array->size());
    for (const eve::Value& item : *array) {
        if (!item.isNumeric())
            return eve::Result<std::vector<float>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "snapshot array item must be numeric", std::string(name)));
        const double number = item.isDouble() ? item.asDouble() : item.asInt();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
            return Result<std::vector<float>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Snapshot number exceeds finite float range", std::string(name)));
        result.push_back(static_cast<float>(number));
    }
    return eve::Result<std::vector<float>>::success(std::move(result));
}

eve::Result<std::vector<std::int32_t>> readIntArray(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    const auto*       array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array)
        return eve::Result<std::vector<std::int32_t>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "snapshot field must be an array", std::string(name)));
    std::vector<std::int32_t> result;
    for (const eve::Value& item : *array) {
        if (!item.isString())
            return eve::Result<std::vector<std::int32_t>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "snapshot integer array item must be a string", std::string(name)));
        std::int64_t       parsed = 0;
        const std::string& text   = item.asString();
        const auto [end, ec]      = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (ec != std::errc{} || end != text.data() + text.size() || parsed < INT32_MIN || parsed > INT32_MAX)
            return eve::Result<std::vector<std::int32_t>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError, "snapshot integer array item is invalid", std::string(name)));
        result.push_back(static_cast<std::int32_t>(parsed));
    }
    return eve::Result<std::vector<std::int32_t>>::success(std::move(result));
}

eve::Result<std::unique_ptr<World3D>> prepareWorld3D(const std::vector<Body3DState>& states,
                                                     const eve::Value::Array& shapes, const eve::Value::Array& joints,
                                                     float gravityX, float gravityY, float gravityZ,
                                                     const SimulationObservation& observation,
                                                     eve::PersistentId instanceId, bool legacyParameters,
                                                     bool allowSleep) {
    try {
        auto prepared = WorldSnapshotAccess::makeDetachedWorld3D(gravityX, gravityY, gravityZ, instanceId, allowSleep);
        std::unordered_map<int, Body3D*> bodyById;
        int                              maxBodyId = 0;
        for (const Body3DState& state : states) {
            Body3D* body = prepared->newBody(state.type, state.x, state.y, state.z);
            WorldSnapshotAccess::ownPreparedBody(*prepared, std::unique_ptr<Body3D>(body));
            WorldSnapshotAccess::setBodyId(*body, state.id);
            bodyById.emplace(state.id, body);
            maxBodyId = std::max(maxBodyId, state.id);
            body->setFixedRotation(state.fixedRotation);
            body->setBullet(state.bullet);
            body->setActive(state.active);
            body->setRotation(state.qx, state.qy, state.qz, state.qw);
        }
        std::vector<std::pair<Shape3D*, float>> densities;
        int                                     maxShapeId = 0;
        std::set<int>                           shapeIds;
        for (const eve::Value& value : shapes) {
            const auto* object = value.getIf<eve::Value::Object>();
            if (!object)
                return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::ParseError, "3D shape must be an object", "payload.shapes"));
            const std::initializer_list<std::string_view> sourceFields{"bodyId",
                                                                       "id",
                                                                       "kind",
                                                                       "a",
                                                                       "b",
                                                                       "c",
                                                                       "hullVertices",
                                                                       "hullMaxVertices",
                                                                       "meshVertices",
                                                                       "meshIndices",
                                                                       "meshWeldVertices",
                                                                       "meshWeldTolerance",
                                                                       "meshIdentifyEdges",
                                                                       "meshUseMedianSplit",
                                                                       "meshMaterialIndices",
                                                                       "heightValues",
                                                                       "heightCountX",
                                                                       "heightCountZ",
                                                                       "heightCellSizeX",
                                                                       "heightCellSizeZ",
                                                                       "heightGlobalMin",
                                                                       "heightGlobalMax",
                                                                       "heightClockwise"};
            if (object->size() != sourceFields.size() + (legacyParameters ? 0 : 2))
                return Result<std::unique_ptr<World3D>>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "Shape has unknown or missing fields", "payload.shapes"));
            for (auto name : sourceFields)
                if (!field(*object, name))
                    return Result<std::unique_ptr<World3D>>::failure(Diagnostic::error(
                        DiagnosticCode::ParseError, "Shape source field is missing", std::string(name)));
            auto bodyId = readInt64(*object, "bodyId");
            if (!bodyId) return Result<std::unique_ptr<World3D>>::failure(bodyId.status());
            auto id = readInt64(*object, "id");
            if (!id) return Result<std::unique_ptr<World3D>>::failure(id.status());
            auto kind = readString(*object, "kind");
            if (!kind) return Result<std::unique_ptr<World3D>>::failure(kind.status());
            auto a = readFloat(*object, "a");
            if (!a) return Result<std::unique_ptr<World3D>>::failure(a.status());
            auto b = readFloat(*object, "b");
            if (!b) return Result<std::unique_ptr<World3D>>::failure(b.status());
            auto c = readFloat(*object, "c");
            if (!c) return Result<std::unique_ptr<World3D>>::failure(c.status());
            if (bodyId.value() <= 0 || bodyId.value() >= std::numeric_limits<int>::max() || !id || id.value() <= 0 ||
                id.value() >= std::numeric_limits<int>::max() ||
                !shapeIds.insert(static_cast<int>(id.value())).second || !kind || !a || !b || !c ||
                !bodyById.contains(static_cast<int>(bodyId.value())))
                return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "3D shape references invalid identity or source", "payload.shapes"));
            Body3D*  body  = bodyById.at(static_cast<int>(bodyId.value()));
            Shape3D* shape = nullptr;
            if (kind.value() == "box")
                shape = body->newBoxShape(a.value() * 2.f, b.value() * 2.f, c.value() * 2.f);
            else if (kind.value() == "sphere")
                shape = body->newSphereShape(a.value());
            else if (kind.value() == "capsule")
                shape = body->newCapsuleShape(a.value() * 2.f, b.value());
            else if (kind.value() == "convexHull") {
                auto vertices = readFloatArray(*object, "hullVertices");
                if (!vertices) return Result<std::unique_ptr<World3D>>::failure(vertices.status());
                auto maxVertices = readInt64(*object, "hullMaxVertices");
                if (!maxVertices) return Result<std::unique_ptr<World3D>>::failure(maxVertices.status());
                if (!vertices || !maxVertices)
                    return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "convex hull source is invalid", "payload.shapes"));
                shape = body->newConvexHullShape(vertices.value(), static_cast<int>(maxVertices.value()));
            } else if (kind.value() == "triangleMesh") {
                auto vertices = readFloatArray(*object, "meshVertices");
                if (!vertices) return Result<std::unique_ptr<World3D>>::failure(vertices.status());
                auto indices = readIntArray(*object, "meshIndices");
                if (!indices) return Result<std::unique_ptr<World3D>>::failure(indices.status());
                auto weld = readBool(*object, "meshWeldVertices");
                if (!weld) return Result<std::unique_ptr<World3D>>::failure(weld.status());
                auto tolerance = readFloat(*object, "meshWeldTolerance");
                if (!tolerance) return Result<std::unique_ptr<World3D>>::failure(tolerance.status());
                auto edges = readBool(*object, "meshIdentifyEdges");
                if (!edges) return Result<std::unique_ptr<World3D>>::failure(edges.status());
                auto median = readBool(*object, "meshUseMedianSplit");
                if (!median) return Result<std::unique_ptr<World3D>>::failure(median.status());
                if (!vertices || !indices || !weld || !tolerance || !edges || !median)
                    return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "mesh source is invalid", "payload.shapes"));
                shape = body->newTriangleMeshShape(vertices.value(), indices.value(), weld.value(), tolerance.value(),
                                                   edges.value(), median.value());
            } else if (kind.value() == "heightField") {
                auto heights = readFloatArray(*object, "heightValues");
                if (!heights) return Result<std::unique_ptr<World3D>>::failure(heights.status());
                auto cx = readInt64(*object, "heightCountX");
                if (!cx) return Result<std::unique_ptr<World3D>>::failure(cx.status());
                auto cz = readInt64(*object, "heightCountZ");
                if (!cz) return Result<std::unique_ptr<World3D>>::failure(cz.status());
                auto sx = readFloat(*object, "heightCellSizeX");
                if (!sx) return Result<std::unique_ptr<World3D>>::failure(sx.status());
                auto sz = readFloat(*object, "heightCellSizeZ");
                if (!sz) return Result<std::unique_ptr<World3D>>::failure(sz.status());
                auto mn = readFloat(*object, "heightGlobalMin");
                if (!mn) return Result<std::unique_ptr<World3D>>::failure(mn.status());
                auto mx = readFloat(*object, "heightGlobalMax");
                if (!mx) return Result<std::unique_ptr<World3D>>::failure(mx.status());
                auto clockwise = readBool(*object, "heightClockwise");
                if (!clockwise) return Result<std::unique_ptr<World3D>>::failure(clockwise.status());
                if (!heights || !cx || !cz || !sx || !sz || !mn || !mx || !clockwise)
                    return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "height field source is invalid", "payload.shapes"));
                shape =
                    body->newHeightFieldShape(static_cast<int>(cx.value()), static_cast<int>(cz.value()), sx.value(),
                                              sz.value(), heights.value(), mn.value(), mx.value(), clockwise.value());
            }
            if (!shape)
                return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Unsupported, "3D shape kind cannot be reconstructed", "payload.shapes.kind"));
            WorldSnapshotAccess::ownPreparedShape(*prepared, std::unique_ptr<Shape3D>(shape));
            WorldSnapshotAccess::setShapeId(*shape, static_cast<int>(id.value()));
            if (legacyParameters) {
                if (object->contains("localTransform"))
                    return Result<std::unique_ptr<World3D>>::failure(
                        Diagnostic::error(DiagnosticCode::ParseError, "Legacy shape has unexpected local transform"));
            } else {
                auto transform = readFloatArray(*object, "localTransform");
                if (!transform) return Result<std::unique_ptr<World3D>>::failure(transform.status());
                const auto& t = transform.value();
                if (t.size() != 7 || !b3IsNormalizedQuat({{t[3], t[4], t[5]}, t[6]}))
                    return Result<std::unique_ptr<World3D>>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "Shape transform requires a position and unit quaternion"));
                shape->setLocalTransform(t[0], t[1], t[2], t[3], t[4], t[5], t[6]);
                auto contact = WorldSnapshotAccess::restoreShapeContact(*shape, *object);
                if (!contact) return Result<std::unique_ptr<World3D>>::failure(contact.status());
                densities.emplace_back(shape, contact.value());
            }
            maxShapeId = std::max(maxShapeId, static_cast<int>(id.value()));
        }
        for (auto [shape, density] : densities)
            b3Shape_SetDensity(eve::physics::box3d::Access::shapeId(*shape), density, false);
        auto mass = WorldSnapshotAccess::finishShapeMass(*prepared);
        if (!mass) return Result<std::unique_ptr<World3D>>::failure(mass.status());
        int           maxJointId = 0;
        std::set<int> jointIds;
        for (const eve::Value& value : joints) {
            const auto* object = value.getIf<eve::Value::Object>();
            if (!object ||
                !(legacyParameters
                      ? hasExactFields(*object, {"bodyAId", "bodyBId", "id", "kind", "localFrames"})
                      : hasExactFields(*object, {"bodyAId", "bodyBId", "id", "kind", "localFrames", "parameters"})))
                return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::ParseError, "3D joint has unknown or missing fields", "payload.joints"));
            auto bodyAId = readInt64(*object, "bodyAId");
            if (!bodyAId) return eve::Result<std::unique_ptr<World3D>>::failure(bodyAId.status());
            auto bodyBId = readInt64(*object, "bodyBId");
            if (!bodyBId) return eve::Result<std::unique_ptr<World3D>>::failure(bodyBId.status());
            auto id = readInt64(*object, "id");
            if (!id) return eve::Result<std::unique_ptr<World3D>>::failure(id.status());
            auto kind = readString(*object, "kind");
            if (!kind) return eve::Result<std::unique_ptr<World3D>>::failure(kind.status());
            auto frames = readFloatArray(*object, "localFrames");
            if (!frames) return eve::Result<std::unique_ptr<World3D>>::failure(frames.status());
            if (id.value() <= 0 || id.value() >= std::numeric_limits<int>::max() || bodyAId.value() <= 0 ||
                bodyAId.value() >= std::numeric_limits<int>::max() || bodyBId.value() <= 0 ||
                bodyBId.value() >= std::numeric_limits<int>::max() || bodyAId.value() == bodyBId.value() ||
                !jointIds.insert(static_cast<int>(id.value())).second || frames.value().size() != 14 ||
                !bodyById.contains(static_cast<int>(bodyAId.value())) ||
                !bodyById.contains(static_cast<int>(bodyBId.value())))
                return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "3D joint references invalid bodies or frames", "payload.joints"));
            Body3D*     aBody = bodyById.at(static_cast<int>(bodyAId.value()));
            Body3D*     bBody = bodyById.at(static_cast<int>(bodyBId.value()));
            const auto& f     = frames.value();
            const b3Pos anchorA =
                b3Body_GetWorldPoint(eve::physics::box3d::Access::bodyId(*aBody), b3Pos{f[0], f[1], f[2]});
            const b3Pos anchorB =
                b3Body_GetWorldPoint(eve::physics::box3d::Access::bodyId(*bBody), b3Pos{f[7], f[8], f[9]});
            const b3Quat qa{{f[3], f[4], f[5]}, f[6]};
            const b3Quat qb{{f[10], f[11], f[12]}, f[13]};
            if (!b3IsNormalizedQuat(qa) || !b3IsNormalizedQuat(qb))
                return eve::Result<std::unique_ptr<World3D>>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                           "Joint frame rotation must be normalized", "payload.joints.localFrames"));
            Joint3D* joint = nullptr;
            if (kind.value() == "distance") {
                const b3Vec3 d      = anchorB - anchorA;
                const float  length = std::sqrt(b3Dot(d, d));
                joint = prepared->newDistanceJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, anchorB.x, anchorB.y,
                                                   anchorB.z, length);
            } else if (kind.value() == "revolute") {
                const b3Vec3 axis = b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(*aBody),
                                                          b3RotateVector(qa, b3Vec3_axisZ));
                joint =
                    prepared->newRevoluteJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, axis.x, axis.y, axis.z);
            } else if (kind.value() == "prismatic") {
                const b3Vec3 axis = b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(*aBody),
                                                          b3RotateVector(qa, b3Vec3_axisX));
                joint =
                    prepared->newPrismaticJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, axis.x, axis.y, axis.z);
            } else if (kind.value() == "spherical") {
                const b3Vec3 axis = b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(*aBody),
                                                          b3RotateVector(qa, b3Vec3_axisZ));
                joint =
                    prepared->newSphericalJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, axis.x, axis.y, axis.z);
            } else if (kind.value() == "wheel") {
                const b3Vec3 suspension = b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(*aBody),
                                                                b3RotateVector(qa, b3Vec3_axisX));
                const b3Vec3 wheel      = b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(*bBody),
                                                                b3RotateVector(qb, b3Vec3_axisZ));
                joint = prepared->newWheelJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, suspension.x,
                                                suspension.y, suspension.z, wheel.x, wheel.y, wheel.z);
            } else if (kind.value() == "weld") {
                joint = prepared->newWeldJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z);
            } else if (kind.value() == "motor") {
                joint = prepared->newMotorJoint(aBody, bBody);
            } else if (kind.value() == "parallel") {
                const b3Vec3 axis =
                    b3Body_GetWorldVector(box3d::Access::bodyId(*aBody), b3RotateVector(qa, b3Vec3_axisZ));
                joint = prepared->newParallelJoint(aBody, bBody, axis.x, axis.y, axis.z);
            } else if (kind.value() == "filter") {
                joint = prepared->newFilterJoint(aBody, bBody);
            }
            if (!joint)
                return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Unsupported, "3D joint kind cannot be reconstructed", "payload.joints.kind"));
            WorldSnapshotAccess::ownPreparedJoint(*prepared, std::unique_ptr<Joint3D>(joint));
            b3Joint_SetLocalFrameA(eve::physics::box3d::Access::jointId(*joint),
                                   b3Transform{b3Pos{f[0], f[1], f[2]}, qa});
            b3Joint_SetLocalFrameB(eve::physics::box3d::Access::jointId(*joint),
                                   b3Transform{b3Pos{f[7], f[8], f[9]}, qb});
            if (!legacyParameters) {
                auto parameters =
                    restoreJoint3DParameters(box3d::Access::jointId(*joint), *field(*object, "parameters"));
                if (!parameters) return eve::Result<std::unique_ptr<World3D>>::failure(parameters.status());
            }
            WorldSnapshotAccess::setJointId(*joint, static_cast<int>(id.value()));
            maxJointId = std::max(maxJointId, static_cast<int>(id.value()));
        }
        for (const auto& state : states) {
            auto* body = bodyById.at(state.id);
            b3Body_SetLinearDamping(eve::physics::box3d::Access::bodyId(*body), state.linearDamping);
            b3Body_SetAngularDamping(eve::physics::box3d::Access::bodyId(*body), state.angularDamping);
            b3Body_SetGravityScale(eve::physics::box3d::Access::bodyId(*body), state.gravityScale);
            b3Body_SetSleepThreshold(eve::physics::box3d::Access::bodyId(*body), state.sleepThreshold);
            b3Body_EnableSleep(eve::physics::box3d::Access::bodyId(*body), state.allowSleep);
            body->setLinearVelocity(state.vx, state.vy, state.vz);
            body->setAngularVelocity(state.wx, state.wy, state.wz);
            body->setAwake(state.awake);
        }
        auto restored =
            WorldSnapshotAccess::finishPrepared(*prepared, maxBodyId + 1, maxShapeId + 1, maxJointId + 1, observation);
        if (!restored) return eve::Result<std::unique_ptr<World3D>>::failure(restored.status());
        return eve::Result<std::unique_ptr<World3D>>::success(std::move(prepared));
    } catch (const std::exception& error) {
        return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, std::string("3D topology preparation failed: ") + error.what(),
            "physics.world3d.restore.prepare"));
    }
}

}  // namespace eve::physics::snapshot_detail
