#pragma once

#include "graphics/RenderSystem3D.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::decal {

/** @brief One live decal instance (CPU-side state; GPU draw happens per frame). */
struct DecalInstance {
    int id = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    float nx = 0.f, ny = 1.f, nz = 0.f;
    float yaw = 0.f;  // random rotation around the surface normal
    float size = 0.5f;
    float depth = 0.15f;
    float age = 0.f;        // seconds since spawn
    float lifetime = 0.f;   // 0 = persistent
    float fadeIn = 0.f;
    float fadeOut = 0.f;
    graphics::Texture *albedo = nullptr;
    graphics::Texture *normal = nullptr;
    graphics::Texture *params = nullptr;
    float uvRect[4] = {0.f, 0.f, 1.f, 1.f};
    float normalStrength = 0.f;
    float roughnessStrength = 0.f;
    float metalStrength = 0.f;
    float emissiveStrength = 0.f;
    std::string kind;  // quota group ("blood", "dirt", ...)
};

/**
 * @brief CPU-side decal registry: spawn / remove / fade / quotas, and the
 * per-frame drawer that feeds the graphics decal pass.
 *
 * Rendering path: DecalManager registers a RenderSystem3D::DecalExtraDrawer,
 * which runs between the G-buffer and the forward pass. For each visible
 * instance it builds the box transform (Z axis = surface normal, optional
 * random yaw) and calls gfx.drawDecal.
 */
class DecalManager {
public:
    static DecalManager &inst();

    /** @brief Spawn a decal at a world-space point facing along (nx,ny,nz). */
    int project(float x, float y, float z, float nx, float ny, float nz,
                graphics::Texture *albedo, const std::string &kind, float size, float depth,
                bool randomYaw, int seed, float fadeIn, float lifetime, float fadeOut,
                float normalStrength, float roughnessStrength, float metalStrength,
                float emissiveStrength);

    bool remove(int id);
    void clearAll();
    int count() const;

    /** @brief Per-kind quota ("blood" -> 64 etc.); oldest of the kind is evicted. */
    void setLimit(const std::string &kind, int limit);

    /** @brief Advance instance ages; expire and evict finished decals. */
    void update(float dt);

    /** @brief Draw all live instances into the active graphics decal pass. */
    void drawAll(graphics::Graphics &gfx, const graphics::Camera3D::Data &cam,
                 const glm::mat4 &viewProj, float aspect);

    std::vector<DecalInstance> &instances() { return decals_; }
    const std::vector<DecalInstance> &instances() const { return decals_; }

private:
    DecalManager() = default;
    ~DecalManager() = default;
    DecalManager(const DecalManager &) = delete;
    DecalManager &operator=(const DecalManager &) = delete;

    int nextId_ = 1;
    std::vector<DecalInstance> decals_;
    struct KindLimit {
        std::string kind;
        int limit = 0;  // 0 = unlimited
    };
    std::vector<KindLimit> limits_;
    int limitFor(const std::string &kind) const;
};

}  // namespace eve::decal
