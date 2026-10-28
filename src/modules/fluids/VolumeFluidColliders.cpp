#include "fluids/VolumeFluidInternal.inc"

namespace eve::fluids {
Result<void> VolumeFluid::setColliders(std::span<const VolumeFluidCollider> input) {
    if (input.size() > 1024 || uint64_t(input.size()) * impl_->state.size() > 4000000)
        return invalid("Analytic collider count or candidate budget exceeded");
    for (size_t i = 0; i < input.size(); ++i) {
        const auto& c = input[i];
        if (!finite(c.center) || !finite(c.halfExtent) || !finite(c.velocity) || glm::length(c.velocity) > 100.f ||
            !finite(c.angularVelocity) || glm::length(c.angularVelocity) > 1000.f || !finite(c.rotation) ||
            std::abs(glm::dot(c.rotation, c.rotation) - 1.f) > .001f ||
            glm::any(glm::lessThanEqual(c.halfExtent, glm::vec3(0.f))) || !range(c.radius, 0.001f, 10000.f) ||
            !validColliderMaterial(c) || !validFilter(c.collisionFilter) || (c.isTrigger && c.solidify) ||
            (c.shape != VolumeFluidColliderShape::Sphere && c.shape != VolumeFluidColliderShape::Box &&
             c.shape != VolumeFluidColliderShape::Capsule))
            return invalid("Invalid collider sample");
        for (size_t j = 0; j < i; ++j)
            if (input[j].label == c.label) return invalid("Duplicate collider label");
        if (std::any_of(impl_->sdfColliders.begin(), impl_->sdfColliders.end(),
                        [&](const auto& sdf) { return sdf.label == c.label; }))
            return invalid("Collider labels must be unique across analytic and SDF colliders");
        if (std::any_of(impl_->heightFieldColliders.begin(), impl_->heightFieldColliders.end(),
                        [&](const auto& terrain) { return terrain.label == c.label; }))
            return invalid("Collider labels must be unique across all collider kinds");
    }
    std::vector<VolumeFluidCollider> replacement(input.begin(), input.end());
    std::vector<glm::mat3>           rotations;
    rotations.reserve(input.size());
    for (const auto& c : input)
        rotations.push_back(
            glm::mat3_cast(glm::normalize(glm::quat(c.rotation.w, c.rotation.x, c.rotation.y, c.rotation.z))));
    std::vector<unsigned> labels;
    if (!impl_->attachments.empty()) {
        labels.reserve(input.size());
        for (const auto& c : input) labels.push_back(c.label);
        std::sort(labels.begin(), labels.end());
    }
    impl_->colliders.swap(replacement);
    impl_->colliderRotations.swap(rotations);
    std::erase_if(impl_->attachments,
                  [&](const auto& a) { return !std::binary_search(labels.begin(), labels.end(), a.colliderLabel); });
    impl_->attachmentLabels.clear();
    impl_->attachmentOrientationConstraints.clear();
    impl_->releaseMissingGrabbers();
    return Result<void>::success();
}

Result<unsigned> VolumeFluid::bindStaticParticles(std::span<const unsigned> particleIndices, unsigned colliderLabel,
                                                  bool constrainOrientation) {
    return bindParticles(particleIndices, colliderLabel, constrainOrientation, false, 0.f, 1e12f);
}

Result<unsigned> VolumeFluid::bindDynamicParticles(std::span<const unsigned> particleIndices, unsigned colliderLabel,
                                                   float compliance, float breakThreshold, bool constrainOrientation) {
    return bindParticles(particleIndices, colliderLabel, constrainOrientation, true, compliance, breakThreshold);
}

Result<unsigned> VolumeFluid::bindParticles(std::span<const unsigned> particleIndices, unsigned colliderLabel,
                                            bool constrainOrientation, bool dynamic, float compliance,
                                            float breakThreshold) {
    using Output = Result<unsigned>;
    if (particleIndices.empty() || particleIndices.size() > impl_->state.size() || !range(compliance, 0.f, 1000000.f) ||
        !range(breakThreshold, 1e-6f, 1e12f))
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "Invalid particle attachment group or constraint settings",
                                                 "fluids.volume.attachment"));
    const auto collider = std::find_if(impl_->colliders.begin(), impl_->colliders.end(),
                                       [&](const auto& item) { return item.label == colliderLabel; });
    if (collider == impl_->colliders.end())
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "Static attachment target collider is missing",
                                                 "fluids.volume.attachment"));
    std::vector<unsigned> ordered(particleIndices.begin(), particleIndices.end());
    std::sort(ordered.begin(), ordered.end());
    if (ordered.back() >= impl_->state.size() || std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end())
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "Static attachment indices must be unique and current",
                                                 "fluids.volume.attachment"));
    for (const auto& attachment : impl_->attachments)
        if (std::binary_search(ordered.begin(), ordered.end(), attachment.particleIndex))
            return Output::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Particle already owns an attachment", "fluids.volume.attachment"));

    const auto colliderQ = glm::normalize(
        glm::quat(collider->rotation.w, collider->rotation.x, collider->rotation.y, collider->rotation.z));
    const auto                         inverseRotation = glm::transpose(glm::mat3_cast(colliderQ));
    std::vector<VolumeFluidAttachment> additions;
    additions.reserve(ordered.size());
    for (const unsigned index : ordered) {
        const auto particleQ = particleQuaternion(impl_->state[index]);
        const auto localQ    = glm::normalize(glm::conjugate(colliderQ) * particleQ);
        additions.push_back({index,
                             colliderLabel,
                             inverseRotation * (impl_->state[index].position - collider->center),
                             {localQ.x, localQ.y, localQ.z, localQ.w},
                             constrainOrientation,
                             dynamic,
                             compliance,
                             breakThreshold});
    }
    impl_->attachments.reserve(impl_->attachments.size() + additions.size());
    impl_->attachments.insert(impl_->attachments.end(), additions.begin(), additions.end());
    impl_->attachmentLabels.clear();
    impl_->attachmentOrientationConstraints.clear();
    impl_->contacts.clear();
    impl_->attachmentReactions.clear();
    return Output::success(unsigned(additions.size()));
}

