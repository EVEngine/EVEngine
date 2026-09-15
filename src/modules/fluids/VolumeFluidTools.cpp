#include "fluids/VolumeFluidInternal.inc"

namespace eve::fluids {
Result<void> VolumeFluid::setSimplexes(std::span<const VolumeFluidSimplex> simplexes) {
    if (simplexes.size() > 65536) return invalid("Too many fluid simplexes");
    for (const auto& simplex : simplexes) {
        if (simplex.size < 1 || simplex.size > 3) return invalid("Simplex size must be 1, 2 or 3");
        for (unsigned i = 0; i < simplex.size; ++i) {
            if (simplex.particleIndices[i] >= impl_->state.size())
                return invalid("Simplex particle index is out of range");
            for (unsigned j = 0; j < i; ++j)
                if (simplex.particleIndices[i] == simplex.particleIndices[j])
                    return invalid("Simplex particle indices must be distinct");
        }
    }
    impl_->simplexes.assign(simplexes.begin(), simplexes.end());
    return Result<void>::success();
}

Result<void> VolumeFluid::paint(glm::vec3 center, float radius, const VolumeFluidMaterial& material) {
    if (!finite(center) || !range(radius, 0.f, 10000.f) || !validMaterial(material))
        return invalid("Invalid material brush");
    for (auto& p : impl_->state)
        if (glm::length(p.position - center) <= radius) {
            p.material = material;
            if (material.phase == VolumeFluidPhase::Solid) {
                p.velocity        = glm::vec3(0.f);
                p.angularVelocity = glm::vec3(0.f);
            }
        }
    std::erase_if(impl_->attachments, [&](const auto& a) {
        return impl_->state[a.particleIndex].material.phase != VolumeFluidPhase::Solid;
    });
    return Result<void>::success();
}

Result<void> VolumeFluid::applyMaterialChannels(std::span<const VolumeFluidViscosityColorKey> colorKeys) {
    if (!colorKeys.empty()) {
        if (colorKeys.size() < 2 || colorKeys.size() > 32) return invalid("Viscosity gradient requires 2..32 keys");
        float previous = -1.f;
        for (const auto& key : colorKeys) {
            if (!range(key.viscosity, 0.f, 100.f) || key.viscosity <= previous || !range(key.color.x, 0.f, 1.f) ||
                !range(key.color.y, 0.f, 1.f) || !range(key.color.z, 0.f, 1.f) || !range(key.color.w, 0.f, 1.f))
                return invalid("Invalid viscosity gradient coordinate or color");
            previous = key.viscosity;
        }
    }
    for (const auto& particle : impl_->state)
        if (!range(particle.data.x, 0.f, 100.f) || !range(particle.data.y, 0.f, 10.f))
            return invalid("Material channels exceed viscosity/cohesion ranges");
    for (auto& particle : impl_->state) {
        if (!colorKeys.empty()) {
            const float viscosity = particle.material.viscosity;
            const auto  upper = std::upper_bound(colorKeys.begin(), colorKeys.end(), viscosity,
                                                 [](float value, const auto& key) { return value < key.viscosity; });
            if (upper == colorKeys.begin())
                particle.color = colorKeys.front().color;
            else if (upper == colorKeys.end())
                particle.color = colorKeys.back().color;
            else {
                const auto& lower = *(upper - 1);
                const float t     = (viscosity - lower.viscosity) / (upper->viscosity - lower.viscosity);
                particle.color    = lower.color * (1.f - t) + upper->color * t;
            }
        }
        particle.material.viscosity = particle.data.x;
        particle.material.cohesion  = particle.data.y;
    }
    return Result<void>::success();
}

Result<void> VolumeFluid::applySolidColor(glm::vec4 color) {
    if (!range(color.x, 0.f, 1.f) || !range(color.y, 0.f, 1.f) || !range(color.z, 0.f, 1.f) ||
        !range(color.w, 0.f, 1.f))
        return invalid("Invalid solid color");
    for (auto& particle : impl_->state)
        if (particle.material.phase == VolumeFluidPhase::Solid) particle.color = color;
    return Result<void>::success();
}

Result<void> VolumeFluid::applyVelocityColors(float sensibility) {
    if (!range(sensibility, 1e-6f, 1000.f)) return invalid("Invalid velocity color sensitivity");
    for (auto& particle : impl_->state) {
        const auto rgb = glm::clamp(particle.velocity / sensibility, glm::vec3(-1.f), glm::vec3(1.f)) * .5f + .5f;
        particle.color = {rgb, 1.f};
    }
    return Result<void>::success();
}

Result<void> VolumeFluid::applyActorGroupColors() {
    static const std::array<glm::vec4, 26> alphabet = {{{240 / 255.f, 163 / 255.f, 1, 1},
                                                        {0, 117 / 255.f, 220 / 255.f, 1},
                                                        {153 / 255.f, 63 / 255.f, 0, 1},
                                                        {76 / 255.f, 0, 92 / 255.f, 1},
                                                        {25 / 255.f, 25 / 255.f, 25 / 255.f, 1},
                                                        {0, 92 / 255.f, 49 / 255.f, 1},
                                                        {43 / 255.f, 206 / 255.f, 72 / 255.f, 1},
                                                        {1, 204 / 255.f, 153 / 255.f, 1},
                                                        {128 / 255.f, 128 / 255.f, 128 / 255.f, 1},
                                                        {148 / 255.f, 1, 181 / 255.f, 1},
                                                        {143 / 255.f, 124 / 255.f, 0, 1},
                                                        {157 / 255.f, 204 / 255.f, 0, 1},
                                                        {194 / 255.f, 0, 136 / 255.f, 1},
                                                        {0, 51 / 255.f, 128 / 255.f, 1},
                                                        {1, 164 / 255.f, 5 / 255.f, 1},
                                                        {1, 168 / 255.f, 187 / 255.f, 1},
                                                        {66 / 255.f, 102 / 255.f, 0, 1},
                                                        {1, 0, 16 / 255.f, 1},
                                                        {94 / 255.f, 241 / 255.f, 242 / 255.f, 1},
                                                        {0, 153 / 255.f, 143 / 255.f, 1},
                                                        {224 / 255.f, 1, 102 / 255.f, 1},
                                                        {116 / 255.f, 10 / 255.f, 1, 1},
                                                        {153 / 255.f, 0, 0, 1},
                                                        {1, 1, 128 / 255.f, 1},
                                                        {1, 1, 0, 1},
                                                        {1, 80 / 255.f, 5 / 255.f, 1}}};
    for (auto& particle : impl_->state) particle.color = alphabet[particle.actorGroup % alphabet.size()];
    return Result<void>::success();
}

namespace {
bool validColorGradient(std::span<const VolumeFluidColorKey> gradient) {
    if (gradient.size() < 2 || gradient.size() > 32) return false;
    float previous = -1.f;
    for (const auto& key : gradient) {
        if (!range(key.coordinate, 0.f, 1.f) || key.coordinate <= previous || !range(key.color.x, 0.f, 1.f) ||
            !range(key.color.y, 0.f, 1.f) || !range(key.color.z, 0.f, 1.f) || !range(key.color.w, 0.f, 1.f))
            return false;
        previous = key.coordinate;
    }
    return true;
}
glm::vec4 sampleColorGradient(std::span<const VolumeFluidColorKey> gradient, float value) {
    const auto upper = std::upper_bound(gradient.begin(), gradient.end(), value,
                                        [](float coordinate, const auto& key) { return coordinate < key.coordinate; });
    if (upper == gradient.begin()) return gradient.front().color;
    if (upper == gradient.end()) return gradient.back().color;
    const auto& lower = *(upper - 1);
    const float t     = (value - lower.coordinate) / (upper->coordinate - lower.coordinate);
    return lower.color * (1.f - t) + upper->color * t;
}
float colorRandom(uint32_t& state) {
    state += 0x9e3779b9u;
    uint32_t x = state;
    x          = (x ^ (x >> 16u)) * 0x85ebca6bu;
    x          = (x ^ (x >> 13u)) * 0xc2b2ae35u;
    x ^= x >> 16u;
    return float(x >> 8u) * (1.f / 16777216.f);
}
}  // namespace

Result<void> VolumeFluid::applyDataColors(unsigned channel, std::span<const VolumeFluidColorKey> gradient) {
    if (channel > 3 || !validColorGradient(gradient)) return invalid("Invalid data color channel or gradient");
    for (auto& particle : impl_->state) particle.color = sampleColorGradient(gradient, particle.data[channel]);
    return Result<void>::success();
}

Result<void> VolumeFluid::applyRandomColors(std::span<const VolumeFluidColorKey> gradient, uint32_t seed) {
    if (!validColorGradient(gradient)) return invalid("Invalid random color gradient");
    for (auto& particle : impl_->state) particle.color = sampleColorGradient(gradient, colorRandom(seed));
    return Result<void>::success();
}

Result<void> VolumeFluid::addRandomVelocity(float intensity, uint32_t seed) {
    if (!range(intensity, 0.f, 100.f)) return invalid("Invalid random velocity intensity");
    const float     z      = colorRandom(seed) * 2.f - 1.f;
    const float     phi    = colorRandom(seed) * glm::two_pi<float>();
    const float     radial = std::sqrt(std::max(0.f, 1.f - z * z));
    const glm::vec3 impulse(radial * std::cos(phi), radial * std::sin(phi), z);
    for (const auto& particle : impl_->state)
        if (glm::length(particle.velocity + impulse * intensity) > 100.f)
            return invalid("Random velocity impulse exceeds particle speed limit");
    for (auto& particle : impl_->state) particle.velocity += impulse * intensity;
    return Result<void>::success();
}

Result<void> VolumeFluid::teleportActor(glm::vec3 currentPosition, glm::vec4 currentRotation, glm::vec3 targetPosition,
                                        glm::vec4 targetRotation) {
    const auto validPose = [](glm::vec3 position, glm::vec4 rotation) {
        return finite(position) && finite(rotation) && std::abs(glm::dot(rotation, rotation) - 1.f) <= .001f;
    };
    if (!validPose(currentPosition, currentRotation) || !validPose(targetPosition, targetRotation))
        return invalid("Invalid actor teleport pose");
    const glm::quat from(currentRotation.w, currentRotation.x, currentRotation.y, currentRotation.z);
    const glm::quat to(targetRotation.w, targetRotation.x, targetRotation.y, targetRotation.z);
    const auto      rotation = glm::normalize(to) * glm::conjugate(glm::normalize(from));
    for (const auto& particle : impl_->state) {
        const auto position = targetPosition + rotation * (particle.position - currentPosition);
        if (!finite(position) || glm::any(glm::notEqual(impl_->constrain(position), position)))
            return invalid("Actor teleport moves a particle outside solver bounds");
    }
    for (auto& particle : impl_->state) {
        particle.position = targetPosition + rotation * (particle.position - currentPosition);
        const glm::quat orientation(particle.orientation.w, particle.orientation.x, particle.orientation.y,
                                    particle.orientation.z);
        const auto      transformed = glm::normalize(rotation * orientation);
        particle.orientation        = {transformed.x, transformed.y, transformed.z, transformed.w};
        particle.velocity           = glm::vec3(0.f);
        particle.angularVelocity    = glm::vec3(0.f);
    }
    impl_->renderPrevious.resize(impl_->state.size());
    for (size_t i = 0; i < impl_->state.size(); ++i) impl_->renderPrevious[i] = impl_->state[i].position;
    impl_->contacts.clear();
    impl_->attachmentReactions.clear();
    impl_->attachmentReactionScratch.clear();
    std::fill(impl_->grabberLabels.begin(), impl_->grabberLabels.end(), Impl::noGrabber);
    return Result<void>::success();
}

Result<VolumeFluidMassProperties> VolumeFluid::actorMassProperties(unsigned actorGroup) const {
    if (actorGroup > 0x00ffffffu)
        return Result<VolumeFluidMassProperties>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Actor group exceeds 24-bit range", "fluids.volume.actorMass"));
    double     mass = 0.0;
    glm::dvec3 weightedPosition(0.0);
    unsigned   count = 0;
    for (const auto& particle : impl_->state) {
        if (particle.actorGroup != actorGroup) continue;
        const double particleMass = double(impl_->volume) * double(particle.material.density);
        mass += particleMass;
        weightedPosition += glm::dvec3(particle.position) * particleMass;
        ++count;
    }
    if (count == 0)
        return Result<VolumeFluidMassProperties>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Actor group has no live particles", "fluids.volume.actorMass"));
    VolumeFluidMassProperties properties;
    properties.mass          = float(mass);
    properties.centerOfMass  = glm::vec3(weightedPosition / mass);
    properties.particleCount = count;
    return Result<VolumeFluidMassProperties>::success(properties);
}

Result<void> VolumeFluid::setActorFilterCategory(unsigned actorGroup, unsigned category) {
    if (actorGroup > 0x00ffffffu || category > 15u) return invalid("Invalid actor group or filter category");
    if (std::none_of(impl_->state.begin(), impl_->state.end(),
                     [&](const auto& particle) { return particle.actorGroup == actorGroup; }))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Actor group has no live particles", "fluids.volume.filter"));
    const unsigned categoryBit = 1u << category;
    for (auto& particle : impl_->state)
        if (particle.actorGroup == actorGroup)
            particle.collisionFilter = (particle.collisionFilter & 0xffff0000u) | categoryBit;
    return Result<void>::success();
}

Result<void> VolumeFluid::setActorCollisionFilter(unsigned actorGroup, unsigned collisionFilter) {
    if (actorGroup > 0x00ffffffu || !validFilter(collisionFilter))
        return invalid("Invalid actor group or collision filter");
    if (std::none_of(impl_->state.begin(), impl_->state.end(),
                     [&](const auto& particle) { return particle.actorGroup == actorGroup; }))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Actor group has no live particles", "fluids.volume.filter"));
    for (auto& particle : impl_->state)
        if (particle.actorGroup == actorGroup) particle.collisionFilter = collisionFilter;
    return Result<void>::success();
}

Result<void> VolumeFluid::updateActorMaterial(unsigned actorGroup, const VolumeFluidParticle& prototype) {
    const bool automaticRadii = glm::all(glm::equal(prototype.radii, glm::vec3(0.f)));
    const bool explicitRadii  = finite(prototype.radii) &&
                                glm::all(glm::greaterThanEqual(prototype.radii, glm::vec3(.0005f))) &&
                                glm::all(glm::lessThanEqual(prototype.radii, glm::vec3(10.f)));
    if (actorGroup > 0x00ffffffu || !validMaterial(prototype.material) || !finite(prototype.data) ||
        (!automaticRadii && !explicitRadii) || !validFilter(prototype.collisionFilter))
        return invalid("Invalid actor group or emitter material prototype");
    if (std::none_of(impl_->state.begin(), impl_->state.end(),
                     [&](const auto& particle) { return particle.actorGroup == actorGroup; }))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Actor group has no live particles",
                                                       "fluids.volume.actorMaterial"));
    const auto radii = glm::all(glm::equal(prototype.radii, glm::vec3(0.f))) ? glm::vec3(impl_->settings.spacing * .5f)
                                                                             : prototype.radii;
    for (auto& particle : impl_->state) {
        if (particle.actorGroup != actorGroup) continue;
        particle.material        = prototype.material;
        particle.data            = prototype.data;
        particle.radii           = radii;
        particle.collisionFilter = prototype.collisionFilter;
        particle.selfCollide     = prototype.selfCollide;
    }
    return Result<void>::success();
}

