#include "physics/rope/Rope.h"

#include "common/Exception.h"
#include "common/Json.h"
#include "physics/rope/Rope3D.h"
#include "schema/SchemaRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace eve::physics {
namespace {

constexpr auto kRopeCreateSchemaId = "physics:rope3d-create";

eve::schema::FieldDefinition ropeField(std::string name, eve::schema::ValueType type, bool required,
                                       std::optional<double> minimum = {}, std::optional<double> maximum = {},
                                       std::string defaultJson = {}) {
    eve::schema::FieldDefinition field;
    field.name        = std::move(name);
    field.type        = type;
    field.required    = required;
    field.minimum     = minimum;
    field.maximum     = maximum;
    field.defaultJson = std::move(defaultJson);
    return field;
}

eve::schema::SchemaDefinition ropeCreateSchema() {
    using eve::schema::ValueType;
    eve::schema::SchemaDefinition schema;
    schema.id                   = kRopeCreateSchemaId;
    schema.version              = 1;
    schema.title                = "3D Rope Creation Parameters";
    schema.description          = "Validated construction and initial solver settings for Rope3D.";
    schema.additionalProperties = false;
    schema.fields               = {
        ropeField("particleCount", ValueType::Integer, true, 2, 100000),
        ropeField("startX", ValueType::Number, true),
        ropeField("startY", ValueType::Number, true),
        ropeField("startZ", ValueType::Number, true),
        ropeField("endX", ValueType::Number, true),
        ropeField("endY", ValueType::Number, true),
        ropeField("endZ", ValueType::Number, true),
        ropeField("gravityX", ValueType::Number, false, {}, {}, "0"),
        ropeField("gravityY", ValueType::Number, false, {}, {}, "-9.81"),
        ropeField("gravityZ", ValueType::Number, false, {}, {}, "0"),
        ropeField("stretchCompliance", ValueType::Number, false, 0, {}, "0"),
        ropeField("distanceConstraintsEnabled", ValueType::Boolean, false, {}, {}, "true"),
        ropeField("bendCompliance", ValueType::Number, false, 0, {}, "0.002"),
        ropeField("bendConstraintsEnabled", ValueType::Boolean, false, {}, {}, "true"),
        ropeField("maxBending", ValueType::Number, false, 0, 0.5, "0.025"),
        ropeField("plasticYield", ValueType::Number, false, 0, 0.5, "0"),
        ropeField("plasticCreep", ValueType::Number, false, 0, {}, "0"),
        ropeField("maxCompression", ValueType::Number, false, 0, 1, "0"),
        ropeField("damping", ValueType::Number, false, 0, 1, "0.01"),
        ropeField("particleMass", ValueType::Number, false, 0.000001, {}, "0.1"),
        ropeField("radius", ValueType::Number, false, 0.000001, {}, "0.04"),
        ropeField("collisionFriction", ValueType::Number, false, 0, 1, "0"),
        ropeField("collisionRestitution", ValueType::Number, false, 0, 1, "0"),
        ropeField("continuousCollision", ValueType::Boolean, false, {}, {}, "true"),
        ropeField("selfCollision", ValueType::Boolean, false, {}, {}, "true"),
        ropeField("pinStart", ValueType::Boolean, false, {}, {}, "false"),
        ropeField("pinEnd", ValueType::Boolean, false, {}, {}, "false"),
        ropeField("tearingEnabled", ValueType::Boolean, false, {}, {}, "false"),
        ropeField("tearResistance", ValueType::Number, false, 0.000001, {}, "1000"),
        ropeField("maxTearsPerStep", ValueType::Integer, false, 1, 1000, "1"),
    };
    return schema;
}

eve::Result<void> ensureRopeCreateSchema() {
    if (eve::schema::SchemaRegistry::resolve(kRopeCreateSchemaId, 1)) return eve::Result<void>::success();
    auto registration = eve::schema::SchemaRegistry::registerVersioned(ropeCreateSchema());
    if (!registration.ok()) return eve::Result<void>::failure(registration.status());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool ropeChangeOrThrow(eve::Result<RopeTopologyChange> result, const char* operation) {
    if (result.ok()) return result.value() == RopeTopologyChange::Changed;
    const auto* diagnostic = result.status().primaryDiagnostic();
    throw Exception("Rope3D.%s: %s", operation, diagnostic ? diagnostic->message().c_str() : "operation failed");
}

bool ropeColliderChangeOrThrow(eve::Result<RopeColliderChange> result, const char* operation) {
    if (result.ok()) return result.value() == RopeColliderChange::Changed;
    const auto* diagnostic = result.status().primaryDiagnostic();
    throw Exception("Rope3D.%s: %s", operation, diagnostic ? diagnostic->message().c_str() : "operation failed");
}

std::int64_t ropeColliderIdOrThrow(eve::Result<RopeColliderId> result, const char* operation) {
    if (result.ok()) return static_cast<std::int64_t>(result.value().value);
    const auto* diagnostic = result.status().primaryDiagnostic();
    throw Exception("Rope3D.%s: %s", operation, diagnostic ? diagnostic->message().c_str() : "operation failed");
}

template <class T>
eve::Result<T> ropeCreateFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path),
                               eve::DiagnosticDetails{{"schemaId", kRopeCreateSchemaId}, {"schemaVersion", "1"}},
                               "physics.rope3d.create-schema"));
}

}  // namespace