Result<unsigned> VolumeFluid::unbindStaticParticles(std::span<const unsigned> particleIndices) {
    using Output = Result<unsigned>;
    if (particleIndices.empty() || particleIndices.size() > impl_->state.size())
        return Output::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid static attachment particle group", "fluids.volume.attachment"));
    std::vector<unsigned> ordered(particleIndices.begin(), particleIndices.end());
    std::sort(ordered.begin(), ordered.end());
    if (ordered.back() >= impl_->state.size() || std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end())
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "Static attachment indices must be unique and current",
                                                 "fluids.volume.attachment"));
    const auto oldSize = impl_->attachments.size();
    std::erase_if(impl_->attachments, [&](const auto& attachment) {
        return std::binary_search(ordered.begin(), ordered.end(), attachment.particleIndex);
    });
    impl_->attachmentLabels.clear();
    impl_->attachmentOrientationConstraints.clear();
    impl_->contacts.clear();
    impl_->attachmentReactions.clear();
    return Output::success(unsigned(oldSize - impl_->attachments.size()));
}

Result<void> VolumeFluid::setStitches(std::span<const VolumeFluidStitch> stitches) {
    if (stitches.size() > 65536) return invalid("Too many particle stitches");
    std::vector<VolumeFluidStitch> candidate(stitches.begin(), stitches.end());
    for (auto& stitch : candidate) {
        if (stitch.particleIndex1 >= impl_->state.size() || stitch.particleIndex2 >= impl_->state.size() ||
            stitch.particleIndex1 == stitch.particleIndex2 || !range(stitch.compliance, 0.f, 1000000.f))
            return invalid("Invalid particle stitch");
        if (stitch.particleIndex2 < stitch.particleIndex1) std::swap(stitch.particleIndex1, stitch.particleIndex2);
    }
    std::sort(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) {
        return std::tie(a.particleIndex1, a.particleIndex2) < std::tie(b.particleIndex1, b.particleIndex2);
    });
    if (std::adjacent_find(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) {
            return a.particleIndex1 == b.particleIndex1 && a.particleIndex2 == b.particleIndex2;
        }) != candidate.end())
        return invalid("Duplicate particle stitch");
    impl_->stitches.swap(candidate);
    return Result<void>::success();
}

