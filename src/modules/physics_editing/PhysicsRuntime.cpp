#include "physics_editing/PhysicsTarget.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"

#include <utility>

namespace eve::physics_editing {
namespace {

template <class T>
EditorResult<T> bridgeError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

template <class T>
const T* propertyValue(const EditorValue::Object& values, const char* path) {
    const auto found = values.find(path);
    return found == values.end() ? nullptr : found->second.getIf<T>();
}

}  // namespace

EditorResult<physics::Fixture*> PhysicsColliderRuntimeBuilder::build2D(const PhysicsColliderTarget& target,
                                                                       physics::Body*               body) const {
    if (!body || target.describe().type != "physics-collider-2d")
        return bridgeError<physics::Fixture*>(EditorStatus::Rejected, "editor.physics.runtime-2d-input",
                                              "A 2D collider target and body are required");
    const EditorValue snapshot = target.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    const auto*       values   = root ? root->at("properties").getIf<EditorValue::Object>() : nullptr;
    if (!values)
        return bridgeError<physics::Fixture*>(EditorStatus::Failed, "editor.physics.runtime-properties",
                                              "Collider properties are unavailable");
    const std::string kind        = *propertyValue<std::string>(*values, "shape.kind");
    const auto*       size        = propertyValue<EditorValue::Array>(*values, "shape.size");
    const auto*       offset      = propertyValue<EditorValue::Array>(*values, "shape.offset");
    const float       radius      = static_cast<float>(*propertyValue<double>(*values, "shape.radius"));
    const float       density     = static_cast<float>(*propertyValue<double>(*values, "material.density"));
    const float       friction    = static_cast<float>(*propertyValue<double>(*values, "material.friction"));
    const float       restitution = static_cast<float>(*propertyValue<double>(*values, "material.restitution"));
    physics::Fixture* fixture     = nullptr;
    if (kind == "box")
        fixture = body->newRectangleFixtureAt(
            static_cast<float>(*(*size)[0].getIf<double>()), static_cast<float>(*(*size)[1].getIf<double>()),
            static_cast<float>(*(*offset)[0].getIf<double>()), static_cast<float>(*(*offset)[1].getIf<double>()),
            density, friction, restitution);
    else if (kind == "circle")
        fixture = body->newCircleFixture(radius, density, friction, restitution);
    else
        return bridgeError<physics::Fixture*>(EditorStatus::Unsupported, "editor.physics.runtime-2d-asset-shape",
                                              "Polygon and chain colliders require an asset resolver");
    if (!fixture)
        return bridgeError<physics::Fixture*>(EditorStatus::Failed, "editor.physics.runtime-create",
                                              "Physics backend failed to create the fixture");
    fixture->setSensor(*propertyValue<bool>(*values, "shape.sensor"));
    fixture->setCategoryBits(static_cast<int>(*propertyValue<int64_t>(*values, "collision.category")));
    fixture->setMaskBits(static_cast<int>(*propertyValue<int64_t>(*values, "collision.mask")));
    return EditorResult<physics::Fixture*>::applied(fixture);
}

EditorResult<physics::Fixture*> PhysicsColliderRuntimeBuilder::build2D(
    const PhysicsColliderTarget& target, physics::Body* body, const IPhysicsColliderAssetResolver& assets) const {
    if (!body || target.describe().type != "physics-collider-2d")
        return bridgeError<physics::Fixture*>(EditorStatus::Rejected, "editor.physics.runtime-2d-input",
                                              "A 2D collider target and body are required");
    const EditorValue snapshot = target.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    const auto*       values   = root ? root->at("properties").getIf<EditorValue::Object>() : nullptr;
    if (!values)
        return bridgeError<physics::Fixture*>(EditorStatus::Failed, "editor.physics.runtime-properties",
                                              "Collider properties are unavailable");
    const std::string kind = *propertyValue<std::string>(*values, "shape.kind");
    if (kind == "box" || kind == "circle") return build2D(target, body);
    const std::string asset    = *propertyValue<std::string>(*values, "shape.asset");
    auto              geometry = assets.resolve(asset, kind);
    if (!geometry.value) {
        EditorResult<physics::Fixture*> failed;
        failed.status      = geometry.status;
        failed.diagnostics = std::move(geometry.diagnostics);
        return failed;
    }
    std::vector<float> vertices = geometry.value->vertices;
    const auto*        offset   = propertyValue<EditorValue::Array>(*values, "shape.offset");
    const float        offsetX  = static_cast<float>(*(*offset)[0].getIf<double>());
    const float        offsetY  = static_cast<float>(*(*offset)[1].getIf<double>());
    for (size_t index = 0; index < vertices.size(); index += 2) {
        vertices[index] += offsetX;
        vertices[index + 1] += offsetY;
    }
    physics::Fixture* fixture = nullptr;
    try {
        const float density     = static_cast<float>(*propertyValue<double>(*values, "material.density"));
        const float friction    = static_cast<float>(*propertyValue<double>(*values, "material.friction"));
        const float restitution = static_cast<float>(*propertyValue<double>(*values, "material.restitution"));
        if (kind == "polygon")
            fixture = body->newPolygonFixture(vertices, density, friction, restitution);
        else if (kind == "chain")
            fixture = body->newChainFixture(vertices, geometry.value->loop, friction, restitution);
    } catch (const std::exception& exception) {
        return bridgeError<physics::Fixture*>(EditorStatus::Rejected, "editor.physics.runtime-2d-asset-shape",
                                              exception.what());
    }
    if (!fixture)
        return bridgeError<physics::Fixture*>(EditorStatus::Failed, "editor.physics.runtime-create",
                                              "Physics backend failed to create the 2D asset fixture");
    fixture->setSensor(*propertyValue<bool>(*values, "shape.sensor"));
    fixture->setCategoryBits(static_cast<int>(*propertyValue<int64_t>(*values, "collision.category")));
    fixture->setMaskBits(static_cast<int>(*propertyValue<int64_t>(*values, "collision.mask")));
    return EditorResult<physics::Fixture*>::applied(fixture);
}

EditorResult<physics::Shape3D*> PhysicsColliderRuntimeBuilder::build3D(const PhysicsColliderTarget& target,
                                                                       physics::Body3D*             body) const {
    if (!body || target.describe().type != "physics-collider-3d")
        return bridgeError<physics::Shape3D*>(EditorStatus::Rejected, "editor.physics.runtime-3d-input",
                                              "A 3D collider target and body are required");
    const EditorValue snapshot = target.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    const auto*       values   = root ? root->at("properties").getIf<EditorValue::Object>() : nullptr;
    if (!values)
        return bridgeError<physics::Shape3D*>(EditorStatus::Failed, "editor.physics.runtime-properties",
                                              "Collider properties are unavailable");
    const std::string kind          = *propertyValue<std::string>(*values, "shape.kind");
    const auto*       size          = propertyValue<EditorValue::Array>(*values, "shape.size");
    const auto*       offset        = propertyValue<EditorValue::Array>(*values, "shape.offset");
    const float       radius        = static_cast<float>(*propertyValue<double>(*values, "shape.radius"));
    const float       capsuleHeight = static_cast<float>(*propertyValue<double>(*values, "shape.capsule-height"));
    const float       density       = static_cast<float>(*propertyValue<double>(*values, "material.density"));
    const float       friction      = static_cast<float>(*propertyValue<double>(*values, "material.friction"));
    const float       restitution   = static_cast<float>(*propertyValue<double>(*values, "material.restitution"));
    physics::Shape3D* shape         = nullptr;
    if (kind == "box")
        shape = body->newBoxShape(static_cast<float>(*(*size)[0].getIf<double>()),
                                  static_cast<float>(*(*size)[1].getIf<double>()),
                                  static_cast<float>(*(*size)[2].getIf<double>()), density, friction, restitution);
    else if (kind == "sphere")
        shape = body->newSphereShape(radius, density, friction, restitution);
    else if (kind == "capsule")
        shape = body->newCapsuleShape(capsuleHeight, radius, density, friction, restitution);
    else
        return bridgeError<physics::Shape3D*>(EditorStatus::Unsupported, "editor.physics.runtime-3d-asset-shape",
                                              "Mesh, hull and height-field colliders require an asset resolver");
    if (!shape)
        return bridgeError<physics::Shape3D*>(EditorStatus::Failed, "editor.physics.runtime-create",
                                              "Physics backend failed to create the shape");
    shape->setLocalPosition(static_cast<float>(*(*offset)[0].getIf<double>()),
                            static_cast<float>(*(*offset)[1].getIf<double>()),
                            static_cast<float>(*(*offset)[2].getIf<double>()));
    shape->setSensor(*propertyValue<bool>(*values, "shape.sensor"));
    shape->setFilterBits(static_cast<uint64_t>(*propertyValue<int64_t>(*values, "collision.category")),
                         static_cast<uint64_t>(*propertyValue<int64_t>(*values, "collision.mask")));
    return EditorResult<physics::Shape3D*>::applied(shape);
}

EditorResult<physics::Shape3D*> PhysicsColliderRuntimeBuilder::build3D(
    const PhysicsColliderTarget& target, physics::Body3D* body, const IPhysicsColliderAssetResolver& assets) const {
    if (!body || target.describe().type != "physics-collider-3d")
        return bridgeError<physics::Shape3D*>(EditorStatus::Rejected, "editor.physics.runtime-3d-input",
                                              "A 3D collider target and body are required");
    const EditorValue snapshot = target.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    const auto*       values   = root ? root->at("properties").getIf<EditorValue::Object>() : nullptr;
    if (!values)
        return bridgeError<physics::Shape3D*>(EditorStatus::Failed, "editor.physics.runtime-properties",
                                              "Collider properties are unavailable");
    const std::string kind = *propertyValue<std::string>(*values, "shape.kind");
    if (kind == "box" || kind == "sphere" || kind == "capsule") return build3D(target, body);
    const std::string asset    = *propertyValue<std::string>(*values, "shape.asset");
    auto              geometry = assets.resolve(asset, kind);
    if (!geometry.value) {
        EditorResult<physics::Shape3D*> failed;
        failed.status      = geometry.status;
        failed.diagnostics = std::move(geometry.diagnostics);
        return failed;
    }
    physics::Shape3D* shape = nullptr;
    try {
        const float density     = static_cast<float>(*propertyValue<double>(*values, "material.density"));
        const float friction    = static_cast<float>(*propertyValue<double>(*values, "material.friction"));
        const float restitution = static_cast<float>(*propertyValue<double>(*values, "material.restitution"));
        if (kind == "convex-hull")
            shape = body->newConvexHullShape(geometry.value->vertices, 64, density, friction, restitution);
        else if (kind == "triangle-mesh")
            shape = body->newTriangleMeshShape(geometry.value->vertices, geometry.value->indices);
        else if (kind == "height-field")
            shape = body->newHeightFieldShape(geometry.value->countX, geometry.value->countZ, geometry.value->cellSizeX,
                                              geometry.value->cellSizeZ, geometry.value->heights,
                                              geometry.value->minimumHeight, geometry.value->maximumHeight);
    } catch (const std::exception& exception) {
        return bridgeError<physics::Shape3D*>(EditorStatus::Rejected, "editor.physics.runtime-complex-shape",
                                              exception.what());
    }
    if (!shape)
        return bridgeError<physics::Shape3D*>(EditorStatus::Failed, "editor.physics.runtime-create",
                                              "Physics backend failed to create the resolved shape");
    shape->setDensity(static_cast<float>(*propertyValue<double>(*values, "material.density")));
    shape->setFriction(static_cast<float>(*propertyValue<double>(*values, "material.friction")));
    shape->setRestitution(static_cast<float>(*propertyValue<double>(*values, "material.restitution")));
    const auto* offset = propertyValue<EditorValue::Array>(*values, "shape.offset");
    shape->setLocalPosition(static_cast<float>(*(*offset)[0].getIf<double>()),
                            static_cast<float>(*(*offset)[1].getIf<double>()),
                            static_cast<float>(*(*offset)[2].getIf<double>()));
    shape->setSensor(*propertyValue<bool>(*values, "shape.sensor"));
    shape->setFilterBits(static_cast<uint64_t>(*propertyValue<int64_t>(*values, "collision.category")),
                         static_cast<uint64_t>(*propertyValue<int64_t>(*values, "collision.mask")));
    return EditorResult<physics::Shape3D*>::applied(shape);
}

EditorResult<void> PhysicsCollider3DRuntimeSink::publish(const PhysicsColliderTarget& candidate) {
    if (!body_ || !body_->isValid() || candidate.describe().type != "physics-collider-3d")
        return bridgeError<void>(EditorStatus::Rejected, "editor.physics.live-3d-input",
                                 "A live Body3D and 3D collider candidate are required");
    if (current_ && current_->isValid() && current_->getBody() != body_)
        return bridgeError<void>(EditorStatus::Conflict, "editor.physics.live-shape-owner",
                                 "Current collider shape belongs to another body");
    const auto diagnostics = candidate.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            EditorResult<void> failed;
            failed.status      = EditorStatus::Rejected;
            failed.diagnostics = diagnostics;
            return failed;
        }
    }
    const EditorValue snapshot = candidate.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    const auto*       values   = root ? root->at("properties").getIf<EditorValue::Object>() : nullptr;
    if (!values)
        return bridgeError<void>(EditorStatus::Failed, "editor.physics.live-properties",
                                 "Collider properties are unavailable");
    const std::string kind             = *propertyValue<std::string>(*values, "shape.kind");
    const std::string desiredBodyType  = *propertyValue<std::string>(*values, "body.type");
    const std::string previousBodyType = body_->getType();
    const bool currentRequiresStatic = current_ && current_->isValid() &&
                                       (current_->getKind() == "triangleMesh" || current_->getKind() == "heightField");
    if (currentRequiresStatic && desiredBodyType != "static" && desiredBodyType != previousBodyType)
        return bridgeError<void>(EditorStatus::Unsupported, "editor.physics.live-complex-type-transition",
                                 "Swap the complex collider to a primitive before changing its body type");
    try {
        if (previousBodyType != desiredBodyType) body_->setType(desiredBodyType);
    } catch (const std::exception& exception) {
        return bridgeError<void>(EditorStatus::Rejected, "editor.physics.live-body-type", exception.what());
    }

    EditorResult<physics::Shape3D*> built = assets_
                                                ? PhysicsColliderRuntimeBuilder().build3D(candidate, body_, *assets_)
                                                : PhysicsColliderRuntimeBuilder().build3D(candidate, body_);
    if (!built.isAccepted() || !built.value || !*built.value) {
        if (previousBodyType != desiredBodyType) {
            try {
                body_->setType(previousBodyType);
            } catch (...) {
                EditorResult<void> failed;
                failed.status      = EditorStatus::Failed;
                failed.diagnostics = std::move(built.diagnostics);
                failed.diagnostics.push_back(
                    {RuleId("editor.physics.live-rollback-body-type"), DiagnosticSeverity::Error,
                     "Collider build failed and the previous body type could not be restored"});
                return failed;
            }
        }
        EditorResult<void> failed;
        failed.status      = built.status;
        failed.diagnostics = std::move(built.diagnostics);
        return failed;
    }

    physics::Shape3D* replacement = *built.value;
    if (current_ && current_->isValid()) current_->destroy();
    current_ = replacement;
    return EditorResult<void>::applied();
}

