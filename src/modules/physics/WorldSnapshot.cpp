#include "physics/World.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/backend/SimulationBackend.h"
#include "physics/World3D.h"

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

namespace eve::physics {

struct WorldSnapshotAccess {
    static eve::Value shapeSource(const Shape3D& shape) {
        eve::Value::Object object;
        object.emplace("bodyId", eve::Value(std::to_string(shape.body_ ? shape.body_->getId() : -1)));
        object.emplace("id", eve::Value(std::to_string(shape.id_)));
        object.emplace("kind", eve::Value(shape.getKind()));
        object.emplace("a", eve::Value(shape.a_));
        object.emplace("b", eve::Value(shape.b_));
        object.emplace("c", eve::Value(shape.c_));
        auto floats = [](const std::vector<float>& source) {
            eve::Value::Array values;
            values.reserve(source.size());
            for (float value : source) values.emplace_back(value);
            return eve::Value(std::move(values));
        };
        auto integers = [](const auto& source) {
            eve::Value::Array values;
            values.reserve(source.size());
            for (auto value : source) values.emplace_back(std::to_string(static_cast<std::int64_t>(value)));
            return eve::Value(std::move(values));
        };
        object.emplace("hullVertices", floats(shape.hullVertices_));
        object.emplace("hullMaxVertices", eve::Value(std::to_string(shape.hullMaxVertices_)));
        object.emplace("meshVertices", floats(shape.meshVertices_));
        object.emplace("meshIndices", integers(shape.meshIndices_));
        object.emplace("meshWeldVertices", eve::Value(shape.meshWeldVertices_));
        object.emplace("meshWeldTolerance", eve::Value(shape.meshWeldTolerance_));
        object.emplace("meshIdentifyEdges", eve::Value(shape.meshIdentifyEdges_));
        object.emplace("meshUseMedianSplit", eve::Value(shape.meshUseMedianSplit_));
        object.emplace("meshMaterialIndices", integers(shape.meshMaterialIndices_));
        object.emplace("heightValues", floats(shape.heightValues_));
        object.emplace("heightCountX", eve::Value(std::to_string(shape.heightCountX_)));
        object.emplace("heightCountZ", eve::Value(std::to_string(shape.heightCountZ_)));
        object.emplace("heightCellSizeX", eve::Value(shape.heightCellSizeX_));
        object.emplace("heightCellSizeZ", eve::Value(shape.heightCellSizeZ_));
        object.emplace("heightGlobalMin", eve::Value(shape.heightGlobalMin_));
        object.emplace("heightGlobalMax", eve::Value(shape.heightGlobalMax_));
        object.emplace("heightClockwise", eve::Value(shape.heightClockwise_));
        return eve::Value(std::move(object));
    }

    static void setBodyId(Body& body, int id) { body.id_ = id; }
    static void setBodyId(Body3D& body, int id) { body.id_ = id; }
    static void setShapeId(Shape3D& shape, int id) { shape.id_ = id; }
    static void setJointId(Joint3D& joint, int id) { joint.id_ = id; }
    static std::unique_ptr<World3D> makeDetachedWorld3D(float gx, float gy, float gz,
                                                        eve::PersistentId instanceId) {
        return std::unique_ptr<World3D>(new World3D(gx, gy, gz, false, instanceId, false));
    }
    static eve::Result<void> finishPrepared(World& world, int nextId,
                                            const SimulationObservation& observation) {
        world.nextId_ = nextId;
        auto restored = world.simulation_->restoreObservation(observation);
        if (!restored) return restored;
        world.simulationTick_ = observation.lastTick;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    static eve::Result<void> finishPrepared(World3D& world, int nextBodyId, int nextShapeId, int nextJointId,
                                            const SimulationObservation& observation) {
        world.nextId_ = nextBodyId;
        world.nextShapeId_ = nextShapeId;
        world.nextJointId_ = nextJointId;
        auto restored = world.simulation_->restoreObservation(observation);
        if (!restored) return restored;
        world.simulationTick_ = observation.lastTick;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    static void adopt(World& live, World& prepared) { live.adoptPreparedTopology(prepared); }
    static void adopt(World3D& live, World3D& prepared) { live.adoptPreparedTopology(prepared); }
};
namespace {

constexpr std::string_view kWorld2DType     = "physics.world2d";
constexpr std::string_view kWorld2DSchema   = "physics:world2d";
constexpr std::string_view kWorld3DType     = "physics.world3d";
constexpr std::string_view kWorld3DSchema   = "physics:world3d";
constexpr std::uint64_t    kSnapshotVersion = 2;

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

bool hasExactFields(const eve::Value::Object& object, std::initializer_list<std::string_view> expected) {
    if (object.size() != expected.size()) return false;
    for (const std::string_view field : expected) {
        if (!object.contains(std::string(field))) return false;
    }
    return true;
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

eve::Result<std::string> readString(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isString())
        return failure<std::string>(eve::DiagnosticCode::ParseError, "snapshot field must be a string",
                                    std::string(name));
    return eve::Result<std::string>::success(value->asString());
}

eve::Result<std::uint64_t> readUint64(const eve::Value::Object& object, std::string_view name) {
    auto text = readString(object, name);
    if (!text) return eve::Result<std::uint64_t>::failure(text.status());
    std::uint64_t      output = 0;
    const std::string& value  = text.value();
    const auto [end, error]   = std::from_chars(value.data(), value.data() + value.size(), output);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size())
        return failure<std::uint64_t>(eve::DiagnosticCode::ParseError,
                                      "snapshot integer is not a uint64 decimal string", std::string(name));
    return eve::Result<std::uint64_t>::success(output);
}

eve::Result<std::int64_t> readInt64(const eve::Value::Object& object, std::string_view name) {
    auto text = readString(object, name);
    if (!text) return eve::Result<std::int64_t>::failure(text.status());
    std::int64_t       output = 0;
    const std::string& value  = text.value();
    const auto [end, error]   = std::from_chars(value.data(), value.data() + value.size(), output);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size())
        return failure<std::int64_t>(eve::DiagnosticCode::ParseError, "snapshot integer is not an int64 decimal string",
                                     std::string(name));
    return eve::Result<std::int64_t>::success(output);
}

eve::Result<double> readNumber(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isNumeric())
        return failure<double>(eve::DiagnosticCode::ParseError, "snapshot field must be numeric", std::string(name));
    const double result = value->isDouble() ? value->asDouble() : static_cast<double>(value->asInt());
    if (!std::isfinite(result))
        return failure<double>(eve::DiagnosticCode::InvalidArgument, "snapshot numeric field must be finite",
                               std::string(name));
    return eve::Result<double>::success(result);
}

eve::Result<float> readFloat(const eve::Value::Object& object, std::string_view name) {
    auto number = readNumber(object, name);
    if (!number) return eve::Result<float>::failure(number.status());
    if (number.value() < -std::numeric_limits<float>::max() || number.value() > std::numeric_limits<float>::max())
        return failure<float>(eve::DiagnosticCode::InvalidArgument, "snapshot numeric field is outside float range",
                              std::string(name));
    return eve::Result<float>::success(static_cast<float>(number.value()));
}

eve::Result<bool> readBool(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isBool())
        return failure<bool>(eve::DiagnosticCode::ParseError, "snapshot field must be boolean", std::string(name));
    return eve::Result<bool>::success(value->asBool());
}

