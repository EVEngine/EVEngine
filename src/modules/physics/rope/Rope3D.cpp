#include "physics/rope/Rope3D.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::physics {
namespace {
using V = Rope3D::Vec3;
V     add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V     sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V     mul(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(V v) { return std::sqrt(dot(v, v)); }
bool  finite(V v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace

Rope3D::Rope3D(int count, float sx, float sy, float sz, float ex, float ey, float ez) {
    const Vec3 start{sx, sy, sz};
    const Vec3 end{ex, ey, ez};
    if (count < 2 || !finite(start) || !finite(end) || length(sub(end, start)) <= 1e-6f)
        throw Exception("Rope3D: requires at least two particles and distinct finite endpoints");
    particles_.resize(static_cast<std::size_t>(count));
    elements_.resize(static_cast<std::size_t>(count - 1));
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        auto&       p = particles_[static_cast<std::size_t>(i)];
        p.position = p.previous = add(start, mul(sub(end, start), t));
        if (i + 1 < count) {
            auto& e      = elements_[static_cast<std::size_t>(i)];
            e.a          = i;
            e.b          = i + 1;
            e.restLength = length(sub(end, start)) / static_cast<float>(count - 1);
        }
    }
    syncBendState();
}

void Rope3D::update(float dt) {
    if (!std::isfinite(dt) || dt < 0.f) throw Exception("Rope3D: dt must be finite and non-negative");
    if (dt == 0.f) return;
    lastTornElements_.clear();
    dt                     = std::clamp(dt, 0.f, 0.05f);
    constexpr int substeps = 4;
    for (int substep = 0; substep < substeps; ++substep) {
        const float h = dt / static_cast<float>(substeps);
        for (auto& element : elements_) element.lambda = 0.f;
        integrate(h);
        collideBorrowedSurfaces(h);
        if (bendConstraintsEnabled_) updatePlasticity(h);
        for (int iteration = 0; iteration < 3; ++iteration) {
            if (distanceConstraintsEnabled_) solveStretch(h);
            if (bendConstraintsEnabled_) solveBend(h);
            if (selfCollision_) solveSelfCollision();
            solveExternalCollisions();
            collideBounds();
            applyAttachments();
        }
    }
    applyTearing();
    force_ = {};
}

eve::Result<void> Rope3D::step(const eve::SimulationStep& tick, const SimulationSettings& settings) {
    auto validation = detail::validateSimulationStep(tick, settings, observation_);
    if (!validation) return validation;
    const float dt                  = static_cast<float>(tick.delta.seconds());
    const auto  oldParticles        = particles_;
    const auto  oldElements         = elements_;
    const auto  oldBendPlasticity   = bendPlasticity_;
    const auto  oldTopologyRevision = topologyRevision_;
    const auto  oldTornElements     = lastTornElements_;
    lastTornElements_.clear();
    for (int substep = 0; substep < settings.subStepCount; ++substep) {
        const float h = dt / static_cast<float>(settings.subStepCount);
        for (auto& element : elements_) element.lambda = 0.f;
        integrate(h);
        collideBorrowedSurfaces(h);
        if (bendConstraintsEnabled_) updatePlasticity(h);
        for (int iteration = 0; iteration < settings.positionIterations; ++iteration) {
            if (distanceConstraintsEnabled_) solveStretch(h);
            if (bendConstraintsEnabled_) solveBend(h);
            if (selfCollision_) solveSelfCollision();
            solveExternalCollisions();
            collideBounds();
            applyAttachments();
        }
    }
    applyTearing();
    force_    = {};
    auto next = detail::advanceSimulationObservation(observation_, tick);
    if (!next) {
        particles_        = oldParticles;
        elements_         = oldElements;
        bendPlasticity_   = oldBendPlasticity;
        topologyRevision_ = oldTopologyRevision;
        lastTornElements_ = oldTornElements;
        return eve::Result<void>::failure(next.status());
    }
    observation_ = std::move(next).takeValue();
    return eve::Result<void>::success();
}

eve::Result<void> Rope3D::restoreObservation(const SimulationObservation& observation) {
    auto result = detail::validateSimulationObservation(observation, "physics.rope3d.observation");
    if (!result) return result;
    observation_ = observation;
    return eve::Result<void>::success();
}

void Rope3D::setGravity(float x, float y, float z) {
    if (!finite({x, y, z})) throw Exception("Rope3D: gravity must be finite");
    gravity_ = {x, y, z};
}
void Rope3D::setStretchCompliance(float value) {
    if (!std::isfinite(value) || value < 0.f)
        throw Exception("Rope3D: stretch compliance must be finite and non-negative");
    stretchCompliance_ = value;
}
void Rope3D::setBendCompliance(float value) {
    if (!std::isfinite(value) || value < 0.f)
        throw Exception("Rope3D: bend compliance must be finite and non-negative");
    bendCompliance_ = value;
}
void Rope3D::setMaxBending(float value) {
    if (!std::isfinite(value) || value < 0.f || value > 0.5f) throw Exception("Rope3D: max bending must be in [0,0.5]");
    maxBending_ = value;
}
void Rope3D::setPlasticity(float yield, float creep) {
    if (!std::isfinite(yield) || yield < 0.f || yield > 0.5f || !std::isfinite(creep) || creep < 0.f)
        throw Exception("Rope3D: plastic yield must be in [0,0.5] and creep must be non-negative");
    plasticYield_ = yield;
    plasticCreep_ = creep;
}
float Rope3D::getBendPlasticity(int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < bendPlasticity_.size()
               ? bendPlasticity_[static_cast<std::size_t>(index)]
               : 0.f;
}
void Rope3D::setMaxCompression(float value) {
    if (!std::isfinite(value) || value < 0.f || value > 1.f)
        throw Exception("Rope3D: max compression must be in [0,1]");
    maxCompression_ = value;
}
void Rope3D::setDamping(float value) {
    if (!std::isfinite(value) || value < 0.f || value > 1.f) throw Exception("Rope3D: damping must be in [0,1]");
    damping_ = value;
}
void Rope3D::setParticleMass(float value) {
    if (!std::isfinite(value) || value <= 0.f) throw Exception("Rope3D: particle mass must be positive");
    particleMass_ = value;
}
void Rope3D::setRadius(float value) {
    if (!std::isfinite(value) || value <= 0.f) throw Exception("Rope3D: radius must be positive");
    radius_ = value;
}
void Rope3D::setBounds(float x, float y, float z, float w, float h, float d) {
    if (!finite({x, y, z}) || !finite({w, h, d}) || w <= 0.f || h <= 0.f || d <= 0.f)
        throw Exception("Rope3D: bounds extents must be finite and positive");
    boundsMin_ = {x, y, z};
    boundsMax_ = {x + w, y + h, z + d};
    hasBounds_ = true;
}

eve::Result<RopeTopologyChange> Rope3D::pin(int i) {
    if (!validParticle(i))
        return eve::Result<RopeTopologyChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Particle index is out of range", "physics.rope3d.pin"));
    const auto& p = particles_[static_cast<std::size_t>(i)];
    return attach(i, p.position.x, p.position.y, p.position.z);
}
eve::Result<RopeTopologyChange> Rope3D::attach(int i, float x, float y, float z) {
    if (!validParticle(i) || !finite({x, y, z}))
        return eve::Result<RopeTopologyChange>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "Invalid particle index or attachment position", "physics.rope3d.attach"));
    auto&      p       = particles_[static_cast<std::size_t>(i)];
    const bool changed = !p.attached || p.attachment.x != x || p.attachment.y != y || p.attachment.z != z;
    p.attached         = true;
    p.attachment = p.position = p.previous = {x, y, z};
    return eve::Result<RopeTopologyChange>::success(changed ? RopeTopologyChange::Changed
                                                            : RopeTopologyChange::Unchanged);
}
eve::Result<RopeTopologyChange> Rope3D::moveAttachment(int i, float x, float y, float z) {
    if (!validParticle(i) || !particles_[static_cast<std::size_t>(i)].attached || !finite({x, y, z}))
        return eve::Result<RopeTopologyChange>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "Attachment does not exist or target is invalid", "physics.rope3d.moveAttachment"));
    return attach(i, x, y, z);
}
eve::Result<RopeTopologyChange> Rope3D::detach(int i) {
    if (!validParticle(i))
        return eve::Result<RopeTopologyChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Particle index is out of range", "physics.rope3d.detach"));
    auto& p = particles_[static_cast<std::size_t>(i)];
    if (!p.attached) return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Unchanged);
    p.attached = false;
    p.previous = p.position;
    return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Changed);
}
bool Rope3D::isAttached(int i) const { return validParticle(i) && particles_[static_cast<std::size_t>(i)].attached; }
void Rope3D::applyForce(float x, float y, float z) {
    if (!finite({x, y, z})) throw Exception("Rope3D: force must be finite");
    force_ = add(force_, {x, y, z});
}

