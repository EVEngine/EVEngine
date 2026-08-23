#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "decal/DecalManager.h"

#include "graphics/Texture.h"

#include <string>

namespace eve::decal {
namespace {

class DecalQueryImpl final : public eve::IDecalQuery {
public:
    int project(float x, float y, float z, float nx, float ny, float nz, void *albedoTexture,
                const std::string &kind, float size, float depth, bool randomYaw, int seed,
                float fadeIn, float lifetime, float fadeOut) override {
        return DecalManager::inst().project(
            x, y, z, nx, ny, nz, static_cast<graphics::Texture *>(albedoTexture), kind, size,
            depth, randomYaw, seed, fadeIn, lifetime, fadeOut, 0.f, 0.f, 0.f, 0.f);
    }

    bool remove(int id) override { return DecalManager::inst().remove(id); }

    void clearAll() override { DecalManager::inst().clearAll(); }

    int count() override { return DecalManager::inst().count(); }

    void setLimit(const std::string &kind, int limit) override {
        DecalManager::inst().setLimit(kind, limit);
    }
};

}  // namespace

void registerDecalCapabilities() {
    static DecalQueryImpl impl;
    eve::cap::provide<eve::IDecalQuery>(&impl);
}

}  // namespace eve::decal