Result<void> VolumeFluid::setActorSelfCollisions(unsigned actorGroup, bool enabled) {
    if (actorGroup > 0x00ffffffu) return invalid("Invalid actor group");
    if (std::none_of(impl_->state.begin(), impl_->state.end(),
                     [&](const auto& particle) { return particle.actorGroup == actorGroup; }))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Actor group has no live particles",
                                                       "fluids.volume.selfCollisions"));
    for (auto& particle : impl_->state)
        if (particle.actorGroup == actorGroup) particle.selfCollide = enabled;
    return Result<void>::success();
}

Result<void> VolumeFluid::applyParticleDrag(unsigned particleIndex, glm::vec3 targetPosition, float stiffness,
                                            float damping, float seconds) {
    if (particleIndex >= impl_->state.size() || !finite(targetPosition) || !range(stiffness, 0.f, 10000.f) ||
        !range(damping, 0.f, 10000.f) || !range(seconds, std::numeric_limits<float>::min(), 1.f / 30.f))
        return invalid("Invalid particle drag input");
    auto&      particle     = impl_->state[particleIndex];
    const auto acceleration = (targetPosition - particle.position) * stiffness - particle.velocity * damping;
    const auto velocity     = particle.velocity + acceleration * seconds;
    if (!finite(velocity) || glm::length(velocity) > 1000.f) return invalid("Particle drag exceeds speed limit");
    particle.velocity = velocity;
    return Result<void>::success();
}

