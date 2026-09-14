#include "fluids/VolumeFluidInternal.inc"

namespace eve::fluids {
VolumeFluid::VolumeFluid(const VolumeFluidSettings& settings) : impl_(std::make_unique<Impl>(settings)) {}
VolumeFluid::~VolumeFluid() = default;

Result<std::unique_ptr<VolumeFluid>> VolumeFluid::create(const VolumeFluidSettings& s) {
    const auto extent = s.maximum - s.minimum;
    const bool valid  = s.capacity > 0 && s.capacity <= 1000000 && std::isfinite(s.spacing) && s.spacing >= 0.001f &&
                        s.spacing <= 10.f && finite(s.gravity) && glm::length(s.gravity) <= 1000.f &&
                        finite(s.minimum) && finite(s.maximum) &&
                        glm::all(glm::greaterThan(extent, glm::vec3(s.spacing * 2.f))) && s.iterations > 0 &&
                        s.iterations <= 20;
    if (!valid)
        return Result<std::unique_ptr<VolumeFluid>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid volume-fluid settings", "fluids.volume.settings"));
    const auto cells = glm::ceil(glm::dvec3(extent) / double(2.f * s.spacing)) + 1.0;
    if (cells.x * cells.y * cells.z > 2000000.0)
        return Result<std::unique_ptr<VolumeFluid>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Volume-fluid grid exceeds 2000000 cells", "fluids.volume.bounds"));
    return Result<std::unique_ptr<VolumeFluid>>::success(std::unique_ptr<VolumeFluid>(new VolumeFluid(s)));
}

Result<void> VolumeFluid::emit(std::span<const VolumeFluidParticle> input) {
    if (input.size() > impl_->settings.capacity - impl_->state.size()) return invalid("Particle capacity exceeded");
    if (!impl_->colliders.empty() && uint64_t(impl_->state.size() + input.size()) * impl_->colliders.size() > 4000000)
        return invalid("Analytic collider candidate budget exceeded");
    if (!impl_->sdfColliders.empty() &&
        uint64_t(impl_->state.size() + input.size()) * impl_->sdfColliders.size() > 4000000)
        return invalid("SDF collider candidate budget exceeded");
    if (!impl_->heightFieldColliders.empty() &&
        uint64_t(impl_->state.size() + input.size()) * impl_->heightFieldColliders.size() > 4000000)
        return invalid("Height-field collider candidate budget exceeded");
    for (const auto& p : input) {
        if (!finite(p.position) || !finite(p.velocity) || !finite(p.angularVelocity) || !finite(p.color) ||
            !finite(p.data) || !validMaterial(p.material) || !validParticleShape(p) || !range(p.life, 0.f, 86400.f) ||
            !validFilter(p.collisionFilter) || p.actorGroup > 0x00ffffffu || glm::length(p.velocity) > 100.f ||
            glm::length(p.angularVelocity) > 1000.f ||
            glm::any(glm::notEqual(impl_->constrain(p.position), p.position)))
            return invalid("Invalid emitted particle");
    }
    const size_t first = impl_->state.size();
    impl_->state.insert(impl_->state.end(), input.begin(), input.end());
    for (size_t i = first; i < impl_->state.size(); ++i) {
        auto& particle = impl_->state[i];
        if (glm::all(glm::equal(particle.radii, glm::vec3(0.f))))
            particle.radii = glm::vec3(impl_->settings.spacing * .5f);
        particle.orientation = glm::normalize(particle.orientation);
        impl_->renderPrevious.push_back(particle.position);
        impl_->grabberLabels.push_back(Impl::noGrabber);
        impl_->grabberLocalPositions.push_back(glm::vec3(0.f));
        impl_->recordParticleEvent(VolumeFluidParticleEventType::Emitted, unsigned(i), particle);
    }
    impl_->winds.clear();
    impl_->externalForces.clear();
    return Result<void>::success();
}

Result<void> VolumeFluid::configureParticleEvents(unsigned capacity) {
    if (capacity > 65536) return invalid("Particle event capacity exceeds 65536");
    std::vector<VolumeFluidParticleEvent> replacement;
    replacement.reserve(capacity);
    impl_->particleEvents.swap(replacement);
    impl_->particleEventCapacity = capacity;
    impl_->droppedParticleEvents = 0;
    return Result<void>::success();
}

VolumeFluidParticleEventBatch VolumeFluid::drainParticleEvents() {
    VolumeFluidParticleEventBatch batch;
    batch.events  = impl_->particleEvents;
    batch.dropped = impl_->droppedParticleEvents;
    impl_->particleEvents.clear();
    impl_->droppedParticleEvents = 0;
    return batch;
}

Result<void> VolumeFluid::step(float seconds, unsigned substeps) {
    return stepWithThermalContacts(seconds, substeps, {});
}

Result<void> VolumeFluid::setParticleWinds(std::span<const glm::vec3> winds) {
    if (winds.empty()) {
        impl_->winds.clear();
        return Result<void>::success();
    }
    if (winds.size() != impl_->state.size() || std::any_of(winds.begin(), winds.end(), [](glm::vec3 wind) {
            return !finite(wind) || glm::length(wind) > 1000.f;
        }))
        return invalid("Invalid per-particle wind count or velocity");
    impl_->winds.assign(winds.begin(), winds.end());
    return Result<void>::success();
}

Result<void> VolumeFluid::setParticleExternalForces(std::span<const glm::vec3> forces) {
    if (forces.empty()) {
        impl_->externalForces.clear();
        return Result<void>::success();
    }
    if (forces.size() != impl_->state.size() || std::any_of(forces.begin(), forces.end(), [](glm::vec3 force) {
            return !finite(force) || glm::length(force) > 1000.f;
        }))
        return invalid("Invalid per-particle external force count or magnitude");
    impl_->externalForces.assign(forces.begin(), forces.end());
    return Result<void>::success();
}

Result<void> VolumeFluid::accumulateWindZones(std::span<const VolumeFluidWindZone> zones, float fixedTimeSeconds) {
    return accumulateForceZones(zones, fixedTimeSeconds, true);
}

Result<void> VolumeFluid::accumulateExternalForceZones(std::span<const VolumeFluidWindZone> zones,
                                                       float                                fixedTimeSeconds) {
    return accumulateForceZones(zones, fixedTimeSeconds, false);
}

Result<void> VolumeFluid::accumulateForceZones(std::span<const VolumeFluidWindZone> zones, float fixedTimeSeconds,
                                               bool asWind) {
    constexpr size_t kMaxZones        = 64;
    constexpr size_t kCandidateBudget = 4000000;
    if (zones.size() > kMaxZones || !std::isfinite(fixedTimeSeconds) || fixedTimeSeconds < 0.f ||
        fixedTimeSeconds > 1e9f)
        return invalid("Invalid wind-zone count or fixed time");
    if (zones.empty() || impl_->state.empty()) return Result<void>::success();

    size_t             sphericalCount = 0;
    glm::vec3          ambient(0.f);
    std::vector<float> resolvedIntensity;
    resolvedIntensity.reserve(zones.size());
    for (const auto& zone : zones) {
        const bool validType =
            zone.type == VolumeFluidWindZoneType::Ambient || zone.type == VolumeFluidWindZoneType::Spherical;
        if (!validType || !finite(zone.center) || !finite(zone.direction) || glm::length(zone.direction) > 1000.f ||
            !range(zone.intensity, -1000.f, 1000.f) || !range(zone.turbulence, -1000.f, 1000.f) ||
            !range(zone.turbulenceFrequency, 0.f, 1000.f) || !range(zone.turbulenceSeed, -1000000.f, 1000000.f) ||
            (zone.type == VolumeFluidWindZoneType::Spherical && !range(zone.radius, .001f, 10000.f)))
            return invalid("Invalid wind-zone field");
        const float noise = std::clamp(
            .5f + .5f * glm::perlin(glm::vec2(fixedTimeSeconds * zone.turbulenceFrequency, zone.turbulenceSeed)), 0.f,
            1.f);
        const float intensity = zone.intensity + noise * zone.turbulence;
        resolvedIntensity.push_back(intensity);
        if (zone.type == VolumeFluidWindZoneType::Ambient)
            ambient += zone.direction * intensity;
        else
            ++sphericalCount;
    }
    if (sphericalCount != 0 && impl_->state.size() > kCandidateBudget / sphericalCount)
        return invalid("Wind-zone candidate budget exceeded");

    auto& current   = asWind ? impl_->winds : impl_->externalForces;
    auto& candidate = asWind ? impl_->windScratch : impl_->externalForceScratch;
    if (current.empty())
        candidate.assign(impl_->state.size(), glm::vec3(0.f));
    else
        candidate.assign(current.begin(), current.end());
    for (size_t particleIndex = 0; particleIndex < impl_->state.size(); ++particleIndex) {
        auto force = candidate[particleIndex] + ambient;
        for (size_t zoneIndex = 0; zoneIndex < zones.size(); ++zoneIndex) {
            const auto& zone = zones[zoneIndex];
            if (zone.type != VolumeFluidWindZoneType::Spherical) continue;
            const auto  distance        = impl_->state[particleIndex].position - zone.center;
            const float squaredDistance = glm::dot(distance, distance);
            const float squaredRadius   = zone.radius * zone.radius;
            if (squaredDistance >= squaredRadius) continue;
            const float falloff = std::clamp((squaredRadius - squaredDistance) / squaredRadius, 0.f, 1.f);
            if (zone.radial) {
                force += distance / (std::sqrt(squaredDistance) + std::numeric_limits<float>::epsilon()) * falloff *
                         resolvedIntensity[zoneIndex];
            } else {
                force += zone.direction * falloff * resolvedIntensity[zoneIndex];
            }
        }
        if (!finite(force) || glm::length(force) > 1000.f)
            return invalid(asWind ? "Accumulated wind-zone velocity exceeds limit"
                                  : "Accumulated external force exceeds limit");
        candidate[particleIndex] = force;
    }
    current.swap(candidate);
    return Result<void>::success();
}

Result<void> VolumeFluid::stepWithThermalContacts(float seconds, unsigned substeps,
                                                  std::span<const VolumeFluidThermalRule> rules) {
    EV_PROFILE_MODULE("fluids", "VolumeFluid::step");
    if (!std::isfinite(seconds) || seconds <= 0.f || seconds > 1.f / 30.f || substeps == 0 || substeps > 32 ||
        seconds / float(substeps) > 1.f / 120.f)
        return invalid("Invalid timestep or substep count");
    if (rules.size() > 1024) return invalid("Too many thermal rules");
    auto& ordered = impl_->thermalRulesScratch;
    ordered.assign(rules.begin(), rules.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.colliderLabel < b.colliderLabel; });
    for (size_t i = 0; i < ordered.size(); ++i) {
        const auto& rule = ordered[i];
        if (!range(rule.rate, -1000000.f, 1000000.f) || !range(rule.minimumViscosity, 0.f, 100.f) ||
            !range(rule.maximumViscosity, rule.minimumViscosity, 100.f) || !range(rule.minimumCohesion, 0.f, 10.f) ||
            !range(rule.maximumCohesion, rule.minimumCohesion, 10.f) ||
            (i > 0 && ordered[i - 1].colliderLabel == rule.colliderLabel) ||
            std::none_of(impl_->colliders.begin(), impl_->colliders.end(),
                         [&](const auto& c) { return c.label == rule.colliderLabel; }))
            return invalid("Invalid thermal rule or collider label");
    }
    auto& contactParticles = impl_->thermalContactParticles;
    contactParticles.clear();
    struct Motion {
        size_t    particle;
        glm::vec3 start, target, velocity;
        glm::quat startOrientation, targetOrientation;
        glm::vec3 localPosition;
        glm::quat localOrientation;
        unsigned  colliderLabel;
        size_t    colliderIndex;
        glm::vec3 inertialReaction;
        glm::vec3 angularVelocity, angularReaction;
        bool      constrainOrientation;
        bool      dynamic;
        float     compliance, breakThreshold;
        glm::vec3 constraintReaction{0.f};
        glm::vec3 dynamicAngularReaction{0.f};
        bool      broken = false;
    };
    std::vector<Motion> motions;
    if (!impl_->attachments.empty()) {
        std::vector<std::pair<unsigned, size_t>> labels;
        labels.reserve(impl_->colliders.size());
        for (size_t i = 0; i < impl_->colliders.size(); ++i) labels.emplace_back(impl_->colliders[i].label, i);
        std::sort(labels.begin(), labels.end());
        motions.reserve(impl_->attachments.size());
        for (const auto& attachment : impl_->attachments) {
            const auto found = std::lower_bound(labels.begin(), labels.end(),
                                                std::pair<unsigned, size_t>{attachment.colliderLabel, 0});
            if (found == labels.end() || found->first != attachment.colliderLabel)
                return invalid("Missing solid attachment collider");
            const auto  target   = impl_->colliders[found->second].center +
                                   impl_->colliderRotations[found->second] * attachment.localPosition;
            const auto  start    = impl_->state[attachment.particleIndex].position;
            const auto  velocity = (target - start) / seconds;
            const auto& collider = impl_->colliders[found->second];
            const auto  colliderQ =
                glm::quat(collider.rotation.w, collider.rotation.x, collider.rotation.y, collider.rotation.z);
            const auto localQ  = glm::quat(attachment.localOrientation.w, attachment.localOrientation.x,
                                           attachment.localOrientation.y, attachment.localOrientation.z);
            const auto startQ  = particleQuaternion(impl_->state[attachment.particleIndex]);
            const auto targetQ = glm::normalize(colliderQ * localQ);
            if (!finite(target) || !finite(velocity) || glm::length(velocity) > 100.f ||
                glm::any(glm::notEqual(impl_->constrain(target), target)))
                return invalid("Solid attachment motion exceeds bounds or speed");
            const auto& particle = impl_->state[attachment.particleIndex];
            const float mass     = impl_->volume * particle.material.density;
            const auto  unconstrainedVelocity =
                particle.velocity + impl_->settings.gravity * particle.material.buoyancy * seconds;
            const auto      targetAngular = collider.angularVelocity;
            const auto      radii         = particle.radii;
            const glm::vec3 principal =
                mass * .2f *
                glm::vec3(radii.y * radii.y + radii.z * radii.z, radii.x * radii.x + radii.z * radii.z,
                          radii.x * radii.x + radii.y * radii.y);
            const auto orientation = glm::mat3_cast(startQ);
            const auto angularReaction =
                -orientation * glm::mat3(principal.x, 0.f, 0.f, 0.f, principal.y, 0.f, 0.f, 0.f, principal.z) *
                glm::transpose(orientation) * (targetAngular - particle.angularVelocity);
            motions.push_back(
                {attachment.particleIndex, start, target, velocity, startQ, targetQ, attachment.localPosition, localQ,
                 attachment.colliderLabel, found->second,
                 attachment.dynamic ? glm::vec3(0.f) : -mass * (velocity - unconstrainedVelocity),
                 attachment.constrainOrientation ? targetAngular : particle.angularVelocity,
                 !attachment.dynamic && attachment.constrainOrientation ? angularReaction : glm::vec3(0.f),
                 attachment.constrainOrientation, attachment.dynamic, attachment.compliance,
                 attachment.breakThreshold});
        }
    }
    auto& colliderEnds    = impl_->colliderPoseScratch;
    auto& sdfColliderEnds = impl_->sdfColliderPoseScratch;
    auto& heightFieldEnds = impl_->heightFieldPoseScratch;
    colliderEnds          = impl_->colliders;
    sdfColliderEnds.resize(impl_->sdfColliders.size());
    for (size_t i = 0; i < impl_->sdfColliders.size(); ++i) {
        const auto& collider = impl_->sdfColliders[i];
        sdfColliderEnds[i]   = {collider.label, collider.position, collider.rotation,
                                collider.scale, collider.velocity, collider.angularVelocity};
    }
    heightFieldEnds.resize(impl_->heightFieldColliders.size());
    for (size_t i = 0; i < impl_->heightFieldColliders.size(); ++i) {
        const auto& collider = impl_->heightFieldColliders[i];
        heightFieldEnds[i]   = {collider.label,    collider.position,       collider.rotation, 1.f,
                                collider.velocity, collider.angularVelocity};
    }
    auto interpolatedRotation = [](glm::vec4 rotation, glm::vec3 angularVelocity, float seconds, float fraction) {
        const auto  end   = glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
        const float speed = glm::length(angularVelocity);
        if (speed <= 1e-7f) return end;
        return glm::normalize(glm::angleAxis(-speed * seconds * (1.f - fraction), angularVelocity / speed) * end);
    };
    for (size_t i = 0; i < sdfColliderEnds.size(); ++i)
        if (impl_->sdfColliders[i].inverted) {
            const auto& pose = sdfColliderEnds[i];
            const auto& sdf  = impl_->sdfColliders[i].sdf;
            const auto  startRotation =
                glm::mat3_cast(interpolatedRotation(pose.rotation, pose.angularVelocity, seconds, 0.f));
            const auto startPosition = pose.position - pose.velocity * seconds;
            const auto minimum       = sdf.origin;
            const auto maximum       = minimum + glm::vec3(sdf.dims - glm::ivec3(1)) * sdf.cellSize;
            for (unsigned corner = 0; corner < 8; ++corner) {
                const glm::vec3 world{corner & 1 ? impl_->settings.maximum.x : impl_->settings.minimum.x,
                                      corner & 2 ? impl_->settings.maximum.y : impl_->settings.minimum.y,
                                      corner & 4 ? impl_->settings.maximum.z : impl_->settings.minimum.z};
                const auto      local = glm::transpose(startRotation) * (world - startPosition) / pose.scale;
                if (glm::any(glm::lessThan(local, minimum)) || glm::any(glm::greaterThan(local, maximum)))
                    return invalid("Moving inverted SDF domain must contain solver bounds for the complete step");
            }
        }
    const auto restoreColliderEnds = [&]() {
        impl_->colliders.swap(colliderEnds);
        for (size_t i = 0; i < sdfColliderEnds.size(); ++i) {
            impl_->sdfColliders[i].position = sdfColliderEnds[i].position;
            impl_->sdfColliders[i].rotation = sdfColliderEnds[i].rotation;
        }
        for (size_t i = 0; i < heightFieldEnds.size(); ++i) {
            impl_->heightFieldColliders[i].position = heightFieldEnds[i].position;
            impl_->heightFieldColliders[i].rotation = heightFieldEnds[i].rotation;
        }
        impl_->colliderRotations.resize(impl_->colliders.size());
        for (size_t i = 0; i < impl_->colliders.size(); ++i) {
            const auto& r               = impl_->colliders[i].rotation;
            impl_->colliderRotations[i] = glm::mat3_cast(glm::normalize(glm::quat(r.w, r.x, r.y, r.z)));
        }
        impl_->sdfColliderRotations.resize(impl_->sdfColliders.size());
        for (size_t i = 0; i < impl_->sdfColliders.size(); ++i) {
            const auto& r                  = impl_->sdfColliders[i].rotation;
            impl_->sdfColliderRotations[i] = glm::mat3_cast(glm::normalize(glm::quat(r.w, r.x, r.y, r.z)));
        }
        impl_->heightFieldColliderRotations.resize(impl_->heightFieldColliders.size());
        for (size_t i = 0; i < impl_->heightFieldColliders.size(); ++i) {
            const auto& r                          = impl_->heightFieldColliders[i].rotation;
            impl_->heightFieldColliderRotations[i] = glm::mat3_cast(glm::normalize(glm::quat(r.w, r.x, r.y, r.z)));
        }
    };
    auto& original = impl_->rollbackState;
    original       = impl_->state;
    impl_->renderPreviousScratch.resize(impl_->state.size());
    for (size_t i = 0; i < impl_->state.size(); ++i) impl_->renderPreviousScratch[i] = impl_->state[i].position;
    auto& originalContacts    = impl_->rollbackContacts;
    originalContacts          = impl_->contacts;
    auto& originalAttachments = impl_->rollbackAttachments;
    originalAttachments       = impl_->attachments;
    impl_->attachmentLabels.clear();
    impl_->attachmentOrientationConstraints.clear();
    impl_->attachmentStressImpulse.clear();
    if (!motions.empty()) {
        impl_->attachmentLabels.assign(impl_->state.size(), -1);
        impl_->attachmentOrientationConstraints.assign(impl_->state.size(), uint8_t(0));
        impl_->attachmentStressImpulse.assign(impl_->state.size(), glm::vec3(0.f));
        for (const auto& motion : motions) {
            if (motion.dynamic) continue;
            impl_->attachmentLabels[motion.particle]                 = int(motion.colliderLabel);
            impl_->attachmentOrientationConstraints[motion.particle] = motion.constrainOrientation ? 1 : 0;
        }
    }
    impl_->contacts.clear();
    for (auto& p : impl_->state)
        if (p.material.phase == VolumeFluidPhase::Solid) {
            p.velocity        = glm::vec3(0.f);
            p.angularVelocity = glm::vec3(0.f);
        }
    for (unsigned k = 0; k < substeps; ++k) {
        const float fraction = float(k + 1) / float(substeps);
        for (size_t i = 0; i < colliderEnds.size(); ++i) {
            const auto& end             = colliderEnds[i];
            auto&       sample          = impl_->colliders[i];
            sample.center               = end.center - end.velocity * (seconds * (1.f - fraction));
            const auto q                = interpolatedRotation(end.rotation, end.angularVelocity, seconds, fraction);
            sample.rotation             = {q.x, q.y, q.z, q.w};
            impl_->colliderRotations[i] = glm::mat3_cast(q);
        }
        for (size_t i = 0; i < sdfColliderEnds.size(); ++i) {
            const auto& end                = sdfColliderEnds[i];
            auto&       sample             = impl_->sdfColliders[i];
            sample.position                = end.position - end.velocity * (seconds * (1.f - fraction));
            const auto q                   = interpolatedRotation(end.rotation, end.angularVelocity, seconds, fraction);
            sample.rotation                = {q.x, q.y, q.z, q.w};
            impl_->sdfColliderRotations[i] = glm::mat3_cast(q);
        }
        for (size_t i = 0; i < heightFieldEnds.size(); ++i) {
            const auto& end    = heightFieldEnds[i];
            auto&       sample = impl_->heightFieldColliders[i];
            sample.position    = end.position - end.velocity * (seconds * (1.f - fraction));
            const auto q       = interpolatedRotation(end.rotation, end.angularVelocity, seconds, fraction);
            sample.rotation    = {q.x, q.y, q.z, q.w};
            impl_->heightFieldColliderRotations[i] = glm::mat3_cast(q);
        }
        for (const auto& motion : motions) {
            if (motion.dynamic) continue;
            auto& p    = impl_->state[motion.particle];
            p.position = glm::mix(motion.start, motion.target, float(k + 1) / float(substeps));
            p.velocity = p.material.phase == VolumeFluidPhase::Solid ? motion.velocity : glm::vec3(0.f);
            if (motion.constrainOrientation) {
                p.angularVelocity      = motion.angularVelocity;
                const auto orientation = glm::normalize(
                    glm::slerp(motion.startOrientation, motion.targetOrientation, float(k + 1) / float(substeps)));
                p.orientation = {orientation.x, orientation.y, orientation.z, orientation.w};
            }
        }
        if (motions.empty()) {
            if (impl_->externalForces.empty())
                impl_->substep<false, false>(seconds / float(substeps), rules.empty() ? nullptr : &contactParticles);
            else
                impl_->substep<false, true>(seconds / float(substeps), rules.empty() ? nullptr : &contactParticles);
        } else if (impl_->externalForces.empty()) {
            impl_->substep<true, false>(seconds / float(substeps), rules.empty() ? nullptr : &contactParticles);
        } else {
            impl_->substep<true, true>(seconds / float(substeps), rules.empty() ? nullptr : &contactParticles);
        }
        if (!impl_->stitches.empty()) {
            const float stitchDt = seconds / float(substeps);
            impl_->stitchStart.resize(impl_->state.size());
            impl_->stitchDelta.resize(impl_->state.size());
            impl_->stitchCounts.resize(impl_->state.size());
            impl_->stitchTouched.clear();
            impl_->stitchTouched.reserve(impl_->stitches.size() * 2);
            for (const auto& stitch : impl_->stitches) {
                impl_->stitchTouched.push_back(stitch.particleIndex1);
                impl_->stitchTouched.push_back(stitch.particleIndex2);
                impl_->stitchStart[stitch.particleIndex1] = impl_->state[stitch.particleIndex1].position;
                impl_->stitchStart[stitch.particleIndex2] = impl_->state[stitch.particleIndex2].position;
            }
            std::sort(impl_->stitchTouched.begin(), impl_->stitchTouched.end());
            impl_->stitchTouched.erase(std::unique(impl_->stitchTouched.begin(), impl_->stitchTouched.end()),
                                       impl_->stitchTouched.end());
            impl_->stitchLambdas.assign(impl_->stitches.size(), 0.f);
            for (unsigned iteration = 0; iteration < impl_->settings.iterations; ++iteration) {
                for (const unsigned index : impl_->stitchTouched) {
                    impl_->stitchDelta[index]  = glm::vec3(0.f);
                    impl_->stitchCounts[index] = 0;
                }
                for (size_t stitchIndex = 0; stitchIndex < impl_->stitches.size(); ++stitchIndex) {
                    const auto& stitch = impl_->stitches[stitchIndex];
                    const auto  fixed  = [&](unsigned index) {
                        return impl_->state[index].material.phase == VolumeFluidPhase::Solid ||
                               impl_->grabberLabels[index] != Impl::noGrabber ||
                               (!impl_->attachmentLabels.empty() && impl_->attachmentLabels[index] >= 0);
                    };
                    const float w1 = fixed(stitch.particleIndex1)
                                         ? 0.f
                                         : 1.f / (impl_->volume * impl_->state[stitch.particleIndex1].material.density);
                    const float w2 = fixed(stitch.particleIndex2)
                                         ? 0.f
                                         : 1.f / (impl_->volume * impl_->state[stitch.particleIndex2].material.density);
                    const auto  distance =
                        impl_->state[stitch.particleIndex1].position - impl_->state[stitch.particleIndex2].position;
                    const float length      = glm::length(distance);
                    const float alpha       = stitch.compliance / (stitchDt * stitchDt);
                    const float deltaLambda = (-length - alpha * impl_->stitchLambdas[stitchIndex]) /
                                              (w1 + w2 + alpha + std::numeric_limits<float>::epsilon());
                    const auto  delta       = deltaLambda * distance / (length + std::numeric_limits<float>::epsilon());
                    impl_->stitchDelta[stitch.particleIndex1] += delta * w1;
                    impl_->stitchDelta[stitch.particleIndex2] -= delta * w2;
                    ++impl_->stitchCounts[stitch.particleIndex1];
                    ++impl_->stitchCounts[stitch.particleIndex2];
                    impl_->stitchLambdas[stitchIndex] += deltaLambda;
                }
                for (const unsigned index : impl_->stitchTouched)
                    if (impl_->stitchCounts[index] != 0)
                        impl_->state[index].position += impl_->stitchDelta[index] / float(impl_->stitchCounts[index]);
            }
            for (const unsigned index : impl_->stitchTouched)
                if (impl_->stitchCounts[index] != 0)
                    impl_->state[index].velocity +=
                        (impl_->state[index].position - impl_->stitchStart[index]) / stitchDt;
        }
        const float substepSeconds = seconds / float(substeps);
        for (auto& motion : motions) {
            if (!motion.dynamic || motion.broken) continue;
            auto&       particle    = impl_->state[motion.particle];
            const auto& collider    = impl_->colliders[motion.colliderIndex];
            const auto& rotation    = impl_->colliderRotations[motion.colliderIndex];
            const auto  target      = collider.center + rotation * motion.localPosition;
            const float mass        = impl_->volume * particle.material.density;
            const float inverseMass = 1.f / mass;
            const float alpha       = motion.compliance / (substepSeconds * substepSeconds);
            const float response    = inverseMass / (inverseMass + alpha);
            const auto  correction  = (target - particle.position) * response;
            particle.position += correction;
            particle.velocity += correction / substepSeconds;
            motion.constraintReaction -= mass * correction / substepSeconds;
            float force = mass * glm::length(correction) / (substepSeconds * substepSeconds);

            if (motion.constrainOrientation) {
                const auto colliderQ =
                    glm::quat(collider.rotation.w, collider.rotation.x, collider.rotation.y, collider.rotation.z);
                const auto currentQ = particleQuaternion(particle);
                const auto desiredQ = glm::normalize(colliderQ * motion.localOrientation);
                const auto nextQ    = glm::normalize(glm::slerp(currentQ, desiredQ, response));
                auto       deltaQ   = glm::normalize(nextQ * glm::conjugate(currentQ));
                if (deltaQ.w < 0.f) deltaQ = -deltaQ;
                const float angle = 2.f * std::acos(std::clamp(deltaQ.w, -1.f, 1.f));
                glm::vec3   deltaAngular(0.f);
                const float sine = std::sqrt(std::max(0.f, 1.f - deltaQ.w * deltaQ.w));
                if (sine > 1e-6f)
                    deltaAngular = glm::vec3(deltaQ.x, deltaQ.y, deltaQ.z) * (angle / sine / substepSeconds);
                particle.orientation = {nextQ.x, nextQ.y, nextQ.z, nextQ.w};
                particle.angularVelocity += deltaAngular;
                const auto  radii = particle.radii;
                const float inertia =
                    mass * .2f *
                    ((radii.y * radii.y + radii.z * radii.z) + (radii.x * radii.x + radii.z * radii.z) +
                     (radii.x * radii.x + radii.y * radii.y)) /
                    3.f;
                motion.dynamicAngularReaction -= deltaAngular * inertia;
            }
            if (force > motion.breakThreshold) motion.broken = true;
        }
        for (const auto& p : impl_->state)
            if (!finite(p.position) || !finite(p.velocity) || !finite(p.angularVelocity) ||
                glm::length(p.angularVelocity) > 1000.f || !finite(p.color) || !finite(p.data) ||
                (p.material.phase == VolumeFluidPhase::Solid &&
                 glm::any(glm::notEqual(impl_->constrain(p.position), p.position)))) {
                impl_->state       = original;
                impl_->contacts    = originalContacts;
                impl_->attachments = originalAttachments;
                restoreColliderEnds();
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::Failed, "Nonfinite state or solid outside bounds; step rolled back",
                    "fluids.volume.step"));
            }
    }
    restoreColliderEnds();
    if (std::any_of(motions.begin(), motions.end(), [](const auto& motion) { return motion.broken; })) {
        std::erase_if(impl_->attachments, [&](const auto& attachment) {
            return std::any_of(motions.begin(), motions.end(), [&](const auto& motion) {
                return motion.broken && motion.particle == attachment.particleIndex &&
                       motion.colliderLabel == attachment.colliderLabel;
            });
        });
    }
    // The final substep's grid still describes these unchanged positions.
    auto propagated = impl_->propagateSolidification();
    if (!propagated) {
        impl_->state       = original;
        impl_->contacts    = originalContacts;
        impl_->attachments = originalAttachments;
        return propagated;
    }
    impl_->attachmentReactionScratch.clear();
    impl_->attachmentReactionScratch.reserve(motions.size());
    for (const auto& motion : motions) {
        const auto impulse =
            motion.inertialReaction + motion.constraintReaction + impl_->attachmentStressImpulse[motion.particle];
        const auto angularImpulse = motion.angularReaction + motion.dynamicAngularReaction;
        if (glm::dot(impulse, impulse) > 0.f || glm::dot(angularImpulse, angularImpulse) > 0.f)
            impl_->attachmentReactionScratch.push_back({motion.colliderLabel, motion.target, impulse, angularImpulse});
    }
    impl_->attachmentReactions.swap(impl_->attachmentReactionScratch);
    auto& hits = impl_->thermalHitScratch;
    hits.clear();
    hits.reserve(contactParticles.size());
    for (size_t i = 0; i < contactParticles.size(); ++i)
        hits.push_back((uint64_t(impl_->contacts[i].colliderLabel) << 32u) | uint64_t(contactParticles[i]));
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
    for (const uint64_t hit : hits) {
        const unsigned label = unsigned(hit >> 32u);
        const auto rule = std::lower_bound(ordered.begin(), ordered.end(), label,
                                           [](const auto& item, unsigned value) { return item.colliderLabel < value; });
        if (rule == ordered.end() || rule->colliderLabel != label) continue;
        auto& data = impl_->state[size_t(hit & 0xFFFFFFFFull)].data;
        data.x     = std::clamp(data.x + rule->rate * seconds, rule->minimumViscosity, rule->maximumViscosity);
        data.y     = std::clamp(data.y + rule->rate * seconds, rule->minimumCohesion, rule->maximumCohesion);
    }
    // Reuse the neighbor-link scratch as the old-to-new index map after stepping.
    impl_->next.resize(impl_->state.size());
    impl_->grabberLabelScratch.resize(impl_->state.size());
    impl_->grabberLocalScratch.resize(impl_->state.size());
    size_t remaining = 0;
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        auto&      p       = impl_->state[i];
        const bool expires = p.life > 0.f && (p.life -= seconds) <= 0.f;
        impl_->next[i]     = expires ? -1 : int(remaining);
        if (expires) impl_->recordParticleEvent(VolumeFluidParticleEventType::Killed, unsigned(i), p);
        if (!expires) {
            if (remaining != i) impl_->state[remaining] = p;
            impl_->renderPreviousScratch[remaining] = impl_->renderPreviousScratch[i];
            impl_->grabberLabelScratch[remaining]   = impl_->grabberLabels[i];
            impl_->grabberLocalScratch[remaining]   = impl_->grabberLocalPositions[i];
            ++remaining;
        }
    }
    impl_->state.resize(remaining);
    impl_->renderPreviousScratch.resize(remaining);
    impl_->renderPrevious.swap(impl_->renderPreviousScratch);
    impl_->grabberLabelScratch.resize(remaining);
    impl_->grabberLabels.swap(impl_->grabberLabelScratch);
    impl_->grabberLocalScratch.resize(remaining);
    impl_->grabberLocalPositions.swap(impl_->grabberLocalScratch);
    std::erase_if(impl_->attachments, [&](auto& a) {
        const int mapped = impl_->next[a.particleIndex];
        if (mapped < 0) return true;
        a.particleIndex = unsigned(mapped);
        return false;
    });
    std::erase_if(impl_->stitches, [&](auto& stitch) {
        const int first  = impl_->next[stitch.particleIndex1];
        const int second = impl_->next[stitch.particleIndex2];
        if (first < 0 || second < 0) return true;
        stitch.particleIndex1 = unsigned(first);
        stitch.particleIndex2 = unsigned(second);
        return false;
    });
    std::erase_if(impl_->simplexes, [&](auto& simplex) {
        for (unsigned i = 0; i < simplex.size; ++i) {
            const int mapped = impl_->next[simplex.particleIndices[i]];
            if (mapped < 0) return true;
            simplex.particleIndices[i] = unsigned(mapped);
        }
        return false;
    });
    impl_->winds.clear();
    impl_->externalForces.clear();
    return Result<void>::success();
}