Result<void> VolumeFluid::setSdfColliders(std::span<const VolumeFluidSdfCollider> input) {
    if (input.size() > 16 || uint64_t(input.size()) * impl_->state.size() > 4000000)
        return invalid("SDF collider count or candidate budget exceeded");
    uint64_t               totalSamples = 0;
    std::vector<glm::mat3> rotations;
    rotations.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        const auto& c   = input[i];
        const auto& sdf = c.sdf;
        if (sdf.dims.x < 2 || sdf.dims.y < 2 || sdf.dims.z < 2 || sdf.dims.x > 256 || sdf.dims.y > 256 ||
            sdf.dims.z > 256 || !range(sdf.cellSize, .0005f, 1000.f) || !finite(sdf.origin) || !finite(c.position) ||
            !finite(c.rotation) || std::abs(glm::dot(c.rotation, c.rotation) - 1.f) > .001f ||
            !range(c.scale, .0001f, 10000.f) || !finite(c.velocity) || glm::length(c.velocity) > 100.f ||
            !finite(c.angularVelocity) || glm::length(c.angularVelocity) > 1000.f || !validColliderMaterial(c) ||
            !validFilter(c.collisionFilter))
            return invalid("Invalid SDF collider");
        const uint64_t samples = uint64_t(sdf.dims.x) * uint64_t(sdf.dims.y) * uint64_t(sdf.dims.z);
        totalSamples += samples;
        if (samples != sdf.distances.size() || totalSamples > 4000000 ||
            std::any_of(sdf.distances.begin(), sdf.distances.end(),
                        [](float value) { return !std::isfinite(value) || std::abs(value) > 10000.f; }))
            return invalid("Invalid SDF collider samples");
        for (size_t j = 0; j < i; ++j)
            if (input[j].label == c.label) return invalid("Duplicate SDF collider label");
        if (std::any_of(impl_->colliders.begin(), impl_->colliders.end(),
                        [&](const auto& analytic) { return analytic.label == c.label; }))
            return invalid("Collider labels must be unique across analytic and SDF colliders");
        if (std::any_of(impl_->heightFieldColliders.begin(), impl_->heightFieldColliders.end(),
                        [&](const auto& terrain) { return terrain.label == c.label; }))
            return invalid("Collider labels must be unique across all collider kinds");
        const auto rotation =
            glm::mat3_cast(glm::normalize(glm::quat(c.rotation.w, c.rotation.x, c.rotation.y, c.rotation.z)));
        if (c.inverted) {
            const auto minimum = sdf.origin;
            const auto maximum = sdf.origin + glm::vec3(sdf.dims - glm::ivec3(1)) * sdf.cellSize;
            for (unsigned corner = 0; corner < 8; ++corner) {
                const glm::vec3 world{corner & 1 ? impl_->settings.maximum.x : impl_->settings.minimum.x,
                                      corner & 2 ? impl_->settings.maximum.y : impl_->settings.minimum.y,
                                      corner & 4 ? impl_->settings.maximum.z : impl_->settings.minimum.z};
                const auto      local = glm::transpose(rotation) * (world - c.position) / c.scale;
                if (glm::any(glm::lessThan(local, minimum)) || glm::any(glm::greaterThan(local, maximum)))
                    return invalid("Inverted SDF domain must contain solver bounds");
            }
        }
        rotations.push_back(rotation);
    }
    std::vector<VolumeFluidSdfCollider> replacement(input.begin(), input.end());
    impl_->sdfColliders.swap(replacement);
    impl_->sdfColliderRotations.swap(rotations);
    impl_->releaseMissingGrabbers();
    return Result<void>::success();
}

Result<void> VolumeFluid::setHeightFieldColliders(std::span<const VolumeFluidHeightFieldCollider> input) {
    if (input.size() > 16 || uint64_t(input.size()) * impl_->state.size() > 4000000)
        return invalid("Height-field collider count or candidate budget exceeded");
    uint64_t               totalSamples = 0;
    std::vector<glm::mat3> rotations;
    rotations.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        const auto& c = input[i];
        if (c.resolution.x < 2 || c.resolution.y < 2 || c.resolution.x > 2048 || c.resolution.y > 2048 ||
            !finite(c.size) || glm::any(glm::lessThanEqual(c.size, glm::vec3(0.f))) || !finite(c.position) ||
            !finite(c.rotation) || std::abs(glm::dot(c.rotation, c.rotation) - 1.f) > .001f || !finite(c.velocity) ||
            glm::length(c.velocity) > 100.f || !finite(c.angularVelocity) || glm::length(c.angularVelocity) > 1000.f ||
            !validColliderMaterial(c) || !validFilter(c.collisionFilter))
            return invalid("Invalid height-field collider");
        const uint64_t samples = uint64_t(c.resolution.x) * uint64_t(c.resolution.y);
        totalSamples += samples;
        if (samples != c.heights.size() || totalSamples > 4000000 ||
            std::any_of(c.heights.begin(), c.heights.end(),
                        [](float h) { return !std::isfinite(h) || h < 0.f || h > 1.f; }))
            return invalid("Invalid height-field samples or sample budget exceeded");
        for (size_t j = 0; j < i; ++j)
            if (input[j].label == c.label) return invalid("Duplicate height-field collider label");
        if (std::any_of(impl_->colliders.begin(), impl_->colliders.end(),
                        [&](const auto& analytic) { return analytic.label == c.label; }) ||
            std::any_of(impl_->sdfColliders.begin(), impl_->sdfColliders.end(),
                        [&](const auto& sdf) { return sdf.label == c.label; }))
            return invalid("Collider labels must be unique across all collider kinds");
        rotations.push_back(
            glm::mat3_cast(glm::normalize(glm::quat(c.rotation.w, c.rotation.x, c.rotation.y, c.rotation.z))));
    }
    std::vector<VolumeFluidHeightFieldCollider> replacement(input.begin(), input.end());
    impl_->heightFieldColliders.swap(replacement);
    impl_->heightFieldColliderRotations.swap(rotations);
    impl_->releaseMissingGrabbers();
    return Result<void>::success();
}

