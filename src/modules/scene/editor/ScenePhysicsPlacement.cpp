#include "scene/editor/ScenePhysicsPlacement.h"

#include "common/Exception.h"
#include "common/Time.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/backend/SimulationBackend.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/vec3.hpp>

namespace eve::scene_editor {
namespace {

template <class T>
editing::Result<T> placementError(editing::Status status, const char* rule, std::string message) {
    return editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

bool finite(double value) { return std::isfinite(value); }

bool finiteTransform(const scene_editing::SceneTransformValue& value) {
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.rotationX) &&
           finite(value.rotationY) && finite(value.rotationZ) && finite(value.scaleX) && finite(value.scaleY) &&
           finite(value.scaleZ);
}

glm::quat rotationOf(const scene_editing::SceneTransformValue& value) {
    return glm::quat(glm::vec3(static_cast<float>(value.rotationX), static_cast<float>(value.rotationY),
                               static_cast<float>(value.rotationZ)));
}

}  // namespace

struct ScenePhysicsPlacementBackend::Impl {
    struct RuntimeObject {
        PhysicsPlacementObject                         admitted;
        std::unique_ptr<physics::Body3D>               body;
        std::vector<std::unique_ptr<physics::Shape3D>> shapes;
        glm::dvec3                                     appliedScale{1.0};
    };

    static std::vector<PhysicsPlacementCollider> selectedColliders(const PhysicsPlacementObject&  object,
                                                                   PhysicsPlacementColliderPolicy policy) {
        std::vector<PhysicsPlacementCollider> values = object.colliders;
        if (values.empty()) {
            PhysicsPlacementCollider legacy;
            legacy.shape             = object.shape;
            legacy.halfExtentX       = object.halfExtentX;
            legacy.halfExtentY       = object.halfExtentY;
            legacy.halfExtentZ       = object.halfExtentZ;
            legacy.convexVertices    = object.convexVertices;
            legacy.convexMaxVertices = object.convexMaxVertices;
            values.push_back(std::move(legacy));
        }
        const bool hasExisting = std::any_of(values.begin(), values.end(), [](const auto& value) {
            return value.source == PhysicsPlacementColliderSource::Existing;
        });
        values.erase(std::remove_if(values.begin(), values.end(),
                                    [&](const auto& value) {
                                        switch (policy) {
                                            case PhysicsPlacementColliderPolicy::ExistingOnly:
                                                return value.source != PhysicsPlacementColliderSource::Existing;
                                            case PhysicsPlacementColliderPolicy::GeneratedOnly:
                                                return value.source != PhysicsPlacementColliderSource::Generated;
                                            case PhysicsPlacementColliderPolicy::GeneratedWhenMissing:
                                                return hasExisting
                                                           ? value.source != PhysicsPlacementColliderSource::Existing
                                                           : value.source != PhysicsPlacementColliderSource::Generated;
                                            case PhysicsPlacementColliderPolicy::ExistingAndGenerated: return false;
                                        }
                                        return true;
                                    }),
                     values.end());
        return values;
    }

    static std::unique_ptr<physics::Shape3D> createShape(physics::Body3D&                body,
                                                         const PhysicsPlacementCollider& collider,
                                                         const glm::dvec3&               scale) {
        const float       width  = static_cast<float>(2.0 * collider.halfExtentX * std::abs(scale.x));
        const float       height = static_cast<float>(2.0 * collider.halfExtentY * std::abs(scale.y));
        const float       depth  = static_cast<float>(2.0 * collider.halfExtentZ * std::abs(scale.z));
        physics::Shape3D* raw    = nullptr;
        if (collider.shape == PhysicsPlacementShape::Sphere)
            raw = body.newSphereShape(std::max({width, height, depth}) * 0.5f, 1.0f, 0.5f, 0.0f);
        else if (collider.shape == PhysicsPlacementShape::Capsule)
            raw = body.newCapsuleShape(height, std::max(width, depth) * 0.5f, 1.0f, 0.5f, 0.0f);
        else if (collider.shape == PhysicsPlacementShape::ConvexHull) {
            auto vertices = collider.convexVertices;
            for (std::size_t index = 0; index < vertices.size(); index += 3) {
                vertices[index] *= static_cast<float>(scale.x);
                vertices[index + 1] *= static_cast<float>(scale.y);
                vertices[index + 2] *= static_cast<float>(scale.z);
            }
            raw = body.newConvexHullShape(vertices, collider.convexMaxVertices, 1.0f, 0.5f, 0.0f);
        } else
            raw = body.newBoxShape(width, height, depth, 1.0f, 0.5f, 0.0f);
        std::unique_ptr<physics::Shape3D> shape(raw);
        if (shape) {
            const auto rotation = glm::quat(glm::vec3(static_cast<float>(collider.localRotationX),
                                                      static_cast<float>(collider.localRotationY),
                                                      static_cast<float>(collider.localRotationZ)));
            shape->setLocalTransform(
                static_cast<float>(collider.localX * scale.x), static_cast<float>(collider.localY * scale.y),
                static_cast<float>(collider.localZ * scale.z), rotation.x, rotation.y, rotation.z, rotation.w);
        }
        return shape;
    }