Result<unsigned> VolumeFluid::grabContactParticles(unsigned colliderLabel, glm::vec3 position, glm::vec4 rotation,
                                                   float distanceThreshold) {
    const auto failure = [](const char* message) {
        return Result<unsigned>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.contactGrabber"));
    };
    if (!finite(position) || !finite(rotation) || std::abs(glm::dot(rotation, rotation) - 1.f) > .001f ||
        !range(distanceThreshold, 0.f, .1f))
        return failure("Invalid contact-grabber pose or distance threshold");
    if (!impl_->hasColliderLabel(colliderLabel)) return failure("Unknown contact-grabber collider label");

    impl_->grabberSelection.assign(impl_->state.size(), uint8_t(0));
    for (const auto& contact : impl_->contacts) {
        if (contact.colliderLabel != colliderLabel || contact.distance >= distanceThreshold ||
            contact.particleIndex >= impl_->state.size())
            continue;
        if (impl_->state[contact.particleIndex].material.phase != VolumeFluidPhase::Solid)
            impl_->grabberSelection[contact.particleIndex] = 1;
    }
    for (size_t i = 0; i < impl_->state.size(); ++i)
        if (impl_->grabberSelection[i] && impl_->grabberLabels[i] != Impl::noGrabber &&
            impl_->grabberLabels[i] != int64_t(colliderLabel))
            return failure("Particle is already owned by another contact grabber");

    const auto q               = glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
    const auto inverseRotation = glm::transpose(glm::mat3_cast(q));
    impl_->grabberLabelScratch = impl_->grabberLabels;
    impl_->grabberLocalScratch = impl_->grabberLocalPositions;
    unsigned count             = 0;
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        if (impl_->grabberLabelScratch[i] == int64_t(colliderLabel)) impl_->grabberLabelScratch[i] = Impl::noGrabber;
        if (!impl_->grabberSelection[i]) continue;
        const auto local = inverseRotation * (impl_->state[i].position - position);
        if (!finite(local)) return failure("Nonfinite contact-grabber local position");
        impl_->grabberLabelScratch[i] = int64_t(colliderLabel);
        impl_->grabberLocalScratch[i] = local;
        ++count;
    }
    impl_->grabberLabels.swap(impl_->grabberLabelScratch);
    impl_->grabberLocalPositions.swap(impl_->grabberLocalScratch);
    for (size_t i = 0; i < impl_->state.size(); ++i)
        if (impl_->grabberLabels[i] == int64_t(colliderLabel)) {
            impl_->state[i].velocity        = glm::vec3(0.f);
            impl_->state[i].angularVelocity = glm::vec3(0.f);
        }
    return Result<unsigned>::success(count);
}