eve::Result<eve::LogicalId> snapshotSchema(std::string_view text) {
    const auto parsed = eve::LogicalId::parse(text);
    if (!parsed)
        return failure<eve::LogicalId>(eve::DiagnosticCode::InvariantViolation,
                                       "physics snapshot schema constant is invalid", "schema");
    return eve::Result<eve::LogicalId>::success(*parsed);
}

eve::Result<void> checkEnvelope(const eve::SnapshotEnvelope& snapshot, std::string_view type, std::string_view schema) {
    if (snapshot.type != type || snapshot.schema.format() != schema)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                   "snapshot type or schema does not belong to this physics world", "snapshot.type"));
    if (snapshot.schemaVersion.value() != kSnapshotVersion && snapshot.schemaVersion.value() != 1)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion,
                                   "physics world snapshot schema version is not supported", "snapshot.schemaVersion"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

struct ObservationState {
    SimulationObservation value;
};

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
        return failure<ObservationState>(eve::DiagnosticCode::Conflict,
                                         "physics snapshot payload progress disagrees with its envelope",
                                         "payload.observation");
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

struct Body2DState {
    int         id = 0;
    std::string type;
    float       x = 0.f, y = 0.f, angle = 0.f;
    float       vx = 0.f, vy = 0.f, angularVelocity = 0.f;
    bool        active = false, bullet = false, awake = false, fixedRotation = false;
    eve::Value  fixtures;
};

struct Body3DState {
    int         id = 0;
    std::string type;
    float       x = 0.f, y = 0.f, z = 0.f;
    float       qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    float       vx = 0.f, vy = 0.f, vz = 0.f;
    float       wx = 0.f, wy = 0.f, wz = 0.f;
    bool        active = false, bullet = false, awake = false, fixedRotation = false;
};

bool validBodyType(const std::string& type) { return type == "static" || type == "kinematic" || type == "dynamic"; }

eve::Result<Body2DState> parseBody2D(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"active", "angle", "awake", "bullet", "fixedRotation", "fixtures", "id", "type", "vx",
                                             "vy", "angularVelocity", "x", "y"}))
        return failure<Body2DState>(eve::DiagnosticCode::ParseError,
                                    "2D physics snapshot body has unknown or missing fields", "payload.bodies");
    Body2DState state;
    auto        id = readUint64(*object, "id");
    if (!id || id.value() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return failure<Body2DState>(eve::DiagnosticCode::ParseError, "2D physics snapshot body id is invalid",
                                    "payload.bodies.id");
    state.id  = static_cast<int>(id.value());
    auto type = readString(*object, "type");
    if (!type || !validBodyType(type.value()))
        return failure<Body2DState>(eve::DiagnosticCode::InvalidArgument, "2D physics snapshot body type is invalid",
                                    "payload.bodies.type");
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
    state.fixedRotation = fixed.value();
    const eve::Value* fixtures = field(*object, "fixtures");
    if (!fixtures || !fixtures->isArray())
        return failure<Body2DState>(eve::DiagnosticCode::ParseError,
                                    "2D physics snapshot fixtures must be an array", "payload.bodies.fixtures");
    state.fixtures = *fixtures;
    return eve::Result<Body2DState>::success(std::move(state));
}