std::vector<VolumeFluidParticle>     VolumeFluid::particles() const { return impl_->state; }
std::span<const VolumeFluidParticle> VolumeFluid::particleView() const { return impl_->state; }
Result<void>                         VolumeFluid::stepWithColliders(float seconds, unsigned substeps,
                                                                    std::span<const VolumeFluidCollider>    colliders,
                                                                    std::span<const VolumeFluidThermalRule> rules) {
    auto previousColliders   = impl_->colliders;
    auto previousRotations   = impl_->colliderRotations;
    auto previousAttachments = impl_->attachments;
    auto configured          = setColliders(colliders);
    if (!configured) return configured;
    auto advanced = stepWithThermalContacts(seconds, substeps, rules);
    if (!advanced) {
        impl_->colliders.swap(previousColliders);
        impl_->colliderRotations.swap(previousRotations);
        impl_->attachments.swap(previousAttachments);
    }
    return advanced;
}
size_t       VolumeFluid::particleCount() const { return impl_->state.size(); }
size_t       VolumeFluid::availableCapacity() const { return impl_->settings.capacity - impl_->state.size(); }
float        VolumeFluid::spacing() const { return impl_->settings.spacing; }
Result<void> VolumeFluid::setGravity(glm::vec3 gravity) {
    if (!finite(gravity) || glm::length(gravity) > 1000.f) return invalid("Invalid world-space gravity");
    impl_->settings.gravity = gravity;
    return Result<void>::success();
}
glm::vec3    VolumeFluid::gravity() const { return impl_->settings.gravity; }
Result<void> VolumeFluid::copyInterpolatedPositions(float alpha, std::vector<glm::vec3>& positions) const {
    if (!std::isfinite(alpha) || alpha < 0.f || alpha > 1.f) return invalid("Invalid render interpolation alpha");
    positions.resize(impl_->state.size());
    for (size_t i = 0; i < impl_->state.size(); ++i) positions[i] = interpolatedPositionUnchecked(i, alpha);
    return Result<void>::success();
}
Result<float> VolumeFluid::copyInterpolatedRenderData(float alpha, std::vector<glm::vec3>& positions,
                                                      std::vector<glm::vec4>& colors) const {
    if (!std::isfinite(alpha) || alpha < 0.f || alpha > 1.f)
        return Result<float>::failure(invalid("Invalid render interpolation alpha").status());
    positions.resize(impl_->state.size());
    colors.resize(impl_->state.size());
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        positions[i] = interpolatedPositionUnchecked(i, alpha);
        colors[i]    = impl_->state[i].color;
    }
    return Result<float>::success(impl_->settings.spacing);
}
glm::vec3 VolumeFluid::interpolatedPositionUnchecked(size_t particleIndex, float alpha) const {
    return glm::mix(impl_->renderPrevious[particleIndex], impl_->state[particleIndex].position, alpha);
}
float VolumeFluid::copyRenderData(std::vector<glm::vec3>& positions, std::vector<glm::vec4>& colors) const {
    positions.resize(impl_->state.size());
    colors.resize(impl_->state.size());
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        positions[i] = impl_->state[i].position;
        colors[i]    = impl_->state[i].color;
    }
    return impl_->settings.spacing;
}
float VolumeFluid::copySurfaceRenderData(std::vector<glm::vec3>& positions, std::vector<glm::vec4>& colors,
                                         std::vector<glm::vec3>& radii, std::vector<glm::vec4>& orientations) const {
    const size_t count = impl_->state.size();
    positions.resize(count);
    colors.resize(count);
    radii.resize(count);
    orientations.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& particle = impl_->state[i];
        positions[i]         = particle.position;
        colors[i]            = particle.color;
        radii[i]             = particle.radii;
        orientations[i]      = particle.orientation;
    }
    return impl_->settings.spacing;
}
Result<float> VolumeFluid::copyInterpolatedSurfaceRenderData(float alpha, std::vector<glm::vec3>& positions,
                                                             std::vector<glm::vec4>& colors,
                                                             std::vector<glm::vec3>& radii,
                                                             std::vector<glm::vec4>& orientations) const {
    if (!std::isfinite(alpha) || alpha < 0.f || alpha > 1.f)
        return Result<float>::failure(invalid("Invalid render interpolation alpha").status());
    const size_t count = impl_->state.size();
    positions.resize(count);
    colors.resize(count);
    radii.resize(count);
    orientations.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& particle = impl_->state[i];
        positions[i]         = interpolatedPositionUnchecked(i, alpha);
        colors[i]            = particle.color;
        radii[i]             = particle.radii;
        orientations[i]      = particle.orientation;
    }
    return Result<float>::success(impl_->settings.spacing);
}
size_t VolumeFluid::copyPhaseRenderData(VolumeFluidPhase phase, std::vector<glm::vec3>& positions,
                                        std::vector<glm::vec4>& colors, size_t maxParticles) const {
    positions.clear();
    colors.clear();
    positions.reserve(std::min(maxParticles, impl_->state.size()));
    colors.reserve(std::min(maxParticles, impl_->state.size()));
    size_t selected = 0;
    for (const auto& particle : impl_->state) {
        if (particle.material.phase != phase) continue;
        if (selected < maxParticles) {
            positions.push_back(particle.position);
            colors.push_back(particle.color);
        }
        ++selected;
    }
    return selected;
}
void VolumeFluid::clear() {
    impl_->state.clear();
    impl_->renderPrevious.clear();
    impl_->grabberLabels.clear();
    impl_->grabberLocalPositions.clear();
    impl_->contacts.clear();
    impl_->attachments.clear();
    impl_->stitches.clear();
    impl_->attachmentLabels.clear();
    impl_->attachmentOrientationConstraints.clear();
    impl_->simplexes.clear();
    impl_->attachmentReactions.clear();
    impl_->attachmentReactionScratch.clear();
    impl_->winds.clear();
    impl_->windScratch.clear();
    impl_->externalForces.clear();
    impl_->externalForceScratch.clear();
    impl_->particleEvents.clear();
    impl_->droppedParticleEvents = 0;
}