Result<void> VolumeFluid::updateSdfColliderPoses(std::span<const VolumeFluidSdfPose> poses) {
    if (poses.size() > 16) return invalid("Too many SDF collider pose updates");
    std::vector<size_t> indices;
    indices.reserve(poses.size());
    std::vector<glm::mat3> rotations;
    rotations.reserve(poses.size());
    for (size_t i = 0; i < poses.size(); ++i) {
        const auto& pose = poses[i];
        if (!finite(pose.position) || !finite(pose.rotation) ||
            std::abs(glm::dot(pose.rotation, pose.rotation) - 1.f) > .001f || !range(pose.scale, .0001f, 10000.f) ||
            !finite(pose.velocity) || glm::length(pose.velocity) > 100.f || !finite(pose.angularVelocity) ||
            glm::length(pose.angularVelocity) > 1000.f)
            return invalid("Invalid SDF collider pose");
        for (size_t j = 0; j < i; ++j)
            if (poses[j].label == pose.label) return invalid("Duplicate SDF collider pose label");
        const auto found = std::find_if(impl_->sdfColliders.begin(), impl_->sdfColliders.end(),
                                        [&](const auto& collider) { return collider.label == pose.label; });
        if (found == impl_->sdfColliders.end()) return invalid("Unknown SDF collider pose label");
        const size_t index    = size_t(found - impl_->sdfColliders.begin());
        const auto   rotation = glm::mat3_cast(
            glm::normalize(glm::quat(pose.rotation.w, pose.rotation.x, pose.rotation.y, pose.rotation.z)));
        if (found->inverted) {
            const auto minimum = found->sdf.origin;
            const auto maximum = minimum + glm::vec3(found->sdf.dims - glm::ivec3(1)) * found->sdf.cellSize;
            for (unsigned corner = 0; corner < 8; ++corner) {
                const glm::vec3 world{corner & 1 ? impl_->settings.maximum.x : impl_->settings.minimum.x,
                                      corner & 2 ? impl_->settings.maximum.y : impl_->settings.minimum.y,
                                      corner & 4 ? impl_->settings.maximum.z : impl_->settings.minimum.z};
                const auto      local = glm::transpose(rotation) * (world - pose.position) / pose.scale;
                if (glm::any(glm::lessThan(local, minimum)) || glm::any(glm::greaterThan(local, maximum)))
                    return invalid("Updated inverted SDF domain must contain solver bounds");
            }
        }
        indices.push_back(index);
        rotations.push_back(rotation);
    }
    for (size_t i = 0; i < poses.size(); ++i) {
        auto&       collider                    = impl_->sdfColliders[indices[i]];
        const auto& pose                        = poses[i];
        collider.position                       = pose.position;
        collider.rotation                       = pose.rotation;
        collider.scale                          = pose.scale;
        collider.velocity                       = pose.velocity;
        collider.angularVelocity                = pose.angularVelocity;
        impl_->sdfColliderRotations[indices[i]] = rotations[i];
    }
    return Result<void>::success();
}

std::vector<VolumeFluidContact> VolumeFluid::contacts() const { return impl_->contacts; }

std::span<const VolumeFluidAttachmentReaction> VolumeFluid::attachmentReactions() const {
    return impl_->attachmentReactions;
}

Result<std::vector<VolumeFluidParticle>> VolumeFluid::overlap(glm::vec3 center, float radius) const {
    if (!finite(center) || !range(radius, 0.f, 10000.f))
        return Result<std::vector<VolumeFluidParticle>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid overlap sphere", "fluids.volume.overlap"));
    std::vector<VolumeFluidParticle> hits;
    for (const auto& p : impl_->state)
        if (glm::length(p.position - center) <= radius) hits.push_back(p);
    return Result<std::vector<VolumeFluidParticle>>::success(std::move(hits));
}

