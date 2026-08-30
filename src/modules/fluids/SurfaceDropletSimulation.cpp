#include "fluids/SurfaceDropletSimulation.h"

#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>

namespace eve::fluids {

SurfaceDropletSimulation::SurfaceDropletSimulation(FluidSurfaceBinding* binding,
                                                   const SurfaceDropletParams& params,
                                                   SurfaceWetnessField* wetness)
    : binding_(binding), params_(params), wetness_(wetness) {}

float SurfaceDropletSimulation::dropletRadius(float volume) const {
    const float theta = glm::radians(std::clamp(params_.contactAngleDegrees, 5.f, 175.f));
    const float cosine = std::cos(theta);
    const float sine = std::max(1e-3f, std::sin(theta));
    constexpr float pi = 3.14159265358979323846f;
    const float factor = pi * (2.f - 3.f * cosine + cosine * cosine * cosine) /
                         (3.f * sine * sine * sine);
    return std::cbrt(std::max(0.f, volume) / std::max(1e-6f, factor));
}

bool SurfaceDropletSimulation::addDroplet(const SurfaceLocation& location, float volume,
                                          const glm::vec3& relativeVelocity) {
    if (!binding_ || !binding_->isValid() || location.triangle >= uint32_t(binding_->triangleCount()) ||
        volume <= 0.f)
        return false;
    SurfaceDroplet droplet;
    droplet.id               = nextDropletId_++;
    droplet.location         = location;
    droplet.volume           = volume;
    const SurfaceSample sample = binding_->evaluate(location, 0.f);
    droplet.relativeVelocity = relativeVelocity - sample.normal * glm::dot(relativeVelocity, sample.normal);
    droplets_.push_back(droplet);
    return true;
}

void SurfaceDropletSimulation::step(float dt) {
    detached_.clear();
    if (!binding_ || !binding_->isValid() || dt <= 0.f || (droplets_.empty() && airborne_.empty())) return;
    float remaining = dt;
    while (remaining > 1e-7f) {
        const float substep = std::min(remaining, 0.05f);
        stepSubstep(substep, dt);
        remaining -= substep;
    }
}

void SurfaceDropletSimulation::stepSubstep(float dt, float poseDt) {

    std::vector<SurfaceDroplet> attached;
    attached.reserve(droplets_.size());
    for (SurfaceDroplet droplet : droplets_) {
        const SurfaceLocation previousLocation = droplet.location;
        const SurfaceSample previousSample = binding_->evaluate(previousLocation, poseDt);
        const SurfaceSample sample = binding_->evaluate(droplet.location, poseDt);
        glm::vec3 surfaceAcceleration(0.f);
        if (droplet.hasPreviousSurfaceVelocity)
            surfaceAcceleration = (sample.velocity - droplet.previousSurfaceVelocity) / poseDt;

        const glm::vec3 relativeAcceleration = params_.gravity - surfaceAcceleration;
        const float outwardAcceleration = glm::dot(relativeAcceleration, sample.normal);
        if (outwardAcceleration > std::max(0.f, params_.adhesionAcceleration)) {
            detached_.push_back(
                {sample.position, sample.velocity + droplet.relativeVelocity, droplet.volume});
            airborne_.push_back(
                {sample.position, sample.velocity + droplet.relativeVelocity, droplet.volume});
            continue;
        }

        glm::vec3 tangentAcceleration =
            relativeAcceleration - sample.normal * glm::dot(relativeAcceleration, sample.normal);
        droplet.relativeVelocity += tangentAcceleration * dt;
        droplet.relativeVelocity *= std::exp(-std::max(0.f, params_.friction) * dt);
        const float speed2 = glm::length2(droplet.relativeVelocity);
        const float maxSpeed = std::max(0.f, params_.maxSpeed);
        if (maxSpeed > 0.f && speed2 > maxSpeed * maxSpeed)
            droplet.relativeVelocity *= maxSpeed / std::sqrt(speed2);

        const SurfaceWalkResult walk = binding_->walkAcrossSurface(
            droplet.location, droplet.relativeVelocity * dt, std::max(0, params_.maxCrossings));
        if (!walk.valid) continue;
        droplet.location = walk.location;
        if (walk.reachedBoundary) {
            const SurfaceSample edge = binding_->evaluate(walk.location, poseDt);
            detached_.push_back({edge.position, edge.velocity + droplet.relativeVelocity, droplet.volume});
            airborne_.push_back({edge.position, edge.velocity + droplet.relativeVelocity, droplet.volume, 0.f});
            continue;
        }

        const SurfaceSample end = binding_->evaluate(droplet.location, poseDt);
        droplet.relativeVelocity -= end.normal * glm::dot(droplet.relativeVelocity, end.normal);
        droplet.previousSurfaceVelocity = end.velocity;
        droplet.hasPreviousSurfaceVelocity = true;
        if (wetness_) {
            const float distance = glm::distance(previousSample.position, end.position);
            wetness_->deposit(previousLocation, distance * params_.trailDeposition * droplet.volume);
            wetness_->deposit(droplet.location, distance * params_.trailDeposition * droplet.volume);
        }
        attached.push_back(droplet);
    }
    droplets_.swap(attached);

    // Merge overlapping spherical caps without depending on mesh UV seams.
    for (size_t i = 0; i < droplets_.size(); ++i) {
        const SurfaceSample a = binding_->evaluate(droplets_[i].location, poseDt);
        for (size_t j = i + 1u; j < droplets_.size();) {
            const SurfaceSample b = binding_->evaluate(droplets_[j].location, poseDt);
            const float mergeDistance = params_.mergeRadiusScale *
                (dropletRadius(droplets_[i].volume) + dropletRadius(droplets_[j].volume));
            if (glm::distance2(a.position, b.position) > mergeDistance * mergeDistance) {
                ++j;
                continue;
            }
            const float combined = droplets_[i].volume + droplets_[j].volume;
            droplets_[i].relativeVelocity =
                (droplets_[i].relativeVelocity * droplets_[i].volume +
                 droplets_[j].relativeVelocity * droplets_[j].volume) / combined;
            droplets_[i].volume = combined;
            droplets_.erase(droplets_.begin() + std::ptrdiff_t(j));
        }
    }

    std::vector<AirborneDroplet> flying;
    flying.reserve(airborne_.size());
    for (AirborneDroplet drop : airborne_) {
        drop.age += dt;
        drop.velocity += params_.gravity * dt;
        drop.velocity *= std::exp(-std::max(0.f, params_.airDrag) * dt);
        drop.position += drop.velocity * dt;
        SurfaceLocation hit;
        if (drop.age > 0.08f &&
            binding_->project(drop.position, std::max(0.f, params_.reattachDistance), hit)) {
            const SurfaceSample surface = binding_->evaluate(hit, poseDt);
            if (glm::dot(drop.velocity - surface.velocity, surface.normal) <= 0.f) {
                addDroplet(hit, drop.volume, drop.velocity - surface.velocity);
                if (wetness_) wetness_->deposit(hit, drop.volume * 0.15f);
                continue;
            }
        }
        flying.push_back(drop);
    }
    airborne_.swap(flying);
}

void SurfaceDropletSimulation::clear() {
    droplets_.clear();
    detached_.clear();
    airborne_.clear();
    nextDropletId_ = 1;
}

}  // namespace eve::fluids