Result<void> VolumeFluid::killParticle(unsigned particleIndex) {
    if (particleIndex >= impl_->state.size()) return invalid("Particle index is stale or out of range");
    impl_->recordParticleEvent(VolumeFluidParticleEventType::Killed, particleIndex, impl_->state[particleIndex]);
    impl_->state.erase(impl_->state.begin() + particleIndex);
    impl_->renderPrevious.erase(impl_->renderPrevious.begin() + particleIndex);
    impl_->grabberLabels.erase(impl_->grabberLabels.begin() + particleIndex);
    impl_->grabberLocalPositions.erase(impl_->grabberLocalPositions.begin() + particleIndex);
    std::erase_if(impl_->attachments, [&](auto& attachment) {
        if (attachment.particleIndex == particleIndex) return true;
        if (attachment.particleIndex > particleIndex) --attachment.particleIndex;
        return false;
    });
    std::erase_if(impl_->stitches, [&](auto& stitch) {
        if (stitch.particleIndex1 == particleIndex || stitch.particleIndex2 == particleIndex) return true;
        if (stitch.particleIndex1 > particleIndex) --stitch.particleIndex1;
        if (stitch.particleIndex2 > particleIndex) --stitch.particleIndex2;
        return false;
    });
    impl_->attachmentLabels.clear();
    impl_->attachmentOrientationConstraints.clear();
    std::erase_if(impl_->simplexes, [&](auto& simplex) {
        for (unsigned i = 0; i < simplex.size; ++i)
            if (simplex.particleIndices[i] == particleIndex) return true;
        for (unsigned i = 0; i < simplex.size; ++i)
            if (simplex.particleIndices[i] > particleIndex) --simplex.particleIndices[i];
        return false;
    });
    if (!impl_->winds.empty()) impl_->winds.erase(impl_->winds.begin() + particleIndex);
    if (!impl_->externalForces.empty()) impl_->externalForces.erase(impl_->externalForces.begin() + particleIndex);
    impl_->contacts.clear();
    impl_->attachmentReactions.clear();
    impl_->attachmentReactionScratch.clear();
    return Result<void>::success();
}