    explicit Impl(PhysicsPlacementRequest value) : request(std::move(value)), world(0.0f, 0.0f, 0.0f, false) {}

    editing::Result<void> build() {
        try {
            if (request.settings.mode == PhysicsPlacementMode::Fall)
                world.setGravity(static_cast<float>(request.settings.gravityX),
                                 static_cast<float>(request.settings.gravityY),
                                 static_cast<float>(request.settings.gravityZ));
            world.setContinuousCollisionEnabled(true);
            world.setMaximumLinearSpeed(static_cast<float>(request.settings.maximumLinearSpeed));
            world.setContactTuning(
                static_cast<float>(request.settings.softCollision ? request.settings.contactHertz
                                                                  : std::max(120.0, request.settings.contactHertz)),
                static_cast<float>(request.settings.softCollision ? request.settings.contactDampingRatio : 1.0),
                static_cast<float>(request.settings.softCollision ? request.settings.maximumPushOutSpeed
                                                                  : std::max(request.settings.maximumLinearSpeed,
                                                                             request.settings.maximumPushOutSpeed)));
            glm::dvec3  centroid(0.0);
            std::size_t selectedCount = 0;
            for (const auto& object : request.objects) {
                const auto& transform = object.transform;
                auto* rawBody = world.newBody(object.selected ? "dynamic" : "static", static_cast<float>(transform.x),
                                              static_cast<float>(transform.y), static_cast<float>(transform.z));
                if (!rawBody)
                    return placementError<void>(editing::Status::Failed, "scene.physics-placement.body-create",
                                                "Box3D could not create a placement body");
                RuntimeObject runtime;
                runtime.admitted     = object;
                runtime.appliedScale = {transform.scaleX, transform.scaleY, transform.scaleZ};
                runtime.body.reset(rawBody);
                const glm::quat rotation = rotationOf(transform);
                runtime.body->setRotation(rotation.x, rotation.y, rotation.z, rotation.w);
                for (const auto& collider : selectedColliders(object, request.settings.colliderPolicy)) {
                    auto shape = createShape(*runtime.body, collider, runtime.appliedScale);
                    if (shape) runtime.shapes.push_back(std::move(shape));
                }
                if (runtime.shapes.empty())
                    return placementError<void>(editing::Status::Failed, "scene.physics-placement.shape-create",
                                                "Collider policy left the placement object without a usable collider");
                if (object.selected) {
                    const bool dropping = request.settings.mode == PhysicsPlacementMode::Fall;
                    runtime.body->setGravityScale(dropping ? 1.0f : 0.0f);
                    runtime.body->setLinearDamping(static_cast<float>(dropping ? 0.6 : request.settings.linearDamping));
                    runtime.body->setAngularDamping(
                        static_cast<float>(dropping ? 0.8 : request.settings.angularDamping));
                    runtime.body->setMotionLocks(request.settings.freezePositionX, request.settings.freezePositionY,
                                                 request.settings.freezePositionZ, request.settings.freezeRotationX,
                                                 request.settings.freezeRotationY, request.settings.freezeRotationZ);
                    centroid += glm::dvec3(transform.x, transform.y, transform.z);
                    if ((request.primaryObject.empty() && selectedCount == 0) ||
                        request.primaryObject == object.object) {
                        initialHandleRotation = handleRotation = rotation;
                        initialHandleScale = handleScale = runtime.appliedScale;
                    }
                    ++selectedCount;
                }
                objects.push_back(std::move(runtime));
            }
            centroid /= static_cast<double>(selectedCount);
            initialHandle = handle = centroid;
            if (request.settings.preserveSelectionLayout) {
                for (std::size_t left = 0; left < objects.size(); ++left) {
                    if (!objects[left].admitted.selected) continue;
                    for (std::size_t right = left + 1; right < objects.size(); ++right) {
                        if (objects[right].admitted.selected)
                            world.setBodyPairCollisionEnabled(objects[left].body.get(), objects[right].body.get(),
                                                              false);
                    }
                }
            }
            return editing::applied<void>();
        } catch (const std::exception& error) {
            return placementError<void>(editing::Status::Failed, "scene.physics-placement.build",
                                        std::string("Could not build placement preview: ") + error.what());
        }
    }