EditorResult<void> PhysicsCollider2DRuntimeSink::publish(const PhysicsColliderTarget& candidate) {
    if (!body_ || !body_->raw() || candidate.describe().type != "physics-collider-2d")
        return bridgeError<void>(EditorStatus::Rejected, "editor.physics.live-2d-input",
                                 "A live Body and 2D collider candidate are required");
    if (current_ && current_->raw() && current_->getBody() != body_)
        return bridgeError<void>(EditorStatus::Conflict, "editor.physics.live-fixture-owner",
                                 "Current fixture belongs to another body");
    const auto diagnostics = candidate.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            EditorResult<void> failed;
            failed.status      = EditorStatus::Rejected;
            failed.diagnostics = diagnostics;
            return failed;
        }
    }
    const EditorValue snapshot = candidate.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    const auto*       values   = root ? root->at("properties").getIf<EditorValue::Object>() : nullptr;
    if (!values)
        return bridgeError<void>(EditorStatus::Failed, "editor.physics.live-properties",
                                 "Collider properties are unavailable");
    const std::string desiredType  = *propertyValue<std::string>(*values, "body.type");
    const std::string previousType = body_->getType();
    try {
        if (desiredType != previousType) body_->setType(desiredType);
    } catch (const std::exception& exception) {
        return bridgeError<void>(EditorStatus::Rejected, "editor.physics.live-body-type", exception.what());
    }
    EditorResult<physics::Fixture*> built = assets_
                                                ? PhysicsColliderRuntimeBuilder().build2D(candidate, body_, *assets_)
                                                : PhysicsColliderRuntimeBuilder().build2D(candidate, body_);
    if (!built.isAccepted() || !built.value || !*built.value) {
        if (desiredType != previousType) body_->setType(previousType);
        EditorResult<void> failed;
        failed.status      = built.status;
        failed.diagnostics = std::move(built.diagnostics);
        return failed;
    }
    if (current_ && current_->raw()) current_->destroy();
    current_ = *built.value;
    return EditorResult<void>::applied();
}

}  // namespace eve::physics_editing