Result<std::vector<VolumeFluidParticle>> VolumeFluid::overlapBox(glm::vec3 center, glm::vec3 halfExtent,
                                                                 glm::vec4 rotation) const {
    if (!finite(center) || !finite(halfExtent) || glm::any(glm::lessThan(halfExtent, glm::vec3(0.f))) ||
        glm::any(glm::greaterThan(halfExtent, glm::vec3(10000.f))) || !finite(rotation) ||
        std::abs(glm::dot(rotation, rotation) - 1.f) > 0.001f)
        return Result<std::vector<VolumeFluidParticle>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid overlap box", "fluids.volume.overlapBox"));
    const auto inverseRotation =
        glm::transpose(glm::mat3_cast(glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z))));
    std::vector<VolumeFluidParticle> hits;
    for (const auto& particle : impl_->state) {
        const auto local = inverseRotation * (particle.position - center);
        if (glm::all(glm::lessThanEqual(glm::abs(local), halfExtent))) hits.push_back(particle);
    }
    return Result<std::vector<VolumeFluidParticle>>::success(std::move(hits));
}

Result<std::vector<VolumeFluidRayHit>> VolumeFluid::raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance,
                                                            unsigned maxHits, unsigned phaseMask) const {
    using Output       = Result<std::vector<VolumeFluidRayHit>>;
    const auto failure = [](const char* message) {
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.raycast"));
    };
    const float directionLength = glm::length(direction);
    if (!finite(origin) || !finite(direction) || !std::isfinite(directionLength) || directionLength <= 1e-7f ||
        !range(maxDistance, 0.f, 10000.f) || maxHits == 0 || maxHits > 4096 || phaseMask == 0 ||
        (phaseMask & ~0x0fu) != 0)
        return failure("Invalid ray, distance, hit limit or phase mask");
    if (impl_->state.size() > 65536) return failure("Ray query exceeds 65536-particle work budget");
    direction /= directionLength;
    struct Farther {
        bool operator()(const VolumeFluidRayHit& a, const VolumeFluidRayHit& b) const {
            if (a.distance != b.distance) return a.distance < b.distance;
            return a.particleIndex < b.particleIndex;
        }
    };
    std::priority_queue<VolumeFluidRayHit, std::vector<VolumeFluidRayHit>, Farther> nearest;
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        const auto&    particle = impl_->state[i];
        const unsigned phase    = unsigned(particle.material.phase);
        if ((phaseMask & (1u << phase)) == 0) continue;
        float     distance = 0.f;
        glm::vec3 normal;
        if (!rayEllipsoid(particle, origin, direction, maxDistance, distance, normal)) continue;
        const auto        point = origin + direction * distance;
        VolumeFluidRayHit hit{unsigned(i), distance, point, normal, particle};
        if (nearest.size() < maxHits)
            nearest.push(std::move(hit));
        else {
            const auto& worst = nearest.top();
            if (distance > worst.distance || (distance == worst.distance && i >= worst.particleIndex)) continue;
            nearest.pop();
            nearest.push(std::move(hit));
        }
    }
    std::vector<VolumeFluidRayHit> hits(nearest.size());
    for (size_t i = hits.size(); i > 0; --i) {
        hits[i - 1] = nearest.top();
        nearest.pop();
    }
    return Output::success(std::move(hits));
}

Result<std::vector<VolumeFluidDistanceHit>> VolumeFluid::querySphere(glm::vec3 center, float radius,
                                                                     float contactOffset, float maxDistance,
                                                                     unsigned maxHits, unsigned phaseMask,
                                                                     unsigned collisionFilter) const {
    using Output       = Result<std::vector<VolumeFluidDistanceHit>>;
    const auto failure = [](const char* message) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.querySphere"));
    };
    if (!finite(center) || !range(radius, 0.f, 10000.f) || !range(contactOffset, 0.f, 10000.f) ||
        !range(maxDistance, 0.f, 10000.f) || maxHits == 0 || maxHits > 4096 || phaseMask == 0 ||
        (phaseMask & ~0x0fu) != 0 || !validFilter(collisionFilter))
        return failure("Invalid sphere query, distance, hit limit, phase mask or collision filter");
    if (impl_->state.size() > 65536) return failure("Sphere query exceeds 65536-particle work budget");
    struct Farther {
        bool operator()(const VolumeFluidDistanceHit& a, const VolumeFluidDistanceHit& b) const {
            if (a.distance != b.distance) return a.distance < b.distance;
            return a.particleIndex < b.particleIndex;
        }
    };
    std::priority_queue<VolumeFluidDistanceHit, std::vector<VolumeFluidDistanceHit>, Farther> nearest;
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        const auto&    particle = impl_->state[i];
        const unsigned phase    = unsigned(particle.material.phase);
        if ((phaseMask & (1u << phase)) == 0 || !filtersMatch(collisionFilter, particle.collisionFilter)) continue;
        const auto  offset         = particle.position - center;
        const float centerDistance = glm::length(offset);
        const auto  normal         = centerDistance > 1e-7f ? offset / centerDistance : glm::vec3(0.f, 1.f, 0.f);
        const float distance       = centerDistance - radius - contactOffset - ellipsoidRadius(particle, normal);
        if (distance > maxDistance) continue;
        VolumeFluidDistanceHit hit{unsigned(i), distance, center + normal * (radius + contactOffset), normal, particle};
        if (nearest.size() < maxHits)
            nearest.push(std::move(hit));
        else {
            const auto& worst = nearest.top();
            if (distance > worst.distance || (distance == worst.distance && i >= worst.particleIndex)) continue;
            nearest.pop();
            nearest.push(std::move(hit));
        }
    }
    std::vector<VolumeFluidDistanceHit> hits(nearest.size());
    for (size_t i = hits.size(); i > 0; --i) {
        hits[i - 1] = nearest.top();
        nearest.pop();
    }
    return Output::success(std::move(hits));
}