Result<unsigned> VolumeFluid::updateGrabbedParticles(unsigned colliderLabel, glm::vec3 position, glm::vec4 rotation) {
    const auto failure = [](const char* message) {
        return Result<unsigned>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.contactGrabber"));
    };
    if (!finite(position) || !finite(rotation) || std::abs(glm::dot(rotation, rotation) - 1.f) > .001f)
        return failure("Invalid contact-grabber pose");
    const auto q         = glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
    const auto transform = glm::mat3_cast(q);
    unsigned   count     = 0;
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        if (impl_->grabberLabels[i] != int64_t(colliderLabel)) continue;
        const auto target = position + transform * impl_->grabberLocalPositions[i];
        if (!finite(target) || glm::any(glm::notEqual(impl_->constrain(target), target)))
            return failure("Contact-grabber target is outside solver bounds");
        ++count;
    }
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        if (impl_->grabberLabels[i] != int64_t(colliderLabel)) continue;
        const auto target               = position + transform * impl_->grabberLocalPositions[i];
        impl_->state[i].position        = target;
        impl_->state[i].velocity        = glm::vec3(0.f);
        impl_->state[i].angularVelocity = glm::vec3(0.f);
        impl_->renderPrevious[i]        = target;
    }
    impl_->contacts.clear();
    return Result<unsigned>::success(count);
}

