#include "physics/softbody/SoftBody3D.h"

#include "common/Exception.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace eve::physics {
namespace {

using V3 = glm::vec3;

float tetraVolume(const V3& a, const V3& b, const V3& c, const V3& d) {
    return std::fabs(glm::dot(b - a, glm::cross(c - a, d - a))) / 6.f;
}

glm::quat bestFitRotation(const glm::mat3& covariance) {
    glm::quat rotation(1.f, 0.f, 0.f, 0.f);
    for (int i = 0; i < 8; ++i) {
        const glm::mat3 basis     = glm::mat3_cast(rotation);
        const V3        omega     = (glm::cross(basis[0], covariance[0]) + glm::cross(basis[1], covariance[1]) +
                                     glm::cross(basis[2], covariance[2])) /
                                    (std::fabs(glm::dot(basis[0], covariance[0]) + glm::dot(basis[1], covariance[1]) +
                                               glm::dot(basis[2], covariance[2])) +
                                     1e-8f);
        const float     magnitude = glm::length(omega);
        if (magnitude < 1e-6f) break;
        rotation = glm::normalize(glm::angleAxis(magnitude, omega / magnitude) * rotation);
    }
    return rotation;
}

}  // namespace

eve::Result<std::unique_ptr<SoftBody3D>> SoftBody3D::create(int cols, int rows, int layers, float spacing,
                                                            float originX, float originY, float originZ) {
    SoftBody3DDefinition definition;
    definition.cols    = cols;
    definition.rows    = rows;
    definition.layers  = layers;
    definition.spacing = spacing;
    definition.originX = originX;
    definition.originY = originY;
    definition.originZ = originZ;
    return create(definition);
}

eve::Result<std::unique_ptr<SoftBody3D>> SoftBody3D::create(const SoftBody3DDefinition& definition) {
    auto registered = SoftBody3DDefinition::ensureSchemaRegistered();
    if (!registered) return eve::Result<std::unique_ptr<SoftBody3D>>::failure(registered.status());
    auto valid = definition.validate();
    if (!valid) return eve::Result<std::unique_ptr<SoftBody3D>>::failure(valid.status());
    auto result = std::unique_ptr<SoftBody3D>(new SoftBody3D(definition.cols, definition.rows, definition.layers,
                                                             definition.spacing, definition.originX, definition.originY,
                                                             definition.originZ));
    result->setGravity(definition.gravityX, definition.gravityY, definition.gravityZ);
    result->setDeformationResistance(definition.deformationResistance);
    result->setIterations(definition.iterations);
    result->setDamping(definition.damping);
    result->setParticleRadius(definition.particleRadius);
    result->setParticleMass(definition.particleMass);
    result->setPlasticity(definition.plasticYield, definition.plasticCreep, definition.plasticRecovery,
                          definition.maxDeformation);
    result->setSelfCollision(definition.selfCollision);
    return eve::Result<std::unique_ptr<SoftBody3D>>::success(std::move(result));
}

SoftBody3D::SoftBody3D(int cols, int rows, int layers, float spacing, float originX, float originY, float originZ)
    : cols_(cols), rows_(rows), layers_(layers), spacing_(spacing) {
    particles_.reserve(static_cast<size_t>(cols_) * rows_ * layers_);
    for (int z = 0; z < layers_; ++z) {
        for (int y = 0; y < rows_; ++y) {
            for (int x = 0; x < cols_; ++x) {
                const float px = originX + float(x) * spacing_;
                const float py = originY + float(y) * spacing_;
                const float pz = originZ + float(z) * spacing_;
                particles_.push_back({px, py, pz, px, py, pz, px, py, pz, false});
            }
        }
    }
    clusters_.reserve(static_cast<size_t>(cols_ - 1) * (rows_ - 1) * (layers_ - 1));
    for (int z = 0; z + 1 < layers_; ++z) {
        for (int y = 0; y + 1 < rows_; ++y) {
            for (int x = 0; x + 1 < cols_; ++x) {
                Cluster cluster;
                cluster.indices[0] = indexOf(x, y, z);
                cluster.indices[1] = indexOf(x + 1, y, z);
                cluster.indices[2] = indexOf(x, y + 1, z);
                cluster.indices[3] = indexOf(x + 1, y + 1, z);
                cluster.indices[4] = indexOf(x, y, z + 1);
                cluster.indices[5] = indexOf(x + 1, y, z + 1);
                cluster.indices[6] = indexOf(x, y + 1, z + 1);
                cluster.indices[7] = indexOf(x + 1, y + 1, z + 1);
                clusters_.push_back(cluster);
            }
        }
    }
    particleRadius_ = spacing_ * 0.35f;
}

