#include "physics/cloth/Cloth3D.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eve::physics {

float Cloth3D::getStretchCompliance() const { return stretchCompliance_; }
float Cloth3D::getShearCompliance() const { return shearCompliance_; }
float Cloth3D::getBendCompliance() const { return bendCompliance_; }
float Cloth3D::getTetherScale() const { return tetherScale_; }
float Cloth3D::getTetherCompliance() const { return tetherCompliance_; }
float Cloth3D::getPressure() const { return pressure_; }
float Cloth3D::getVolumeCompliance() const { return volumeCompliance_; }
float Cloth3D::getCollisionFriction() const { return collisionFriction_; }
float Cloth3D::getCollisionRestitution() const { return collisionRestitution_; }
uint64_t Cloth3D::getCollisionCategoryBits() const { return collisionCategoryBits_; }
uint64_t Cloth3D::getCollisionMaskBits() const { return collisionMaskBits_; }
int Cloth3D::getTriangleCount() const { return static_cast<int>(triangles_.size()); }
int Cloth3D::getDistanceConstraintCount() const { return static_cast<int>(links_.size()); }
int Cloth3D::getTetherConstraintCount() const { return static_cast<int>(tethers_.size()); }
int Cloth3D::getSkinConstraintCount() const { return static_cast<int>(skinConstraints_.size()); }
namespace {

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float length(const Vec3& value) { return std::sqrt(dot(value, value)); }

}  // namespace

bool Cloth3D::supportsFeature(const std::string& feature) const {
    return feature == "arbitrary_topology" || feature == "xpbd_material" || feature == "tether" ||
           feature == "volume_pressure" || feature == "skin_backstop" || feature == "attachment" ||
           feature == "world_collision" || feature == "collision_filter" || feature == "friction" ||
           feature == "self_collision" || feature == "aerodynamics" || feature == "runtime_tearing";
}

void Cloth3D::setSkinConstraint(int index, float x, float y, float z, float nx, float ny, float nz, float radius,
                                float backstopDistance, float backstopRadius, float compliance) {
    if (!validIndex(index)) throw Exception("Cloth3D.setSkinConstraint: index out of range");
    const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!std::isfinite(normalLength) || normalLength <= 1e-7f)
        throw Exception("Cloth3D.setSkinConstraint: normal must be finite and non-zero");
    auto found = std::find_if(skinConstraints_.begin(), skinConstraints_.end(),
                              [index](const SkinConstraint& item) { return item.particle == index; });
    SkinConstraint value{index,
                         x,
                         y,
                         z,
                         nx / normalLength,
                         ny / normalLength,
                         nz / normalLength,
                         std::isfinite(radius) ? std::max(0.f, radius) : 0.f,
                         std::isfinite(backstopDistance) ? backstopDistance : -1.f,
                         std::isfinite(backstopRadius) ? std::max(0.f, backstopRadius) : 0.f,
                         std::isfinite(compliance) ? std::max(0.f, compliance) : 0.f,
                         0.f};
    if (found == skinConstraints_.end())
        skinConstraints_.push_back(value);
    else
        *found = value;
}

void Cloth3D::updateSkinReference(int index, float x, float y, float z, float nx, float ny, float nz) {
    auto found = std::find_if(skinConstraints_.begin(), skinConstraints_.end(),
                              [index](const SkinConstraint& item) { return item.particle == index; });
    if (found == skinConstraints_.end()) throw Exception("Cloth3D.updateSkinReference: particle is not constrained");
    const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!std::isfinite(normalLength) || normalLength <= 1e-7f)
        throw Exception("Cloth3D.updateSkinReference: normal must be finite and non-zero");
    found->x  = x;
    found->y  = y;
    found->z  = z;
    found->nx = nx / normalLength;
    found->ny = ny / normalLength;
    found->nz = nz / normalLength;
}

void Cloth3D::clearSkinConstraint(int index) {
    std::erase_if(skinConstraints_, [index](const SkinConstraint& item) { return item.particle == index; });
}

bool Cloth3D::hasSkinConstraint(int index) const {
    return std::any_of(skinConstraints_.begin(), skinConstraints_.end(),
                       [index](const SkinConstraint& item) { return item.particle == index; });
}