eve::Result<RopeTopologyChange> Rope3D::setRestLength(float requested) {
    const float current = getRestLength();
    if (!std::isfinite(requested) || requested <= 0.f || current <= 0.f)
        return eve::Result<RopeTopologyChange>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Rest length must be finite and positive",
                                   "physics.rope3d.setRestLength"));
    if (std::abs(requested - current) <= 1e-6f)
        return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Unchanged);
    const float scale = requested / current;
    for (auto& e : elements_)
        if (e.active) e.restLength *= scale;
    ++topologyRevision_;
    return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Changed);
}
float Rope3D::getRestLength() const {
    float sum = 0.f;
    for (const auto& e : elements_)
        if (e.active) sum += e.restLength;
    return sum;
}
float Rope3D::calculateLength() const {
    float sum = 0.f;
    for (const auto& e : elements_)
        if (e.active) sum += length(sub(particles_[e.b].position, particles_[e.a].position));
    return sum;
}

eve::Result<RopeTopologyChange> Rope3D::cut(int i) {
    if (!validElement(i))
        return eve::Result<RopeTopologyChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Element index is out of range", "physics.rope3d.cut"));
    auto& e = elements_[static_cast<std::size_t>(i)];
    if (!e.active) return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Unchanged);
    e.active = false;
    e.lambda = 0.f;
    e.force  = 0.f;
    ++topologyRevision_;
    return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Changed);
}
eve::Result<RopeTopologyChange> Rope3D::repair(int i) {
    if (!validElement(i))
        return eve::Result<RopeTopologyChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Element index is out of range", "physics.rope3d.repair"));
    auto& e = elements_[static_cast<std::size_t>(i)];
    if (e.active) return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Unchanged);
    e.restLength = std::max(length(sub(particles_[e.b].position, particles_[e.a].position)), 1e-5f);
    e.active     = true;
    ++topologyRevision_;
    return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Changed);
}
bool  Rope3D::isElementActive(int i) const { return validElement(i) && elements_[static_cast<std::size_t>(i)].active; }
float Rope3D::getElementForce(int i) const {
    return validElement(i) ? elements_[static_cast<std::size_t>(i)].force : 0.f;
}
eve::Result<RopeTopologyChange> Rope3D::setElementTearResistance(int i, float multiplier) {
    if (!validElement(i) || !std::isfinite(multiplier) || multiplier <= 0.f)
        return eve::Result<RopeTopologyChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Element index and tearing multiplier must be valid",
            "physics.rope3d.setElementTearResistance"));
    auto& element = elements_[static_cast<std::size_t>(i)];
    if (element.tearResistance == multiplier)
        return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Unchanged);
    element.tearResistance = multiplier;
    return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Changed);
}
void Rope3D::setTearing(float resistance, int maxTears) {
    if (!std::isfinite(resistance) || resistance <= 0.f || maxTears < 1)
        throw Exception("Rope3D: tearing requires positive resistance and tear count");
    tearingEnabled_  = true;
    tearResistance_  = resistance;
    maxTearsPerStep_ = maxTears;
}