eve::Result<Body3DState> parseBody3D(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"active", "awake", "bullet", "fixedRotation", "id", "type", "x", "y", "z",
                                             "qx", "qy", "qz", "qw", "vx", "vy", "vz", "wx", "wy", "wz"}))
        return failure<Body3DState>(eve::DiagnosticCode::ParseError,
                                    "3D physics snapshot body has unknown or missing fields", "payload.bodies");
    Body3DState state;
    auto        id = readUint64(*object, "id");
    if (!id || id.value() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return failure<Body3DState>(eve::DiagnosticCode::ParseError, "3D physics snapshot body id is invalid",
                                    "payload.bodies.id");
    state.id  = static_cast<int>(id.value());
    auto type = readString(*object, "type");
    if (!type || !validBodyType(type.value()))
        return failure<Body3DState>(eve::DiagnosticCode::InvalidArgument, "3D physics snapshot body type is invalid",
                                    "payload.bodies.type");
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
    if (!(quaternionLengthSquared > 1e-16))
        return failure<Body3DState>(eve::DiagnosticCode::InvalidArgument,
                                    "3D physics snapshot rotation must be non-zero", "payload.bodies.rotation");
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
    return eve::Result<Body3DState>::success(std::move(state));
}

eve::Value bodyValue(const Body& body) {
    eve::Value::Object object;
    object.emplace("active", eve::Value(body.isActive()));
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
    const b2Body* rawBody = body.raw();
    for (const b2Fixture* fixture = rawBody ? rawBody->GetFixtureList() : nullptr; fixture;
         fixture = fixture->GetNext()) {
        eve::Value::Object object;
        const b2Shape* shape = fixture->GetShape();
        object.emplace("type", eve::Value(std::to_string(static_cast<int>(shape->GetType()))));
        object.emplace("radius", eve::Value(shape->m_radius));
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
    std::sort(sorted.begin(), sorted.end(), [](const Shape3D* left, const Shape3D* right) {
        return left->getId() < right->getId();
    });
    eve::Value::Array values;
    for (const Shape3D* shape : sorted)
        if (shape && shape->isValid()) values.push_back(WorldSnapshotAccess::shapeSource(*shape));
    return eve::Value(std::move(values));
}

eve::Value jointsTopologyValue(const std::vector<Joint3D*>& joints) {
    std::vector<Joint3D*> sorted = joints;
    std::sort(sorted.begin(), sorted.end(), [](const Joint3D* left, const Joint3D* right) {
        return left->getId() < right->getId();
    });
    eve::Value::Array values;
    for (const Joint3D* joint : sorted) {
        if (!joint || !joint->isValid()) continue;
        eve::Value::Object object;
        object.emplace("id", eve::Value(std::to_string(joint->getId())));
        object.emplace("kind", eve::Value(joint->getKind()));
        object.emplace("bodyAId", eve::Value(std::to_string(joint->getBodyAId())));
        object.emplace("bodyBId", eve::Value(std::to_string(joint->getBodyBId())));
        const b3Transform frameA = b3Joint_GetLocalFrameA(joint->raw());
        const b3Transform frameB = b3Joint_GetLocalFrameB(joint->raw());
        eve::Value::Array frames;
        for (float component : {static_cast<float>(frameA.p.x), static_cast<float>(frameA.p.y),
                                static_cast<float>(frameA.p.z), frameA.q.v.x, frameA.q.v.y, frameA.q.v.z, frameA.q.s,
                                static_cast<float>(frameB.p.x), static_cast<float>(frameB.p.y),
                                static_cast<float>(frameB.p.z), frameB.q.v.x, frameB.q.v.y, frameB.q.v.z, frameB.q.s})
            frames.emplace_back(component);
        object.emplace("localFrames", eve::Value(std::move(frames)));
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

eve::Value make3DPayload(const World3D& world, const std::vector<Body3D*>& bodies,
                         const std::vector<Shape3D*>& shapes, const std::vector<Joint3D*>& joints) {
    const auto         observation = world.simulationObservation();
    eve::Value::Object object      = observationFields(observation, world.simulationTick());
    object.emplace("gravityX", eve::Value(world.getGravityX()));
    object.emplace("gravityY", eve::Value(world.getGravityY()));
    object.emplace("gravityZ", eve::Value(world.getGravityZ()));
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

template <typename BodyState>
eve::Result<void> matchBodyIds(const std::vector<BodyState>& states, std::set<int>& ids) {
    for (const BodyState& state : states) {
        if (!ids.insert(state.id).second)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "physics snapshot contains a duplicate body id", "payload.bodies.id"));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::vector<Body2DState>> parseBodies2D(const eve::Value::Object& object) {
    const eve::Value* value = field(object, "bodies");
    const auto*       array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array)
        return failure<std::vector<Body2DState>>(eve::DiagnosticCode::ParseError,
                                                 "physics snapshot bodies must be an array", "payload.bodies");
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
        return failure<std::vector<Body3DState>>(eve::DiagnosticCode::ParseError,
                                                 "physics snapshot bodies must be an array", "payload.bodies");
    std::vector<Body3DState> result;
    result.reserve(array->size());
    for (const eve::Value& entry : *array) {
        auto body = parseBody3D(entry);
        if (!body) return eve::Result<std::vector<Body3DState>>::failure(body.status());
        result.push_back(std::move(body).takeValue());
    }
    return eve::Result<std::vector<Body3DState>>::success(std::move(result));
}

eve::Result<std::unique_ptr<World>> prepareWorld2D(const std::vector<Body2DState>& states,
                                                   float gravityX, float gravityY, float meter,
                                                   const SimulationObservation& observation,
                                                   eve::PersistentId instanceId) {
    try {
        auto prepared = std::make_unique<World>(gravityX, gravityY, false, meter, instanceId);
        int maxId = 0;
        for (const Body2DState& state : states) {
            Body* body = prepared->newBody(state.type, state.x, state.y);
            WorldSnapshotAccess::setBodyId(*body, state.id);
            maxId = std::max(maxId, state.id);
            const auto* fixtures = state.fixtures.getIf<eve::Value::Array>();
            for (auto it = fixtures->rbegin(); it != fixtures->rend(); ++it) {
                const auto* fixture = it->getIf<eve::Value::Object>();
                if (!fixture || !hasExactFields(*fixture, {"geometry", "radius", "type"}))
                    return failure<std::unique_ptr<World>>(eve::DiagnosticCode::ParseError,
                                                           "2D fixture has unknown or missing fields",
                                                           "payload.bodies.fixtures");
                auto type = readInt64(*fixture, "type");
                auto radius = readFloat(*fixture, "radius");
                const eve::Value* geometryValue = field(*fixture, "geometry");
                const auto* geometry = geometryValue ? geometryValue->getIf<eve::Value::Array>() : nullptr;
                if (!type || !radius || !geometry)
                    return failure<std::unique_ptr<World>>(eve::DiagnosticCode::ParseError,
                                                           "2D fixture geometry is malformed",
                                                           "payload.bodies.fixtures.geometry");
                std::vector<float> values;
                values.reserve(geometry->size());
                for (const eve::Value& component : *geometry) {
                    if (!component.isNumeric()) {
                        if (component.isBool()) continue;
                        return failure<std::unique_ptr<World>>(eve::DiagnosticCode::ParseError,
                                                               "2D fixture coordinate must be numeric",
                                                               "payload.bodies.fixtures.geometry");
                    }
                    values.push_back(static_cast<float>(component.isDouble() ? component.asDouble() : component.asInt()) * meter);
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
                    created = body->newChainFixture(values, loop);
                }
                if (!created)
                    return failure<std::unique_ptr<World>>(eve::DiagnosticCode::Unsupported,
                                                           "2D fixture shape cannot be reconstructed",
                                                           "payload.bodies.fixtures.type");
            }
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
        return failure<std::unique_ptr<World>>(eve::DiagnosticCode::Failed,
                                               std::string("2D topology preparation failed: ") + error.what(),
                                               "physics.world.restore.prepare");
    }
}

eve::Result<std::vector<float>> readFloatArray(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    const auto* array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array) return failure<std::vector<float>>(eve::DiagnosticCode::ParseError, "snapshot field must be an array", std::string(name));
    std::vector<float> result;
    result.reserve(array->size());
    for (const eve::Value& item : *array) {
        if (!item.isNumeric()) return failure<std::vector<float>>(eve::DiagnosticCode::ParseError, "snapshot array item must be numeric", std::string(name));
        result.push_back(static_cast<float>(item.isDouble() ? item.asDouble() : item.asInt()));
    }
    return eve::Result<std::vector<float>>::success(std::move(result));
}

eve::Result<std::vector<std::int32_t>> readIntArray(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    const auto* array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array) return failure<std::vector<std::int32_t>>(eve::DiagnosticCode::ParseError, "snapshot field must be an array", std::string(name));
    std::vector<std::int32_t> result;
    for (const eve::Value& item : *array) {
        if (!item.isString()) return failure<std::vector<std::int32_t>>(eve::DiagnosticCode::ParseError, "snapshot integer array item must be a string", std::string(name));
        std::int64_t parsed = 0;
        const std::string& text = item.asString();
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (ec != std::errc{} || end != text.data() + text.size() || parsed < INT32_MIN || parsed > INT32_MAX)
            return failure<std::vector<std::int32_t>>(eve::DiagnosticCode::ParseError, "snapshot integer array item is invalid", std::string(name));
        result.push_back(static_cast<std::int32_t>(parsed));
    }
    return eve::Result<std::vector<std::int32_t>>::success(std::move(result));
}

eve::Result<std::unique_ptr<World3D>> prepareWorld3D(const std::vector<Body3DState>& states,
                                                     const eve::Value::Array& shapes,
                                                     const eve::Value::Array& joints,
                                                     float gravityX, float gravityY, float gravityZ,
                                                     const SimulationObservation& observation,
                                                     eve::PersistentId instanceId) {
    try {
        auto prepared = WorldSnapshotAccess::makeDetachedWorld3D(gravityX, gravityY, gravityZ, instanceId);
        std::unordered_map<int, Body3D*> bodyById;
        int maxBodyId = 0;
        for (const Body3DState& state : states) {
            Body3D* body = prepared->newBody(state.type, state.x, state.y, state.z);
            WorldSnapshotAccess::setBodyId(*body, state.id);
            bodyById.emplace(state.id, body);
            maxBodyId = std::max(maxBodyId, state.id);
            body->setFixedRotation(state.fixedRotation);
            body->setBullet(state.bullet);
            body->setActive(state.active);
            body->setRotation(state.qx, state.qy, state.qz, state.qw);
            body->setLinearVelocity(state.vx, state.vy, state.vz);
            body->setAngularVelocity(state.wx, state.wy, state.wz);
            body->setAwake(state.awake);
        }
        int maxShapeId = 0;
        std::set<int> shapeIds;
        for (const eve::Value& value : shapes) {
            const auto* object = value.getIf<eve::Value::Object>();
            if (!object) return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::ParseError, "3D shape must be an object", "payload.shapes");
            auto bodyId = readInt64(*object, "bodyId");
            auto id = readInt64(*object, "id");
            auto kind = readString(*object, "kind");
            auto a = readFloat(*object, "a"); auto b = readFloat(*object, "b"); auto c = readFloat(*object, "c");
            if (!bodyId || !id || id.value() <= 0 || id.value() > std::numeric_limits<int>::max() ||
                !shapeIds.insert(static_cast<int>(id.value())).second || !kind || !a || !b || !c ||
                !bodyById.contains(static_cast<int>(bodyId.value())))
                return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::Conflict, "3D shape references invalid identity or source", "payload.shapes");
            Body3D* body = bodyById.at(static_cast<int>(bodyId.value()));
            Shape3D* shape = nullptr;
            if (kind.value() == "box") shape = body->newBoxShape(a.value() * 2.f, b.value() * 2.f, c.value() * 2.f);
            else if (kind.value() == "sphere") shape = body->newSphereShape(a.value());
            else if (kind.value() == "capsule") shape = body->newCapsuleShape(a.value() * 2.f, b.value());
            else if (kind.value() == "convexHull") {
                auto vertices = readFloatArray(*object, "hullVertices"); auto maxVertices = readInt64(*object, "hullMaxVertices");
                if (!vertices || !maxVertices) return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::ParseError, "convex hull source is invalid", "payload.shapes");
                shape = body->newConvexHullShape(vertices.value(), static_cast<int>(maxVertices.value()));
            } else if (kind.value() == "triangleMesh") {
                auto vertices = readFloatArray(*object, "meshVertices"); auto indices = readIntArray(*object, "meshIndices");
                auto weld = readBool(*object, "meshWeldVertices"); auto tolerance = readFloat(*object, "meshWeldTolerance");
                auto edges = readBool(*object, "meshIdentifyEdges"); auto median = readBool(*object, "meshUseMedianSplit");
                if (!vertices || !indices || !weld || !tolerance || !edges || !median) return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::ParseError, "mesh source is invalid", "payload.shapes");
                shape = body->newTriangleMeshShape(vertices.value(), indices.value(), weld.value(), tolerance.value(), edges.value(), median.value());
            } else if (kind.value() == "heightField") {
                auto heights = readFloatArray(*object, "heightValues"); auto cx = readInt64(*object, "heightCountX"); auto cz = readInt64(*object, "heightCountZ");
                auto sx = readFloat(*object, "heightCellSizeX"); auto sz = readFloat(*object, "heightCellSizeZ"); auto mn = readFloat(*object, "heightGlobalMin"); auto mx = readFloat(*object, "heightGlobalMax"); auto clockwise = readBool(*object, "heightClockwise");
                if (!heights || !cx || !cz || !sx || !sz || !mn || !mx || !clockwise) return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::ParseError, "height field source is invalid", "payload.shapes");
                shape = body->newHeightFieldShape(static_cast<int>(cx.value()), static_cast<int>(cz.value()), sx.value(), sz.value(), heights.value(), mn.value(), mx.value(), clockwise.value());
            }
            if (!shape) return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::Unsupported, "3D shape kind cannot be reconstructed", "payload.shapes.kind");
            WorldSnapshotAccess::setShapeId(*shape, static_cast<int>(id.value()));
            maxShapeId = std::max(maxShapeId, static_cast<int>(id.value()));
        }
        int maxJointId = 0;
        std::set<int> jointIds;
        for (const eve::Value& value : joints) {
            const auto* object = value.getIf<eve::Value::Object>();
            if (!object || !hasExactFields(*object, {"bodyAId", "bodyBId", "id", "kind", "localFrames"}))
                return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::ParseError, "3D joint has unknown or missing fields", "payload.joints");
            auto bodyAId = readInt64(*object, "bodyAId"); auto bodyBId = readInt64(*object, "bodyBId");
            auto id = readInt64(*object, "id");
            auto kind = readString(*object, "kind"); auto frames = readFloatArray(*object, "localFrames");
            if (!bodyAId || !bodyBId || !id || id.value() <= 0 || id.value() > std::numeric_limits<int>::max() ||
                !jointIds.insert(static_cast<int>(id.value())).second || !kind || !frames || frames.value().size() != 14 ||
                !bodyById.contains(static_cast<int>(bodyAId.value())) || !bodyById.contains(static_cast<int>(bodyBId.value())))
                return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::Conflict, "3D joint references invalid bodies or frames", "payload.joints");
            Body3D* aBody = bodyById.at(static_cast<int>(bodyAId.value())); Body3D* bBody = bodyById.at(static_cast<int>(bodyBId.value()));
            const auto& f = frames.value();
            const b3Pos anchorA = b3Body_GetWorldPoint(aBody->raw(), b3Pos{f[0], f[1], f[2]});
            const b3Pos anchorB = b3Body_GetWorldPoint(bBody->raw(), b3Pos{f[7], f[8], f[9]});
            const b3Quat qa{{f[3], f[4], f[5]}, f[6]}; const b3Quat qb{{f[10], f[11], f[12]}, f[13]};
            Joint3D* joint = nullptr;
            if (kind.value() == "distance") {
                const b3Vec3 d = anchorB - anchorA; const float length = std::sqrt(b3Dot(d, d));
                joint = prepared->newDistanceJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, anchorB.x, anchorB.y, anchorB.z, length);
            } else if (kind.value() == "revolute") {
                const b3Vec3 axis = b3Body_GetWorldVector(aBody->raw(), b3RotateVector(qa, b3Vec3_axisZ));
                joint = prepared->newRevoluteJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, axis.x, axis.y, axis.z);
            } else if (kind.value() == "prismatic") {
                const b3Vec3 axis = b3Body_GetWorldVector(aBody->raw(), b3RotateVector(qa, b3Vec3_axisX));
                joint = prepared->newPrismaticJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, axis.x, axis.y, axis.z);
            } else if (kind.value() == "spherical") {
                const b3Vec3 axis = b3Body_GetWorldVector(aBody->raw(), b3RotateVector(qa, b3Vec3_axisZ));
                joint = prepared->newSphericalJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, axis.x, axis.y, axis.z);
            } else if (kind.value() == "wheel") {
                const b3Vec3 suspension = b3Body_GetWorldVector(aBody->raw(), b3RotateVector(qa, b3Vec3_axisX));
                const b3Vec3 wheel = b3Body_GetWorldVector(bBody->raw(), b3RotateVector(qb, b3Vec3_axisZ));
                joint = prepared->newWheelJoint(aBody, bBody, anchorA.x, anchorA.y, anchorA.z, suspension.x, suspension.y, suspension.z, wheel.x, wheel.y, wheel.z);
            }
            if (!joint) return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::Unsupported, "3D joint kind cannot be reconstructed", "payload.joints.kind");
            WorldSnapshotAccess::setJointId(*joint, static_cast<int>(id.value()));
            maxJointId = std::max(maxJointId, static_cast<int>(id.value()));
        }
        auto restored = WorldSnapshotAccess::finishPrepared(*prepared, maxBodyId + 1, maxShapeId + 1,
                                                             maxJointId + 1, observation);
        if (!restored) return eve::Result<std::unique_ptr<World3D>>::failure(restored.status());
        return eve::Result<std::unique_ptr<World3D>>::success(std::move(prepared));
    } catch (const std::exception& error) {
        return failure<std::unique_ptr<World3D>>(eve::DiagnosticCode::Failed, std::string("3D topology preparation failed: ") + error.what(), "physics.world3d.restore.prepare");
    }
}

}  // namespace