void Cloth3D::attachParticle(int index, float x, float y, float z, float compliance) {
    if (!validIndex(index)) throw Exception("Cloth3D.attachParticle: index out of range");
    auto found = std::find_if(attachments_.begin(), attachments_.end(),
                              [index](const Attachment& item) { return item.particle == index; });
    const Attachment value{index, x, y, z, std::isfinite(compliance) ? std::max(0.f, compliance) : 0.f, 0.f};
    if (found == attachments_.end())
        attachments_.push_back(value);
    else
        *found = value;
}

void Cloth3D::updateAttachment(int index, float x, float y, float z) {
    auto found = std::find_if(attachments_.begin(), attachments_.end(),
                              [index](const Attachment& item) { return item.particle == index; });
    if (found == attachments_.end()) throw Exception("Cloth3D.updateAttachment: particle is not attached");
    found->x = x;
    found->y = y;
    found->z = z;
}

void Cloth3D::detachParticle(int index) {
    std::erase_if(attachments_, [index](const Attachment& item) { return item.particle == index; });
}

bool Cloth3D::isAttached(int index) const {
    return std::any_of(attachments_.begin(), attachments_.end(),
                       [index](const Attachment& item) { return item.particle == index; });
}

void Cloth3D::setTetherScale(float scale) { tetherScale_ = std::isfinite(scale) ? std::max(0.f, scale) : 0.f; }

void Cloth3D::setTetherCompliance(float compliance) {
    tetherCompliance_ = std::isfinite(compliance) ? std::max(0.f, compliance) : 0.f;
}

void Cloth3D::setPressure(float pressure) { pressure_ = std::isfinite(pressure) ? std::max(0.f, pressure) : 0.f; }

void Cloth3D::setVolumeCompliance(float compliance) {
    volumeCompliance_ = std::isfinite(compliance) ? std::max(0.f, compliance) : 0.f;
}

float Cloth3D::getCurrentVolume() const {
    if (restVolume_ == 0.f) return 0.f;
    double volume = 0.0;
    for (const Tri& triangle : triangles_) {
        const Particle& a = particles_[static_cast<size_t>(triangle.v[0])];
        const Particle& b = particles_[static_cast<size_t>(triangle.v[1])];
        const Particle& c = particles_[static_cast<size_t>(triangle.v[2])];
        volume += (double(a.x) * (double(b.y) * c.z - double(b.z) * c.y) +
                   double(a.y) * (double(b.z) * c.x - double(b.x) * c.z) +
                   double(a.z) * (double(b.x) * c.y - double(b.y) * c.x)) /
                  6.0;
    }
    return static_cast<float>(volume);
}

void Cloth3D::solveTetherConstraints(float dt) {
    if (tetherScale_ <= 0.f || tethers_.empty() || dt <= 0.f) return;
    const float alpha = tetherCompliance_ / (dt * dt);
    for (Tether& tether : tethers_) {
        Particle&       particle = particles_[static_cast<size_t>(tether.particle)];
        const Particle& anchor   = particles_[static_cast<size_t>(tether.anchor)];
        if (particle.pinned) continue;
        const float dx       = particle.x - anchor.x;
        const float dy       = particle.y - anchor.y;
        const float dz       = particle.z - anchor.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float maximum  = tether.maxLength * tetherScale_;
        if (distance <= maximum || distance <= 1e-7f) continue;
        const float weight = particle.inverseMass;
        const float deltaLambda = (-(distance - maximum) - alpha * tether.lambda) / (weight + alpha);
        tether.lambda += deltaLambda;
        particle.x += weight * dx / distance * deltaLambda;
        particle.y += weight * dy / distance * deltaLambda;
        particle.z += weight * dz / distance * deltaLambda;
    }
}

