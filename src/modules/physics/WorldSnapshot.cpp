#include "physics/World.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/SimulationBackend.h"
#include "physics/World3D.h"

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
namespace {

constexpr std::string_view kWorld2DType     = "physics.world2d";
constexpr std::string_view kWorld2DSchema   = "physics:world2d";
constexpr std::string_view kWorld3DType     = "physics.world3d";
constexpr std::string_view kWorld3DSchema   = "physics:world3d";
constexpr std::uint64_t    kSnapshotVersion = 1;

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
    if (snapshot.schemaVersion.value() != kSnapshotVersion)
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
    if (!object || !hasExactFields(*object, {"active", "angle", "awake", "bullet", "fixedRotation", "id", "type", "vx",
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
        if (body && body->isValid()) values.push_back(bodyValue(*body));
    object.emplace("bodies", eve::Value(std::move(values)));
    return eve::Value(std::move(object));
}

eve::Value make3DPayload(const World3D& world, const std::vector<Body3D*>& bodies) {
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
                                     eve::SchemaVersion(kSnapshotVersion), eve::PersistentId::nil(),
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
    const auto* object = snapshotValue.payload.getIf<eve::Value::Object>();
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

    std::set<int> ids;
    for (const Body* body : bodies_)
        if (body && body->isValid()) ids.insert(body->getId());
    std::set<int> snapshotIds;
    auto          matched = matchBodyIds(bodies.value(), snapshotIds);
    if (!matched) return matched;
    if (ids != snapshotIds)
        return failure<void>(eve::DiagnosticCode::Conflict, "physics snapshot body ids do not match the live world",
                             "payload.bodies");
    std::vector<std::pair<Body*, Body2DState>> targets;
    targets.reserve(bodies.value().size());
    for (const Body2DState& state : bodies.value()) {
        Body* target = nullptr;
        for (Body* candidate : bodies_)
            if (candidate && candidate->getId() == state.id) target = candidate;
        if (!target)
            return failure<void>(eve::DiagnosticCode::Conflict,
                                 "physics snapshot body target disappeared during validation", "payload.bodies");
        targets.emplace_back(target, state);
    }
    const auto               previousObservation = simulationObservation();
    const auto               previousTick        = simulationTick_;
    const float              previousGravityX    = getGravityX();
    const float              previousGravityY    = getGravityY();
    const float              previousMeter       = getMeter();
    std::vector<Body2DState> backup;
    backup.reserve(targets.size());
    for (const auto& target : targets) {
        const Body& body = *target.first;
        backup.push_back({body.getId(), body.getType(), body.getX(), body.getY(), body.getAngle(),
                          body.getLinearVelocityX(), body.getLinearVelocityY(), body.getAngularVelocity(),
                          body.isActive(), body.isBullet(), body.isAwake(), body.isFixedRotation()});
    }
    auto backendRestore = simulation_->restoreObservation(observation.value().value);
    if (!backendRestore) return backendRestore;
    try {
        setMeter(meter.value());
        setGravity(gravityX.value(), gravityY.value());
        for (const auto& target : targets) {
            Body&              body  = *target.first;
            const Body2DState& state = target.second;
            body.setType(state.type);
            body.setFixedRotation(state.fixedRotation);
            body.setBullet(state.bullet);
            body.setActive(state.active);
            body.setPosition(state.x, state.y);
            body.setAngle(state.angle);
            body.setLinearVelocity(state.vx, state.vy);
            body.setAngularVelocity(state.angularVelocity);
            body.setAwake(state.awake);
        }
    } catch (const std::exception& error) {
        try {
            setMeter(previousMeter);
            setGravity(previousGravityX, previousGravityY);
            for (const Body2DState& state : backup) {
                Body* body = nullptr;
                for (Body* candidate : bodies_)
                    if (candidate && candidate->getId() == state.id) body = candidate;
                if (!body) continue;
                body->setType(state.type);
                body->setFixedRotation(state.fixedRotation);
                body->setBullet(state.bullet);
                body->setActive(state.active);
                body->setPosition(state.x, state.y);
                body->setAngle(state.angle);
                body->setLinearVelocity(state.vx, state.vy);
                body->setAngularVelocity(state.angularVelocity);
                body->setAwake(state.awake);
            }
            auto rollback = simulation_->restoreObservation(previousObservation);
            rollback.ignore("physics snapshot rollback after failed body restore");
            simulationTick_ = previousTick;
        } catch (...) {
            simulationTick_ = previousTick;
        }
        return failure<void>(eve::DiagnosticCode::Failed,
                             std::string("physics snapshot restore rolled back: ") + error.what(),
                             "physics.world.restore");
    }
    simulationTick_ = observation.value().value.lastTick;
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
                                     eve::SchemaVersion(kSnapshotVersion), eve::PersistentId::nil(),
                                     eve::Revision(observation.stepCount), simulationTick(),
                                     make3DPayload(*this, bodies), hashProvider);
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
    const auto* object = snapshotValue.payload.getIf<eve::Value::Object>();
    if (!object || !hasExactFields(*object, {"bodies", "gravityX", "gravityY", "gravityZ", "tick", "revision",
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
    std::set<int> ids;
    for (const Body3D* body : bodies_)
        if (body && body->isValid()) ids.insert(body->getId());
    std::set<int> snapshotIds;
    auto          matched = matchBodyIds(bodies.value(), snapshotIds);
    if (!matched) return matched;
    if (ids != snapshotIds)
        return failure<void>(eve::DiagnosticCode::Conflict, "physics snapshot body ids do not match the live world",
                             "payload.bodies");
    std::vector<std::pair<Body3D*, Body3DState>> targets;
    targets.reserve(bodies.value().size());
    for (const Body3DState& state : bodies.value()) {
        Body3D* target = nullptr;
        for (Body3D* candidate : bodies_)
            if (candidate && candidate->getId() == state.id) target = candidate;
        if (!target)
            return failure<void>(eve::DiagnosticCode::Conflict,
                                 "physics snapshot body target disappeared during validation", "payload.bodies");
        targets.emplace_back(target, state);
    }
    const auto               previousObservation = simulationObservation();
    const auto               previousTick        = simulationTick_;
    const float              previousGravityX    = getGravityX();
    const float              previousGravityY    = getGravityY();
    const float              previousGravityZ    = getGravityZ();
    std::vector<Body3DState> backup;
    backup.reserve(targets.size());
    for (const auto& target : targets) {
        const Body3D& body = *target.first;
        backup.push_back({body.getId(), body.getType(), body.getX(), body.getY(), body.getZ(), body.getRotX(),
                          body.getRotY(), body.getRotZ(), body.getRotW(), body.getLinearVelocityX(),
                          body.getLinearVelocityY(), body.getLinearVelocityZ(), body.getAngularVelocityX(),
                          body.getAngularVelocityY(), body.getAngularVelocityZ(), body.isActive(), body.isBullet(),
                          body.isAwake(), body.isFixedRotation()});
    }
    auto backendRestore = simulation_->restoreObservation(observation.value().value);
    if (!backendRestore) return backendRestore;
    try {
        setGravity(gravityX.value(), gravityY.value(), gravityZ.value());
        for (const auto& target : targets) {
            Body3D&            body  = *target.first;
            const Body3DState& state = target.second;
            body.setType(state.type);
            body.setFixedRotation(state.fixedRotation);
            body.setBullet(state.bullet);
            body.setActive(state.active);
            body.setPosition(state.x, state.y, state.z);
            body.setRotation(state.qx, state.qy, state.qz, state.qw);
            body.setLinearVelocity(state.vx, state.vy, state.vz);
            body.setAngularVelocity(state.wx, state.wy, state.wz);
            body.setAwake(state.awake);
        }
    } catch (const std::exception& error) {
        try {
            setGravity(previousGravityX, previousGravityY, previousGravityZ);
            for (const Body3DState& state : backup) {
                Body3D* body = nullptr;
                for (Body3D* candidate : bodies_)
                    if (candidate && candidate->getId() == state.id) body = candidate;
                if (!body) continue;
                body->setType(state.type);
                body->setFixedRotation(state.fixedRotation);
                body->setBullet(state.bullet);
                body->setActive(state.active);
                body->setPosition(state.x, state.y, state.z);
                body->setRotation(state.qx, state.qy, state.qz, state.qw);
                body->setLinearVelocity(state.vx, state.vy, state.vz);
                body->setAngularVelocity(state.wx, state.wy, state.wz);
                body->setAwake(state.awake);
            }
            auto rollback = simulation_->restoreObservation(previousObservation);
            rollback.ignore("physics 3D snapshot rollback after failed body restore");
            simulationTick_ = previousTick;
        } catch (...) {
            simulationTick_ = previousTick;
        }
        return failure<void>(eve::DiagnosticCode::Failed,
                             std::string("physics 3D snapshot restore rolled back: ") + error.what(),
                             "physics.world3d.restore");
    }
    simulationTick_ = observation.value().value.lastTick;
    clearContactEvents();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::physics