float Rope3D::getParticleX(int i) const { return validParticle(i) ? particles_[i].position.x : 0.f; }
float Rope3D::getParticleY(int i) const { return validParticle(i) ? particles_[i].position.y : 0.f; }
float Rope3D::getParticleZ(int i) const { return validParticle(i) ? particles_[i].position.z : 0.f; }
float Rope3D::getParticleVelocityX(int i, float dt) const {
    return validParticle(i) && dt > 0 ? (particles_[i].position.x - particles_[i].previous.x) / dt : 0.f;
}
float Rope3D::getParticleVelocityY(int i, float dt) const {
    return validParticle(i) && dt > 0 ? (particles_[i].position.y - particles_[i].previous.y) / dt : 0.f;
}
float Rope3D::getParticleVelocityZ(int i, float dt) const {
    return validParticle(i) && dt > 0 ? (particles_[i].position.z - particles_[i].previous.z) / dt : 0.f;
}
bool Rope3D::validParticle(int i) const noexcept { return i >= 0 && i < getParticleCount(); }
bool Rope3D::validElement(int i) const noexcept { return i >= 0 && i < getElementCount(); }

void Rope3D::integrate(float dt) {
    const Vec3 accel = add(gravity_, mul(force_, 1.f / particleMass_));
    for (auto& p : particles_) {
        if (p.attached) continue;
        const Vec3 velocity = mul(sub(p.position, p.previous), 1.f - damping_);
        p.previous          = p.position;
        p.position          = add(add(p.position, velocity), mul(accel, dt * dt));
    }
}
void Rope3D::solveStretch(float dt) {
    const float invMass = 1.f / particleMass_;
    for (auto& e : elements_) {
        if (!e.active) continue;
        auto&       a     = particles_[e.a];
        auto&       b     = particles_[e.b];
        const Vec3  delta = sub(b.position, a.position);
        const float l     = length(delta);
        if (l < 1e-7f) continue;
        const float target = std::max(e.restLength * (1.f - maxCompression_), std::min(l, e.restLength));
        const float c      = l - target;
        const float wa = a.attached ? 0.f : invMass, wb = b.attached ? 0.f : invMass;
        const float alpha = stretchCompliance_ / (dt * dt);
        const float dl    = (-c - alpha * e.lambda) / (wa + wb + alpha);
        e.lambda += dl;
        const Vec3 correction = mul(delta, dl / l);
        if (!a.attached) a.position = sub(a.position, mul(correction, wa));
        if (!b.attached) b.position = add(b.position, mul(correction, wb));
        e.force = std::abs(e.lambda) / (dt * dt);
    }
}
void Rope3D::solveBend(float dt) {
    const float invMass = 1.f / particleMass_, alpha = bendCompliance_ / (dt * dt);
    for (std::size_t i = 1; i < particles_.size() - 1; ++i) {
        if (!elements_[i - 1].active || !elements_[i].active) continue;
        auto&       a        = particles_[i - 1];
        auto&       c        = particles_[i + 1];
        const Vec3  delta    = sub(c.position, a.position);
        const float l        = length(delta);
        const float baseSpan = elements_[i - 1].restLength + elements_[i].restLength;
        const float rest     = baseSpan * (1.f - std::max(maxBending_, bendPlasticity_[i - 1]));
        if (l < 1e-7f) continue;
        if (l >= rest) continue;
        const float wa = a.attached ? 0.f : invMass, wc = c.attached ? 0.f : invMass;
        const float dl   = -(l - rest) / (wa + wc + alpha);
        const Vec3  corr = mul(delta, dl / l);
        if (!a.attached) a.position = sub(a.position, mul(corr, wa));
        if (!c.attached) c.position = add(c.position, mul(corr, wc));
    }
}
void Rope3D::updatePlasticity(float dt) {
    if (plasticCreep_ <= 0.f) return;
    for (std::size_t i = 1; i + 1 < particles_.size(); ++i) {
        if (!elements_[i - 1].active || !elements_[i].active) continue;
        const float baseSpan    = elements_[i - 1].restLength + elements_[i].restLength;
        const float currentSpan = length(sub(particles_[i + 1].position, particles_[i - 1].position));
        const float deformation = std::clamp(1.f - currentSpan / baseSpan, 0.f, 0.5f);
        if (deformation > plasticYield_)
            bendPlasticity_[i - 1] = std::min(deformation, bendPlasticity_[i - 1] + plasticCreep_ * dt);
    }
}
void Rope3D::syncBendState() { bendPlasticity_.resize(particles_.size() > 2 ? particles_.size() - 2 : 0, 0.f); }
void Rope3D::solveSelfCollision() {
    const float minDist = 2.f * radius_;
    for (std::size_t i = 0; i < particles_.size(); ++i)
        for (std::size_t j = i + 2; j < particles_.size(); ++j) {
            auto& a = particles_[i];
            auto& b = particles_[j];
            Vec3  d = sub(b.position, a.position);
            float l = length(d);
            if (l >= minDist || l < 1e-7f) continue;
            const float wa = a.attached ? 0.f : 1.f, wb = b.attached ? 0.f : 1.f, total = wa + wb;
            if (total == 0) continue;
            Vec3 corr = mul(d, (minDist - l) / (l * total));
            if (!a.attached) a.position = sub(a.position, mul(corr, wa));
            if (!b.attached) b.position = add(b.position, mul(corr, wb));
        }
}
void Rope3D::collideBounds() {
    if (!hasBounds_) return;
    for (auto& p : particles_)
        if (!p.attached) {
            p.position.x = std::clamp(p.position.x, boundsMin_.x + radius_, boundsMax_.x - radius_);
            p.position.y = std::clamp(p.position.y, boundsMin_.y + radius_, boundsMax_.y - radius_);
            p.position.z = std::clamp(p.position.z, boundsMin_.z + radius_, boundsMax_.z - radius_);
        }
}
void Rope3D::applyAttachments() {
    for (auto& p : particles_)
        if (p.attached) p.position = p.previous = p.attachment;
}
void Rope3D::applyTearing() {
    if (!tearingEnabled_) return;
    std::vector<std::size_t> candidates;
    candidates.reserve(elements_.size());
    for (std::size_t i = 0; i < elements_.size(); ++i) {
        const auto& element = elements_[i];
        if (element.active && element.force > tearResistance_ * element.tearResistance) candidates.push_back(i);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](std::size_t lhs, std::size_t rhs) { return elements_[lhs].force > elements_[rhs].force; });
    int tears = 0;
    for (std::size_t index : candidates) {
        auto& element  = elements_[index];
        element.active = false;
        element.lambda = 0.f;
        element.force  = 0.f;
        lastTornElements_.push_back(static_cast<int>(index));
        ++topologyRevision_;
        if (++tears >= maxTearsPerStep_) break;
    }
}
}  // namespace eve::physics