SoftBody3D::~SoftBody3D() { destroy(); }

void SoftBody3D::destroy() {
    destroyed_      = true;
    world_          = nullptr;
    collisionWorld_ = nullptr;
    ownedCollisionWorld_.reset();
}

bool SoftBody3D::validIndex(int index) const noexcept { return index >= 0 && index < getParticleCount(); }

int SoftBody3D::indexOf(int x, int y, int z) const noexcept { return (z * rows_ + y) * cols_ + x; }

void SoftBody3D::setGravity(float x, float y, float z) {
    gravityX_ = std::isfinite(x) ? x : 0.f;
    gravityY_ = std::isfinite(y) ? y : 0.f;
    gravityZ_ = std::isfinite(z) ? z : 0.f;
}

void SoftBody3D::setDeformationResistance(float value) { deformationResistance_ = std::clamp(value, 0.f, 1.f); }

float SoftBody3D::getDeformationResistance() const { return deformationResistance_; }

void SoftBody3D::setIterations(int value) { iterations_ = std::clamp(value, 1, 32); }
void SoftBody3D::setDamping(float value) { damping_ = std::clamp(value, 0.f, 1.f); }
void SoftBody3D::setParticleRadius(float value) {
    if (std::isfinite(value)) particleRadius_ = std::max(0.f, value);
}
float SoftBody3D::getParticleRadius() const { return particleRadius_; }
void  SoftBody3D::setParticleMass(float value) {
    if (std::isfinite(value)) particleMass_ = std::max(1e-5f, value);
}

void SoftBody3D::setPlasticity(float yield, float creep, float recovery, float maxDeformation) {
    plasticYield_    = std::max(0.f, std::isfinite(yield) ? yield : 0.f);
    plasticCreep_    = std::clamp(std::isfinite(creep) ? creep : 0.f, 0.f, 1.f);
    plasticRecovery_ = std::max(0.f, std::isfinite(recovery) ? recovery : 0.f);
    maxDeformation_  = std::max(0.f, std::isfinite(maxDeformation) ? maxDeformation : 0.f);
}

float SoftBody3D::getPlasticYield() const { return plasticYield_; }
float SoftBody3D::getPlasticCreep() const { return plasticCreep_; }
float SoftBody3D::getPlasticRecovery() const { return plasticRecovery_; }
float SoftBody3D::getMaxDeformation() const { return maxDeformation_; }
int   SoftBody3D::getLayers() const { return layers_; }

void SoftBody3D::setBounds(float x, float y, float z, float width, float height, float depth) {
    boundX_    = x;
    boundY_    = y;
    boundZ_    = z;
    boundW_    = std::max(0.f, width);
    boundH_    = std::max(0.f, height);
    boundD_    = std::max(0.f, depth);
    hasBounds_ = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(width) &&
                 std::isfinite(height) && std::isfinite(depth);
}

void SoftBody3D::pin(int index) {
    if (!validIndex(index)) throw Exception("SoftBody3D.pin: index out of range");
    particles_[static_cast<size_t>(index)].pinned = true;
}
void SoftBody3D::unpin(int index) {
    if (!validIndex(index)) throw Exception("SoftBody3D.unpin: index out of range");
    particles_[static_cast<size_t>(index)].pinned = false;
}
bool SoftBody3D::isPinned(int index) const {
    if (!validIndex(index)) throw Exception("SoftBody3D.isPinned: index out of range");
    return particles_[static_cast<size_t>(index)].pinned;
}

int SoftBody3D::grabAt(float x, float y, float z, float radius) {
    float best  = radius * radius;
    int   found = -1;
    for (int i = 0; i < getParticleCount(); ++i) {
        const Particle& p  = particles_[static_cast<size_t>(i)];
        const float     dx = p.x - x, dy = p.y - y, dz = p.z - z;
        const float     distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!p.pinned && distanceSquared <= best) {
            best  = distanceSquared;
            found = i;
        }
    }
    if (found >= 0) {
        grabIndex_ = found;
        moveGrab(x, y, z);
    }
    return found;
}

