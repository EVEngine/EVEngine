#include "physics/rope/Rope3D.h"

#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/DistanceField3D.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cmath>

namespace eve::physics {
namespace {
Rope3D::Vec3 subtract(Rope3D::Vec3 a, Rope3D::Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Rope3D::Vec3 add(Rope3D::Vec3 a, Rope3D::Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Rope3D::Vec3 multiply(Rope3D::Vec3 value, float scale) { return {value.x * scale, value.y * scale, value.z * scale}; }
float        dot(Rope3D::Vec3 a, Rope3D::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float        magnitude(Rope3D::Vec3 value) { return std::sqrt(dot(value, value)); }
bool finite(Rope3D::Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
}  // namespace

void Rope3D::setCollisionFriction(float value) {
    if (!std::isfinite(value) || value < 0.f || value > 1.f)
        throw Exception("Rope3D: collision friction must be in [0,1]");
    collisionFriction_ = value;
}

void Rope3D::setCollisionRestitution(float value) {
    if (!std::isfinite(value) || value < 0.f || value > 1.f)
        throw Exception("Rope3D: collision restitution must be in [0,1]");
    collisionRestitution_ = value;
}

eve::Result<RopeColliderId> Rope3D::addSphereCollider(float x, float y, float z, float colliderRadius) {
    if (!finite({x, y, z}) || !std::isfinite(colliderRadius) || colliderRadius <= 0.f)
        return eve::Result<RopeColliderId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Sphere position and radius must be finite and radius positive",
            "physics.rope3d.collider.sphere"));
    Collider collider;
    collider.id       = RopeColliderId{nextColliderId_++};
    collider.kind     = ColliderKind::Sphere;
    collider.position = {x, y, z};
    collider.radius   = colliderRadius;
    colliders_.push_back(collider);
    return eve::Result<RopeColliderId>::success(collider.id, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<RopeColliderId> Rope3D::addPlaneCollider(float x, float y, float z, float nx, float ny, float nz) {
    const Vec3  normal{nx, ny, nz};
    const float normalLength = magnitude(normal);
    if (!finite({x, y, z}) || !finite(normal) || normalLength <= 1e-6f)
        return eve::Result<RopeColliderId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Plane position and non-zero normal must be finite",
            "physics.rope3d.collider.plane"));
    Collider collider;
    collider.id       = RopeColliderId{nextColliderId_++};
    collider.kind     = ColliderKind::Plane;
    collider.position = {x, y, z};
    collider.normal   = multiply(normal, 1.f / normalLength);
    colliders_.push_back(collider);
    return eve::Result<RopeColliderId>::success(collider.id, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<RopeColliderChange> Rope3D::moveSphereCollider(RopeColliderId id, float x, float y, float z) {
    if (!id || !finite({x, y, z}))
        return eve::Result<RopeColliderChange>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                               "Collider id and position must be valid",
                                                                               "physics.rope3d.collider.move"));
    const auto found =
        std::find_if(colliders_.begin(), colliders_.end(), [&](const Collider& collider) { return collider.id == id; });
    if (found == colliders_.end())
        return eve::Result<RopeColliderChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "Collider id is stale", "physics.rope3d.collider.move"));
    if (found->kind != ColliderKind::Sphere)
        return eve::Result<RopeColliderChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "Only sphere colliders can be moved by this operation",
            "physics.rope3d.collider.move"));
    if (found->position.x == x && found->position.y == y && found->position.z == z)
        return eve::Result<RopeColliderChange>::success(RopeColliderChange::Unchanged);
    found->position = {x, y, z};
    return eve::Result<RopeColliderChange>::success(RopeColliderChange::Changed);
}

eve::Result<RopeColliderChange> Rope3D::removeCollider(RopeColliderId id) {
    if (!id)
        return eve::Result<RopeColliderChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Collider id must be valid", "physics.rope3d.collider.remove"));
    const auto found =
        std::find_if(colliders_.begin(), colliders_.end(), [&](const Collider& collider) { return collider.id == id; });
    if (found == colliders_.end())
        return eve::Result<RopeColliderChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "Collider id is stale", "physics.rope3d.collider.remove"));
    colliders_.erase(found);
    return eve::Result<RopeColliderChange>::success(RopeColliderChange::Changed);
}