Result<unsigned> VolumeFluid::releaseGrabbedParticles(unsigned colliderLabel) {
    unsigned count = 0;
    for (auto& label : impl_->grabberLabels)
        if (label == int64_t(colliderLabel)) {
            label = Impl::noGrabber;
            ++count;
        }
    return Result<unsigned>::success(count);
}

Result<std::vector<VolumeFluidFieldSample>> VolumeFluid::sampleField(std::span<const glm::vec3> positions) const {
    std::vector<VolumeFluidFieldSample> samples;
    auto                                sampled = sampleFieldInto(positions, samples);
    if (!sampled) return Result<std::vector<VolumeFluidFieldSample>>::failure(sampled.status());
    return Result<std::vector<VolumeFluidFieldSample>>::success(std::move(samples));
}

Result<void> VolumeFluid::sampleFieldInto(std::span<const glm::vec3>           positions,
                                          std::vector<VolumeFluidFieldSample>& samples) const {
    const auto failure = [](const char* message) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.field"));
    };
    if (positions.size() > 65536 || std::any_of(positions.begin(), positions.end(), [](auto p) { return !finite(p); }))
        return failure("Invalid field query positions or count");
    samples.assign(positions.size(), VolumeFluidFieldSample{});
    if (positions.empty() || impl_->state.empty()) return Result<void>::success();
    impl_->grid();
    size_t      visits = 0;
    const float h2     = impl_->h * impl_->h;
    for (size_t i = 0; i < positions.size(); ++i) {
        const auto position = positions[i];
        if (glm::any(glm::lessThan(position, impl_->settings.minimum - impl_->h)) ||
            glm::any(glm::greaterThan(position, impl_->settings.maximum + impl_->h)))
            continue;
        const auto cell = impl_->cell(position);
        const auto low = glm::max(cell - 1, glm::ivec3(0)), high = glm::min(cell + 1, impl_->dims - 1);
        auto&      sample    = samples[i];
        float      weightSum = 0.f;
        glm::vec3  gradientSum(0.f), curlSum(0.f), referenceVelocity(0.f);
        for (int z = low.z; z <= high.z; ++z)
            for (int y = low.y; y <= high.y; ++y)
                for (int x = low.x; x <= high.x; ++x) {
                    for (int j = impl_->heads[impl_->index({x, y, z})]; j >= 0; j = impl_->next[size_t(j)]) {
                        if (++visits > 4000000) return failure("Field query candidate budget exceeded");
                        const auto& particle = impl_->state[size_t(j)];
                        if (!fluid(particle.material)) continue;
                        const auto  dx = position - particle.position;
                        const float r2 = glm::dot(dx, dx);
                        if (r2 >= h2) continue;
                        const float q        = 1.f - r2 / h2;
                        const float weight   = impl_->volume * impl_->kernel(r2);
                        const auto  gradient = dx * (-6.f * impl_->volume * impl_->selfKernel * q * q / h2);
                        if (sample.neighborCount == 0) referenceVelocity = particle.velocity;
                        ++sample.neighborCount;
                        const auto relativeVelocity = particle.velocity - referenceVelocity;
                        sample.density += particle.material.density * weight;
                        sample.velocity += relativeVelocity * weight;
                        gradientSum += gradient;
                        curlSum += glm::cross(gradient, relativeVelocity);
                        weightSum += weight;
                    }
                }
        if (weightSum > 1e-12f) {
            sample.velocity /= weightSum;
            sample.vorticity = (curlSum - glm::cross(gradientSum, sample.velocity)) / weightSum;
            sample.velocity += referenceVelocity;
        } else {
            sample.velocity = sample.vorticity = glm::vec3(0.f);
        }
    }
    return Result<void>::success();
}

