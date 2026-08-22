#include "decal/DecalManager.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace eve::decal {

namespace {

glm::mat4 decalModel(const DecalInstance &d) {
    glm::vec3 normal(d.nx, d.ny, d.nz);
    const float len = glm::length(normal);
    if (len < 1e-6f) normal = glm::vec3(0.f, 1.f, 0.f);
    else normal /= len;

    // Lift slightly along the normal to avoid z-fighting with the surface.
    glm::mat4 m = glm::translate(glm::mat4(1.f),
                                 glm::vec3(d.x, d.y, d.z) + normal * 0.008f);
    const glm::quat orient = glm::rotation(glm::vec3(0.f, 0.f, 1.f), normal);
    m *= glm::mat4_cast(orient);
    m = glm::rotate(m, d.yaw, glm::vec3(0.f, 0.f, 1.f));
    m = glm::scale(m, glm::vec3(d.size, d.size, d.depth));
    return m;
}

}  // namespace

DecalManager &DecalManager::inst() {
    static DecalManager manager;
    return manager;
}

int DecalManager::limitFor(const std::string &kind) const {
    for (const auto &l : limits_) {
        if (l.kind == kind) return l.limit;
    }
    return 0;
}

int DecalManager::project(float x, float y, float z, float nx, float ny, float nz,
                          graphics::Texture *albedo, const std::string &kind, float size,
                          float depth, bool randomYaw, int seed, float fadeIn, float lifetime,
                          float fadeOut, float normalStrength, float roughnessStrength,
                          float metalStrength, float emissiveStrength) {
    const int limit = limitFor(kind);
    if (limit > 0) {
        int ofKind = 0;
        for (const auto &d : decals_) {
            if (d.kind == kind) ++ofKind;
        }
        while (ofKind >= limit) {
            // Evict the oldest instance of this kind.
            auto it = decals_.end();
            for (auto i = decals_.begin(); i != decals_.end(); ++i) {
                if (i->kind != kind) continue;
                if (it == decals_.end() || i->age > it->age) it = i;
            }
            if (it == decals_.end()) break;
            decals_.erase(it);
            --ofKind;
        }
    }

    DecalInstance d;
    d.id = nextId_++;
    d.x = x;
    d.y = y;
    d.z = z;
    d.nx = nx;
    d.ny = ny;
    d.nz = nz;
    d.yaw = randomYaw ? (seed != 0 ? float((seed * 2654435761u) % 6283u) / 1000.f
                                   : float(std::rand()) / float(RAND_MAX) * 6.28318f)
                      : 0.f;
    d.size = size > 0.f ? size : 0.5f;
    d.depth = depth > 0.f ? depth : 0.15f;
    d.lifetime = lifetime > 0.f ? lifetime : 0.f;
    d.fadeIn = fadeIn > 0.f ? fadeIn : 0.f;
    d.fadeOut = fadeOut > 0.f ? fadeOut : 0.f;
    d.albedo = albedo;
    d.normalStrength = normalStrength;
    d.roughnessStrength = roughnessStrength;
    d.metalStrength = metalStrength;
    d.emissiveStrength = emissiveStrength;
    d.kind = kind;
    decals_.push_back(d);
    return d.id;
}

bool DecalManager::remove(int id) {
    for (auto it = decals_.begin(); it != decals_.end(); ++it) {
        if (it->id == id) {
            decals_.erase(it);
            return true;
        }
    }
    return false;
}

void DecalManager::clearAll() { decals_.clear(); }

int DecalManager::count() const { return int(decals_.size()); }

void DecalManager::setLimit(const std::string &kind, int limit) {
    for (auto &l : limits_) {
        if (l.kind == kind) {
            l.limit = limit > 0 ? limit : 0;
            return;
        }
    }
    limits_.push_back({kind, limit > 0 ? limit : 0});
}

void DecalManager::update(float dt) {
    if (dt <= 0.f) return;
    for (auto it = decals_.begin(); it != decals_.end();) {
        it->age += dt;
        const bool expired =
            it->lifetime > 0.f && it->age >= it->lifetime + std::max(it->fadeOut, 0.f);
        if (expired)
            it = decals_.erase(it);
        else
            ++it;
    }
}

void DecalManager::drawAll(graphics::Graphics &gfx, const graphics::Camera3D::Data &cam,
                           const glm::mat4 &viewProj, float aspect) {
    (void)cam;
    (void)viewProj;
    (void)aspect;
    for (const auto &d : decals_) {
        if (!d.albedo) continue;
        float fade = 1.f;
        if (d.fadeIn > 0.f && d.age < d.fadeIn) fade = std::min(1.f, d.age / d.fadeIn);
        if (d.lifetime > 0.f) {
            const float outT = d.age - d.lifetime;
            if (outT >= 0.f) {
                const float span = std::max(d.fadeOut, 0.001f);
                fade *= std::max(0.f, 1.f - outT / span);
            }
        }
        if (fade <= 0.001f) continue;
        gfx.drawDecal(decalModel(d), d.albedo, d.normal, d.params, d.uvRect, fade,
                      d.normalStrength, d.roughnessStrength, d.metalStrength, d.emissiveStrength);
    }
}

}  // namespace eve::decal
