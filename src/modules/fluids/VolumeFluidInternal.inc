#pragma once
#include "common/Profile.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidSimplexQuery.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/noise.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <queue>
#include <tuple>

namespace eve::fluids {
namespace {
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(glm::vec4 v) { return finite(glm::vec3(v)) && std::isfinite(v.w); }
bool range(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool fluid(const VolumeFluidMaterial& m) {
    return m.phase == VolumeFluidPhase::Liquid || m.phase == VolumeFluidPhase::Gas;
}
float inverseWeight(const VolumeFluidMaterial& m) {
    return m.phase == VolumeFluidPhase::Solid ? 0.f : 1000.f / m.density;
}
bool validCombineMode(VolumeFluidMaterialCombineMode mode) {
    return mode == VolumeFluidMaterialCombineMode::Average || mode == VolumeFluidMaterialCombineMode::Minimum ||
           mode == VolumeFluidMaterialCombineMode::Multiply || mode == VolumeFluidMaterialCombineMode::Maximum;
}
float combineCoefficient(float a, float b, VolumeFluidMaterialCombineMode mode) {
    switch (mode) {
        case VolumeFluidMaterialCombineMode::Average: return (a + b) * .5f;
        case VolumeFluidMaterialCombineMode::Minimum: return std::min(a, b);
        case VolumeFluidMaterialCombineMode::Multiply: return a * b;
        case VolumeFluidMaterialCombineMode::Maximum: return std::max(a, b);
    }
    return 0.f;
}
struct CombinedContactMaterial {
    float dynamicFriction, staticFriction, rollingFriction, stickiness, stickDistance;
    bool  rollingContacts;
};
template <class Collider>
CombinedContactMaterial combineContactMaterial(const VolumeFluidMaterial& particle, const Collider& collider) {
    const auto frictionMode = static_cast<VolumeFluidMaterialCombineMode>(
        std::max(unsigned(particle.frictionCombine), unsigned(collider.frictionCombine)));
    const auto stickinessMode = static_cast<VolumeFluidMaterialCombineMode>(
        std::max(unsigned(particle.stickinessCombine), unsigned(collider.stickinessCombine)));
    return {combineCoefficient(particle.dynamicFriction, collider.friction, frictionMode),
            combineCoefficient(particle.staticFriction, collider.staticFriction, frictionMode),
            combineCoefficient(particle.rollingFriction, collider.rollingFriction, frictionMode),
            combineCoefficient(particle.stickiness, collider.stickiness, stickinessMode),
            std::max(particle.stickDistance, collider.stickDistance),
            particle.rollingContacts || collider.rollingContacts};
}
glm::vec3 contactFriction(glm::vec3 relative, glm::vec3 normal, const CombinedContactMaterial& material,
                          float normalTravel, float dt, unsigned iterations) {
    const auto  tangent = relative - normal * glm::dot(relative, normal);
    const float speed   = glm::length(tangent);
    if (speed <= 1e-7f || normalTravel <= 0.f) return glm::vec3(0.f);
    const float coefficient = speed * dt <= material.staticFriction * normalTravel ? 1.f : material.dynamicFriction;
    return tangent * (coefficient / float(iterations));
}
float contactAdhesion(float distance, float clearance, const CombinedContactMaterial& material, float dt,
                      unsigned iterations) {
    if (material.stickiness <= 0.f || material.stickDistance <= 0.f) return 0.f;
    const float separation = std::max(distance - clearance, 0.f);
    const float weight     = 1.f - std::min(separation / material.stickDistance, 1.f);
    return std::min(material.stickDistance, material.stickiness * weight * dt / float(iterations));
}
template <class Collider>
bool validColliderMaterial(const Collider& collider) {
    return range(collider.friction, 0.f, 1.f) && range(collider.staticFriction, 0.f, 1.f) &&
           range(collider.rollingFriction, 0.f, 1.f) && range(collider.stickiness, 0.f, 1000.f) &&
           range(collider.stickDistance, 0.f, 10.f) && validCombineMode(collider.frictionCombine) &&
           validCombineMode(collider.stickinessCombine);
}
bool validMaterial(const VolumeFluidMaterial& m) {
    return range(m.density, 0.1f, 100000.f) && range(m.smoothing, 1.f, 4.f) && range(m.viscosity, 0.f, 100.f) &&
           range(m.cohesion, 0.f, 10.f) && range(m.buoyancy, -10.f, 10.f) && range(m.drag, 0.f, 100.f) &&
           range(m.atmosphericPressure, 0.f, 1000.f) && range(m.diffusion, 0.f, 100.f) && range(m.yield, 0.f, 100.f) &&
           range(m.vorticity, 0.f, 10.f) && range(m.dynamicFriction, 0.f, 1.f) && range(m.staticFriction, 0.f, 1.f) &&
           range(m.rollingFriction, 0.f, 1.f) && range(m.stickiness, 0.f, 1000.f) &&
           range(m.stickDistance, 0.f, 10.f) && validCombineMode(m.frictionCombine) &&
           validCombineMode(m.stickinessCombine) &&
           (fluid(m) || m.phase == VolumeFluidPhase::Granular || m.phase == VolumeFluidPhase::Solid);
}
bool validFilter(unsigned filter) { return (filter & 0xffffu) != 0 && (filter >> 16u) != 0; }
bool filtersMatch(unsigned a, unsigned b) {
    return ((a & 0xffffu) & (b >> 16u)) != 0 && ((b & 0xffffu) & (a >> 16u)) != 0;
}
bool validParticleShape(const VolumeFluidParticle& particle) {
    const bool automatic     = glm::all(glm::equal(particle.radii, glm::vec3(0.f)));
    const bool explicitRadii = glm::all(glm::greaterThanEqual(particle.radii, glm::vec3(.0005f))) &&
                               glm::all(glm::lessThanEqual(particle.radii, glm::vec3(10.f)));
    return finite(particle.radii) && (automatic || explicitRadii) && finite(particle.orientation) &&
           std::abs(glm::dot(particle.orientation, particle.orientation) - 1.f) <= .001f;
}
glm::quat particleQuaternion(const VolumeFluidParticle& particle) {
    return {particle.orientation.w, particle.orientation.x, particle.orientation.y, particle.orientation.z};
}
float ellipsoidRadius(const VolumeFluidParticle& particle, glm::vec3 worldNormal) {
    const auto local = glm::conjugate(particleQuaternion(particle)) * worldNormal;
    return 1.f / glm::length(local / particle.radii);
}
bool rayEllipsoid(const VolumeFluidParticle& particle, glm::vec3 origin, glm::vec3 direction, float maxDistance,
                  float& distance, glm::vec3& normal) {
    const auto  rotation       = particleQuaternion(particle);
    const auto  inverse        = glm::conjugate(rotation);
    const auto  localOrigin    = (inverse * (origin - particle.position)) / particle.radii;
    const auto  localDirection = (inverse * direction) / particle.radii;
    const float a              = glm::dot(localDirection, localDirection);
    const float b              = glm::dot(localOrigin, localDirection);
    const float c              = glm::dot(localOrigin, localOrigin) - 1.f;
    if (c <= 0.f)
        distance = 0.f;
    else {
        const float discriminant = b * b - a * c;
        if (b >= 0.f || discriminant < 0.f) return false;
        distance = (-b - std::sqrt(discriminant)) / a;
        if (distance < 0.f || distance > maxDistance) return false;
    }
    const auto point      = origin + direction * distance;
    const auto localPoint = inverse * (point - particle.position);
    normal                = rotation * (localPoint / (particle.radii * particle.radii));
    const float length    = glm::length(normal);
    normal                = length > 1e-7f ? normal / length : -direction;
    return true;
}
Result<void> invalid(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume"));
}
}  // namespace

struct VolumeFluid::Impl {
    static constexpr int64_t                    noGrabber = -1;
    VolumeFluidSettings                         settings;
    float                                       h, volume, selfKernel;
    glm::ivec3                                  dims;
    std::vector<VolumeFluidParticle>            state;
    std::vector<VolumeFluidCollider>            colliders;
    std::vector<VolumeFluidSdfCollider>         sdfColliders;
    std::vector<VolumeFluidHeightFieldCollider> heightFieldColliders;
    /** Reused end-pose copies while collider transforms are interpolated across one outer step. */
    std::vector<VolumeFluidCollider>           colliderPoseScratch;
    std::vector<VolumeFluidSdfPose>            sdfColliderPoseScratch;
    std::vector<VolumeFluidSdfPose>            heightFieldPoseScratch;
    std::vector<VolumeFluidAttachment>         attachments;
    std::vector<VolumeFluidStitch>             stitches;
    std::vector<VolumeFluidSimplex>            simplexes;
    std::vector<glm::mat3>                     colliderRotations;
    std::vector<glm::mat3>                     sdfColliderRotations;
    std::vector<glm::mat3>                     heightFieldColliderRotations;
    std::vector<VolumeFluidContact>            contacts;
    std::vector<VolumeFluidAttachmentReaction> attachmentReactions, attachmentReactionScratch;
    /** Reused transactional and thermal workspaces; no per-step ownership escapes. */
    std::vector<VolumeFluidParticle>    rollbackState;
    std::vector<VolumeFluidContact>     rollbackContacts;
    std::vector<VolumeFluidAttachment>  rollbackAttachments;
    std::vector<VolumeFluidThermalRule> thermalRulesScratch;
    std::vector<size_t>                 thermalContactParticles;
    std::vector<uint64_t>               thermalHitScratch;
    std::vector<glm::vec3> previous, delta, frictionDelta, velocity, curl, boundaryGradient, surfaceNormal, winds,
        windScratch, externalForces, externalForceScratch;
    /** Previous successful outer-step positions plus transactional candidate, parallel to state. */
    std::vector<glm::vec3> renderPrevious, renderPreviousScratch;
    std::vector<glm::vec4> colors, channels;
    /** Transient contact-grabber ownership and local coordinates, parallel to state. */
    std::vector<int64_t>   grabberLabels, grabberLabelScratch;
    std::vector<glm::vec3> grabberLocalPositions, grabberLocalScratch;
    std::vector<uint8_t>   grabberSelection;
    /** Capacity-reserved explicit debugger scratch; unused during normal stepping. */
    std::vector<size_t> debugCellIndices;
    std::vector<float>  lambda, viscousRates, diffusionRates, neighborWeights, densityRatio, supportRadii,
        supportRadiusSquared;
    std::vector<int>                      heads, next;
    std::vector<int>                      attachmentLabels;
    std::vector<uint8_t>                  attachmentOrientationConstraints;
    std::vector<glm::vec3>                stitchDelta, stitchStart;
    std::vector<float>                    stitchLambdas;
    std::vector<unsigned>                 stitchCounts;
    std::vector<unsigned>                 stitchTouched;
    std::vector<glm::vec3>                attachmentPairDelta, attachmentStressImpulse;
    std::vector<VolumeFluidParticleEvent> particleEvents;
    size_t                                particleEventCapacity = 0;
    uint64_t                              droppedParticleEvents = 0;