void Rope3D::solveExternalCollisions() {
    for (auto& particle : particles_) {
        if (particle.attached) continue;
        for (const auto& collider : colliders_) {
            Vec3  normal{};
            float penetration = 0.f;
            if (collider.kind == ColliderKind::Sphere) {
                const Vec3  delta           = subtract(particle.position, collider.position);
                const float distance        = magnitude(delta);
                const float minimumDistance = radius_ + collider.radius;
                if (distance >= minimumDistance) continue;
                normal      = distance > 1e-6f ? multiply(delta, 1.f / distance) : Vec3{1.f, 0.f, 0.f};
                penetration = minimumDistance - distance;
            } else {
                normal               = collider.normal;
                const float distance = dot(subtract(particle.position, collider.position), normal);
                if (distance >= radius_) continue;
                penetration = radius_ - distance;
            }
            particle.position = add(particle.position, multiply(normal, penetration));
            if (collisionFriction_ > 0.f) {
                const Vec3 velocity = subtract(particle.position, particle.previous);
                const Vec3 tangent  = subtract(velocity, multiply(normal, dot(velocity, normal)));
                particle.previous   = add(particle.previous, multiply(tangent, collisionFriction_));
            }
        }
    }
}

void Rope3D::collideBorrowedSurfaces(float dt) {
    if (dt <= 1e-6f) return;
    for (auto& particle : particles_) {
        if (particle.attached) continue;
        const Vec3 predicted = particle.position;
        const Vec3 sweep     = subtract(predicted, particle.previous);

        Vec3    normal{};
        Vec3    contactPoint = predicted;
        Body3D* body         = nullptr;
        bool    hit          = false;

        if (world_ && world_->isValid()) {
            if (continuousCollision_ && magnitude(sweep) > 1e-7f &&
                world_->castSphere(particle.previous.x, particle.previous.y, particle.previous.z, radius_, sweep.x,
                                   sweep.y, sweep.z) >= 0) {
                const float fraction = std::clamp(world_->getShapeCastFraction(), 0.f, 1.f);
                normal = {world_->getShapeCastNormalX(), world_->getShapeCastNormalY(), world_->getShapeCastNormalZ()};
                particle.position = add(particle.previous, multiply(sweep, fraction));
                particle.position = add(particle.position, multiply(normal, 1e-4f));
                contactPoint      = {world_->getShapeCastX(), world_->getShapeCastY(), world_->getShapeCastZ()};
                body              = world_->findBodyById(world_->getShapeCastBodyId());
                hit               = true;
            } else {
                ClothContact3D contact;
                if (world_->pointProbe(predicted.x, predicted.y, predicted.z, radius_, &contact) && contact.hit) {
                    normal            = {contact.nx, contact.ny, contact.nz};
                    particle.position = add(predicted, multiply(normal, contact.depth));
                    contactPoint      = subtract(particle.position, multiply(normal, radius_));
                    body              = contact.body;
                    hit               = true;
                }
            }
        }

        if (!hit && sdf_) {
            if (continuousCollision_ && magnitude(sweep) > 1e-7f &&
                sdf_->castSphere(particle.previous.x, particle.previous.y, particle.previous.z, radius_, sweep.x,
                                 sweep.y, sweep.z)) {
                normal            = {sdf_->getNormalX(), sdf_->getNormalY(), sdf_->getNormalZ()};
                particle.position = add(particle.previous, multiply(sweep, sdf_->getCastFraction()));
                particle.position = add(particle.position, multiply(normal, 1e-4f));
                hit               = true;
            } else if (sdf_->checkSphere(predicted.x, predicted.y, predicted.z, radius_)) {
                normal            = {sdf_->getNormalX(), sdf_->getNormalY(), sdf_->getNormalZ()};
                particle.position = add(predicted, multiply(normal, sdf_->getPenetrationDepth()));
                hit               = true;
            }
        }

        if (!hit || magnitude(normal) <= 1e-6f) continue;
        normal = multiply(normal, 1.f / magnitude(normal));
        Vec3       bodyVelocity{};
        float      bodyMass = 0.f;
        const bool dynamic  = body && body->getType() == "dynamic";
        if (dynamic) {
            bodyVelocity = {body->getLinearVelocityX(), body->getLinearVelocityY(), body->getLinearVelocityZ()};
            bodyMass     = body->getMass();
        }
        Vec3        velocity            = multiply(sweep, 1.f / dt);
        const float relativeNormalSpeed = dot(subtract(velocity, bodyVelocity), normal);
        if (relativeNormalSpeed < 0.f) {
            const float reducedMass =
                bodyMass > 0.f ? particleMass_ * bodyMass / (particleMass_ + bodyMass) : particleMass_;
            const float impulse =
                std::min(-(1.f + collisionRestitution_) * relativeNormalSpeed * reducedMass, particleMass_ * 20.f);
            velocity           = add(velocity, multiply(normal, impulse / particleMass_));
            const Vec3 tangent = subtract(velocity, multiply(normal, dot(velocity, normal)));
            velocity           = subtract(velocity, multiply(tangent, collisionFriction_));
            particle.previous  = subtract(particle.position, multiply(velocity, dt));
            if (dynamic && bodyMass > 0.f)
                body->applyLinearImpulseAt(-normal.x * impulse, -normal.y * impulse, -normal.z * impulse,
                                           contactPoint.x, contactPoint.y, contactPoint.z);
        }
    }
}

}  // namespace eve::physics