void Cloth3D::solveVolumeConstraint(float dt) {
    if (pressure_ <= 0.f || restVolume_ == 0.f || dt <= 0.f) return;
    std::vector<Vec3> gradients(particles_.size());
    for (const Tri& triangle : triangles_) {
        const Particle& a = particles_[static_cast<size_t>(triangle.v[0])];
        const Particle& b = particles_[static_cast<size_t>(triangle.v[1])];
        const Particle& c = particles_[static_cast<size_t>(triangle.v[2])];
        const Vec3      av{a.x, a.y, a.z};
        const Vec3      bv{b.x, b.y, b.z};
        const Vec3      cv{c.x, c.y, c.z};
        const Vec3      ga = cross(bv, cv);
        const Vec3      gb = cross(cv, av);
        const Vec3      gc = cross(av, bv);
        for (const auto [index, gradient] :
             {std::pair{triangle.v[0], ga}, std::pair{triangle.v[1], gb}, std::pair{triangle.v[2], gc}}) {
            gradients[static_cast<size_t>(index)].x += gradient.x / 6.f;
            gradients[static_cast<size_t>(index)].y += gradient.y / 6.f;
            gradients[static_cast<size_t>(index)].z += gradient.z / 6.f;
        }
    }

    float denominator = 0.f;
    for (size_t i = 0; i < particles_.size(); ++i)
        if (!particles_[i].pinned) denominator += particles_[i].inverseMass * dot(gradients[i], gradients[i]);
    const float alpha = volumeCompliance_ / (dt * dt);
    denominator += alpha;
    if (denominator <= 1e-10f) return;
    const float constraint  = getCurrentVolume() - restVolume_ * pressure_;
    const float deltaLambda = (-constraint - alpha * volumeLambda_) / denominator;
    volumeLambda_ += deltaLambda;
    for (size_t i = 0; i < particles_.size(); ++i) {
        if (particles_[i].pinned) continue;
        particles_[i].x += particles_[i].inverseMass * gradients[i].x * deltaLambda;
        particles_[i].y += particles_[i].inverseMass * gradients[i].y * deltaLambda;
        particles_[i].z += particles_[i].inverseMass * gradients[i].z * deltaLambda;
    }
}

void Cloth3D::solveSkinConstraints(float dt) {
    if (dt <= 0.f) return;
    for (SkinConstraint& skin : skinConstraints_) {
        Particle& particle = particles_[static_cast<size_t>(skin.particle)];
        if (particle.pinned) continue;
        Vec3 delta{particle.x - skin.x, particle.y - skin.y, particle.z - skin.z};
        float distance = length(delta);
        if (skin.radius > 0.f && distance > skin.radius && distance > 1e-7f) {
            const float alpha = skin.compliance / (dt * dt);
            const float weight = particle.inverseMass;
            const float dl    = (-(distance - skin.radius) - alpha * skin.lambda) / (weight + alpha);
            skin.lambda += dl;
            particle.x += weight * delta.x / distance * dl;
            particle.y += weight * delta.y / distance * dl;
            particle.z += weight * delta.z / distance * dl;
        }
        if (skin.backstopDistance < 0.f || skin.backstopRadius <= 0.f) continue;
        const Vec3 center{skin.x - skin.nx * skin.backstopDistance, skin.y - skin.ny * skin.backstopDistance,
                          skin.z - skin.nz * skin.backstopDistance};
        delta    = {particle.x - center.x, particle.y - center.y, particle.z - center.z};
        distance = length(delta);
        if (distance >= skin.backstopRadius) continue;
        Vec3 direction = distance > 1e-7f ? Vec3{delta.x / distance, delta.y / distance, delta.z / distance}
                                          : Vec3{skin.nx, skin.ny, skin.nz};
        const float correction = skin.backstopRadius - distance;
        particle.x += direction.x * correction;
        particle.y += direction.y * correction;
        particle.z += direction.z * correction;
    }
}

void Cloth3D::solveAttachments(float dt) {
    if (dt <= 0.f) return;
    for (Attachment& attachment : attachments_) {
        Particle& particle = particles_[static_cast<size_t>(attachment.particle)];
        const Vec3 delta{particle.x - attachment.x, particle.y - attachment.y, particle.z - attachment.z};
        const float distance = length(delta);
        if (distance <= 1e-7f) continue;
        const float alpha = attachment.compliance / (dt * dt);
        const float weight = std::max(particle.inverseMass, 1.f);
        const float dl    = (-distance - alpha * attachment.lambda) / (weight + alpha);
        attachment.lambda += dl;
        particle.x += weight * delta.x / distance * dl;
        particle.y += weight * delta.y / distance * dl;
        particle.z += weight * delta.z / distance * dl;
    }
}

}  // namespace eve::physics
