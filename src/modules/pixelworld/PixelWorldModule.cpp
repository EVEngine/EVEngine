#include "pixelworld/PixelWorldModule.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>

namespace eve::pixelworld {

void registerPixelWorldAutomation();
void unregisterPixelWorldAutomation();

Module_IMPL(PixelWorldModule, new PixelWorldModule());

PixelWorldModule::PixelWorldModule() { registerPixelWorldAutomation(); }
PixelWorldModule::~PixelWorldModule() { unregisterPixelWorldAutomation(); }

PixelWorld* PixelWorldModule::newWorld(std::uint64_t seed) { return new PixelWorld(seed); }

void PixelWorldModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, PixelWorldModule::create, false);
    expose(module);

    auto world = table.addClass<PixelWorld>(
        "PixelWorld", std::function<PixelWorld*()>([]() -> PixelWorld* { return nullptr; }), true);
    world.addFunc("getMaterial", [](PixelWorld* self, int x, int y) { return self->getMaterial(x, y); });
    world.addFunc("setMaterial", [](PixelWorld* self, int x, int y, const std::string& material) {
        self->setMaterial(x, y, material);
    });
    world.addFunc("paintCircle", [](PixelWorld* self, int x, int y, int radius, const std::string& material) {
        return int(self->paintCircle(x, y, radius, material));
    });
    world.addFunc("step", [](PixelWorld* self) {
        return int(self->step().cellsMoved);
    });
    world.addFunc("clear", [](PixelWorld* self) { self->clear(); });
    world.addFunc("getSeed", [](PixelWorld* self) { return std::int64_t(self->seed()); });
    world.addFunc("getRevision", [](PixelWorld* self) { return std::int64_t(self->revision()); });
    world.addFunc("getTick", [](PixelWorld* self) { return std::int64_t(self->tickValue()); });
    world.addFunc("getLastEditSequence", [](PixelWorld* self) {
        return std::int64_t(self->lastEditSequence());
    });
    world.addFunc("getChunkCount", [](PixelWorld* self) { return self->chunkCount(); });
    world.addFunc("getActiveChunkCount", [](PixelWorld* self) { return self->activeChunkCount(); });
    world.addFunc("getChangedChunkCountSince", [](PixelWorld* self, int revision) {
        return int(self->snapshotChangedChunks(revision < 0 ? 0U : std::uint64_t(revision)).size());
    });
    world.addFunc("explode", [](PixelWorld* self, int x, int y, int radius, int strength, int heat) {
        return int(self->explode(x, y, radius, strength,
                                 std::int16_t(std::clamp(heat, -32768, 32767))).cellsRemoved);
    });
}

void PixelWorldModule::expose(ssq::Class& cls) {
    cls.addFunc("newWorld", [](PixelWorldModule* self, int seed) {
        return self->newWorld(std::uint64_t(std::uint32_t(seed)));
    });
}

}  // namespace eve::pixelworld