Result<std::vector<VolumeFluidGridCell>> VolumeFluid::debugParticleGrid(unsigned maxCells) const {
    if (maxCells == 0 || maxCells > 1000000)
        return Result<std::vector<VolumeFluidGridCell>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid particle-grid debug cell budget", "fluids.volume.gridDebug"));
    auto& indices = impl_->debugCellIndices;
    indices.resize(impl_->state.size());
    for (size_t i = 0; i < impl_->state.size(); ++i) indices[i] = impl_->index(impl_->cell(impl_->state[i].position));
    std::sort(indices.begin(), indices.end());

    size_t occupied = 0;
    for (size_t i = 0; i < indices.size();) {
        const size_t cell = indices[i];
        while (i < indices.size() && indices[i] == cell) ++i;
        ++occupied;
    }
    if (occupied > maxCells)
        return Result<std::vector<VolumeFluidGridCell>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Particle-grid debug cell budget exceeded", "fluids.volume.gridDebug"));

    std::vector<VolumeFluidGridCell> cells;
    cells.reserve(occupied);
    for (size_t i = 0; i < indices.size();) {
        const size_t linear = indices[i];
        size_t       end    = i + 1;
        while (end < indices.size() && indices[end] == linear) ++end;
        const int    x      = int(linear % size_t(impl_->dims.x));
        const size_t yz     = linear / size_t(impl_->dims.x);
        const int    y      = int(yz % size_t(impl_->dims.y));
        const int    z      = int(yz / size_t(impl_->dims.y));
        const auto   center = impl_->settings.minimum + (glm::vec3(x, y, z) + .5f) * impl_->h;
        cells.push_back({center, glm::vec3(impl_->h), unsigned(end - i)});
        i = end;
    }
    return Result<std::vector<VolumeFluidGridCell>>::success(std::move(cells));
}

Result<std::vector<VolumeFluidParticleFrame>> VolumeFluid::debugParticleFrames(unsigned actorGroup, float size,
                                                                               unsigned maxParticles) const {
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<std::vector<VolumeFluidParticleFrame>>::failure(
            Diagnostic::error(code, message, "fluids.volume.frameDebug"));
    };
    if (actorGroup > 0x00ffffffu || !range(size, std::numeric_limits<float>::min(), 100.f) || maxParticles == 0 ||
        maxParticles > 1000000)
        return failure(DiagnosticCode::InvalidArgument, "Invalid particle-frame debug input");
    const size_t count = size_t(std::count_if(impl_->state.begin(), impl_->state.end(),
                                              [&](const auto& p) { return p.actorGroup == actorGroup; }));
    if (count == 0) return failure(DiagnosticCode::NotFound, "Actor group has no live particles");
    if (count > maxParticles) return failure(DiagnosticCode::InvalidArgument, "Particle-frame debug budget exceeded");
    std::vector<VolumeFluidParticleFrame> frames;
    frames.reserve(count);
    for (size_t i = 0; i < impl_->state.size(); ++i) {
        const auto& particle = impl_->state[i];
        if (particle.actorGroup != actorGroup) continue;
        const auto rotation = glm::mat3_cast(particleQuaternion(particle));
        frames.push_back({unsigned(i), particle.position, particle.position + rotation[0] * size,
                          particle.position + rotation[1] * size, particle.position + rotation[2] * size});
    }
    return Result<std::vector<VolumeFluidParticleFrame>>::success(std::move(frames));
}