Module_IMPL(Rope, new Rope());

Rope3D* Rope::newRope3D(int count, float sx, float sy, float sz, float ex, float ey, float ez) {
    return new Rope3D(count, sx, sy, sz, ex, ey, ez);
}

eve::Result<void> Rope::registerRope3DCreateSchema() { return ensureRopeCreateSchema(); }

eve::Result<Rope3D*> Rope::newRope3DFromJson(const std::string& json) {
    auto registered = registerRope3DCreateSchema();
    if (!registered.ok()) return eve::Result<Rope3D*>::failure(registered.status());
    const auto errors = eve::schema::SchemaRegistry::validate(kRopeCreateSchemaId, 1, json);
    if (!errors.empty())
        return ropeCreateFailure<Rope3D*>(eve::DiagnosticCode::InvalidArgument, errors.front().message,
                                          errors.front().path);
    std::string parseError;
    auto        document = eve::json::Document::parse(json, &parseError);
    if (!document.valid()) return ropeCreateFailure<Rope3D*>(eve::DiagnosticCode::ParseError, std::move(parseError));
    const auto root = document.root();
    try {
        auto rope = std::make_unique<Rope3D>(root.getInt("particleCount"), root.getFloat("startX"),
                                             root.getFloat("startY"), root.getFloat("startZ"), root.getFloat("endX"),
                                             root.getFloat("endY"), root.getFloat("endZ"));
        rope->setGravity(root.getFloat("gravityX", 0.f), root.getFloat("gravityY", -9.81f),
                         root.getFloat("gravityZ", 0.f));
        rope->setStretchCompliance(root.getFloat("stretchCompliance", 0.f));
        rope->setDistanceConstraintsEnabled(root.getBool("distanceConstraintsEnabled", true));
        rope->setBendCompliance(root.getFloat("bendCompliance", 0.002f));
        rope->setBendConstraintsEnabled(root.getBool("bendConstraintsEnabled", true));
        rope->setMaxBending(root.getFloat("maxBending", 0.025f));
        rope->setPlasticity(root.getFloat("plasticYield", 0.f), root.getFloat("plasticCreep", 0.f));
        rope->setMaxCompression(root.getFloat("maxCompression", 0.f));
        rope->setDamping(root.getFloat("damping", 0.01f));
        rope->setParticleMass(root.getFloat("particleMass", 0.1f));
        rope->setRadius(root.getFloat("radius", 0.04f));
        rope->setCollisionFriction(root.getFloat("collisionFriction", 0.f));
        rope->setCollisionRestitution(root.getFloat("collisionRestitution", 0.f));
        rope->setContinuousCollision(root.getBool("continuousCollision", true));
        rope->setSelfCollision(root.getBool("selfCollision", true));
        if (root.getBool("pinStart", false)) {
            auto pinned = rope->pin(0);
            if (!pinned.ok()) return eve::Result<Rope3D*>::failure(pinned.status());
        }
        if (root.getBool("pinEnd", false)) {
            auto pinned = rope->pin(rope->getParticleCount() - 1);
            if (!pinned.ok()) return eve::Result<Rope3D*>::failure(pinned.status());
        }
        if (root.getBool("tearingEnabled", false))
            rope->setTearing(root.getFloat("tearResistance", 1000.f), root.getInt("maxTearsPerStep", 1));
        return eve::Result<Rope3D*>::success(rope.release(), eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception& exception) {
        return ropeCreateFailure<Rope3D*>(eve::DiagnosticCode::InvalidArgument, exception.what());
    }
}

Rope3D* Rope::newRope3DFromJsonScript(const std::string& json) {
    auto created = newRope3DFromJson(json);
    if (created.ok()) return created.value();
    const auto* diagnostic = created.status().primaryDiagnostic();
    throw Exception("Rope.newRope3DFromJson: %s", diagnostic ? diagnostic->message().c_str() : "creation failed");
}

void Rope::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Rope::create, false);
    expose(cls);
    auto rope = table.addClass<Rope3D>("Rope3D", std::function<Rope3D*()>([]() -> Rope3D* { return nullptr; }), true);
    rope.addFunc("update", &Rope3D::update);
    rope.addFunc("setGravity", &Rope3D::setGravity);
    rope.addFunc("getGravityX", &Rope3D::getGravityX);
    rope.addFunc("getGravityY", &Rope3D::getGravityY);
    rope.addFunc("getGravityZ", &Rope3D::getGravityZ);
    rope.addFunc("setStretchCompliance", &Rope3D::setStretchCompliance);
    rope.addFunc("setDistanceConstraintsEnabled", &Rope3D::setDistanceConstraintsEnabled);
    rope.addFunc("getDistanceConstraintsEnabled", [](Rope3D* self) { return self->getDistanceConstraintsEnabled(); });
    rope.addFunc("setBendCompliance", &Rope3D::setBendCompliance);
    rope.addFunc("setBendConstraintsEnabled", &Rope3D::setBendConstraintsEnabled);
    rope.addFunc("getBendConstraintsEnabled", [](Rope3D* self) { return self->getBendConstraintsEnabled(); });
    rope.addFunc("setMaxBending", &Rope3D::setMaxBending);
    rope.addFunc("getMaxBending", [](Rope3D* self) { return self->getMaxBending(); });
    rope.addFunc("setPlasticity", &Rope3D::setPlasticity);
    rope.addFunc("getPlasticYield", [](Rope3D* self) { return self->getPlasticYield(); });
    rope.addFunc("getPlasticCreep", [](Rope3D* self) { return self->getPlasticCreep(); });
    rope.addFunc("getBendPlasticity", &Rope3D::getBendPlasticity);
    rope.addFunc("setMaxCompression", &Rope3D::setMaxCompression);
    rope.addFunc("setDamping", &Rope3D::setDamping);
    rope.addFunc("getDamping", &Rope3D::getDamping);
    rope.addFunc("setParticleMass", &Rope3D::setParticleMass);
    rope.addFunc("getParticleMass", &Rope3D::getParticleMass);
    rope.addFunc("setRadius", &Rope3D::setRadius);
    rope.addFunc("getRadius", &Rope3D::getRadius);
    rope.addFunc("setCollisionFriction", &Rope3D::setCollisionFriction);
    rope.addFunc("getCollisionFriction", [](Rope3D* self) { return self->getCollisionFriction(); });
    rope.addFunc("setCollisionRestitution", &Rope3D::setCollisionRestitution);
    rope.addFunc("getCollisionRestitution", [](Rope3D* self) { return self->getCollisionRestitution(); });
    rope.addFunc("setContinuousCollision", &Rope3D::setContinuousCollision);
    rope.addFunc("getContinuousCollision", [](Rope3D* self) { return self->getContinuousCollision(); });
    rope.addFunc("setCollideWorld", &Rope3D::setCollideWorld);
    rope.addFunc("getCollideWorld", [](Rope3D* self) { return self->getCollideWorld(); });
    rope.addFunc("setCollideSdf", &Rope3D::setCollideSdf);
    rope.addFunc("getCollideSdf", [](Rope3D* self) { return self->getCollideSdf(); });
    rope.addFunc("clearColliders", &Rope3D::clearColliders);
    rope.addFunc("setSelfCollision", &Rope3D::setSelfCollision);
    rope.addFunc("getSelfCollision", &Rope3D::getSelfCollision);
    rope.addFunc("setBounds", &Rope3D::setBounds);
    rope.addFunc("clearBounds", &Rope3D::clearBounds);
    rope.addFunc("isAttached", &Rope3D::isAttached);
    rope.addFunc("applyForce", &Rope3D::applyForce);
    rope.addFunc("getRestLength", &Rope3D::getRestLength);
    rope.addFunc("calculateLength", &Rope3D::calculateLength);
    rope.addFunc("isElementActive", &Rope3D::isElementActive);
    rope.addFunc("getElementForce", &Rope3D::getElementForce);
    rope.addFunc("getTopologyRevision", [](Rope3D* self) { return self->getTopologyRevision(); });
    rope.addFunc("getLastTornElementCount", [](Rope3D* self) { return self->getLastTornElementCount(); });
    rope.addFunc("getLastTornElement", &Rope3D::getLastTornElement);
    rope.addFunc("setTearing", &Rope3D::setTearing);
    rope.addFunc("disableTearing", &Rope3D::disableTearing);
    rope.addFunc("getParticleCount", &Rope3D::getParticleCount);
    rope.addFunc("getParticleX", &Rope3D::getParticleX);
    rope.addFunc("getParticleY", &Rope3D::getParticleY);
    rope.addFunc("getParticleZ", &Rope3D::getParticleZ);
    rope.addFunc("getSampleX", &Rope3D::getSampleX);
    rope.addFunc("getSampleY", &Rope3D::getSampleY);
    rope.addFunc("getSampleZ", &Rope3D::getSampleZ);
    rope.addFunc("getSampleTangentX", &Rope3D::getSampleTangentX);
    rope.addFunc("getSampleTangentY", &Rope3D::getSampleTangentY);
    rope.addFunc("getSampleTangentZ", &Rope3D::getSampleTangentZ);
    rope.addFunc("addSphereCollider", [](Rope3D* self, float x, float y, float z, float radius) {
        return ropeColliderIdOrThrow(self->addSphereCollider(x, y, z, radius), "addSphereCollider");
    });
    rope.addFunc("addPlaneCollider", [](Rope3D* self, float x, float y, float z, float nx, float ny, float nz) {
        return ropeColliderIdOrThrow(self->addPlaneCollider(x, y, z, nx, ny, nz), "addPlaneCollider");
    });
    rope.addFunc("moveSphereCollider", [](Rope3D* self, std::int64_t id, float x, float y, float z) {
        return ropeColliderChangeOrThrow(
            self->moveSphereCollider(RopeColliderId{static_cast<std::uint64_t>(id)}, x, y, z), "moveSphereCollider");
    });
    rope.addFunc("removeCollider", [](Rope3D* self, std::int64_t id) {
        return ropeColliderChangeOrThrow(self->removeCollider(RopeColliderId{static_cast<std::uint64_t>(id)}),
                                         "removeCollider");
    });
    rope.addFunc("pin", [](Rope3D* self, int index) { return ropeChangeOrThrow(self->pin(index), "pin"); });
    rope.addFunc("attach", [](Rope3D* self, int index, float x, float y, float z) {
        return ropeChangeOrThrow(self->attach(index, x, y, z), "attach");
    });
    rope.addFunc("moveAttachment", [](Rope3D* self, int index, float x, float y, float z) {
        return ropeChangeOrThrow(self->moveAttachment(index, x, y, z), "moveAttachment");
    });
    rope.addFunc("detach", [](Rope3D* self, int index) { return ropeChangeOrThrow(self->detach(index), "detach"); });
    rope.addFunc("cut", [](Rope3D* self, int index) { return ropeChangeOrThrow(self->cut(index), "cut"); });
    rope.addFunc("repair", [](Rope3D* self, int index) { return ropeChangeOrThrow(self->repair(index), "repair"); });
    rope.addFunc("setRestLength", [](Rope3D* self, float length) {
        return ropeChangeOrThrow(self->setRestLength(length), "setRestLength");
    });
    rope.addFunc("changeLength", [](Rope3D* self, float length, float spacing, bool fromEnd) {
        return ropeChangeOrThrow(self->changeLength(length, spacing, fromEnd), "changeLength");
    });
}

void Rope::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Rope::getName);
    cls.addFunc("newRope3D", &Rope::newRope3D);
    cls.addFunc("newRope3DFromJson", &Rope::newRope3DFromJsonScript);
}

}  // namespace eve::physics