void SoftBody3D::moveGrab(float x, float y, float z) {
    if (grabIndex_ < 0) return;
    grabX_      = x;
    grabY_      = y;
    grabZ_      = z;
    Particle& p = particles_[static_cast<size_t>(grabIndex_)];
    p.x = p.px = x;
    p.y = p.py = y;
    p.z = p.pz = z;
}

void SoftBody3D::applyForce(float x, float y, float z) {
    if (std::isfinite(x)) forceX_ += x;
    if (std::isfinite(y)) forceY_ += y;
    if (std::isfinite(z)) forceZ_ += z;
}

void SoftBody3D::interactAt(float x, float y, float z, float radius, float strength) {
    hasInteraction_   = radius > 0.f && std::isfinite(radius) && std::isfinite(strength);
    interactX_        = x;
    interactY_        = y;
    interactZ_        = z;
    interactRadius_   = radius;
    interactStrength_ = strength;
}

float SoftBody3D::getParticleX(int index) const {
    if (!validIndex(index)) throw Exception("SoftBody3D.getParticleX: index out of range");
    return particles_[static_cast<size_t>(index)].x;
}
float SoftBody3D::getParticleY(int index) const {
    if (!validIndex(index)) throw Exception("SoftBody3D.getParticleY: index out of range");
    return particles_[static_cast<size_t>(index)].y;
}
float SoftBody3D::getParticleZ(int index) const {
    if (!validIndex(index)) throw Exception("SoftBody3D.getParticleZ: index out of range");
    return particles_[static_cast<size_t>(index)].z;
}
void SoftBody3D::setParticlePosition(int index, float x, float y, float z) {
    if (!validIndex(index)) throw Exception("SoftBody3D.setParticlePosition: index out of range");
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw Exception("SoftBody3D.setParticlePosition: coordinates must be finite");
    Particle& p = particles_[static_cast<size_t>(index)];
    p.x = p.px = x;
    p.y = p.py = y;
    p.z = p.pz = z;
}

void SoftBody3D::reset() {
    for (Particle& p : particles_) {
        p.x = p.px = p.rx;
        p.y = p.py = p.ry;
        p.z = p.pz = p.rz;
        p.pinned   = false;
    }
    for (Cluster& cluster : clusters_)
        for (auto& offset : cluster.plastic) offset[0] = offset[1] = offset[2] = 0.f;
    grabIndex_ = -1;
    forceX_ = forceY_ = forceZ_ = 0.f;
    hasInteraction_             = false;
}

void SoftBody3D::integrate(float dt) {
    const float velocityScale = std::max(0.f, 1.f - damping_);
    for (int i = 0; i < getParticleCount(); ++i) {
        Particle& p = particles_[static_cast<size_t>(i)];
        if (p.pinned || i == grabIndex_) continue;
        float ax = gravityX_ + forceX_ / particleMass_;
        float ay = gravityY_ + forceY_ / particleMass_;
        float az = gravityZ_ + forceZ_ / particleMass_;
        if (hasInteraction_) {
            const V3    delta    = V3(interactX_ - p.x, interactY_ - p.y, interactZ_ - p.z);
            const float distance = glm::length(delta);
            if (distance > 1e-6f && distance < interactRadius_) {
                const float falloff = 1.f - distance / interactRadius_;
                const V3    field   = delta * (interactStrength_ * falloff / distance);
                ax += field.x;
                ay += field.y;
                az += field.z;
            }
        }
        const float vx = (p.x - p.px) * velocityScale;
        const float vy = (p.y - p.py) * velocityScale;
        const float vz = (p.z - p.pz) * velocityScale;
        p.px           = p.x;
        p.py           = p.y;
        p.pz           = p.z;
        p.x += vx + ax * dt * dt;
        p.y += vy + ay * dt * dt;
        p.z += vz + az * dt * dt;
    }
}