Result<std::vector<VolumeFluidParticleInstance>> VolumeFluid::particleInstances(unsigned  actorGroup,
                                                                                glm::vec3 instanceScale, float alpha,
                                                                                unsigned maxInstances) const {
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<std::vector<VolumeFluidParticleInstance>>::failure(
            Diagnostic::error(code, message, "fluids.volume.particleInstances"));
    };
    if (actorGroup > 0x00ffffffu || !finite(instanceScale) ||
        glm::any(glm::lessThanEqual(instanceScale, glm::vec3(0.f))) ||
        glm::any(glm::greaterThan(instanceScale, glm::vec3(10000.f))) || !range(alpha, 0.f, 1.f) || maxInstances == 0 ||
        maxInstances > 1000000)
        return failure(DiagnosticCode::InvalidArgument, "Invalid particle-instance input");
    const size_t count = size_t(std::count_if(impl_->state.begin(), impl_->state.end(),
                                              [&](const auto& particle) { return particle.actorGroup == actorGroup; }));
    if (count == 0) return failure(DiagnosticCode::NotFound, "Particle-instance actor group was not found");
    if (count > maxInstances)
        return failure(DiagnosticCode::InvalidArgument, "Particle-instance output budget exceeded");
    std::vector<VolumeFluidParticleInstance> instances;
    instances.reserve(count);
    for (size_t index = 0; index < impl_->state.size(); ++index) {
        const auto& particle = impl_->state[index];
        if (particle.actorGroup != actorGroup) continue;
        instances.push_back({unsigned(index), interpolatedPositionUnchecked(index, alpha), particle.orientation,
                             particle.radii * instanceScale, particle.color});
    }
    return Result<std::vector<VolumeFluidParticleInstance>>::success(std::move(instances));
}

Result<std::vector<VolumeFluidParticleInstance>> VolumeFluid::particleImpostors(unsigned actorGroup, float radiusScale,
                                                                                glm::vec4 tint, float alpha,
                                                                                unsigned maxInstances) const {
    std::vector<VolumeFluidParticleInstance> instances;
    auto copied = copyParticleImpostors(actorGroup, radiusScale, tint, alpha, maxInstances, instances);
    if (!copied) return Result<std::vector<VolumeFluidParticleInstance>>::failure(copied.status());
    return Result<std::vector<VolumeFluidParticleInstance>>::success(std::move(instances));
}

Result<void> VolumeFluid::copyParticleImpostors(unsigned actorGroup, float radiusScale, glm::vec4 tint, float alpha,
                                                unsigned                                  maxInstances,
                                                std::vector<VolumeFluidParticleInstance>& instances) const {
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<void>::failure(Diagnostic::error(code, message, "fluids.volume.particleImpostors"));
    };
    if (actorGroup > 0x00ffffffu || !range(radiusScale, std::numeric_limits<float>::min(), 10000.f) || !finite(tint) ||
        glm::any(glm::lessThan(tint, glm::vec4(0.f))) || glm::any(glm::greaterThan(tint, glm::vec4(1.f))) ||
        !range(alpha, 0.f, 1.f) || maxInstances == 0 || maxInstances > 1000000)
        return failure(DiagnosticCode::InvalidArgument, "Invalid particle-impostor input");
    const size_t count = size_t(std::count_if(impl_->state.begin(), impl_->state.end(),
                                              [&](const auto& particle) { return particle.actorGroup == actorGroup; }));
    if (count == 0) return failure(DiagnosticCode::NotFound, "Particle-impostor actor group was not found");
    if (count > maxInstances)
        return failure(DiagnosticCode::InvalidArgument, "Particle-impostor output budget exceeded");
    instances.resize(count);
    size_t outputIndex = 0;
    for (size_t index = 0; index < impl_->state.size(); ++index) {
        const auto& particle = impl_->state[index];
        if (particle.actorGroup != actorGroup) continue;
        instances[outputIndex++] = {unsigned(index), interpolatedPositionUnchecked(index, alpha), particle.orientation,
                                    particle.radii * radiusScale, particle.color * tint};
    }
    return Result<void>::success();
}

