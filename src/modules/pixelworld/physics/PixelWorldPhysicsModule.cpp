#include "pixelworld/physics/PixelWorldPhysicsModule.h"

#include "pixelworld/physics/PixelWorldPhysics.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <stdexcept>

namespace eve::pixelworld_physics {

Module_IMPL(PixelWorldPhysics, new PixelWorldPhysics());

PixelTerrainCollisionCache* PixelWorldPhysics::newTerrainCache() {
    return new PixelTerrainCollisionCache();
}

void PixelWorldPhysics::expose(ssq::Table& table) {
    auto module = table.addClass(name, PixelWorldPhysics::create, false);
    expose(module);

    auto cache = table.addClass<PixelTerrainCollisionCache>(
        "PixelTerrainCollisionCache",
        std::function<PixelTerrainCollisionCache*()>([]() -> PixelTerrainCollisionCache* { return nullptr; }), true);
    cache.addFunc("sync", [](PixelTerrainCollisionCache* self, eve::physics::World* physics,
                             eve::pixelworld::PixelWorld* pixels, int maximumFixtures) {
        if (!self || !physics || !pixels) throw std::runtime_error("terrain sync requires cache and live worlds");
        if (maximumFixtures <= 0) throw std::runtime_error("maximumFixtures must be positive");
        auto result = self->sync(*physics, *pixels, static_cast<std::uint32_t>(maximumFixtures));
        if (!result) throw std::runtime_error(result.status().describe());
        return int(result.value().fixturesCreated);
    });
    cache.addFunc("clear", [](PixelTerrainCollisionCache* self, eve::physics::World* physics) {
        if (!self || !physics) throw std::runtime_error("terrain clear requires cache and live physics world");
        auto result = self->clearPhysics(*physics);
        if (!result) throw std::runtime_error(result.status().describe());
    });
    cache.addFunc("getSourceRevision", [](PixelTerrainCollisionCache* self) {
        return std::int64_t(self ? self->sourceRevision() : 0);
    });
    cache.addFunc("getBodyCount", [](PixelTerrainCollisionCache* self) {
        return int(self ? self->bodyCount() : 0);
    });
}

void PixelWorldPhysics::expose(ssq::Class& cls) {
    cls.addFunc("newTerrainCache", &PixelWorldPhysics::newTerrainCache);
}

}  // namespace eve::pixelworld_physics