eve::Result<eve::SnapshotEnvelope> World::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    if (!isValid() || !simulation_)
        return failure<eve::SnapshotEnvelope>(eve::DiagnosticCode::PreconditionViolation,
                                              "Cannot snapshot a destroyed or uninitialized physics world",
                                              "physics.world.snapshot");
    auto schema = snapshotSchema(kWorld2DSchema);
    if (!schema) return eve::Result<eve::SnapshotEnvelope>::failure(schema.status());
    const auto         observation = simulationObservation();
    std::vector<Body*> bodies(bodies_.begin(), bodies_.end());
    return eve::makeSnapshotEnvelope(std::string(kWorld2DType), std::move(schema).takeValue(),
                                     eve::SchemaVersion(kSnapshotVersion), instanceId_,
                                     eve::Revision(observation.stepCount), simulationTick(),
                                     make2DPayload(*this, bodies), hashProvider);
}

eve::Result<void> World::restore(const eve::SnapshotEnvelope&     snapshotValue,
                                 const eve::SnapshotHashProvider& hashProvider) {
    if (!isValid() || !simulation_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot restore a destroyed or uninitialized physics world",
            "physics.world.restore"));
    auto verified = eve::verifySnapshotEnvelope(snapshotValue, hashProvider);
    if (!verified) return verified;
    auto envelope = checkEnvelope(snapshotValue, kWorld2DType, kWorld2DSchema);
    if (!envelope) return envelope;
    const bool legacyV1 = snapshotValue.schemaVersion.value() == 1;
    if (!legacyV1 && (snapshotValue.instanceId.isNil() || snapshotValue.instanceId != instanceId_))
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "physics snapshot belongs to a different world instance", "snapshot.instanceId");
    eve::Value migratedPayload = snapshotValue.payload;
    if (legacyV1) {
        auto* migrated = migratedPayload.getIf<eve::Value::Object>();
        auto* bodyValues = migrated ? (*migrated)["bodies"].getIf<eve::Value::Array>() : nullptr;
        if (!bodyValues) return failure<void>(eve::DiagnosticCode::ParseError, "v1 physics snapshot bodies are malformed", "payload.bodies");
        for (eve::Value& value : *bodyValues) {
            auto* bodyObject = value.getIf<eve::Value::Object>();
            if (!bodyObject) return failure<void>(eve::DiagnosticCode::ParseError, "v1 physics snapshot body is malformed", "payload.bodies");
            auto id = readUint64(*bodyObject, "id");
            Body* live = nullptr;
            for (Body* candidate : bodies_) if (candidate && id && candidate->getId() == static_cast<int>(id.value())) live = candidate;
            if (!live) return failure<void>(eve::DiagnosticCode::Conflict, "v1 snapshot requires topology-compatible live bodies", "payload.bodies");
            bodyObject->emplace("fixtures", fixtureTopologyValue(*live));
        }
    }
    const auto* object = migratedPayload.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"bodies", "gravityX", "gravityY", "meter", "tick", "revision", "stepCount",
                                             "simulatedDurationNs", "lastDeltaSeconds"}))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "2D physics snapshot payload has unknown or missing fields", "payload"));
    auto observation = parseObservation(*object, snapshotValue);
    if (!observation) return eve::Result<void>::failure(observation.status());
    auto bodies = parseBodies2D(*object);
    if (!bodies) return eve::Result<void>::failure(bodies.status());
    auto gravityX = readFloat(*object, "gravityX");
    if (!gravityX) return eve::Result<void>::failure(gravityX.status());
    auto gravityY = readFloat(*object, "gravityY");
    if (!gravityY) return eve::Result<void>::failure(gravityY.status());
    auto meter = readFloat(*object, "meter");
    if (!meter) return eve::Result<void>::failure(meter.status());
    if (meter.value() <= 0.f)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "physics snapshot meter must be positive",
                             "payload.meter");

    std::set<int> snapshotIds;
    auto          matched = matchBodyIds(bodies.value(), snapshotIds);
    if (!matched) return matched;
    auto handleRefresh = prepareRuntimeHandleRefresh();
    if (!handleRefresh) return handleRefresh;
    auto prepared = prepareWorld2D(bodies.value(), gravityX.value(), gravityY.value(), meter.value(),
                                   observation.value().value, instanceId_);
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    WorldSnapshotAccess::adopt(*this, *prepared.value());
    refreshRuntimeHandlesAfterRestore();
    clearContactEvents();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<eve::SnapshotEnvelope> World3D::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    if (!isValid() || !simulation_)
        return failure<eve::SnapshotEnvelope>(eve::DiagnosticCode::PreconditionViolation,
                                              "Cannot snapshot a destroyed or uninitialized 3D physics world",
                                              "physics.world3d.snapshot");
    auto schema = snapshotSchema(kWorld3DSchema);
    if (!schema) return eve::Result<eve::SnapshotEnvelope>::failure(schema.status());
    const auto           observation = simulationObservation();
    std::vector<Body3D*> bodies(bodies_.begin(), bodies_.end());
    return eve::makeSnapshotEnvelope(std::string(kWorld3DType), std::move(schema).takeValue(),
                                     eve::SchemaVersion(kSnapshotVersion), instanceId_,
                                     eve::Revision(observation.stepCount), simulationTick(),
                                     make3DPayload(*this, bodies, std::vector<Shape3D*>(shapes_.begin(), shapes_.end()),
                                                   std::vector<Joint3D*>(joints_.begin(), joints_.end())), hashProvider);
}