Result<VolumeFluidSdfSlice> VolumeFluid::debugSdfSlice(unsigned colliderLabel, VolumeFluidSdfSliceAxis axis,
                                                       float slice, float maxDistance, unsigned maxSamples) const {
    const auto failure = [](DiagnosticCode code, const char* message) {
        return Result<VolumeFluidSdfSlice>::failure(Diagnostic::error(code, message, "fluids.volume.sdfSliceDebug"));
    };
    const int axisIndex = int(axis);
    if (axisIndex < 0 || axisIndex > 2 || !range(slice, 0.f, 1.f) ||
        !range(maxDistance, std::numeric_limits<float>::min(), 10000.f) || maxSamples == 0 || maxSamples > 1000000)
        return failure(DiagnosticCode::InvalidArgument, "Invalid SDF slice debug input");
    const auto found = std::find_if(impl_->sdfColliders.begin(), impl_->sdfColliders.end(),
                                    [&](const auto& collider) { return collider.label == colliderLabel; });
    if (found == impl_->sdfColliders.end())
        return failure(DiagnosticCode::NotFound, "SDF collider label was not found");

    const auto& collider = *found;
    const auto& sdf      = collider.sdf;
    const int   uAxis    = axis == VolumeFluidSdfSliceAxis::X ? 2 : 0;
    const int   vAxis    = axis == VolumeFluidSdfSliceAxis::Y ? 2 : 1;
    const auto  width    = unsigned(sdf.dims[uAxis]);
    const auto  height   = unsigned(sdf.dims[vAxis]);
    if (uint64_t(width) * uint64_t(height) > maxSamples)
        return failure(DiagnosticCode::InvalidArgument, "SDF slice sample budget exceeded");

    glm::vec3 localOrigin = sdf.origin;
    localOrigin[axisIndex] += float(sdf.dims[axisIndex] - 1) * sdf.cellSize * slice;
    glm::vec3 localStepX(0.f), localStepY(0.f);
    localStepX[uAxis]   = sdf.cellSize;
    localStepY[vAxis]   = sdf.cellSize;
    const auto rotation = impl_->sdfColliderRotations[size_t(found - impl_->sdfColliders.begin())];

    VolumeFluidSdfSlice output;
    output.width  = width;
    output.height = height;
    output.origin = collider.position + rotation * (localOrigin * collider.scale);
    output.stepX  = rotation * (localStepX * collider.scale);
    output.stepY  = rotation * (localStepY * collider.scale);
    output.values.resize(size_t(width) * size_t(height));
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const glm::vec3 local                = localOrigin + localStepX * float(x) + localStepY * float(y);
            const float     normalized           = .5f + .5f * sdf.sample(local) / maxDistance;
            output.values[size_t(y) * width + x] = std::clamp(normalized, 0.f, 1.f);
        }
    return Result<VolumeFluidSdfSlice>::success(std::move(output));
}

VolumeFluidSnapshot VolumeFluid::snapshot() const {
    return {"eve.volume-fluid",
            20,
            impl_->settings,
            impl_->state,
            impl_->colliders,
            impl_->sdfColliders,
            impl_->heightFieldColliders,
            impl_->attachments,
            impl_->stitches,
            impl_->simplexes};
}

Result<void> VolumeFluid::restore(const VolumeFluidSnapshot& snapshot) {
    if (snapshot.schema != "eve.volume-fluid" || snapshot.version != 20)
        return invalid("Unsupported volume-fluid schema/version");
    auto candidate = create(snapshot.settings);
    if (!candidate) return Result<void>::failure(candidate.status());
    auto replacement = std::move(candidate).takeValue();
    auto particles   = replacement->emit(snapshot.particles);
    if (!particles) return particles;
    auto colliders = replacement->setColliders(snapshot.colliders);
    if (!colliders) return colliders;
    auto sdfColliders = replacement->setSdfColliders(snapshot.sdfColliders);
    if (!sdfColliders) return sdfColliders;
    auto heightFields = replacement->setHeightFieldColliders(snapshot.heightFieldColliders);
    if (!heightFields) return heightFields;
    if (snapshot.attachments.size() > snapshot.particles.size()) return invalid("Too many particle attachments");
    std::vector<bool>     attached(snapshot.particles.size(), false);
    std::vector<unsigned> labels;
    labels.reserve(snapshot.colliders.size());
    for (const auto& c : snapshot.colliders) labels.push_back(c.label);
    std::sort(labels.begin(), labels.end());
    for (const auto& a : snapshot.attachments) {
        if (a.particleIndex >= snapshot.particles.size() || attached[a.particleIndex] || !finite(a.localPosition) ||
            !finite(a.localOrientation) || std::abs(glm::dot(a.localOrientation, a.localOrientation) - 1.f) > .001f ||
            !range(a.compliance, 0.f, 1000000.f) || !range(a.breakThreshold, 1e-6f, 1e12f) ||
            !std::binary_search(labels.begin(), labels.end(), a.colliderLabel))
            return invalid("Invalid particle attachment reference");
        attached[a.particleIndex] = true;
    }
    replacement->impl_->attachments = snapshot.attachments;
    auto stitches                   = replacement->setStitches(snapshot.stitches);
    if (!stitches) return stitches;
    auto topology = replacement->setSimplexes(snapshot.simplexes);
    if (!topology) return topology;
    impl_.swap(replacement->impl_);
    return Result<void>::success();
}

}  // namespace eve::fluids