Result<unsigned> VolumeFluid::killActorParticles(unsigned actorGroup) {
    if (actorGroup > 0x00ffffffu)
        return Result<unsigned>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid actor group", "fluids.volume.killActor"));
    const size_t originalSize = impl_->state.size();
    const size_t removed = size_t(std::count_if(impl_->state.begin(), impl_->state.end(), [&](const auto& particle) {
        return particle.actorGroup == actorGroup;
    }));
    if (removed == 0)
        return Result<unsigned>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Actor group has no live particles", "fluids.volume.killActor"));
    impl_->next.resize(originalSize);
    size_t remaining = 0;
    for (size_t i = 0; i < originalSize; ++i) {
        const bool erase = impl_->state[i].actorGroup == actorGroup;
        impl_->next[i]   = erase ? -1 : int(remaining);
        if (erase) {
            impl_->recordParticleEvent(VolumeFluidParticleEventType::Killed, unsigned(i), impl_->state[i]);
            continue;
        }
        if (remaining != i) {
            impl_->state[remaining]                 = impl_->state[i];
            impl_->renderPrevious[remaining]        = impl_->renderPrevious[i];
            impl_->grabberLabels[remaining]         = impl_->grabberLabels[i];
            impl_->grabberLocalPositions[remaining] = impl_->grabberLocalPositions[i];
            if (!impl_->winds.empty()) impl_->winds[remaining] = impl_->winds[i];
            if (!impl_->externalForces.empty()) impl_->externalForces[remaining] = impl_->externalForces[i];
        }
        ++remaining;
    }
    impl_->state.resize(remaining);
    impl_->renderPrevious.resize(remaining);
    impl_->grabberLabels.resize(remaining);
    impl_->grabberLocalPositions.resize(remaining);
    if (!impl_->winds.empty()) impl_->winds.resize(remaining);
    if (!impl_->externalForces.empty()) impl_->externalForces.resize(remaining);
    std::erase_if(impl_->attachments, [&](auto& attachment) {
        const int mapped = impl_->next[attachment.particleIndex];
        if (mapped < 0) return true;
        attachment.particleIndex = unsigned(mapped);
        return false;
    });
    std::erase_if(impl_->stitches, [&](auto& stitch) {
        const int first = impl_->next[stitch.particleIndex1], second = impl_->next[stitch.particleIndex2];
        if (first < 0 || second < 0) return true;
        stitch.particleIndex1 = unsigned(first);
        stitch.particleIndex2 = unsigned(second);
        return false;
    });
    std::erase_if(impl_->simplexes, [&](auto& simplex) {
        for (unsigned i = 0; i < simplex.size; ++i) {
            const int mapped = impl_->next[simplex.particleIndices[i]];
            if (mapped < 0) return true;
            simplex.particleIndices[i] = unsigned(mapped);
        }
        return false;
    });
    impl_->attachmentLabels.clear();
    impl_->attachmentOrientationConstraints.clear();
    impl_->contacts.clear();
    impl_->attachmentReactions.clear();
    impl_->attachmentReactionScratch.clear();
    return Result<unsigned>::success(unsigned(removed));
}

Result<unsigned> VolumeFluid::actorParticleCount(unsigned actorGroup) const {
    if (actorGroup > 0x00ffffffu)
        return Result<unsigned>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid actor group",
                                                           "fluids.volume.actorParticleCount"));
    return Result<unsigned>::success(
        unsigned(std::count_if(impl_->state.begin(), impl_->state.end(),
                               [&](const auto& particle) { return particle.actorGroup == actorGroup; })));
}

Result<void> VolumeFluid::killActorParticle(unsigned actorGroup, unsigned actorParticleIndex) {
    if (actorGroup > 0x00ffffffu)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid actor group",
                                                       "fluids.volume.killActorParticle"));
    unsigned localIndex = 0;
    for (size_t denseIndex = 0; denseIndex < impl_->state.size(); ++denseIndex) {
        if (impl_->state[denseIndex].actorGroup != actorGroup) continue;
        if (localIndex == actorParticleIndex) return killParticle(unsigned(denseIndex));
        ++localIndex;
    }
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                   "Actor particle index is stale or out of range",
                                                   "fluids.volume.killActorParticle"));
}

}  // namespace eve::fluids