Result<std::vector<VolumeFluidDistanceHit>> VolumeFluid::queryBox(glm::vec3 center, glm::vec3 halfExtent,
                                                                  glm::vec4 rotation, float contactOffset,
                                                                  float maxDistance, unsigned maxHits,
                                                                  unsigned phaseMask, unsigned collisionFilter) const {
    using Output       = Result<std::vector<VolumeFluidDistanceHit>>;
    const auto failure = [](const char* message) {
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.queryBox"));
    };
    if (!finite(center) || !finite(halfExtent) || glm::any(glm::lessThan(halfExtent, glm::vec3(0.f))) ||
        glm::any(glm::greaterThan(halfExtent, glm::vec3(10000.f))) || !finite(rotation) ||
        std::abs(glm::dot(rotation, rotation) - 1.f) > .001f || !range(contactOffset, 0.f, 10000.f) ||
        !range(maxDistance, 0.f, 10000.f) || maxHits == 0 || maxHits > 4096 || phaseMask == 0 ||
        (phaseMask & ~0x0fu) != 0 || !validFilter(collisionFilter))
        return failure("Invalid box query, transform, distance, hit limit, phase mask or collision filter");
    if (impl_->state.size() > 65536) return failure("Box query exceeds 65536-particle work budget");
    const auto boxRotation = glm::mat3_cast(glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z)));
    const auto inverseRotation = glm::transpose(boxRotation);
    struct Farther {
        bool operator()(const VolumeFluidDistanceHit& a, const VolumeFluidDistanceHit& b) const {
            if (a.distance != b.distance) return a.distance < b.distance;
            return a.particleIndex < b.particleIndex;
        }
    };
    std::priority_queue<VolumeFluidDistanceHit, std::vector<VolumeFluidDistanceHit>, Farther> nearest;
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        const auto&    particle = impl_->state[i];
        const unsigned phase    = unsigned(particle.material.phase);
        if ((phaseMask & (1u << phase)) == 0 || !filtersMatch(collisionFilter, particle.collisionFilter)) continue;
        const auto local        = inverseRotation * (particle.position - center);
        const auto faceDistance = halfExtent - glm::abs(local);
        glm::vec3  localPoint, localNormal(0.f);
        if (glm::all(glm::greaterThanEqual(faceDistance, glm::vec3(0.f)))) {
            int axis = 0;
            if (faceDistance.y < faceDistance[axis]) axis = 1;
            if (faceDistance.z < faceDistance[axis]) axis = 2;
            localNormal[axis] = local[axis] > 0.f ? 1.f : -1.f;
            localPoint        = local;
            localPoint[axis]  = halfExtent[axis] * localNormal[axis];
        } else {
            localPoint  = glm::clamp(local, -halfExtent, halfExtent);
            localNormal = glm::normalize(local - localPoint);
        }
        const auto  normal     = boxRotation * localNormal;
        const auto  queryPoint = center + boxRotation * (localPoint + localNormal * contactOffset);
        const float distance   = glm::dot(particle.position - queryPoint, normal) - ellipsoidRadius(particle, normal);
        if (distance > maxDistance) continue;
        VolumeFluidDistanceHit hit{unsigned(i), distance, queryPoint, normal, particle};
        if (nearest.size() < maxHits)
            nearest.push(std::move(hit));
        else {
            const auto& worst = nearest.top();
            if (distance > worst.distance || (distance == worst.distance && i >= worst.particleIndex)) continue;
            nearest.pop();
            nearest.push(std::move(hit));
        }
    }
    std::vector<VolumeFluidDistanceHit> hits(nearest.size());
    for (size_t i = hits.size(); i > 0; --i) {
        hits[i - 1] = nearest.top();
        nearest.pop();
    }
    return Output::success(std::move(hits));
}