void SoftBody3D::solveShapeMatching(float dt) {
    const float stiffness = 1.f - std::pow(1.f - deformationResistance_, 1.f / float(iterations_));
    for (Cluster& cluster : clusters_) {
        V3 currentCom(0.f), restCom(0.f);
        for (int i = 0; i < 8; ++i) {
            const Particle& p = particles_[static_cast<size_t>(cluster.indices[i])];
            currentCom += V3(p.x, p.y, p.z);
            restCom += V3(p.rx + cluster.plastic[i][0], p.ry + cluster.plastic[i][1], p.rz + cluster.plastic[i][2]);
        }
        currentCom /= 8.f;
        restCom /= 8.f;
        glm::mat3 covariance(0.f);
        for (int i = 0; i < 8; ++i) {
            const Particle& p       = particles_[static_cast<size_t>(cluster.indices[i])];
            const V3        current = V3(p.x, p.y, p.z) - currentCom;
            const V3        rest =
                V3(p.rx + cluster.plastic[i][0], p.ry + cluster.plastic[i][1], p.rz + cluster.plastic[i][2]) - restCom;
            covariance += glm::outerProduct(current, rest);
        }
        const glm::mat3 rotation    = glm::mat3_cast(bestFitRotation(covariance));
        float           deformation = 0.f;
        V3              restCurrent[8];
        for (int i = 0; i < 8; ++i) {
            Particle& p    = particles_[static_cast<size_t>(cluster.indices[i])];
            restCurrent[i] = glm::transpose(rotation) * (V3(p.x, p.y, p.z) - currentCom);
            const V3 rest =
                V3(p.rx + cluster.plastic[i][0], p.ry + cluster.plastic[i][1], p.rz + cluster.plastic[i][2]) - restCom;
            deformation += glm::length(restCurrent[i] - rest) / spacing_;
            if (!p.pinned && cluster.indices[i] != grabIndex_) {
                const V3 goal = currentCom + rotation * rest;
                p.x += (goal.x - p.x) * stiffness;
                p.y += (goal.y - p.y) * stiffness;
                p.z += (goal.z - p.z) * stiffness;
            }
        }
        deformation /= 8.f;
        const float creep    = deformation > plasticYield_ ? plasticCreep_ * dt : 0.f;
        const float recovery = plasticRecovery_ * dt;
        if (maxDeformation_ > 0.f && (creep > 0.f || recovery > 0.f)) {
            for (int i = 0; i < 8; ++i) {
                const Particle& p        = particles_[static_cast<size_t>(cluster.indices[i])];
                const V3        original = V3(p.rx, p.ry, p.rz) - restCom;
                V3              offset(cluster.plastic[i][0], cluster.plastic[i][1], cluster.plastic[i][2]);
                offset += (restCurrent[i] - original - offset) * std::min(1.f, creep);
                offset += (V3(0.f) - offset) * std::min(1.f, recovery);
                const float limit  = maxDeformation_ * spacing_;
                const float length = glm::length(offset);
                if (length > limit && length > 1e-6f) offset *= limit / length;
                cluster.plastic[i][0] = offset.x;
                cluster.plastic[i][1] = offset.y;
                cluster.plastic[i][2] = offset.z;
            }
        }
    }
}

void SoftBody3D::solveSelfCollision() {
    if (!selfCollision_ || particleRadius_ <= 0.f) return;
    const float minimum        = particleRadius_ * 2.f;
    const float minimumSquared = minimum * minimum;
    for (int i = 0; i < getParticleCount(); ++i) {
        for (int j = i + 1; j < getParticleCount(); ++j) {
            Particle&   a = particles_[static_cast<size_t>(i)];
            Particle&   b = particles_[static_cast<size_t>(j)];
            V3          delta(b.x - a.x, b.y - a.y, b.z - a.z);
            const float distanceSquared = glm::dot(delta, delta);
            if (distanceSquared >= minimumSquared || distanceSquared < 1e-12f) continue;
            const float distance   = std::sqrt(distanceSquared);
            const V3    correction = delta * ((minimum - distance) / distance * 0.5f);
            if (!a.pinned && i != grabIndex_) {
                a.x -= correction.x;
                a.y -= correction.y;
                a.z -= correction.z;
            }
            if (!b.pinned && j != grabIndex_) {
                b.x += correction.x;
                b.y += correction.y;
                b.z += correction.z;
            }
        }
    }
}