    explicit Impl(const VolumeFluidSettings& s)
        : settings(s),
          h(s.spacing * 2.f),
          volume(s.spacing * s.spacing * s.spacing),
          selfKernel(315.f / (64.f * 3.14159265359f * h * h * h)),
          dims(glm::ivec3(glm::ceil((s.maximum - s.minimum) / h)) + 1) {
        heads.resize(size_t(dims.x) * size_t(dims.y) * size_t(dims.z));
        state.reserve(s.capacity);
        rollbackState.reserve(s.capacity);
        renderPrevious.reserve(s.capacity);
        renderPreviousScratch.reserve(s.capacity);
        grabberLabels.reserve(s.capacity);
        grabberLabelScratch.reserve(s.capacity);
        grabberLocalPositions.reserve(s.capacity);
        grabberLocalScratch.reserve(s.capacity);
        grabberSelection.reserve(s.capacity);
        debugCellIndices.reserve(s.capacity);
        attachmentLabels.reserve(s.capacity);
        attachmentOrientationConstraints.reserve(s.capacity);
    }
    void recordParticleEvent(VolumeFluidParticleEventType type, unsigned particleIndex,
                             const VolumeFluidParticle& particle) {
        if (particleEventCapacity == 0) return;
        if (particleEvents.size() == particleEventCapacity) {
            ++droppedParticleEvents;
            return;
        }
        particleEvents.push_back({type, particleIndex, particle.actorGroup, particle});
    }
    float kernel(float r2) const {
        const float q = std::max(0.f, 1.f - r2 / (h * h));
        return selfKernel * q * q * q;
    }
    glm::vec3 gradient(glm::vec3 dx) const {
        const float r = glm::length(dx);
        if (r < 1e-7f || r >= h) return glm::vec3(0.f);
        const float q = 1.f - r / h;
        return dx * (-45.f * q * q / (3.14159265359f * h * h * h * h * r));
    }
    float support(const VolumeFluidMaterial& material) const { return settings.spacing * material.smoothing; }
    template <bool WithAttachments>
    float particleInverseWeight(size_t index) const {
        if (grabberLabels[index] != noGrabber) return 0.f;
        if constexpr (WithAttachments)
            if (attachmentLabels[index] >= 0) return 0.f;
        return inverseWeight(state[index].material);
    }
    bool hasColliderLabel(unsigned label) const {
        return std::any_of(colliders.begin(), colliders.end(), [&](const auto& item) { return item.label == label; }) ||
               std::any_of(sdfColliders.begin(), sdfColliders.end(),
                           [&](const auto& item) { return item.label == label; }) ||
               std::any_of(heightFieldColliders.begin(), heightFieldColliders.end(),
                           [&](const auto& item) { return item.label == label; });
    }
    void releaseMissingGrabbers() {
        for (auto& label : grabberLabels)
            if (label != noGrabber && !hasColliderLabel(unsigned(label))) label = noGrabber;
    }
    float materialKernel(float r2, float radius) const {
        const float q = std::max(0.f, 1.f - r2 / (radius * radius));
        return 315.f / (64.f * 3.14159265359f * radius * radius * radius) * q * q * q;
    }
    glm::vec3 materialGradient(glm::vec3 dx, float radius) const {
        const float distance = glm::length(dx);
        if (distance < 1e-7f || distance >= radius) return glm::vec3(0.f);
        const float q = 1.f - distance / radius;
        return dx * (-45.f * q * q / (3.14159265359f * radius * radius * radius * radius * distance));
    }
    glm::ivec3 cell(glm::vec3 p) const {
        return glm::clamp(glm::ivec3(glm::floor((p - settings.minimum) / h)), glm::ivec3(0), dims - 1);
    }
    size_t index(glm::ivec3 c) const {
        return size_t(c.x) + size_t(dims.x) * (size_t(c.y) + size_t(dims.y) * size_t(c.z));
    }
    void grid() {
        std::fill(heads.begin(), heads.end(), -1);
        next.resize(state.size());
        for (size_t i = 0; i < state.size(); ++i) {
            const auto c = index(cell(state[i].position));
            next[i]      = heads[c];
            heads[c]     = int(i);
        }
    }
    template <class F>
    void neighbors(size_t i, F&& fn) const {
        const auto position = state[i].position;
        const auto c        = cell(position);
        const int  x0 = std::max(0, c.x - 1), x1 = std::min(dims.x - 1, c.x + 1);
        const int  y0 = std::max(0, c.y - 1), y1 = std::min(dims.y - 1, c.y + 1);
        const int  z0 = std::max(0, c.z - 1), z1 = std::min(dims.z - 1, c.z + 1);
        // Clip the cell range once while retaining the original z/y/x and
        // linked-list traversal order for deterministic accumulation.
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y) {
                const size_t row = size_t(dims.x) * (size_t(y) + size_t(dims.y) * size_t(z));
                for (int x = x0; x <= x1; ++x) {
                    for (int j = heads[row + size_t(x)]; j >= 0; j = next[size_t(j)]) {
                        if (size_t(j) == i) continue;
                        const auto& a = state[i];
                        const auto& b = state[size_t(j)];
                        if (a.actorGroup == b.actorGroup) {
                            if (!a.selfCollide || !b.selfCollide) continue;
                        } else if (!filtersMatch(a.collisionFilter, b.collisionFilter))
                            continue;
                        const auto  dx = position - state[size_t(j)].position;
                        const float r2 = glm::dot(dx, dx);
                        if (r2 < h * h) fn(size_t(j), dx, r2);
                    }
                }
            }
    }
    template <bool NeedKernel = true, bool NeedGradient = true, class F>
    void fluidNeighbors(size_t i, int cellRadius, F&& fn) const {
        const auto  position = state[i].position;
        const auto  c        = cell(position);
        const int   x0 = std::max(0, c.x - cellRadius), x1 = std::min(dims.x - 1, c.x + cellRadius);
        const int   y0 = std::max(0, c.y - cellRadius), y1 = std::min(dims.y - 1, c.y + cellRadius);
        const int   z0 = std::max(0, c.z - cellRadius), z1 = std::min(dims.z - 1, c.z + cellRadius);
        const float radiusI = supportRadii[i], radiusSquaredI = supportRadiusSquared[i];
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y) {
                const size_t row = size_t(dims.x) * (size_t(y) + size_t(dims.y) * size_t(z));
                for (int x = x0; x <= x1; ++x)
                    for (int j = heads[row + size_t(x)]; j >= 0; j = next[size_t(j)]) {
                        if (size_t(j) == i) continue;
                        const auto& a = state[i];
                        const auto& b = state[size_t(j)];
                        if (a.actorGroup == b.actorGroup) {
                            if (!a.selfCollide || !b.selfCollide) continue;
                        } else if (!filtersMatch(a.collisionFilter, b.collisionFilter))
                            continue;
                        const auto  dx             = position - b.position;
                        const float r2             = glm::dot(dx, dx);
                        const float radiusJ        = supportRadii[size_t(j)];
                        const float radiusSquaredJ = supportRadiusSquared[size_t(j)];
                        if (r2 >= std::max(radiusSquaredI, radiusSquaredJ)) continue;
                        float     pairKernel = 0.f;
                        glm::vec3 pairGradient(0.f);
                        if (radiusI == radiusJ) {
                            if constexpr (NeedKernel)
                                pairKernel = radiusI == h ? kernel(r2) : materialKernel(r2, radiusI);
                            if constexpr (NeedGradient)
                                pairGradient = radiusI == h ? gradient(dx) : materialGradient(dx, radiusI);
                        } else {
                            if constexpr (NeedKernel)
                                pairKernel = .5f * (materialKernel(r2, radiusI) + materialKernel(r2, radiusJ));
                            if constexpr (NeedGradient)
                                pairGradient = .5f * (materialGradient(dx, radiusI) + materialGradient(dx, radiusJ));
                        }
                        fn(size_t(j), dx, r2, pairKernel, pairGradient);
                    }
            }
    }
    glm::vec3 constrain(glm::vec3 p) const {
        return glm::clamp(p, settings.minimum + settings.spacing * 0.5f, settings.maximum - settings.spacing * 0.5f);
    }
    float boundarySupport(glm::vec3 position, const VolumeFluidMaterial& material, float radius, glm::vec3& sum) const {
        sum                 = glm::vec3(0.f);
        const auto distance = glm::min(position - settings.minimum, settings.maximum - position);
        if (std::min({distance.x, distance.y, distance.z}) >= radius) return 0.f;
        const auto base       = glm::ivec3(glm::floor((position - settings.minimum) / settings.spacing));
        const int  cellRadius = int(std::ceil(material.smoothing));
        float      density    = 0.f;
        for (int z = base.z - cellRadius; z <= base.z + cellRadius; ++z) {
            const float pz = settings.minimum.z + (float(z) + .5f) * settings.spacing;
            const float dz = position.z - pz, z2 = dz * dz;
            if (z2 >= radius * radius) continue;
            const bool insideZ = pz >= settings.minimum.z && pz <= settings.maximum.z;
            for (int y = base.y - cellRadius; y <= base.y + cellRadius; ++y) {
                const float py = settings.minimum.y + (float(y) + .5f) * settings.spacing;
                const float dy = position.y - py, yz2 = dy * dy + z2;
                if (yz2 >= radius * radius) continue;
                const bool insideYZ = insideZ && py >= settings.minimum.y && py <= settings.maximum.y;
                for (int x = base.x - cellRadius; x <= base.x + cellRadius; ++x) {
                    const float px = settings.minimum.x + (float(x) + .5f) * settings.spacing;
                    if (insideYZ && px >= settings.minimum.x && px <= settings.maximum.x) continue;
                    const float dx = position.x - px, r2 = dx * dx + yz2;
                    if (r2 >= radius * radius) continue;
                    const auto offset = glm::vec3(dx, dy, dz);
                    density += volume * (radius == h ? kernel(r2) : materialKernel(r2, radius));
                    sum += volume * (radius == h ? gradient(offset) : materialGradient(offset, radius));
                }
            }
        }
        return density;
    }
    void obstacles(size_t i, float dt, std::vector<size_t>* contactParticles, bool reportTriggers) {
        auto& p = state[i];
        if (p.material.phase == VolumeFluidPhase::Solid) return;
        for (size_t colliderIndex = 0; colliderIndex < colliders.size(); ++colliderIndex) {
            const auto& c = colliders[colliderIndex];
            if (!filtersMatch(p.collisionFilter, c.collisionFilter)) continue;
            const auto& rotation = colliderRotations[colliderIndex];
            const auto  offset   = p.position - c.center;
            glm::vec3   normal(0.f, 1.f, 0.f);
            float       distance = 0.f;
            if (c.shape == VolumeFluidColliderShape::Sphere) {
                const float length = glm::length(offset);
                if (length > 1e-7f) normal = offset / length;
                distance = length - c.radius;
            } else if (c.shape == VolumeFluidColliderShape::Box) {
                const auto localOffset = glm::transpose(rotation) * offset;
                const auto q           = glm::abs(localOffset) - c.halfExtent;
                const auto outside     = glm::max(q, glm::vec3(0.f));
                distance               = glm::length(outside) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.f);
                if (glm::length(outside) > 1e-7f)
                    normal = glm::normalize(outside * glm::sign(localOffset));
                else {
                    int axis = q.x > q.y ? 0 : 1;
                    if (q.z > q[axis]) axis = 2;
                    normal       = glm::vec3(0.f);
                    normal[axis] = localOffset[axis] >= 0.f ? 1.f : -1.f;
                }
                normal = rotation * normal;
            } else {
                const auto  localOffset  = glm::transpose(rotation) * offset;
                const float halfSegment  = std::max(c.radius, c.halfExtent.y) - c.radius;
                const auto  centerLine   = glm::vec3(0.f, std::clamp(localOffset.y, -halfSegment, halfSegment), 0.f);
                const auto  centerOffset = localOffset - centerLine;
                const float length       = glm::length(centerOffset);
                if (length > 1e-7f)
                    normal = rotation * (centerOffset / length);
                else
                    normal = rotation * glm::vec3(1.f, 0.f, 0.f);
                distance = length - c.radius;
            }
            const auto  contactMaterial = combineContactMaterial(p.material, c);
            const bool  rolling   = p.material.phase == VolumeFluidPhase::Granular && contactMaterial.rollingContacts;
            const float clearance = rolling ? ellipsoidRadius(p, normal) : settings.spacing * 0.5f;
            if (distance > clearance + contactMaterial.stickDistance) continue;
            if (c.isTrigger) {
                if (reportTriggers)
                    contacts.push_back({c.label, p.position - normal * distance, normal, glm::vec3(0.f), unsigned(i),
                                        p.actorGroup, distance});
                continue;
            }
            const float penetration = std::max(clearance - distance, 0.f);
            const float adhesion    = contactAdhesion(distance, clearance, contactMaterial, dt, settings.iterations);
            const auto  correction  = normal * (penetration - adhesion);
            p.position += correction;
            const float mass            = volume * p.material.density;
            const auto  point           = p.position - normal * clearance;
            const auto  surfaceVelocity = c.velocity + glm::cross(c.angularVelocity, point - c.center);
            auto        relative        = (p.position - previous[i]) / dt - surfaceVelocity;
            if (rolling) relative += glm::cross(p.angularVelocity, -normal * clearance);
            const auto friction =
                contactFriction(relative, normal, contactMaterial, penetration + adhesion, dt, settings.iterations);
            p.position -= friction * dt;
            if (rolling && clearance > 1e-7f) {
                // Solid-sphere inertia gives 5/(2r) after mass cancels from the
                // tangential impulse. The contact radius follows the oriented ellipsoid.
                p.angularVelocity += (2.5f / clearance) * glm::cross(normal, friction);
                const auto  relativeAngular = p.angularVelocity - c.angularVelocity;
                const float angularSpeed    = glm::length(relativeAngular);
                const float maximumDamping =
                    contactMaterial.rollingFriction * glm::length(correction) / (dt * clearance);
                if (angularSpeed <= maximumDamping)
                    p.angularVelocity = c.angularVelocity;
                else if (angularSpeed > 1e-7f)
                    p.angularVelocity -= relativeAngular * (maximumDamping / angularSpeed);
                const float boundedSpeed = glm::length(p.angularVelocity);
                if (boundedSpeed > 1000.f) p.angularVelocity *= 1000.f / boundedSpeed;
            }
            contacts.push_back({c.label, point, normal, -mass * correction / dt + mass * friction, unsigned(i),
                                p.actorGroup, distance});
            if (contactParticles) contactParticles->push_back(i);
            if (c.solidify) {
                p.material.phase     = VolumeFluidPhase::Solid;
                p.velocity           = glm::vec3(0.f);
                p.angularVelocity    = glm::vec3(0.f);
                const auto colliderQ = glm::quat(c.rotation.w, c.rotation.x, c.rotation.y, c.rotation.z);
                const auto particleQ = particleQuaternion(p);
                const auto localQ    = glm::normalize(glm::conjugate(colliderQ) * particleQ);
                attachments.push_back({unsigned(i),
                                       c.label,
                                       glm::transpose(rotation) * (p.position - c.center),
                                       {localQ.x, localQ.y, localQ.z, localQ.w}});
                return;
            }
        }
    }
    void sdfObstacles(size_t i, float dt, std::vector<size_t>* contactParticles, bool reportTriggers) {
        auto& p = state[i];
        if (p.material.phase == VolumeFluidPhase::Solid) return;
        for (size_t colliderIndex = 0; colliderIndex < sdfColliders.size(); ++colliderIndex) {
            const auto& c = sdfColliders[colliderIndex];
            if (!filtersMatch(p.collisionFilter, c.collisionFilter)) continue;
            const auto inverse = glm::transpose(sdfColliderRotations[colliderIndex]);
            const auto local   = inverse * (p.position - c.position) / c.scale;
            const auto maximum = c.sdf.origin + glm::vec3(c.sdf.dims - glm::ivec3(1)) * c.sdf.cellSize;
            if (!c.inverted &&
                (glm::any(glm::lessThan(local, c.sdf.origin)) || glm::any(glm::greaterThan(local, maximum))))
                continue;
            const auto  sampled        = c.sdf.sampleWithGradient(local);
            const float gradientLength = glm::length(sampled.gradient);
            if (gradientLength < 1e-6f) continue;
            const float sign     = c.inverted ? -1.f : 1.f;
            const auto  normal   = sdfColliderRotations[colliderIndex] * (sampled.gradient * (sign / gradientLength));
            const float distance = sampled.distance * c.scale * sign;
            const auto  contactMaterial = combineContactMaterial(p.material, c);
            const bool  rolling   = p.material.phase == VolumeFluidPhase::Granular && contactMaterial.rollingContacts;
            const float clearance = rolling ? ellipsoidRadius(p, normal) : settings.spacing * .5f;
            if (distance > clearance + contactMaterial.stickDistance) continue;
            if (c.isTrigger) {
                if (reportTriggers)
                    contacts.push_back({c.label, p.position - normal * distance, normal, glm::vec3(0.f), unsigned(i),
                                        p.actorGroup, distance});
                continue;
            }
            const float penetration = std::max(clearance - distance, 0.f);
            const float adhesion    = contactAdhesion(distance, clearance, contactMaterial, dt, settings.iterations);
            const auto  correction  = normal * (penetration - adhesion);
            p.position += correction;
            const auto point           = p.position - normal * clearance;
            const auto surfaceVelocity = c.velocity + glm::cross(c.angularVelocity, point - c.position);
            auto       relative        = (p.position - previous[i]) / dt - surfaceVelocity;
            if (rolling) relative += glm::cross(p.angularVelocity, -normal * clearance);
            const auto friction =
                contactFriction(relative, normal, contactMaterial, penetration + adhesion, dt, settings.iterations);
            p.position -= friction * dt;
            if (rolling && clearance > 1e-7f) {
                p.angularVelocity += (2.5f / clearance) * glm::cross(normal, friction);
                const auto  relativeAngular = p.angularVelocity - c.angularVelocity;
                const float angularSpeed    = glm::length(relativeAngular);
                const float maximumDamping =
                    contactMaterial.rollingFriction * glm::length(correction) / (dt * clearance);
                if (angularSpeed <= maximumDamping)
                    p.angularVelocity = c.angularVelocity;
                else if (angularSpeed > 1e-7f)
                    p.angularVelocity -= relativeAngular * (maximumDamping / angularSpeed);
                const float bounded = glm::length(p.angularVelocity);
                if (bounded > 1000.f) p.angularVelocity *= 1000.f / bounded;
            }
            const float mass = volume * p.material.density;
            contacts.push_back({c.label, point, normal, -mass * correction / dt + mass * friction, unsigned(i),
                                p.actorGroup, distance});
            if (contactParticles) contactParticles->push_back(i);
        }
    }
    void heightFieldObstacles(size_t i, float dt, std::vector<size_t>* contactParticles, bool reportTriggers) {
        auto& p = state[i];
        if (p.material.phase == VolumeFluidPhase::Solid) return;
        for (size_t colliderIndex = 0; colliderIndex < heightFieldColliders.size(); ++colliderIndex) {
            const auto& c = heightFieldColliders[colliderIndex];
            if (!filtersMatch(p.collisionFilter, c.collisionFilter)) continue;
            const auto& rotation = heightFieldColliderRotations[colliderIndex];
            const auto  local    = glm::transpose(rotation) * (p.position - c.position);
            if (local.x < 0.f || local.z < 0.f || local.x > c.size.x || local.z > c.size.z) continue;
            const float gx = local.x * float(c.resolution.x - 1) / c.size.x;
            const float gz = local.z * float(c.resolution.y - 1) / c.size.z;
            const int   x  = std::min(int(std::floor(gx)), c.resolution.x - 2);
            const int   z  = std::min(int(std::floor(gz)), c.resolution.y - 2);
            const float u = gx - float(x), v = gz - float(z);
            const auto  sample = [&](int sx, int sz) {
                return c.heights[size_t(sz) * size_t(c.resolution.x) + size_t(sx)] * c.size.y;
            };
            const float h00 = sample(x, z), h10 = sample(x + 1, z);
            const float h01 = sample(x, z + 1), h11 = sample(x + 1, z + 1);
            const float dx = c.size.x / float(c.resolution.x - 1);
            const float dz = c.size.z / float(c.resolution.y - 1);
            float       height, dhdx, dhdz;
            if (u <= v) {
                height = h00 + (h01 - h00) * v + (h11 - h01) * u;
                dhdx   = (h11 - h01) / dx;
                dhdz   = (h01 - h00) / dz;
            } else {
                height = h00 + (h10 - h00) * u + (h11 - h10) * v;
                dhdx   = (h10 - h00) / dx;
                dhdz   = (h11 - h10) / dz;
            }
            const auto  localNormal     = glm::normalize(glm::vec3(-dhdx, 1.f, -dhdz));
            const auto  normal          = rotation * localNormal;
            const float distance        = (local.y - height) * localNormal.y;
            const auto  contactMaterial = combineContactMaterial(p.material, c);
            const bool  rolling   = p.material.phase == VolumeFluidPhase::Granular && contactMaterial.rollingContacts;
            const float clearance = rolling ? ellipsoidRadius(p, normal) : settings.spacing * .5f;
            if (distance > clearance + contactMaterial.stickDistance) continue;
            if (c.isTrigger) {
                if (reportTriggers)
                    contacts.push_back({c.label, p.position - normal * distance, normal, glm::vec3(0.f), unsigned(i),
                                        p.actorGroup, distance});
                continue;
            }
            const float penetration = std::max(clearance - distance, 0.f);
            const float adhesion    = contactAdhesion(distance, clearance, contactMaterial, dt, settings.iterations);
            const auto  correction  = normal * (penetration - adhesion);
            p.position += correction;
            const auto point           = p.position - normal * clearance;
            const auto surfaceVelocity = c.velocity + glm::cross(c.angularVelocity, point - c.position);
            auto       relative        = (p.position - previous[i]) / dt - surfaceVelocity;
            if (rolling) relative += glm::cross(p.angularVelocity, -normal * clearance);
            const auto friction =
                contactFriction(relative, normal, contactMaterial, penetration + adhesion, dt, settings.iterations);
            p.position -= friction * dt;
            if (rolling && clearance > 1e-7f) {
                p.angularVelocity += (2.5f / clearance) * glm::cross(normal, friction);
                const auto  relativeAngular = p.angularVelocity - c.angularVelocity;
                const float speed           = glm::length(relativeAngular);
                const float damping = contactMaterial.rollingFriction * glm::length(correction) / (dt * clearance);
                if (speed <= damping)
                    p.angularVelocity = c.angularVelocity;
                else if (speed > 1e-7f)
                    p.angularVelocity -= relativeAngular * (damping / speed);
            }
            const float mass = volume * p.material.density;
            contacts.push_back({c.label, point, normal, -mass * correction / dt + mass * friction, unsigned(i),
                                p.actorGroup, distance});
            if (contactParticles) contactParticles->push_back(i);
        }
    }
    Result<void> propagateSolidification() {
        if (attachments.empty() || attachments.size() == state.size()) return Result<void>::success();
        std::vector<std::pair<unsigned, size_t>> labels;
        for (size_t i = 0; i < colliders.size(); ++i)
            if (colliders[i].solidify) labels.emplace_back(colliders[i].label, i);
        if (labels.empty()) return Result<void>::success();
        std::sort(labels.begin(), labels.end());
        // Capture seeds once: one contact wave per outer step, independent of
        // solver iteration count and particle traversal order.
        std::vector<int> seed(state.size(), -1);
        for (const auto& a : attachments) {
            const auto found =
                std::lower_bound(labels.begin(), labels.end(), std::pair<unsigned, size_t>{a.colliderLabel, 0});
            if (found != labels.end() && found->first == a.colliderLabel) seed[a.particleIndex] = int(found->second);
        }
        std::vector<VolumeFluidAttachment> additions;
        const float                        contactDistance = settings.spacing * 1.001f;
        for (size_t i = 0; i < state.size(); ++i) {
            if (state[i].material.phase == VolumeFluidPhase::Solid || grabberLabels[i] != noGrabber) continue;
            int selected = -1;
            neighbors(i, [&](size_t j, glm::vec3, float distanceSquared) {
                if (seed[j] < 0 || distanceSquared > contactDistance * contactDistance) return;
                const int candidate = seed[j];
                if (selected < 0 || colliders[size_t(candidate)].label < colliders[size_t(selected)].label)
                    selected = candidate;
            });
            if (selected < 0) continue;
            const auto& collider = colliders[size_t(selected)];
            const auto  local =
                glm::transpose(colliderRotations[size_t(selected)]) * (state[i].position - collider.center);
            const auto colliderQ =
                glm::quat(collider.rotation.w, collider.rotation.x, collider.rotation.y, collider.rotation.z);
            const auto localQ = glm::normalize(glm::conjugate(colliderQ) * particleQuaternion(state[i]));
            if (!finite(local) || glm::any(glm::notEqual(constrain(state[i].position), state[i].position)))
                return invalid("Invalid propagated solid attachment");
            additions.push_back({unsigned(i), collider.label, local, {localQ.x, localQ.y, localQ.z, localQ.w}});
        }
        attachments.reserve(attachments.size() + additions.size());
        for (const auto& a : additions) {
            state[a.particleIndex].material.phase  = VolumeFluidPhase::Solid;
            state[a.particleIndex].velocity        = glm::vec3(0.f);
            state[a.particleIndex].angularVelocity = glm::vec3(0.f);
            attachments.push_back(a);
        }
        return Result<void>::success();
    }
    template <bool WithAttachments, bool WithExternalForces>
    void substep(float dt, std::vector<size_t>* contactParticles = nullptr) {
        const size_t n        = state.size();
        bool  needsAtmosphere = false, needsCurl = false, needsGranularRotation = false, needsGranularContacts = false;
        float maximumSmoothing = 2.f;
        supportRadii.resize(n);
        supportRadiusSquared.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const auto& particle = state[i];
            needsAtmosphere |= particle.material.drag != 0.f || particle.material.atmosphericPressure != 0.f;
            needsCurl |= particle.material.vorticity != 0.f;
            needsGranularRotation |= particle.material.phase == VolumeFluidPhase::Granular &&
                                     (particle.material.rollingContacts ||
                                      glm::dot(particle.angularVelocity, particle.angularVelocity) > 0.f);
            needsGranularContacts |= particle.material.phase == VolumeFluidPhase::Granular;
            maximumSmoothing        = std::max(maximumSmoothing, particle.material.smoothing);
            supportRadii[i]         = support(particle.material);
            supportRadiusSquared[i] = supportRadii[i] * supportRadii[i];
        }
        const int fluidCellRadius = int(std::ceil(maximumSmoothing * .5f));
        previous.resize(n);
        delta.resize(n);
        velocity.resize(n);
        lambda.resize(n);
        boundaryGradient.resize(n);
        if (needsAtmosphere) densityRatio.resize(n);
        for (size_t i = 0; i < n; ++i) {
            previous[i]        = state[i].position;
            bool fixedPosition = state[i].material.phase == VolumeFluidPhase::Solid || grabberLabels[i] != noGrabber;
            if constexpr (WithAttachments) fixedPosition = fixedPosition || attachmentLabels[i] >= 0;
            if (!fixedPosition) {
                state[i].velocity += settings.gravity * (dt * state[i].material.buoyancy);
                if constexpr (WithExternalForces)
                    state[i].velocity += externalForces[i] * (dt / (volume * state[i].material.density));
                state[i].position = constrain(previous[i] + state[i].velocity * dt);
            }
            bool fixedOrientation = state[i].material.phase == VolumeFluidPhase::Solid || grabberLabels[i] != noGrabber;
            if constexpr (WithAttachments)
                fixedOrientation = fixedOrientation || attachmentOrientationConstraints[i] != 0;
            if (!fixedOrientation && needsGranularRotation && state[i].material.phase == VolumeFluidPhase::Granular &&
                glm::dot(state[i].angularVelocity, state[i].angularVelocity) > 0.f) {
                auto            orientation = particleQuaternion(state[i]);
                const glm::quat spin(0.f, state[i].angularVelocity.x, state[i].angularVelocity.y,
                                     state[i].angularVelocity.z);
                orientation          = glm::normalize(orientation + .5f * dt * spin * orientation);
                state[i].orientation = {orientation.x, orientation.y, orientation.z, orientation.w};
            }
        }
        for (unsigned iteration = 0; iteration < settings.iterations; ++iteration) {
            grid();
            if (needsGranularContacts) frictionDelta.assign(n, glm::vec3(0.f));
            if constexpr (WithAttachments) attachmentPairDelta.assign(n, glm::vec3(0.f));
            for (size_t i = 0; i < n; ++i) {
                const auto& material = state[i].material;
                boundaryGradient[i]  = glm::vec3(0.f);
                if (!fluid(material)) {
                    lambda[i] = 0.f;
                    if (needsAtmosphere) densityRatio[i] = 0.f;
                    continue;
                }
                const float radius  = supportRadii[i];
                float       density = volume * materialKernel(0.f, radius), denom = 0.f;
                density += boundarySupport(state[i].position, material, radius, boundaryGradient[i]);
                glm::vec3 sum = boundaryGradient[i];
                fluidNeighbors(i, fluidCellRadius,
                               [&](size_t j, glm::vec3, float, float pairKernel, glm::vec3 pairGradient) {
                                   // Equal-resolution particles displace equal rest volumes;
                                   // density changes mass/inverse weight, not occupied volume.
                                   const float weight = volume;
                                   density += weight * pairKernel;
                                   const auto g = weight * pairGradient;
                                   sum += g;
                                   denom += particleInverseWeight<WithAttachments>(j) * glm::dot(g, g);
                               });
                if (needsAtmosphere) densityRatio[i] = density;
                // Inequality constraint prevents negative pressure at the free surface.
                lambda[i] = -std::max(0.f, density - 1.f) /
                            (denom + particleInverseWeight<WithAttachments>(i) * glm::dot(sum, sum) + 1e-5f);
            }
            float largestCorrection = 0.f;
            auto  projectParticle   = [&]<bool WithGranularFriction>(size_t i) {
                const auto& material   = state[i].material;
                const float wi         = particleInverseWeight<WithAttachments>(i);
                glm::vec3   correction = wi * lambda[i] * boundaryGradient[i];
                fluidNeighbors<false, true>(
                    i, fluidCellRadius, [&](size_t j, glm::vec3 dx, float r2, float, glm::vec3 pairGradient) {
                        const auto& other              = state[j].material;
                        const auto  pressureCorrection = wi * volume * (lambda[i] + lambda[j]) * pairGradient;
                        correction += pressureCorrection;
                        if constexpr (WithAttachments)
                            if (wi > 0.f && attachmentLabels[j] >= 0)
                                attachmentPairDelta[j] -= pressureCorrection * (volume * material.density);
                        // Keep fluid sampling points apart even when their density is
                        // below rest density. Coincident samples have zero kernel
                        // gradient and otherwise remain permanently welded together.
                        const float separation = settings.spacing * ((!fluid(material) || !fluid(other)) ? 1.f : .5f);
                        if (r2 < separation * separation && wi > 0.f) {
                            const float r = std::sqrt(r2);
                            glm::vec3   direction;
                            if (r > 1e-7f)
                                direction = dx / r;
                            else {
                                const size_t low = std::min(i, j), high = std::max(i, j);
                                const size_t axis    = (low * 73856093u + high * 19349663u) % 3u;
                                direction            = glm::vec3(0.f);
                                direction[int(axis)] = i < j ? -1.f : 1.f;
                            }
                            const auto separationCorrection =
                                direction * (separation - r) * wi / (wi + particleInverseWeight<WithAttachments>(j));
                            correction += separationCorrection;
                            if constexpr (WithAttachments)
                                if (attachmentLabels[j] >= 0)
                                    attachmentPairDelta[j] -= separationCorrection * (volume * material.density);
                            if constexpr (WithGranularFriction)
                                if (j > i && material.phase == VolumeFluidPhase::Granular &&
                                    other.phase == VolumeFluidPhase::Granular) {
                                    const float wj = particleInverseWeight<WithAttachments>(j), weightSum = wi + wj;
                                    if (weightSum > 0.f) {
                                        const float radiusI = ellipsoidRadius(state[i], direction);
                                        const float radiusJ = ellipsoidRadius(state[j], -direction);
                                        const auto  linearI = (state[i].position - previous[i]) / dt;
                                        const auto  linearJ = (state[j].position - previous[j]) / dt;
                                        const auto  contactI =
                                            linearI + glm::cross(state[i].angularVelocity, -direction * radiusI);
                                        const auto contactJ =
                                            linearJ + glm::cross(state[j].angularVelocity, direction * radiusJ);
                                        const auto  relative     = contactI - contactJ;
                                        const auto  tangent      = relative - direction * glm::dot(relative, direction);
                                        const float tangentSpeed = glm::length(tangent);
                                        const float overlap      = separation - r;
                                        const float staticCoefficient =
                                            .5f * (material.staticFriction + other.staticFriction);
                                        const float dynamicCoefficient =
                                            .5f * (material.dynamicFriction + other.dynamicFriction);
                                        const float desired = tangentSpeed * dt;
                                        const float limit   = desired <= staticCoefficient * overlap
                                                                  ? desired
                                                                  : dynamicCoefficient * overlap;
                                        if (tangentSpeed > 1e-7f && limit > 0.f) {
                                            const auto tangentialCorrection = tangent * (limit / tangentSpeed);
                                            frictionDelta[i] -= tangentialCorrection * (wi / weightSum);
                                            frictionDelta[j] += tangentialCorrection * (wj / weightSum);
                                            if (material.rollingContacts || other.rollingContacts) {
                                                const auto angularKick =
                                                    2.5f * glm::cross(direction, tangentialCorrection / dt);
                                                state[i].angularVelocity += angularKick * (wi / (weightSum * radiusI));
                                                state[j].angularVelocity += angularKick * (wj / (weightSum * radiusJ));
                                            }
                                        }
                                        if (material.rollingContacts || other.rollingContacts) {
                                            const auto relativeAngular =
                                                state[i].angularVelocity - state[j].angularVelocity;
                                            const float angularSpeed = glm::length(relativeAngular);
                                            const float rolling =
                                                .5f * (material.rollingFriction + other.rollingFriction);
                                            const float meanRadius     = .5f * (radiusI + radiusJ);
                                            const float maximumDamping = rolling * overlap / (dt * meanRadius);
                                            if (angularSpeed > 1e-7f && maximumDamping > 0.f) {
                                                const auto damping =
                                                    relativeAngular *
                                                    (std::min(angularSpeed, maximumDamping) / angularSpeed);
                                                state[i].angularVelocity -= damping * (wi / weightSum);
                                                state[j].angularVelocity += damping * (wj / weightSum);
                                            }
                                        }
                                    }
                                }
                        }
                    });
                const float length = glm::length(correction);
                largestCorrection  = std::max(largestCorrection, length);
                delta[i]           = correction;
            };
            if (needsGranularContacts)
                for (size_t i = 0; i < n; ++i) projectParticle.template operator()<true>(i);
            else
                for (size_t i = 0; i < n; ++i) projectParticle.template operator()<false>(i);
            // A common relaxation factor preserves mass-weighted pair balance;
            // clipping light and heavy particles independently does not.
            const float relaxation =
                largestCorrection > settings.spacing * .25f ? settings.spacing * .25f / largestCorrection : 1.f;
            if constexpr (WithAttachments)
                for (const auto& attachment : attachments)
                    attachmentStressImpulse[attachment.particleIndex] +=
                        attachmentPairDelta[attachment.particleIndex] * (relaxation / dt);
            if (needsGranularContacts)
                for (size_t i = 0; i < n; ++i) {
                    const bool attached = WithAttachments && attachmentLabels[i] >= 0;
                    if (state[i].material.phase != VolumeFluidPhase::Solid && grabberLabels[i] == noGrabber &&
                        !attached)
                        state[i].position = constrain(state[i].position + delta[i] * relaxation + frictionDelta[i]);
                    if (grabberLabels[i] == noGrabber && !attached) {
                        obstacles(i, dt, contactParticles, iteration == 0);
                        sdfObstacles(i, dt, contactParticles, iteration == 0);
                        heightFieldObstacles(i, dt, contactParticles, iteration == 0);
                    }
                }
            else
                for (size_t i = 0; i < n; ++i) {
                    const bool attached = WithAttachments && attachmentLabels[i] >= 0;
                    if (state[i].material.phase != VolumeFluidPhase::Solid && grabberLabels[i] == noGrabber &&
                        !attached)
                        state[i].position = constrain(state[i].position + delta[i] * relaxation);
                    if (grabberLabels[i] == noGrabber && !attached) {
                        obstacles(i, dt, contactParticles, iteration == 0);
                        sdfObstacles(i, dt, contactParticles, iteration == 0);
                        heightFieldObstacles(i, dt, contactParticles, iteration == 0);
                    }
                }
        }
        for (size_t i = 0; i < n; ++i) {
            // Contact projection can move a particle immediately before it
            // freezes. That correction is not a moving solid surface velocity.
            velocity[i] = state[i].material.phase == VolumeFluidPhase::Solid || grabberLabels[i] != noGrabber ||
                                  (WithAttachments && attachmentLabels[i] >= 0)
                              ? state[i].velocity
                              : (state[i].position - previous[i]) / dt;
        }
        grid();
        curl.assign(n, glm::vec3(0.f));
        if (needsAtmosphere) surfaceNormal.assign(n, glm::vec3(0.f));
        colors.resize(n);
        channels.resize(n);
        viscousRates.assign(n, 0.f);
        diffusionRates.assign(n, 0.f);
        neighborWeights.assign(n, 0.f);
        for (size_t i = 0; i < n; ++i) {
            colors[i]   = state[i].color;
            channels[i] = state[i].data;
            if (needsCurl)
                fluidNeighbors<false, true>(i, fluidCellRadius,
                                            [&](size_t j, glm::vec3, float, float, glm::vec3 pairGradient) {
                                                curl[i] += volume * glm::cross(velocity[j] - velocity[i], pairGradient);
                                            });
        }
        for (size_t i = 0; i < n; ++i) {
            const auto& material = state[i].material;
            if (material.phase == VolumeFluidPhase::Solid || grabberLabels[i] != noGrabber ||
                (WithAttachments && attachmentLabels[i] >= 0))
                continue;
            glm::vec3 mixing(0.f), cohesion(0.f), eta(0.f);
            float     weight = 0.f;
            fluidNeighbors(i, fluidCellRadius,
                           [&](size_t j, glm::vec3 dx, float r2, float pairKernel, glm::vec3 pairGradient) {
                               const float w = volume * pairKernel;
                               mixing += w * (velocity[j] - velocity[i]);
                               weight += w;
                               if (needsAtmosphere && (material.drag != 0.f || material.atmosphericPressure != 0.f) &&
                                   fluid(state[j].material))
                                   surfaceNormal[i] +=
                                       volume * pairGradient *
                                       (settings.spacing * (material.smoothing + state[j].material.smoothing) * .25f) /
                                       std::max(densityRatio[j], 1e-5f);
                               if (material.vorticity != 0.f)
                                   eta += volume * (glm::length(curl[j]) - glm::length(curl[i])) * pairGradient;
                               if (material.cohesion != 0.f && r2 > 1e-12f) {
                                   const auto pairCohesion = -dx * (w / std::sqrt(r2));
                                   cohesion += pairCohesion;
                                   if constexpr (WithAttachments)
                                       if (attachmentLabels[j] >= 0)
                                           attachmentStressImpulse[j] -=
                                               pairCohesion * (material.cohesion * dt * volume * material.density);
                               }
                           });
            const float visc   = material.viscosity + material.yield / std::max(0.01f, glm::length(mixing));
            viscousRates[i]    = 1.f - std::exp(-visc * dt);
            diffusionRates[i]  = 1.f - std::exp(-material.diffusion * dt);
            neighborWeights[i] = weight;
            state[i].velocity  = velocity[i] + cohesion * (material.cohesion * dt);
            if (material.drag != 0.f || material.atmosphericPressure != 0.f) {
                const float exposure = std::max(0.f, 1.f - densityRatio[i]);
                const auto  wind     = winds.empty() ? glm::vec3(0.f) : winds[i];
                state[i].velocity -= material.drag * (state[i].velocity - wind) * exposure * dt;
                state[i].velocity += material.atmosphericPressure * surfaceNormal[i] * dt;
            }
            if (glm::length(eta) > 1e-6f)
                state[i].velocity += material.vorticity * dt * glm::cross(glm::normalize(eta), curl[i]);
        }
        std::fill(delta.begin(), delta.end(), glm::vec3(0.f));
        for (size_t i = 0; i < n; ++i) {
            fluidNeighbors<true, false>(
                i, fluidCellRadius, [&](size_t j, glm::vec3, float, float pairKernel, glm::vec3) {
                    if (j <= i) return;
                    const float wi = particleInverseWeight<WithAttachments>(i);
                    const float wj = particleInverseWeight<WithAttachments>(j);
                    if (wi + wj == 0.f) return;
                    // Symmetric conductance and normalization conserve pair momentum
                    // even with unequal density, yield rate and neighbor counts.
                    // Row sums stay <= 1, so isolated viscosity cannot amplify energy.
                    const float rate = (viscousRates[i] + viscousRates[j]) * 0.5f;
                    const float amount =
                        rate * volume * pairKernel / std::max({1.f, neighborWeights[i], neighborWeights[j]});
                    const auto exchange = (velocity[j] - velocity[i]) * amount;
                    delta[i] += exchange * (wi / (wi + wj));
                    delta[j] -= exchange * (wj / (wi + wj));
                    if constexpr (WithAttachments) {
                        if (attachmentLabels[j] >= 0 && wi > 0.f)
                            attachmentStressImpulse[j] -=
                                exchange * (wi / (wi + wj) * volume * state[i].material.density);
                        if (attachmentLabels[i] >= 0 && wj > 0.f)
                            attachmentStressImpulse[i] +=
                                exchange * (wj / (wi + wj) * volume * state[j].material.density);
                    }
                    if (wi > 0.f && wj > 0.f && diffusionRates[i] + diffusionRates[j] > 0.f) {
                        // Equal-resolution particles carry equal rest volumes.
                        // Exchange concentration symmetrically; frozen solids do not
                        // act as an inexhaustible source of dye or user data.
                        const float diffusion = (diffusionRates[i] + diffusionRates[j]) * 0.5f * volume * pairKernel /
                                                std::max({1.f, neighborWeights[i], neighborWeights[j]});
                        const auto  colorExchange = (colors[j] - colors[i]) * diffusion;
                        const auto  dataExchange  = (channels[j] - channels[i]) * diffusion;
                        state[i].color += colorExchange;
                        state[j].color -= colorExchange;
                        state[i].data += dataExchange;
                        state[j].data -= dataExchange;
                    }
                });
        }
        for (size_t i = 0; i < n; ++i)
            if (grabberLabels[i] == noGrabber && (!WithAttachments || attachmentLabels[i] < 0))
                state[i].velocity += delta[i];
    }
};


}  // namespace eve::fluids