eve::Result<void> World3D::restore(const eve::SnapshotEnvelope&     snapshotValue,
                                   const eve::SnapshotHashProvider& hashProvider) {
    if (!isValid() || !simulation_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot restore a destroyed or uninitialized 3D physics world",
            "physics.world3d.restore"));
    auto verified = eve::verifySnapshotEnvelope(snapshotValue, hashProvider);
    if (!verified) return verified;
    auto envelope = checkEnvelope(snapshotValue, kWorld3DType, kWorld3DSchema);
    if (!envelope) return envelope;
    const bool legacyV1 = snapshotValue.schemaVersion.value() == 1;
    if (!legacyV1 && (snapshotValue.instanceId.isNil() || snapshotValue.instanceId != instanceId_))
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "3D physics snapshot belongs to a different world instance", "snapshot.instanceId");
    eve::Value migratedPayload = snapshotValue.payload;
    if (legacyV1) {
        auto* migrated = migratedPayload.getIf<eve::Value::Object>();
        if (!migrated) return failure<void>(eve::DiagnosticCode::ParseError, "v1 3D physics payload is malformed", "payload");
        migrated->emplace("shapes", shapesTopologyValue(std::vector<Shape3D*>(shapes_.begin(), shapes_.end())));
        migrated->emplace("joints", jointsTopologyValue(std::vector<Joint3D*>(joints_.begin(), joints_.end())));
        const eve::Value* bodyValues = field(*migrated, "bodies");
        if (!bodyValues || bodyValues->arraySize() != bodies_.size())
            return failure<void>(eve::DiagnosticCode::Conflict, "v1 snapshot requires topology-compatible live bodies", "payload.bodies");
    }
    const auto* object = migratedPayload.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"bodies", "gravityX", "gravityY", "gravityZ", "joints", "shapes", "tick", "revision",
                                             "stepCount", "simulatedDurationNs", "lastDeltaSeconds"}))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "3D physics snapshot payload has unknown or missing fields", "payload"));
    auto observation = parseObservation(*object, snapshotValue);
    if (!observation) return eve::Result<void>::failure(observation.status());
    auto bodies = parseBodies3D(*object);
    if (!bodies) return eve::Result<void>::failure(bodies.status());
    auto gravityX = readFloat(*object, "gravityX");
    if (!gravityX) return eve::Result<void>::failure(gravityX.status());
    auto gravityY = readFloat(*object, "gravityY");
    if (!gravityY) return eve::Result<void>::failure(gravityY.status());
    auto gravityZ = readFloat(*object, "gravityZ");
    if (!gravityZ) return eve::Result<void>::failure(gravityZ.status());
    const eve::Value* snapshotShapes = field(*object, "shapes");
    const eve::Value* snapshotJoints = field(*object, "joints");
    if (!snapshotShapes || !snapshotShapes->isArray() || !snapshotJoints || !snapshotJoints->isArray())
        return failure<void>(eve::DiagnosticCode::ParseError,
                             "3D physics snapshot topology fields must be arrays", "payload.shapes");
    std::set<int> snapshotIds;
    auto          matched = matchBodyIds(bodies.value(), snapshotIds);
    if (!matched) return matched;
    auto handleRefresh = prepareRuntimeHandleRefresh();
    if (!handleRefresh) return handleRefresh;
    auto prepared = prepareWorld3D(bodies.value(), *snapshotShapes->getIf<eve::Value::Array>(),
                                   *snapshotJoints->getIf<eve::Value::Array>(),
                                   gravityX.value(), gravityY.value(), gravityZ.value(),
                                   observation.value().value, instanceId_);
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    auto restoredObservation = simulation_->restoreObservation(observation.value().value);
    if (!restoredObservation) return restoredObservation;
    WorldSnapshotAccess::adopt(*this, *prepared.value());
    refreshRuntimeHandlesAfterRestore();
    clearContactEvents();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> World::prepareRuntimeHandleRefresh() const {
    const auto available =
        static_cast<std::uint64_t>(PhysicsBodyHandle::invalidIndex) - nextBodyHandleIndex_;
    if (bodies_.size() > available)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "2D physics body handle space cannot represent restored links",
                             "physics.world.restore.handles");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void World::refreshRuntimeHandlesAfterRestore() {
    for (Body* body : bodies_)
        if (body && body->isValid()) body->runtimeHandle_ = nextBodyRuntimeHandle();
}

