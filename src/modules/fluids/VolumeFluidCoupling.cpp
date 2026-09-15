#include "fluids/VolumeFluidCoupling.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include "fluids/VolumeFluid.h"
#include "physics/Body3D.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"

namespace eve::fluids {
struct VolumeFluidCoupling::Impl {
    struct Binding {
        physics::PhysicsLink link;
        VolumeFluidCollider  shape;
    };
    std::vector<Binding>                     bindings;
    std::vector<physics::Body3D*>            bodies;
    std::vector<VolumeFluidCollider>         samples;
    std::vector<glm::vec3>                   centers, linear, angular;
    std::vector<std::pair<unsigned, size_t>> labels;
    float                                    maximumLinearDelta = 20.f, maximumAngularDelta = 100.f;
    unsigned                                 clampedBodyCount = 0;
};
VolumeFluidCoupling::VolumeFluidCoupling() : impl_(std::make_unique<Impl>()) {}
VolumeFluidCoupling::~VolumeFluidCoupling() = default;
Result<void> VolumeFluidCoupling::attach(physics::Body3D& body, const VolumeFluidCollider& localShape) {
    if (impl_->bindings.size() >= 1024 || std::any_of(impl_->bindings.begin(), impl_->bindings.end(),
                                                      [&](const auto& b) { return b.shape.label == localShape.label; }))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Duplicate coupling label or binding limit exceeded",
                                                       "fluids.volume.coupling"));
    auto link = physics::PhysicsLink::fromBody(body);
    if (!link) return Result<void>::failure(link.status());
    auto validator = VolumeFluid::create(VolumeFluidSettings{.capacity = 1});
    if (!validator) return Result<void>::failure(validator.status());
    auto valid = validator.value()->setColliders(std::span(&localShape, 1));
    if (!valid) return valid;
    impl_->bindings.push_back({link.value(), localShape});
    return Result<void>::success();
}
void VolumeFluidCoupling::detach(unsigned label) {
    std::erase_if(impl_->bindings, [label](const auto& b) { return b.shape.label == label; });
}
Result<void> VolumeFluidCoupling::setImpulseLimits(float maximumLinearDelta, float maximumAngularDelta) {
    if (!std::isfinite(maximumLinearDelta) || maximumLinearDelta < .01f || maximumLinearDelta > 1000.f ||
        !std::isfinite(maximumAngularDelta) || maximumAngularDelta < .01f || maximumAngularDelta > 10000.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid rigid impulse velocity-change limits", "fluids.volume.coupling"));
    impl_->maximumLinearDelta  = maximumLinearDelta;
    impl_->maximumAngularDelta = maximumAngularDelta;
    return Result<void>::success();
}
unsigned         VolumeFluidCoupling::lastClampedBodyCount() const { return impl_->clampedBodyCount; }
Result<unsigned> VolumeFluidCoupling::step(physics::World3D& world, VolumeFluid& fluid, float dt, unsigned substeps,
                                           std::span<const VolumeFluidThermalRule> rules) {
    if (!world.isValid())
        return Result<unsigned>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Rigid world is no longer valid", "fluids.volume.coupling"));
    auto& bodies  = impl_->bodies;
    auto& samples = impl_->samples;
    auto& centers = impl_->centers;
    auto& labels  = impl_->labels;
    bodies.clear();
    samples.clear();
    centers.clear();
    labels.clear();
    bodies.reserve(impl_->bindings.size());
    samples.reserve(impl_->bindings.size());
    centers.reserve(impl_->bindings.size());
    labels.reserve(impl_->bindings.size());
    for (const auto& binding : impl_->bindings) {
        auto resolved = binding.link.resolve(world);
        if (!resolved) return Result<unsigned>::failure(resolved.status());
        auto*           body = resolved.value();
        const glm::quat orientation(body->getRotW(), body->getRotX(), body->getRotY(), body->getRotZ());
        auto            sample = binding.shape;
        const auto      center = body->localToWorldPointOwned(sample.center.x, sample.center.y, sample.center.z);
        if (!center) return Result<unsigned>::failure(center.status());
        const auto velocity = body->getLocalPointVelocityOwned(sample.center.x, sample.center.y, sample.center.z);
        if (!velocity) return Result<unsigned>::failure(velocity.status());
        sample.center          = {center.value().x, center.value().y, center.value().z};
        sample.velocity        = {velocity.value().x, velocity.value().y, velocity.value().z};
        sample.angularVelocity = {body->getAngularVelocityX(), body->getAngularVelocityY(),
                                  body->getAngularVelocityZ()};
        const auto q =
            orientation * glm::quat(sample.rotation.w, sample.rotation.x, sample.rotation.y, sample.rotation.z);
        sample.rotation = {q.x, q.y, q.z, q.w};
        labels.emplace_back(sample.label, bodies.size());
        bodies.push_back(body);
        samples.push_back(sample);
        centers.push_back({body->getWorldCenterX(), body->getWorldCenterY(), body->getWorldCenterZ()});
    }
    std::sort(labels.begin(), labels.end());
    // Allocate all output aggregation before advancing either domain.
    auto& linear  = impl_->linear;
    auto& angular = impl_->angular;
    linear.assign(bodies.size(), glm::vec3(0.f));
    angular.assign(bodies.size(), glm::vec3(0.f));
    auto advanced = fluid.stepWithColliders(dt, substeps, samples, rules);
    if (!advanced) return Result<unsigned>::failure(advanced.status());
    const auto contacts = fluid.contacts();
    for (const auto& contact : contacts) {
        const auto at =
            std::lower_bound(labels.begin(), labels.end(), std::pair<unsigned, size_t>{contact.colliderLabel, 0});
        if (at == labels.end() || at->first != contact.colliderLabel) continue;
        const size_t i = at->second;
        linear[i] += contact.impulse;
        angular[i] += glm::cross(contact.point - centers[i], contact.impulse);
    }
    const auto reactions = fluid.attachmentReactions();
    for (const auto& reaction : reactions) {
        const auto at =
            std::lower_bound(labels.begin(), labels.end(), std::pair<unsigned, size_t>{reaction.colliderLabel, 0});
        if (at == labels.end() || at->first != reaction.colliderLabel) continue;
        const size_t i = at->second;
        linear[i] += reaction.impulse;
        angular[i] += glm::cross(reaction.point - centers[i], reaction.impulse);
        angular[i] += reaction.angularImpulse;
    }
    unsigned clampedBodyCount = 0;
    for (size_t i = 0; i < bodies.size(); ++i) {
        bool        clamped      = false;
        const float mass         = bodies[i]->getMass();
        const float linearLength = glm::length(linear[i]);
        if (mass > 0.f && linearLength > mass * impl_->maximumLinearDelta) {
            linear[i] *= mass * impl_->maximumLinearDelta / linearLength;
            clamped = true;
        }
        if (mass > 0.f && glm::dot(angular[i], angular[i]) > 0.f) {
            const glm::mat3 localInertia(
                bodies[i]->getInertiaXX(), bodies[i]->getInertiaXY(), bodies[i]->getInertiaXZ(),
                bodies[i]->getInertiaXY(), bodies[i]->getInertiaYY(), bodies[i]->getInertiaYZ(),
                bodies[i]->getInertiaXZ(), bodies[i]->getInertiaYZ(), bodies[i]->getInertiaZZ());
            const float inertiaScale =
                std::max({std::abs(localInertia[0][0]), std::abs(localInertia[0][1]), std::abs(localInertia[0][2]),
                          std::abs(localInertia[1][0]), std::abs(localInertia[1][1]), std::abs(localInertia[1][2]),
                          std::abs(localInertia[2][0]), std::abs(localInertia[2][1]), std::abs(localInertia[2][2])});
            if (std::isfinite(inertiaScale) && inertiaScale > 0.f) {
                const auto  normalizedInertia = localInertia / inertiaScale;
                const float determinant       = glm::determinant(normalizedInertia);
                if (std::isfinite(determinant) && std::abs(determinant) > 1e-6f) {
                    const glm::quat q(bodies[i]->getRotW(), bodies[i]->getRotX(), bodies[i]->getRotY(),
                                      bodies[i]->getRotZ());
                    const auto      rotation      = glm::mat3_cast(q);
                    const auto      angularDelta  = rotation * (glm::inverse(normalizedInertia) / inertiaScale) *
                                                    glm::transpose(rotation) * angular[i];
                    const float     angularLength = glm::length(angularDelta);
                    if (angularLength > impl_->maximumAngularDelta) {
                        angular[i] *= impl_->maximumAngularDelta / angularLength;
                        clamped = true;
                    }
                }
            }
        }
        if (glm::dot(linear[i], linear[i]) > 0.f) bodies[i]->applyLinearImpulse(linear[i].x, linear[i].y, linear[i].z);
        if (glm::dot(angular[i], angular[i]) > 0.f)
            bodies[i]->applyAngularImpulse(angular[i].x, angular[i].y, angular[i].z);
        if (clamped) ++clampedBodyCount;
    }
    impl_->clampedBodyCount = clampedBodyCount;
    return Result<unsigned>::success(unsigned(contacts.size() + reactions.size()));
}
}  // namespace eve::fluids