    PhysicsPlacementRequest    request;
    physics::World3D           world;
    std::vector<RuntimeObject> objects;
    glm::dvec3                 initialHandle{0.0};
    glm::dvec3                 handle{0.0};
    glm::quat                  initialHandleRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat                  handleRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::dvec3                 initialHandleScale{1.0};
    glm::dvec3                 handleScale{1.0};
    bool                       surfaceAligned = false;
    glm::vec3                  surfaceNormal{0.0f, 1.0f, 0.0f};
    std::uint64_t              tick = 0;
};

ScenePhysicsPlacementBackend::ScenePhysicsPlacementBackend(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ScenePhysicsPlacementBackend::~ScenePhysicsPlacementBackend() = default;

editing::Result<std::unique_ptr<ScenePhysicsPlacementBackend>> ScenePhysicsPlacementBackend::create(
    PhysicsPlacementRequest request) {
    const auto admittedByPolicy = [&](const PhysicsPlacementObject& object) {
        if (request.admission.excludeDisabled && !object.enabled) return false;
        if (request.admission.excludeLocked && object.locked) return false;
        if ((object.layerBits & request.admission.includedLayerBits) == 0 ||
            (object.layerBits & request.admission.excludedLayerBits) != 0)
            return false;
        for (const auto& required : request.admission.requiredTags)
            if (std::find(object.tags.begin(), object.tags.end(), required) == object.tags.end()) return false;
        if (request.admission.useBounds) {
            const auto& value = object.transform;
            if (value.x < request.admission.minimumX || value.y < request.admission.minimumY ||
                value.z < request.admission.minimumZ || value.x > request.admission.maximumX ||
                value.y > request.admission.maximumY || value.z > request.admission.maximumZ)
                return false;
        }
        return true;
    };
    for (const auto& object : request.objects)
        if (object.selected && !admittedByPolicy(object))
            return placementError<std::unique_ptr<ScenePhysicsPlacementBackend>>(
                editing::Status::Rejected, "scene.physics-placement.selection-filtered",
                "Admission policy must not exclude a selected placement object");
    request.objects.erase(std::remove_if(request.objects.begin(), request.objects.end(),
                                         [&](const auto& object) { return !admittedByPolicy(object); }),
                          request.objects.end());
    if (request.objects.empty())
        return placementError<std::unique_ptr<ScenePhysicsPlacementBackend>>(
            editing::Status::Rejected, "scene.physics-placement.empty", "Placement requires at least one object");
    if (request.settings.subStepCount <= 0 || request.settings.subStepCount > 64 ||
        !finite(request.settings.maximumLinearSpeed) || request.settings.maximumLinearSpeed <= 0.0 ||
        !finite(request.settings.gravityX) || !finite(request.settings.gravityY) ||
        !finite(request.settings.gravityZ) || !finite(request.settings.linearDamping) ||
        request.settings.linearDamping < 0.0 || !finite(request.settings.angularDamping) ||
        request.settings.angularDamping < 0.0 || !finite(request.settings.contactHertz) ||
        request.settings.contactHertz <= 0.0 || !finite(request.settings.contactDampingRatio) ||
        request.settings.contactDampingRatio < 0.0 || !finite(request.settings.maximumPushOutSpeed) ||
        request.settings.maximumPushOutSpeed < 0.0 || !finite(request.settings.maximumAngularSpeed) ||
        request.settings.maximumAngularSpeed <= 0.0 || !finite(request.settings.minimumBoundsSpeedFactor) ||
        request.settings.minimumBoundsSpeedFactor <= 0.0 || !finite(request.settings.maximumBoundsSpeedFactor) ||
        request.settings.maximumBoundsSpeedFactor < request.settings.minimumBoundsSpeedFactor ||
        !finite(request.settings.teleportDistance) || request.settings.teleportDistance < 0.0)
        return placementError<std::unique_ptr<ScenePhysicsPlacementBackend>>(
            editing::Status::Rejected, "scene.physics-placement.settings",
            "Placement settings must be finite and use a substep count in [1, 64]");
    std::set<editing::ObjectId> ids;
    std::size_t                 selectedCount = 0;
    for (const auto& object : request.objects) {
        if (object.shape == PhysicsPlacementShape::Auto && object.colliders.empty())
            return placementError<std::unique_ptr<ScenePhysicsPlacementBackend>>(
                editing::Status::Rejected, "scene.physics-placement.unresolved-shape",
                "Automatic placement shapes must be resolved by a scene editor session");
        const bool validHull = object.shape != PhysicsPlacementShape::ConvexHull || !object.colliders.empty() ||
                               (object.convexVertices.size() >= 12 && object.convexVertices.size() % 3 == 0 &&
                                object.convexMaxVertices >= 4 && object.convexMaxVertices <= 254 &&
                                std::all_of(object.convexVertices.begin(), object.convexVertices.end(),
                                            [](float value) { return std::isfinite(value); }));
        const auto colliders = Impl::selectedColliders(object, request.settings.colliderPolicy);
        const bool validColliders =
            !colliders.empty() && std::all_of(colliders.begin(), colliders.end(), [](const auto& collider) {
                const bool validGeometry = finite(collider.halfExtentX) && finite(collider.halfExtentY) &&
                                           finite(collider.halfExtentZ) && collider.halfExtentX > 0.0 &&
                                           collider.halfExtentY > 0.0 && collider.halfExtentZ > 0.0;
                const bool validPose = finite(collider.localX) && finite(collider.localY) && finite(collider.localZ) &&
                                       finite(collider.localRotationX) && finite(collider.localRotationY) &&
                                       finite(collider.localRotationZ);
                const bool hull = collider.shape != PhysicsPlacementShape::ConvexHull ||
                                  (collider.convexVertices.size() >= 12 && collider.convexVertices.size() % 3 == 0 &&
                                   collider.convexMaxVertices >= 4 && collider.convexMaxVertices <= 254 &&
                                   std::all_of(collider.convexVertices.begin(), collider.convexVertices.end(),
                                               [](float value) { return std::isfinite(value); }));
                return collider.shape != PhysicsPlacementShape::Auto && validGeometry && validPose && hull;
            });
        if (object.object.empty() || !ids.insert(object.object).second || !finiteTransform(object.transform) ||
            !finite(object.halfExtentX) || !finite(object.halfExtentY) || !finite(object.halfExtentZ) ||
            object.halfExtentX <= 0.0 || object.halfExtentY <= 0.0 || object.halfExtentZ <= 0.0 ||
            object.transform.scaleX == 0.0 || object.transform.scaleY == 0.0 || object.transform.scaleZ == 0.0 ||
            !validHull || !validColliders)
            return placementError<std::unique_ptr<ScenePhysicsPlacementBackend>>(
                editing::Status::Rejected, "scene.physics-placement.object",
                "Placement objects require unique ids, finite transforms, and positive nonzero bounds");
        if (object.selected) ++selectedCount;
    }
    if (selectedCount == 0)
        return placementError<std::unique_ptr<ScenePhysicsPlacementBackend>>(
            editing::Status::Rejected, "scene.physics-placement.selection",
            "Placement requires at least one selected object");
    if (!request.primaryObject.empty()) {
        const auto primary = std::find_if(request.objects.begin(), request.objects.end(), [&](const auto& object) {
            return object.object == request.primaryObject && object.selected;
        });
        if (primary == request.objects.end())
            return placementError<std::unique_ptr<ScenePhysicsPlacementBackend>>(
                editing::Status::Rejected, "scene.physics-placement.primary",
                "Placement primary object must belong to the selected objects");
    }
    auto impl  = std::make_unique<Impl>(std::move(request));
    auto built = impl->build();
    if (!built.ok()) return editing::Result<std::unique_ptr<ScenePhysicsPlacementBackend>>::failure(built.status());
    return editing::applied<std::unique_ptr<ScenePhysicsPlacementBackend>>(
        std::unique_ptr<ScenePhysicsPlacementBackend>(new ScenePhysicsPlacementBackend(std::move(impl))));
}

std::unique_ptr<editor::IEditorSimulationBackend> ScenePhysicsPlacementBackend::cloneForPreview() const {
    auto cloned = create(impl_->request);
    if (!cloned.ok()) return nullptr;
    auto backend = std::move(cloned.value());
    auto moved   = backend->setHandlePosition(impl_->handle.x, impl_->handle.y, impl_->handle.z);
    if (!moved.ok()) return nullptr;
    backend->impl_->handleRotation = impl_->handleRotation;
    backend->impl_->handleScale    = impl_->handleScale;
    backend->impl_->surfaceAligned = impl_->surfaceAligned;
    backend->impl_->surfaceNormal  = impl_->surfaceNormal;
    return backend;
}

editing::Result<void> ScenePhysicsPlacementBackend::setHandlePosition(double x, double y, double z) {
    if (!finite(x) || !finite(y) || !finite(z))
        return placementError<void>(editing::Status::Rejected, "scene.physics-placement.handle",
                                    "Placement handle coordinates must be finite");
    impl_->handle = {x, y, z};
    return editing::applied<void>();
}

editing::Result<void> ScenePhysicsPlacementBackend::setHandleRotation(double x, double y, double z) {
    if (!finite(x) || !finite(y) || !finite(z))
        return placementError<void>(editing::Status::Rejected, "scene.physics-placement.handle-rotation",
                                    "Placement handle rotation must be finite");
    impl_->handleRotation = glm::quat(glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
    return editing::applied<void>();
}

editing::Result<void> ScenePhysicsPlacementBackend::setHandleScale(double x, double y, double z) {
    if (!finite(x) || !finite(y) || !finite(z) || x == 0.0 || y == 0.0 || z == 0.0)
        return placementError<void>(editing::Status::Rejected, "scene.physics-placement.handle-scale",
                                    "Placement handle scale must be finite and nonzero");
    impl_->handleScale = {x, y, z};
    return editing::applied<void>();
}

editing::Result<void> ScenePhysicsPlacementBackend::alignHandleToSurface(double fromX, double fromY, double fromZ,
                                                                         double toX, double toY, double toZ,
                                                                         double offset) {
    if (!finite(fromX) || !finite(fromY) || !finite(fromZ) || !finite(toX) || !finite(toY) || !finite(toZ) ||
        !finite(offset) || (fromX == toX && fromY == toY && fromZ == toZ))
        return placementError<void>(editing::Status::Rejected, "scene.physics-placement.surface-ray",
                                    "Surface alignment requires a finite, nonzero ray and offset");
    try {
        impl_->world.rayCastAll(static_cast<float>(fromX), static_cast<float>(fromY), static_cast<float>(fromZ),
                                static_cast<float>(toX), static_cast<float>(toY), static_cast<float>(toZ), 256);
        std::set<int> selectedBodies;
        for (const auto& object : impl_->objects)
            if (object.admitted.selected) selectedBodies.insert(object.body->getId());
        int hit = -1;
        for (int index = 0; index < impl_->world.getRayResultCount(); ++index) {
            if (!selectedBodies.contains(impl_->world.getRayResultBodyId(index))) {
                hit = index;
                break;
            }
        }
        if (hit < 0)
            return placementError<void>(editing::Status::NotFound, "scene.physics-placement.surface-miss",
                                        "Surface alignment ray did not hit an admitted static object");
        glm::vec3 normal(impl_->world.getRayResultNormalX(hit), impl_->world.getRayResultNormalY(hit),
                         impl_->world.getRayResultNormalZ(hit));
        if (glm::dot(normal, normal) < 1.0e-8f)
            return placementError<void>(editing::Status::Failed, "scene.physics-placement.surface-normal",
                                        "Surface alignment hit did not provide a usable normal");
        normal                    = glm::normalize(normal);
        const glm::vec3 initialUp = impl_->initialHandleRotation * glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::quat delta     = glm::rotation(glm::normalize(initialUp), normal);
        impl_->handleRotation     = glm::normalize(delta * impl_->initialHandleRotation);
        float minimumProjection   = 0.0f;
        bool  hasProjection       = false;
        for (const auto& object : impl_->objects) {
            if (!object.admitted.selected) continue;
            const auto&     transform   = object.admitted.transform;
            const glm::vec3 groupOffset = delta * glm::vec3(static_cast<float>(transform.x - impl_->initialHandle.x),
                                                            static_cast<float>(transform.y - impl_->initialHandle.y),
                                                            static_cast<float>(transform.z - impl_->initialHandle.z));
            const glm::quat objectRotation = glm::normalize(delta * rotationOf(transform));
            const glm::vec3 half(static_cast<float>(object.admitted.halfExtentX * std::abs(transform.scaleX)),
                                 static_cast<float>(object.admitted.halfExtentY * std::abs(transform.scaleY)),
                                 static_cast<float>(object.admitted.halfExtentZ * std::abs(transform.scaleZ)));
            for (int mask = 0; mask < 8; ++mask) {
                const glm::vec3 corner((mask & 1) ? half.x : -half.x, (mask & 2) ? half.y : -half.y,
                                       (mask & 4) ? half.z : -half.z);
                const float     projection = glm::dot(groupOffset + objectRotation * corner, normal);
                if (!hasProjection || projection < minimumProjection) minimumProjection = projection;
                hasProjection = true;
            }
        }
        const glm::vec3 hitPoint(impl_->world.getRayResultX(hit), impl_->world.getRayResultY(hit),
                                 impl_->world.getRayResultZ(hit));
        const glm::vec3 target = hitPoint + normal * (static_cast<float>(offset) - minimumProjection);
        impl_->handle          = glm::dvec3(target);
        impl_->surfaceAligned  = true;
        impl_->surfaceNormal   = normal;
        return editing::applied<void>();
    } catch (const std::exception& error) {
        return placementError<void>(editing::Status::Failed, "scene.physics-placement.surface-align",
                                    std::string("Surface alignment failed: ") + error.what());
    }
}

editing::Result<void> ScenePhysicsPlacementBackend::step(std::uint64_t tick, double fixedDelta) {
    if (tick <= impl_->tick || !finite(fixedDelta) || fixedDelta <= 0.0 || fixedDelta > 1.0)
        return placementError<void>(editing::Status::Rejected, "scene.physics-placement.step",
                                    "Placement ticks must increase and delta must be within (0, 1]");
    try {
        if (impl_->request.settings.mode != PhysicsPlacementMode::Fall) {
            const glm::quat  delta = glm::normalize(impl_->handleRotation * glm::inverse(impl_->initialHandleRotation));
            const glm::dvec3 scaleRatio          = impl_->handleScale / impl_->initialHandleScale;
            double           requiredLinearSpeed = impl_->request.settings.maximumLinearSpeed;
            for (auto& object : impl_->objects) {
                if (!object.admitted.selected) continue;
                const auto&      transform = object.admitted.transform;
                const glm::dvec3 desiredScale(transform.scaleX * scaleRatio.x, transform.scaleY * scaleRatio.y,
                                              transform.scaleZ * scaleRatio.z);
                if (desiredScale != object.appliedScale) {
                    const auto colliders =
                        Impl::selectedColliders(object.admitted, impl_->request.settings.colliderPolicy);
                    for (std::size_t index = 0; index < colliders.size(); ++index) {
                        const auto& collider = colliders[index];
                        const float width  = static_cast<float>(2.0 * collider.halfExtentX * std::abs(desiredScale.x));
                        const float height = static_cast<float>(2.0 * collider.halfExtentY * std::abs(desiredScale.y));
                        const float depth  = static_cast<float>(2.0 * collider.halfExtentZ * std::abs(desiredScale.z));
                        if (collider.shape == PhysicsPlacementShape::Sphere)
                            object.shapes[index]->setSphereRadius(std::max({width, height, depth}) * 0.5f);
                        else if (collider.shape == PhysicsPlacementShape::Capsule)
                            object.shapes[index]->setCapsuleSize(height, std::max(width, depth) * 0.5f);
                        else if (collider.shape == PhysicsPlacementShape::ConvexHull) {
                            auto vertices = collider.convexVertices;
                            for (std::size_t vertex = 0; vertex < vertices.size(); vertex += 3) {
                                vertices[vertex] *= static_cast<float>(desiredScale.x);
                                vertices[vertex + 1] *= static_cast<float>(desiredScale.y);
                                vertices[vertex + 2] *= static_cast<float>(desiredScale.z);
                            }
                            object.shapes[index]->setConvexHullVertices(vertices, collider.convexMaxVertices);
                        } else
                            object.shapes[index]->setBoxSize(width, height, depth);
                        const auto localRotation = glm::quat(glm::vec3(static_cast<float>(collider.localRotationX),
                                                                       static_cast<float>(collider.localRotationY),
                                                                       static_cast<float>(collider.localRotationZ)));
                        object.shapes[index]->setLocalTransform(static_cast<float>(collider.localX * desiredScale.x),
                                                                static_cast<float>(collider.localY * desiredScale.y),
                                                                static_cast<float>(collider.localZ * desiredScale.z),
                                                                localRotation.x, localRotation.y, localRotation.z,
                                                                localRotation.w);
                    }
                    object.appliedScale = desiredScale;
                }
                const glm::dvec3 unrotatedOffset((transform.x - impl_->initialHandle.x) * scaleRatio.x,
                                                 (transform.y - impl_->initialHandle.y) * scaleRatio.y,
                                                 (transform.z - impl_->initialHandle.z) * scaleRatio.z);
                const glm::vec3  offset   = delta * glm::vec3(unrotatedOffset);
                const glm::quat  rotation = glm::normalize(delta * rotationOf(transform));
                glm::dvec3       target(static_cast<double>(offset.x) + impl_->handle.x,
                                        static_cast<double>(offset.y) + impl_->handle.y,
                                        static_cast<double>(offset.z) + impl_->handle.z);
                glm::quat        targetRotation = rotation;
                if (impl_->request.settings.mode == PhysicsPlacementMode::Rotate)
                    target = glm::dvec3(offset) + impl_->initialHandle;
                if (impl_->request.settings.mode == PhysicsPlacementMode::Point) {
                    target                    = {transform.x, transform.y, transform.z};
                    const glm::vec3 direction = glm::vec3(impl_->handle - target);
                    if (glm::dot(direction, direction) > 1.0e-8f) {
                        const glm::vec3 forward = glm::normalize(direction);
                        const glm::vec3 up      = std::abs(glm::dot(forward, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f
                                                      ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                      : glm::vec3(0.0f, 1.0f, 0.0f);
                        targetRotation          = glm::quatLookAt(forward, up);
                    }
                }
                const glm::dvec3 current(object.body->getX(), object.body->getY(), object.body->getZ());
                const double     bound = std::max({object.admitted.halfExtentX * 2.0, object.admitted.halfExtentY * 2.0,
                                                   object.admitted.halfExtentZ * 2.0});
                const double     boundFactor = impl_->request.settings.scaleSpeedByBounds
                                                   ? std::clamp(bound, impl_->request.settings.minimumBoundsSpeedFactor,
                                                                impl_->request.settings.maximumBoundsSpeedFactor)
                                                   : 1.0;
                requiredLinearSpeed =
                    std::max(requiredLinearSpeed, glm::length(target - current) / fixedDelta * 1.1 * boundFactor);
                if (impl_->request.settings.teleportDistance > 0.0 &&
                    glm::length(target - current) > impl_->request.settings.teleportDistance) {
                    object.body->setPosition(static_cast<float>(target.x), static_cast<float>(target.y),
                                             static_cast<float>(target.z));
                    object.body->setRotation(targetRotation.x, targetRotation.y, targetRotation.z, targetRotation.w);
                    object.body->setLinearVelocity(0.0f, 0.0f, 0.0f);
                    object.body->setAngularVelocity(0.0f, 0.0f, 0.0f);
                    continue;
                }
                object.body->setTargetTransform(static_cast<float>(target.x), static_cast<float>(target.y),
                                                static_cast<float>(target.z), targetRotation.x, targetRotation.y,
                                                targetRotation.z, targetRotation.w, static_cast<float>(fixedDelta));
            }
            impl_->world.setMaximumLinearSpeed(static_cast<float>(requiredLinearSpeed));
        }
        auto duration = eve::Duration::fromSeconds(fixedDelta);
        if (!duration.ok()) return editing::Result<void>::failure(duration.status());
        physics::SimulationSettings settings;
        settings.subStepCount = impl_->request.settings.subStepCount;
        auto stepped          = impl_->world.step({eve::SimulationTick{tick}, duration.value()}, settings);
        if (!stepped.ok()) return editing::Result<void>::failure(stepped.status());
        for (auto& object : impl_->objects) {
            if (!object.admitted.selected) continue;
            glm::vec3   angular(object.body->getAngularVelocityX(), object.body->getAngularVelocityY(),
                                object.body->getAngularVelocityZ());
            const float length = glm::length(angular);
            if (length > impl_->request.settings.maximumAngularSpeed) {
                angular *= static_cast<float>(impl_->request.settings.maximumAngularSpeed) / length;
                object.body->setAngularVelocity(angular.x, angular.y, angular.z);
            }
        }
        impl_->tick = tick;
        return editing::applied<void>();
    } catch (const std::exception& error) {
        return placementError<void>(editing::Status::Failed, "scene.physics-placement.advance",
                                    std::string("Placement preview failed to advance: ") + error.what());
    }
}

editing::Result<std::vector<editor::SimulationObjectSample>> ScenePhysicsPlacementBackend::capture() const {
    std::vector<editor::SimulationObjectSample> samples;
    samples.reserve(impl_->objects.size());
    for (const auto& object : impl_->objects) {
        if (!object.admitted.selected) continue;
        editor::SimulationObjectSample sample;
        sample.object    = object.admitted.object.value();
        sample.positionX = object.body->getX();
        sample.positionY = object.body->getY();
        sample.positionZ = object.body->getZ();
        sample.rotationX = object.body->getRotX();
        sample.rotationY = object.body->getRotY();
        sample.rotationZ = object.body->getRotZ();
        sample.rotationW = object.body->getRotW();
        samples.push_back(std::move(sample));
    }
    return editing::applied<std::vector<editor::SimulationObjectSample>>(std::move(samples));
}

editing::Result<PhysicsPlacementFrame> ScenePhysicsPlacementBackend::placementFrame() const {
    PhysicsPlacementFrame frame;
    frame.tick           = impl_->tick;
    frame.colliding      = impl_->world.getContactCount() > 0;
    frame.settled        = frame.colliding && frame.tick > 2;
    frame.surfaceAligned = impl_->surfaceAligned;
    frame.surfaceNormalX = impl_->surfaceNormal.x;
    frame.surfaceNormalY = impl_->surfaceNormal.y;
    frame.surfaceNormalZ = impl_->surfaceNormal.z;
    frame.objects.reserve(impl_->objects.size());
    for (const auto& object : impl_->objects) {
        PhysicsPlacementObject captured = object.admitted;
        captured.transform.x            = object.body->getX();
        captured.transform.y            = object.body->getY();
        captured.transform.z            = object.body->getZ();
        const glm::quat rotation(object.body->getRotW(), object.body->getRotX(), object.body->getRotY(),
                                 object.body->getRotZ());
        const glm::vec3 euler        = glm::eulerAngles(rotation);
        captured.transform.rotationX = euler.x;
        captured.transform.rotationY = euler.y;
        captured.transform.rotationZ = euler.z;
        captured.transform.scaleX    = object.appliedScale.x;
        captured.transform.scaleY    = object.appliedScale.y;
        captured.transform.scaleZ    = object.appliedScale.z;
        if (captured.selected && (std::abs(object.body->getLinearVelocityX()) > 0.05f ||
                                  std::abs(object.body->getLinearVelocityY()) > 0.05f ||
                                  std::abs(object.body->getLinearVelocityZ()) > 0.05f ||
                                  std::abs(object.body->getAngularVelocityX()) > 0.05f ||
                                  std::abs(object.body->getAngularVelocityY()) > 0.05f ||
                                  std::abs(object.body->getAngularVelocityZ()) > 0.05f))
            frame.settled = false;
        frame.objects.push_back(std::move(captured));
    }
    return editing::applied<PhysicsPlacementFrame>(std::move(frame));
}

editing::Revision ScenePhysicsPlacementBackend::sourceRevision() const noexcept {
    return impl_->request.sourceRevision;
}

}  // namespace eve::scene_editor