eve::Result<void> World3D::prepareRuntimeHandleRefresh() const {
    const auto bodyAvailable = static_cast<std::uint64_t>(PhysicsBodyHandle::invalidIndex) - nextBodyHandleIndex_;
    const auto shapeAvailable = static_cast<std::uint64_t>(PhysicsShapeHandle::invalidIndex) - nextShapeHandleIndex_;
    const auto jointAvailable = static_cast<std::uint64_t>(PhysicsJointHandle::invalidIndex) - nextJointHandleIndex_;
    if (bodies_.size() > bodyAvailable || shapes_.size() > shapeAvailable || joints_.size() > jointAvailable)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "3D physics handle space cannot represent restored links",
                             "physics.world3d.restore.handles");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void World3D::refreshRuntimeHandlesAfterRestore() {
    shapeHandles_.clear();
    shapeRawHandles_.clear();
    jointHandles_.clear();
    for (Body3D* body : bodies_)
        if (body && body->isValid()) body->runtimeHandle_ = nextBodyRuntimeHandle();
    for (Shape3D* shape : shapes_) {
        if (!shape || !shape->isValid()) continue;
        shape->runtimeHandle_ = nextShapeRuntimeHandle();
        shapeHandles_[shape->runtimeHandle_] = shape;
        shapeRawHandles_[b3StoreShapeId(shape->raw())] = shape->runtimeHandle_;
    }
    for (Joint3D* joint : joints_) {
        if (!joint || !joint->isValid()) continue;
        joint->runtimeHandle_ = nextJointRuntimeHandle();
        jointHandles_[joint->runtimeHandle_] = joint;
    }
}

}  // namespace eve::physics