Result<std::vector<VolumeFluidQueryHit>> VolumeFluid::queryBatch(std::span<const VolumeFluidQueryShape> queries,
                                                                 unsigned maxHitsPerQuery) const {
    using Output       = Result<std::vector<VolumeFluidQueryHit>>;
    const auto failure = [](const char* message) {
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.queryBatch"));
    };
    if (queries.size() > 256 || maxHitsPerQuery == 0 || maxHitsPerQuery > 4096)
        return failure("Invalid batch query count or hit limit");
    if (impl_->state.size() > 65536) return failure("Batch query exceeds 65536-particle source budget");
    if (!queries.empty() && impl_->state.size() > 4000000u / queries.size())
        return failure("Batch query exceeds four-million candidate budget");
    for (const auto& query : queries) {
        if (unsigned(query.type) > unsigned(VolumeFluidQueryType::Ray) || !finite(query.center) ||
            !finite(query.size) || !range(query.contactOffset, 0.f, 10000.f) ||
            !range(query.maxDistance, 0.f, 10000.f) || query.phaseMask == 0 || (query.phaseMask & ~0x0fu) != 0 ||
            !validFilter(query.collisionFilter))
            return failure("Invalid batch query shape, distance or phase mask");
        if (query.type == VolumeFluidQueryType::Sphere &&
            (!range(query.size.x, 0.f, 10000.f) || query.size.y != 0.f || query.size.z != 0.f))
            return failure("Sphere batch size must contain radius in x only");
        if (query.type == VolumeFluidQueryType::Box &&
            (glm::any(glm::lessThan(query.size, glm::vec3(0.f))) ||
             glm::any(glm::greaterThan(query.size, glm::vec3(20000.f))) || !finite(query.rotation) ||
             std::abs(glm::dot(query.rotation, query.rotation) - 1.f) > .001f))
            return failure("Invalid batch box size or rotation");
        if (query.type == VolumeFluidQueryType::Ray && glm::length(query.size - query.center) <= 1e-7f)
            return failure("Batch ray segment must be nonzero");
    }
    std::vector<VolumeFluidQueryHit> output;
    output.reserve(std::min<size_t>(queries.size() * size_t(maxHitsPerQuery), 4000000u));
    for (size_t queryIndex = 0; queryIndex < queries.size(); ++queryIndex) {
        const auto& query = queries[queryIndex];
        if (query.type == VolumeFluidQueryType::Ray) {
            const auto  segment       = query.size - query.center;
            const float segmentLength = glm::length(segment);
            const auto  direction     = segment / segmentLength;
            struct Farther {
                bool operator()(const VolumeFluidQueryHit& a, const VolumeFluidQueryHit& b) const {
                    if (a.distance != b.distance) return a.distance < b.distance;
                    return a.particleIndex < b.particleIndex;
                }
            };
            std::priority_queue<VolumeFluidQueryHit, std::vector<VolumeFluidQueryHit>, Farther> nearest;
            for (size_t particleIndex = 0; particleIndex < impl_->state.size(); ++particleIndex) {
                const auto& particle = impl_->state[particleIndex];
                if ((query.phaseMask & (1u << unsigned(particle.material.phase))) == 0 ||
                    !filtersMatch(query.collisionFilter, particle.collisionFilter))
                    continue;
                const auto  fromStart        = particle.position - query.center;
                const float along            = std::clamp(glm::dot(fromStart, direction), 0.f, segmentLength);
                auto        centerLine       = query.center + direction * along;
                auto        centerToParticle = particle.position - centerLine;
                float       centerDistance   = glm::length(centerToParticle);
                auto        normal           = centerDistance > 1e-7f ? centerToParticle / centerDistance : -direction;
                float       distance         = centerDistance - ellipsoidRadius(particle, normal) - query.contactOffset;
                auto        queryPoint       = centerLine + normal * query.contactOffset;
                float       entry            = 0.f;
                glm::vec3   hitNormal;
                if (rayEllipsoid(particle, query.center, direction, segmentLength, entry, hitNormal)) {
                    centerLine = query.center + direction * entry;
                    normal     = -hitNormal;
                    queryPoint = centerLine + normal * query.contactOffset;
                    distance   = -query.contactOffset;
                }
                if (distance > query.maxDistance) continue;
                VolumeFluidQueryHit hit{
                    unsigned(queryIndex), unsigned(particleIndex), distance, queryPoint, normal, particle};
                if (nearest.size() < maxHitsPerQuery)
                    nearest.push(std::move(hit));
                else if (distance < nearest.top().distance ||
                         (distance == nearest.top().distance && particleIndex < nearest.top().particleIndex)) {
                    nearest.pop();
                    nearest.push(std::move(hit));
                }
            }
            std::vector<VolumeFluidQueryHit> hits(nearest.size());
            for (size_t i = hits.size(); i > 0; --i) {
                hits[i - 1] = nearest.top();
                nearest.pop();
            }
            output.insert(output.end(), std::make_move_iterator(hits.begin()), std::make_move_iterator(hits.end()));
        } else {
            Result<std::vector<VolumeFluidDistanceHit>> hits =
                query.type == VolumeFluidQueryType::Sphere
                    ? querySphere(query.center, query.size.x, query.contactOffset, query.maxDistance, maxHitsPerQuery,
                                  query.phaseMask, query.collisionFilter)
                    : queryBox(query.center, query.size * .5f, query.rotation, query.contactOffset, query.maxDistance,
                               maxHitsPerQuery, query.phaseMask, query.collisionFilter);
            if (!hits) return failure("Validated batch distance query failed");
            for (const auto& hit : hits.value())
                output.push_back(
                    {unsigned(queryIndex), hit.particleIndex, hit.distance, hit.queryPoint, hit.normal, hit.particle});
        }
    }
    return Output::success(std::move(output));
}