void SoftBody3D::collideWorld(float dt) {
    if (!collisionWorld_ || !collisionWorld_->softBodyCollisionAvailable() || particleRadius_ <= 0.f) return;
    softbody::SoftBodyContact contact;
    const float               inverseDt = dt > 1e-6f ? 1.f / dt : 0.f;
    for (Particle& p : particles_) {
        if (p.pinned || collisionWorld_->probeSoftBodyParticle(p.x, p.y, p.z, particleRadius_, contact) !=
                            softbody::SoftBodyContactState::Hit ||
            !contact.hit)
            continue;
        const V3 velocity = V3(p.x - p.px, p.y - p.py, p.z - p.pz) * inverseDt;
        p.x += contact.nx * contact.depth;
        p.y += contact.ny * contact.depth;
        p.z += contact.nz * contact.depth;
        if (contact.dynamicBody) {
            const float normalVelocity = glm::dot(velocity, V3(contact.nx, contact.ny, contact.nz));
            if (normalVelocity < 0.f) {
                const float impulse = std::min(-normalVelocity * particleMass_, particleMass_ * 8.f);
                collisionWorld_->applySoftBodyImpulse(contact.bodyId, -contact.nx * impulse, -contact.ny * impulse,
                                                      -contact.nz * impulse);
            }
        }
    }
}

void SoftBody3D::collideBounds() {
    if (!hasBounds_) return;
    const V3 minimum(boundX_, boundY_, boundZ_);
    const V3 maximum(boundX_ + boundW_, boundY_ + boundH_, boundZ_ + boundD_);
    for (Particle& p : particles_) {
        if (p.pinned) continue;
        p.x = std::clamp(p.x, minimum.x, maximum.x);
        p.y = std::clamp(p.y, minimum.y, maximum.y);
        p.z = std::clamp(p.z, minimum.z, maximum.z);
    }
}

void SoftBody3D::update(float dt) {
    if (destroyed_) return;
    updateSubsteps(std::clamp(dt, 0.f, 0.05f), 2);
}

void SoftBody3D::updateSubsteps(float dt, int substeps) {
    if (destroyed_ || substeps < 1) return;
    const float h = dt / float(substeps);
    for (int substep = 0; substep < substeps; ++substep) {
        integrate(h);
        for (int iteration = 0; iteration < iterations_; ++iteration) {
            solveShapeMatching(h);
            solveSelfCollision();
            collideWorld(h);
            collideBounds();
        }
        if (grabIndex_ >= 0) moveGrab(grabX_, grabY_, grabZ_);
    }
    forceX_ = forceY_ = forceZ_ = 0.f;
    hasInteraction_             = false;
}

eve::Result<void> SoftBody3D::step(const eve::SimulationStep& stepValue, const SimulationSettings& settings) {
    if (destroyed_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Cannot step a destroyed soft body",
                                                                 "physics.softbody3d.step"));
    auto valid = detail::validateSimulationStep(stepValue, settings, observation_);
    if (!valid) return valid;
    auto next = detail::advanceSimulationObservation(observation_, stepValue);
    if (!next) return eve::Result<void>::failure(next.status());
    updateSubsteps(static_cast<float>(stepValue.delta.seconds()), settings.subStepCount);
    observation_ = std::move(next).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SoftBody3D::restoreObservation(const SimulationObservation& observation) {
    auto valid = detail::validateSimulationObservation(observation, "physics.softbody3d.restoreObservation");
    if (!valid) return valid;
    if (destroyed_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Cannot restore a destroyed soft body",
                                                                 "physics.softbody3d.restoreObservation"));
    observation_ = observation;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

float SoftBody3D::getVolumeRatio() const {
    if (clusters_.empty()) return 1.f;
    float volume = 0.f;
    for (const Cluster& cluster : clusters_) {
        V3 p[8];
        for (int i = 0; i < 8; ++i) {
            const Particle& particle = particles_[static_cast<size_t>(cluster.indices[i])];
            p[i]                     = V3(particle.x, particle.y, particle.z);
        }
        volume += tetraVolume(p[0], p[1], p[3], p[7]) + tetraVolume(p[0], p[3], p[2], p[7]) +
                  tetraVolume(p[0], p[2], p[6], p[7]) + tetraVolume(p[0], p[6], p[4], p[7]) +
                  tetraVolume(p[0], p[4], p[5], p[7]) + tetraVolume(p[0], p[5], p[1], p[7]);
    }
    const float rest = float(clusters_.size()) * spacing_ * spacing_ * spacing_;
    return rest > 0.f ? volume / rest : 1.f;
}

}  // namespace eve::physics