Result<std::vector<unsigned>> VolumeFluid::applyQueryColors(std::span<const VolumeFluidQueryShape> queries,
                                                            std::span<const glm::vec4>             overlapColors,
                                                            glm::vec4 baseColor, unsigned maxHitsPerQuery) {
    using Output          = Result<std::vector<unsigned>>;
    const auto validColor = [](glm::vec4 color) {
        return range(color.x, 0.f, 1.f) && range(color.y, 0.f, 1.f) && range(color.z, 0.f, 1.f) &&
               range(color.w, 0.f, 1.f);
    };
    if (queries.size() != overlapColors.size() || !validColor(baseColor) ||
        std::any_of(overlapColors.begin(), overlapColors.end(), [&](glm::vec4 color) { return !validColor(color); }))
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "Query colors must be finite RGBA values with one color per query",
                                                 "fluids.volume.applyQueryColors"));
    auto hits = queryBatch(queries, maxHitsPerQuery);
    if (!hits) return Output::failure(hits.status());
    std::vector<unsigned> counts(queries.size(), 0);
    for (auto& particle : impl_->state) particle.color = baseColor;
    for (const auto& hit : hits.value())
        if (hit.distance < 0.f) {
            impl_->state[hit.particleIndex].color = overlapColors[hit.queryIndex];
            ++counts[hit.queryIndex];
        }
    return Output::success(std::move(counts));
}

Result<std::vector<unsigned>> VolumeFluid::applyQueryColorsPreservingOutside(
    std::span<const VolumeFluidQueryShape> queries, std::span<const glm::vec4> overlapColors,
    unsigned maxHitsPerQuery) {
    using Output          = Result<std::vector<unsigned>>;
    const auto validColor = [](glm::vec4 color) {
        return range(color.x, 0.f, 1.f) && range(color.y, 0.f, 1.f) && range(color.z, 0.f, 1.f) &&
               range(color.w, 0.f, 1.f);
    };
    if (queries.size() != overlapColors.size() ||
        std::any_of(overlapColors.begin(), overlapColors.end(), [&](glm::vec4 color) { return !validColor(color); }))
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "Query colors must be finite RGBA values with one color per query",
                                                 "fluids.volume.applyQueryColorsPreservingOutside"));
    auto hits = queryBatch(queries, maxHitsPerQuery);
    if (!hits) return Output::failure(hits.status());
    std::vector<unsigned> counts(queries.size(), 0);
    for (const auto& hit : hits.value())
        if (hit.distance < 0.f) {
            impl_->state[hit.particleIndex].color = overlapColors[hit.queryIndex];
            ++counts[hit.queryIndex];
        }
    return Output::success(std::move(counts));
}

Result<std::vector<VolumeFluidSimplexHit>> VolumeFluid::querySimplexes(std::span<const VolumeFluidQueryShape> queries,
                                                                       unsigned maxHitsPerQuery) const {
    return detail::querySimplexes(impl_->state, impl_->simplexes, queries, maxHitsPerQuery);
}

}  // namespace eve::fluids
