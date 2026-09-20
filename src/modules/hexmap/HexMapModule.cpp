#include "hexmap/HexMapModule.h"

#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "common/Value.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "hexmap/HexFeatures.h"
#include "hexmap/HexMapGenerator.h"
#include "hexmap/HexSphereMesh.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <utility>

namespace eve::hexmap {
namespace {

[[nodiscard]] Result<void> invalidArgument(std::string message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "hexmap"));
}

[[nodiscard]] bool validDirection(std::int32_t direction) noexcept {
    return direction >= 0 && direction < kHexDirectionCount;
}

/**
 * @brief Converts the script-facing cell address into axial coordinates.
 *
 * Scripts address cells with odd-row **offset** `(column, row)` pairs, matching
 * the reference editor's cell labels and the `hexmap` documentation.
 */
[[nodiscard]] HexCoordinates fromScriptCell(int x, int z) noexcept { return HexCoordinates::fromOffset(x, z); }

[[nodiscard]] HexDirection toDirection(std::int32_t direction) noexcept { return static_cast<HexDirection>(direction); }

}  // namespace

Module_IMPL(HexMapModule, new HexMapModule());

HexMapModule::HexMapModule()  = default;
HexMapModule::~HexMapModule() = default;

std::int32_t HexMapModule::meshSlot(std::int32_t chunkIndex, HexSurface surface) const noexcept {
    return chunkIndex * kHexSurfaceCount + static_cast<std::int32_t>(surface);
}

void HexMapModule::clearMeshSlots() noexcept {
    const auto slots = static_cast<std::size_t>(map_.chunkCount() * kHexSurfaceCount);
    meshes_.assign(slots, nullptr);
    meshPopulated_.assign(slots, 0u);
}

void HexMapModule::syncScratch() {
    scratch_.resize(map_.cellCount());
}

Result<void> HexMapModule::generateMap(graphics::Graphics* gfx, std::uint32_t seed, std::int32_t landPercentage,
                                       std::int32_t waterLevel, std::int32_t riverPercentage) {
    if (!gfx) return invalidArgument("generateMap requires a graphics device");
    if (map_.empty()) return invalidArgument("generateMap requires an existing grid");

    if (!meshes_.empty()) releaseMeshes(gfx);
    const std::int32_t cellCountX = map_.cellCountX();
    const std::int32_t cellCountZ = map_.cellCountZ();
    auto               reset      = map_.reset(cellCountX, cellCountZ, seed);
    if (!reset) return reset;
    visibility_.reset(map_.cellCount());
    units_.removeAll(map_, visibility_);
    syncScratch();
    clearMeshSlots();

    HexMapGeneratorSettings settings;
    settings.seed            = seed;
    settings.landPercentage  = landPercentage;
    settings.waterLevel      = waterLevel;
    settings.riverPercentage = riverPercentage;
    return generateHexMap(map_, settings);
}

Result<void> HexMapModule::adoptGrid(graphics::Graphics* gfx, HexMap&& restored,
                                     const std::vector<HexUnitState>& units) {
    if (!gfx) return invalidArgument("adoptGrid requires a graphics device");
    if (restored.empty()) return invalidArgument("adoptGrid requires a non-empty grid");
    if (!meshes_.empty()) releaseMeshes(gfx);
    map_ = std::move(restored);
    visibility_.reset(map_.cellCount());
    units_.removeAll(map_, visibility_);
    syncScratch();
    clearMeshSlots();
    return units_.restore(map_, visibility_, scratch_, units);
}

void HexMapModule::releaseMeshes(graphics::Graphics* gfx) {
    if (!gfx) return;
    for (graphics::Mesh*& mesh : meshes_) {
        if (!mesh) continue;
        (void)gfx->releaseMesh(mesh);
        mesh = nullptr;
    }
    meshPopulated_.assign(meshes_.size(), 0u);
}

Result<void> HexMapModule::newGrid(graphics::Graphics* gfx, std::int32_t cellCountX, std::int32_t cellCountZ,
                                   std::uint32_t seed) {
    if (!gfx) return invalidArgument("newGrid requires a graphics device");
    if (cellCountX <= 0 || cellCountZ <= 0) return invalidArgument("hex map size must be positive");
    // Script-supplied sizes reach a raw cell allocation, so bound them here rather than
    // let std::bad_alloc escape a native binding. The save format caps the same
    // quantity at kMaxHexGridDimension.
    if (cellCountX > kMaxHexGridDimension || cellCountZ > kMaxHexGridDimension)
        return invalidArgument("hex map size exceeds the " + std::to_string(kMaxHexGridDimension) +
                               " cell per side limit");
    // Reset before releasing: `HexMap::reset` validates before it mutates, so a
    // rejected size now leaves the old grid *and* its meshes untouched. Releasing
    // first detached every GPU mesh while the renderables scripts already hold kept
    // pointing at them, and the next draw threw on a null mesh.
    auto reset = map_.reset(cellCountX, cellCountZ, seed);
    if (!reset) return reset;
    if (!meshes_.empty()) releaseMeshes(gfx);
    visibility_.reset(map_.cellCount());
    units_.removeAll(map_, visibility_);
    syncScratch();
    clearMeshSlots();
    return Result<void>::success();
}

graphics::Mesh* HexMapModule::chunkMeshAt(std::int32_t chunkIndex, HexSurface surface) const {
    if (chunkIndex < 0 || chunkIndex >= map_.chunkCount()) return nullptr;
    const std::int32_t slot = meshSlot(chunkIndex, surface);
    if (slot < 0 || static_cast<std::size_t>(slot) >= meshes_.size()) return nullptr;
    // A surface that lost all geometry keeps its last GPU mesh allocated but is
    // no longer exposed, so a renderable can never be asked to draw stale data.
    if (meshPopulated_[static_cast<std::size_t>(slot)] == 0u) return nullptr;
    return meshes_[static_cast<std::size_t>(slot)];
}

std::int32_t HexMapModule::rebuildChunk(graphics::Graphics* gfx, std::int32_t chunkIndex) {
    if (!gfx || map_.empty()) return 0;
    if (chunkIndex < 0 || chunkIndex >= map_.chunkCount()) return 0;

    std::int32_t rebuilt = 0;
    HexMeshData  meshData;
    for (std::int32_t s = 0; s < kHexSurfaceCount; ++s) {
        const auto         surface   = static_cast<HexSurface>(s);
        const std::int32_t slot      = meshSlot(chunkIndex, surface);
        const auto         slotIndex = static_cast<std::size_t>(slot);
        graphics::Mesh*&   mesh      = meshes_[slotIndex];
        std::uint8_t&      populated = meshPopulated_[slotIndex];
        if (surface == HexSurface::Fog) {
            buildFogMesh(map_, visibility_, chunkIndex, meshData);
        } else if (surface == HexSurface::Wall) {
            buildWallMesh(map_, chunkIndex, meshData);
        } else if (surface == HexSurface::Feature) {
            buildFeatureMesh(map_, chunkIndex, meshData);
        } else {
            buildChunkSurfaceMesh(map_, chunkIndex, surface, meshData);
        }
        if (meshData.empty()) {
            // The surface lost all geometry (for example the last road was removed).
            // The GPU mesh is retired lazily: it stays allocated until the grid is
            // replaced, so a renderable that still references it is never dangling.
            populated = 0u;
            continue;
        }

        const int  vertexCount = static_cast<int>(meshData.vertexCount());
        const int  indexCount  = static_cast<int>(meshData.indices().size());
        const auto upload      = [&]() {
            return gfx->newMeshFromArrays(meshData.positions().data(), meshData.normals().data(), meshData.uvs().data(),
                                          vertexCount, meshData.indices().data(), indexCount);
        };

        if (mesh &&
            !gfx->updateMeshVertices(mesh, meshData.positions().data(), meshData.normals().data(),
                                     meshData.uvs().data(), vertexCount, meshData.indices().data(), indexCount)) {
            // The backend rejected the in-place update; recreate rather than keep stale geometry.
            (void)gfx->releaseMesh(mesh);
            mesh = nullptr;
        }
        if (!mesh) mesh = upload();
        populated = mesh ? 1u : 0u;
        if (mesh) ++rebuilt;
    }
    return rebuilt;
}

std::int32_t HexMapModule::rebuildDirtyChunks(graphics::Graphics* gfx) {
    if (!gfx || map_.empty()) return 0;
    std::int32_t rebuilt = 0;
    for (;;) {
        const std::int32_t chunkIndex = map_.takeDirtyChunk();
        if (chunkIndex < 0) break;
        rebuilt += rebuildChunk(gfx, chunkIndex);
    }
    return rebuilt;
}

// --- spherical map ----------------------------------------------------------

void HexMapModule::releaseSphereMeshes(graphics::Graphics* gfx) noexcept {
    if (gfx != nullptr) {
        if (sphereTerrain_ != nullptr) (void)gfx->releaseMesh(sphereTerrain_);
        if (sphereWater_ != nullptr) (void)gfx->releaseMesh(sphereWater_);
    }
    sphereTerrain_ = nullptr;
    sphereWater_   = nullptr;
}

Result<void> HexMapModule::newSphere(graphics::Graphics* gfx, std::int32_t subdivision, float radius,
                                     std::uint32_t seed) {
    if (gfx == nullptr) return invalidArgument("hex sphere needs a graphics device");
    // Same ordering rule as `newGrid`: validate through `reset` first, because
    // `HexSphereMap::reset` rejects its arguments before touching the map. Releasing
    // the sphere meshes up front left the planet example's renderables pointing at
    // GPU meshes that had already been destroyed whenever the arguments were bad.
    auto reset = sphere_.reset(subdivision, radius, seed);
    if (!reset) return reset;
    releaseSphereMeshes(gfx);
    return Result<void>::success();
}

Result<void> HexMapModule::generateSphere(graphics::Graphics* gfx, std::uint32_t seed, std::int32_t landPercentage,
                                          std::int32_t waterLevel) {
    if (gfx == nullptr) return invalidArgument("hex sphere needs a graphics device");
    if (sphere_.empty()) return invalidArgument("hex sphere map has no cells");

    HexSphereGeneratorSettings settings{};
    settings.seed           = seed;
    settings.landPercentage = landPercentage;
    settings.waterLevel     = waterLevel;

    // The generator's only failure is an empty map, which the guard above already
    // rejected, so it cannot leave the cells half-written.
    auto generated = generateSphereMap(sphere_, settings);
    if (!generated.ok()) return Result<void>::failure(generated.status());
    return rebuildSphere(gfx);
}

Result<void> HexMapModule::rebuildSphere(graphics::Graphics* gfx) {
    if (gfx == nullptr) return invalidArgument("hex sphere needs a graphics device");
    if (sphere_.empty()) return invalidArgument("hex sphere map has no cells");

    HexMeshData terrain;
    buildSphereTerrainMesh(sphere_, terrain);
    HexMeshData water;
    buildSphereWaterMesh(sphere_, water);

    bool       uploaded = true;
    const auto upload   = [&](HexMeshData& data, graphics::Mesh*& mesh) {
        if (data.empty()) {
            // Nothing is flooded any more, for example: retire the mesh rather than draw
            // stale geometry.
            if (mesh != nullptr) {
                (void)gfx->releaseMesh(mesh);
                mesh = nullptr;
            }
            return;
        }
        const int vertexCount = static_cast<int>(data.vertexCount());
        const int indexCount  = static_cast<int>(data.indices().size());
        if (mesh != nullptr &&
            !gfx->updateMeshVertices(mesh, data.positions().data(), data.normals().data(), data.uvs().data(),
                                     vertexCount, data.indices().data(), indexCount)) {
            // The backend rejected the in-place update; recreate rather than keep stale geometry.
            (void)gfx->releaseMesh(mesh);
            mesh = nullptr;
        }
        if (mesh == nullptr) {
            mesh = gfx->newMeshFromArrays(data.positions().data(), data.normals().data(), data.uvs().data(),
                                          vertexCount, data.indices().data(), indexCount);
        }
        if (mesh == nullptr) uploaded = false;
    };

    upload(terrain, sphereTerrain_);
    upload(water, sphereWater_);
    if (!uploaded) return invalidArgument("the graphics backend refused the hex sphere mesh upload");
    return Result<void>::success();
}

// --- script bindings --------------------------------------------------------

void HexMapModule::expose(ssq::Table& table) {
    // The script class is `eve.HexMap` (the manifest's SCRIPT name and the slot's
    // class), which deliberately differs from this C++ module's name.
    auto cls = table.addClass("HexMap", HexMapModule::create, false);
    expose(cls);
}

void HexMapModule::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();

    cls.addFunc("newGrid", [vm](HexMapModule* self, graphics::Graphics* gfx, int cellCountX, int cellCountZ, int seed) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        if (seed < 0) seed = 0;
        return script::projectResult(vm, self->newGrid(gfx, cellCountX, cellCountZ, static_cast<std::uint32_t>(seed)));
    });
    cls.addFunc("hasGrid", [](HexMapModule* self) { return self != nullptr && self->hasGrid(); });
    cls.addFunc("cellCountX", [](HexMapModule* self) { return self ? self->map().cellCountX() : 0; });
    cls.addFunc("cellCountZ", [](HexMapModule* self) { return self ? self->map().cellCountZ() : 0; });
    cls.addFunc("chunkCountX", [](HexMapModule* self) { return self ? self->map().chunkCountX() : 0; });
    cls.addFunc("chunkCountZ", [](HexMapModule* self) { return self ? self->map().chunkCountZ() : 0; });
    cls.addFunc("chunkCount", [](HexMapModule* self) { return self ? self->map().chunkCount() : 0; });
    cls.addFunc("seed", [](HexMapModule* self) { return self ? static_cast<std::int64_t>(self->map().seed()) : 0; });
    cls.addFunc("surfaceCount", [](HexMapModule*) { return kHexSurfaceCount; });
    cls.addFunc("dirtyChunkCount", [](HexMapModule* self) { return self ? self->map().dirtyChunkCount() : 0; });
    cls.addFunc("markAllChunksDirty", [](HexMapModule* self) {
        if (self) self->map().markAllChunksDirty();
    });
    cls.addFunc("takeDirtyChunk", [](HexMapModule* self) { return self ? self->map().takeDirtyChunk() : -1; });
    cls.addFunc("rebuildChunk", [](HexMapModule* self, graphics::Graphics* gfx, int chunkIndex) {
        return self ? self->rebuildChunk(gfx, chunkIndex) : 0;
    });
    cls.addFunc("rebuildDirtyChunks",
                [](HexMapModule* self, graphics::Graphics* gfx) { return self ? self->rebuildDirtyChunks(gfx) : 0; });
    cls.addFunc("chunkMeshAt", [](HexMapModule* self, int chunkIndex, int surface) -> graphics::Mesh* {
        if (!self || surface < 0 || surface >= kHexSurfaceCount) return nullptr;
        return self->chunkMeshAt(chunkIndex, static_cast<HexSurface>(surface));
    });
    cls.addFunc("releaseMeshes", [](HexMapModule* self, graphics::Graphics* gfx) {
        if (self) self->releaseMeshes(gfx);
    });

    cls.addFunc("chunkCenterX",
                [](HexMapModule* self, int chunkIndex) { return self ? self->map().chunkCenter(chunkIndex).x : 0.f; });
    cls.addFunc("chunkCenterY",
                [](HexMapModule* self, int chunkIndex) { return self ? self->map().chunkCenter(chunkIndex).y : 0.f; });
    cls.addFunc("chunkCenterZ",
                [](HexMapModule* self, int chunkIndex) { return self ? self->map().chunkCenter(chunkIndex).z : 0.f; });

    // --- cell queries ---
    cls.addFunc("elevation", [](HexMapModule* self, int x, int z) {
        return self ? self->map().elevation(fromScriptCell(x, z)) : 0;
    });
    cls.addFunc("waterLevel", [](HexMapModule* self, int x, int z) {
        return self ? self->map().waterLevel(fromScriptCell(x, z)) : 0;
    });
    cls.addFunc("terrainType", [](HexMapModule* self, int x, int z) {
        return self ? self->map().terrainType(fromScriptCell(x, z)) : 0;
    });
    cls.addFunc("urbanLevel", [](HexMapModule* self, int x, int z) {
        return self ? self->map().urbanLevel(fromScriptCell(x, z)) : 0;
    });
    cls.addFunc("farmLevel", [](HexMapModule* self, int x, int z) {
        return self ? self->map().farmLevel(fromScriptCell(x, z)) : 0;
    });
    cls.addFunc("plantLevel", [](HexMapModule* self, int x, int z) {
        return self ? self->map().plantLevel(fromScriptCell(x, z)) : 0;
    });
    cls.addFunc("specialIndex", [](HexMapModule* self, int x, int z) {
        return self ? self->map().specialIndex(fromScriptCell(x, z)) : 0;
    });
    cls.addFunc("isUnderwater", [](HexMapModule* self, int x, int z) {
        return self != nullptr && self->map().isUnderwater(fromScriptCell(x, z));
    });
    cls.addFunc("hasRiver", [](HexMapModule* self, int x, int z) {
        return self != nullptr && self->map().hasRiver(fromScriptCell(x, z));
    });
    cls.addFunc("hasRoad", [](HexMapModule* self, int x, int z) {
        return self != nullptr && self->map().hasRoad(fromScriptCell(x, z));
    });
    cls.addFunc("isWalled", [](HexMapModule* self, int x, int z) {
        return self != nullptr && self->map().isWalled(fromScriptCell(x, z));
    });
    cls.addFunc("cellPositionX", [](HexMapModule* self, int x, int z) {
        return self ? self->map().cellPosition(fromScriptCell(x, z)).x : 0.f;
    });
    cls.addFunc("cellPositionY", [](HexMapModule* self, int x, int z) {
        return self ? self->map().cellPosition(fromScriptCell(x, z)).y : 0.f;
    });
    cls.addFunc("cellPositionZ", [](HexMapModule* self, int x, int z) {
        return self ? self->map().cellPosition(fromScriptCell(x, z)).z : 0.f;
    });

    // --- picking ---
    cls.addFunc("pickCell", [vm](HexMapModule* self, float ox, float oy, float oz, float dx, float dy, float dz) {
        // Scripts address cells with odd-row **offset** `(column, row)` pairs, which
        // is what every other binding converts through `fromScriptCell`. The picked
        // cell has to come back in the same space, or feeding it straight back into
        // `elevation(x, z)` / `editElevation(x, z, ...)` would address a different
        // cell (and would report negative columns along the offset row's left edge).
        const auto project = [](HexCoordinates coordinates) {
            return Value(Value::Array{Value(static_cast<std::int64_t>(coordinates.offsetX())),
                                      Value(static_cast<std::int64_t>(coordinates.offsetZ()))});
        };
        if (!self)
            return script::projectResult(
                vm,
                Result<HexCoordinates>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "hex map module is not available", "hexmap.pick")),
                project);
        return script::projectResult(vm, self->map().pickCell(HexVec3{ox, oy, oz}, HexVec3{dx, dy, dz}), project);
    });

    // --- cell authoring ---
    cls.addFunc("setElevation", [vm](HexMapModule* self, int x, int z, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setElevation(fromScriptCell(x, z), value));
    });
    cls.addFunc("setWaterLevel", [vm](HexMapModule* self, int x, int z, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setWaterLevel(fromScriptCell(x, z), value));
    });
    cls.addFunc("setTerrainType", [vm](HexMapModule* self, int x, int z, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setTerrainType(fromScriptCell(x, z), value));
    });
    cls.addFunc("setUrbanLevel", [vm](HexMapModule* self, int x, int z, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setUrbanLevel(fromScriptCell(x, z), value));
    });
    cls.addFunc("setFarmLevel", [vm](HexMapModule* self, int x, int z, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setFarmLevel(fromScriptCell(x, z), value));
    });
    cls.addFunc("setPlantLevel", [vm](HexMapModule* self, int x, int z, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setPlantLevel(fromScriptCell(x, z), value));
    });
    cls.addFunc("setSpecialIndex", [vm](HexMapModule* self, int x, int z, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setSpecialIndex(fromScriptCell(x, z), value));
    });
    cls.addFunc("setWalled", [vm](HexMapModule* self, int x, int z, bool value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setWalled(fromScriptCell(x, z), value));
    });
    cls.addFunc("setOutgoingRiver", [vm](HexMapModule* self, int x, int z, int direction) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        if (!validDirection(direction))
            return script::projectResult(vm, invalidArgument("river direction must be in [0, 5]"));
        return script::projectResult(vm, self->map().setOutgoingRiver(fromScriptCell(x, z), toDirection(direction)));
    });
    cls.addFunc("removeRiver", [vm](HexMapModule* self, int x, int z) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().removeRiver(fromScriptCell(x, z)));
    });
    cls.addFunc("addRoad", [vm](HexMapModule* self, int x, int z, int direction) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        if (!validDirection(direction))
            return script::projectResult(vm, invalidArgument("road direction must be in [0, 5]"));
        return script::projectResult(vm, self->map().addRoad(fromScriptCell(x, z), toDirection(direction)));
    });
    cls.addFunc("removeRoads", [vm](HexMapModule* self, int x, int z) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().removeRoads(fromScriptCell(x, z)));
    });

    // --- brush authoring ---
    cls.addFunc("editElevation", [vm](HexMapModule* self, int x, int z, int radius, int delta) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().editElevation(fromScriptCell(x, z), radius, delta));
    });
    cls.addFunc("editWaterLevel", [vm](HexMapModule* self, int x, int z, int radius, int delta) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().editWaterLevel(fromScriptCell(x, z), radius, delta));
    });
    cls.addFunc("editTerrainType", [vm](HexMapModule* self, int x, int z, int radius, int terrainType) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().editTerrainType(fromScriptCell(x, z), radius, terrainType));
    });
    cls.addFunc("editFeatureLevel", [vm](HexMapModule* self, int x, int z, int radius, int feature, int delta) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().editFeatureLevel(fromScriptCell(x, z), radius, feature, delta));
    });

    // --- generation ---
    cls.addFunc("generateMap", [vm](HexMapModule* self, graphics::Graphics* gfx, int seed, int landPercentage,
                                    int waterLevel, int riverPercentage) {
        if (!self || !gfx) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        if (seed < 0) seed = 0;
        return script::projectResult(vm, self->generateMap(gfx, static_cast<std::uint32_t>(seed), landPercentage,
                                                           waterLevel, riverPercentage));
    });

    // --- fog of war ---
    cls.addFunc("isExplored", [](HexMapModule* self, int x, int z) {
        return self != nullptr && self->map().isExplored(fromScriptCell(x, z));
    });
    cls.addFunc("isExplorable", [](HexMapModule* self, int x, int z) {
        return self != nullptr && self->map().isExplorable(fromScriptCell(x, z));
    });
    cls.addFunc("setExplored", [vm](HexMapModule* self, int x, int z, bool value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setExplored(fromScriptCell(x, z), value));
    });
    cls.addFunc("setExplorable", [vm](HexMapModule* self, int x, int z, bool value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->map().setExplorable(fromScriptCell(x, z), value));
    });
    cls.addFunc("isCellVisible", [](HexMapModule* self, int x, int z) {
        if (!self) return false;
        const std::int32_t index = self->map().indexOf(fromScriptCell(x, z));
        return index >= 0 && self->visibility().isVisible(index);
    });
    cls.addFunc("visibleCellCount",
                [](HexMapModule* self) { return self ? self->visibility().visibleCellCount() : 0; });
    cls.addFunc("resetVisibility", [](HexMapModule* self) {
        if (self) self->units().refreshVisibility(self->map(), self->visibility(), self->scratch());
    });

    // --- units ---
    cls.addFunc("unitCount", [](HexMapModule* self) { return self ? self->units().unitCount() : 0; });
    cls.addFunc("addUnit", [vm](HexMapModule* self, int x, int z, float orientation) {
        if (!self) {
            return script::projectResult(
                vm,
                Result<std::int32_t>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "hex map module is not available", "hexmap")),
                [](std::int32_t id) { return Value(static_cast<std::int64_t>(id)); });
        }
        return script::projectResult(
            vm, self->units().addUnit(self->map(), self->visibility(), self->scratch(), fromScriptCell(x, z), orientation),
            [](std::int32_t id) { return Value(static_cast<std::int64_t>(id)); });
    });
    cls.addFunc("removeUnit", [vm](HexMapModule* self, int unitId) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(
            vm, self->units().removeUnit(self->map(), self->visibility(), self->scratch(), unitId));
    });
    cls.addFunc("removeAllUnits", [](HexMapModule* self) {
        if (self) self->units().removeAll(self->map(), self->visibility());
    });
    cls.addFunc("unitIdAt", [](HexMapModule* self, int x, int z) {
        return self ? self->units().unitIdAt(fromScriptCell(x, z)) : -1;
    });
    // Sample layout: [offsetX, offsetZ, worldX, worldY, worldZ, orientation, traveling].
    cls.addFunc("unitSample", [vm](HexMapModule* self, int unitId) {
        const auto project = [](const HexUnitSample& sample) {
            return Value(Value::Array{
                Value(static_cast<std::int64_t>(sample.location.offsetX())),
                Value(static_cast<std::int64_t>(sample.location.offsetZ())),
                Value(static_cast<double>(sample.position.x)),
                Value(static_cast<double>(sample.position.y)),
                Value(static_cast<double>(sample.position.z)),
                Value(static_cast<double>(sample.orientation)),
                Value(static_cast<std::int64_t>(sample.traveling ? 1 : 0)),
            });
        };
        if (!self) {
            return script::projectResult(
                vm,
                Result<HexUnitSample>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "hex map module is not available", "hexmap")),
                project);
        }
        return script::projectResult(vm, self->units().advance(self->map(), self->visibility(), self->scratch(), unitId, 0.f),
                                     project);
    });
    cls.addFunc("travelUnit", [vm](HexMapModule* self, int unitId, ssq::Object path) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        auto owned = script::valueFromSquirrel(path);
        if (!owned) return script::projectResult(vm, invalidArgument("unit path must be an array of [x, z] cells"));
        if (!owned.value().isArray())
            return script::projectResult(vm, invalidArgument("unit path must be an array of [x, z] cells"));
        std::vector<std::int32_t> cells;
        const Value&              list = owned.value();
        cells.reserve(list.arraySize());
        for (std::size_t i = 0; i < list.arraySize(); ++i) {
            const Value& entry = list.at(i);
            if (!entry.isArray() || entry.arraySize() < 2)
                return script::projectResult(vm, invalidArgument("unit path entries must be [x, z] pairs"));
            const Value& ax = entry.at(0);
            const Value& az = entry.at(1);
            if (!ax.isNumeric() || !az.isNumeric())
                return script::projectResult(vm, invalidArgument("unit path entries must hold numeric cells"));
            const std::int32_t index =
                self->map().indexOf(fromScriptCell(static_cast<int>(ax.asInt()), static_cast<int>(az.asInt())));
            if (index < 0) return script::projectResult(vm, invalidArgument("unit path leaves the hex map"));
            cells.push_back(index);
        }
        return script::projectResult(vm,
                                     self->units().beginTravel(self->map(), self->visibility(), self->scratch(), unitId,
                                                               cells));
    });
    cls.addFunc("advanceUnits", [](HexMapModule* self, float dt) {
        if (self) self->units().advanceAll(self->map(), self->visibility(), self->scratch(), dt);
    });

    // --- pathfinding ---
    // Result layout: an array of [offsetX, offsetZ, turn] triples from origin to goal.
    cls.addFunc("findPath", [vm](HexMapModule* self, int fromX, int fromZ, int toX, int toZ) {
        const auto project = [self](const HexPath& path) {
            Value::Array entries;
            entries.reserve(path.size());
            for (std::size_t i = 0; i < path.cells.size(); ++i) {
                const HexCoordinates coordinates = self ? self->map().coordinatesAt(path.cells[i]) : HexCoordinates{};
                const std::int32_t turn          = i < path.turns.size() ? path.turns[i] : 0;
                entries.push_back(Value(Value::Array{Value(static_cast<std::int64_t>(coordinates.offsetX())),
                                                     Value(static_cast<std::int64_t>(coordinates.offsetZ())),
                                                     Value(static_cast<std::int64_t>(turn))}));
            }
            return Value(std::move(entries));
        };
        if (!self) {
            return script::projectResult(
                vm,
                Result<HexPath>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "hex map module is not available", "hexmap")),
                project);
        }
        return script::projectResult(
            vm,
            findPath(self->map(), self->scratch(), fromScriptCell(fromX, fromZ), fromScriptCell(toX, toZ),
                     self->units().tuning().moveRules(), self->units().occupancyQuery()),
            project);
    });

    // --- persistence ---
    cls.addFunc("saveMap", [vm](HexMapModule* self) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        std::vector<std::uint8_t> bytes;
        auto                      saved = saveHexMap(self->map(), self->units().snapshot(), bytes);
        if (!saved) return script::projectResult(vm, std::move(saved));
        return script::projectStatusResult(
            vm, saved.status(), true, true,
            Value::string(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
    });
    cls.addFunc("loadMap", [vm](HexMapModule* self, graphics::Graphics* gfx, std::string blob) {
        if (!self || !gfx) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        std::vector<std::uint8_t> bytes(blob.begin(), blob.end());
        HexMap                    restored;
        std::vector<HexUnitState> units;
        auto                      loaded = loadHexMap(bytes, restored, units);
        if (!loaded) return script::projectResult(vm, std::move(loaded));
        return script::projectResult(vm, self->adoptGrid(gfx, std::move(restored), units));
    });

    // --- spherical map ---
    cls.addFunc(
        "newSphere", [vm](HexMapModule* self, graphics::Graphics* gfx, int subdivision, float radius, int seed) {
            if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
            if (seed < 0) seed = 0;
            return script::projectResult(vm,
                                         self->newSphere(gfx, subdivision, radius, static_cast<std::uint32_t>(seed)));
        });
    cls.addFunc("generateSphere", [vm](HexMapModule* self, graphics::Graphics* gfx, int seed, int landPercentage,
                                       int waterLevel) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        if (seed < 0) seed = 0;
        return script::projectResult(
            vm, self->generateSphere(gfx, static_cast<std::uint32_t>(seed), landPercentage, waterLevel));
    });
    cls.addFunc("rebuildSphere", [vm](HexMapModule* self, graphics::Graphics* gfx) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->rebuildSphere(gfx));
    });
    cls.addFunc("sphereReady", [](HexMapModule* self) { return self != nullptr && !self->sphere().empty(); });
    cls.addFunc("sphereCellCount", [](HexMapModule* self) { return self ? self->sphere().cellCount() : 0; });
    cls.addFunc("spherePentagonCount", [](HexMapModule* self) { return self ? self->sphere().pentagonCount() : 0; });
    cls.addFunc("sphereSubdivision", [](HexMapModule* self) { return self ? self->sphere().subdivision() : 0; });
    cls.addFunc("sphereRadius", [](HexMapModule* self) { return self ? self->sphere().sphereRadius() : 0.f; });
    cls.addFunc("sphereElevationStep", [](HexMapModule* self) { return self ? self->sphere().elevationStep() : 0.f; });
    cls.addFunc("sphereCellSpacing", [](HexMapModule* self) { return self ? self->sphere().cellSpacing() : 0.f; });
    cls.addFunc("sphereIsPentagon",
                [](HexMapModule* self, int cell) { return self != nullptr && self->sphere().isPentagon(cell); });
    cls.addFunc("sphereNeighborCount",
                [](HexMapModule* self, int cell) { return self ? self->sphere().neighborCount(cell) : 0; });
    cls.addFunc("sphereNeighbor", [](HexMapModule* self, int cell, int direction) {
        return self ? self->sphere().neighbor(cell, direction) : kNoHexSphereCell;
    });
    cls.addFunc("sphereDirection", [](HexMapModule* self, int cell) {
        const HexVec3 direction = self ? self->sphere().direction(cell) : HexVec3{};
        return Value(Value::Array{Value(direction.x), Value(direction.y), Value(direction.z)});
    });
    cls.addFunc("sphereCornerDirection", [](HexMapModule* self, int cell, int corner) {
        const HexVec3 direction = self ? self->sphere().cornerDirection(cell, corner) : HexVec3{};
        return Value(Value::Array{Value(direction.x), Value(direction.y), Value(direction.z)});
    });
    cls.addFunc("sphereCornerCount",
                [](HexMapModule* self, int cell) { return self ? self->sphere().cornerCountOf(cell) : 0; });
    cls.addFunc("sphereElevation", [](HexMapModule* self, int cell) { return self ? self->sphere().elevation(cell) : 0; });
    cls.addFunc("sphereWaterLevel",
                [](HexMapModule* self, int cell) { return self ? self->sphere().waterLevel(cell) : 0; });
    cls.addFunc("sphereTerrainType",
                [](HexMapModule* self, int cell) { return self ? self->sphere().terrainType(cell) : 0; });
    cls.addFunc("sphereIsUnderwater",
                [](HexMapModule* self, int cell) { return self != nullptr && self->sphere().isUnderwater(cell); });
    cls.addFunc("sphereCellAt", [](HexMapModule* self, float x, float y, float z) {
        return self ? self->sphere().topology().cellAt(HexVec3{x, y, z}) : kNoHexSphereCell;
    });
    cls.addFunc("sphereDistance", [](HexMapModule* self, int a, int b) {
        return self ? self->sphere().distance(a, b) : kNoHexSphereCell;
    });
    cls.addFunc("spherePickCell",
                [vm](HexMapModule* self, float ox, float oy, float oz, float dx, float dy, float dz) {
                    const auto project = [](HexSphereCell cell) { return Value(static_cast<std::int64_t>(cell)); };
                    if (!self) {
                        return script::projectResult(
                            vm,
                            Result<HexSphereCell>::failure(Diagnostic::error(
                                DiagnosticCode::NotFound, "hex map module is not available", "hexmap.sphere.pick")),
                            project);
                    }
                    return script::projectResult(vm,
                                                 self->sphere().pickCell(HexVec3{ox, oy, oz}, HexVec3{dx, dy, dz}),
                                                 project);
                });
    cls.addFunc("sphereSetElevation", [vm](HexMapModule* self, int cell, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->sphere().setElevation(cell, value));
    });
    cls.addFunc("sphereSetTerrainType", [vm](HexMapModule* self, int cell, int value) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->sphere().setTerrainType(cell, value));
    });
    cls.addFunc("sphereEditElevation", [vm](HexMapModule* self, int cell, int radius, int delta) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->sphere().editElevation(cell, radius, delta));
    });
    cls.addFunc("sphereEditTerrainType", [vm](HexMapModule* self, int cell, int radius, int terrainType) {
        if (!self) return script::projectResult(vm, invalidArgument("hex map module is not available"));
        return script::projectResult(vm, self->sphere().editTerrainType(cell, radius, terrainType));
    });
    cls.addFunc("sphereTerrainMesh",
                [](HexMapModule* self) -> graphics::Mesh* { return self ? self->sphereTerrainMesh() : nullptr; });
    cls.addFunc("sphereWaterMesh",
                [](HexMapModule* self) -> graphics::Mesh* { return self ? self->sphereWaterMesh() : nullptr; });
    cls.addFunc("sphereReleaseMeshes", [](HexMapModule* self, graphics::Graphics* gfx) {
        if (self) self->releaseSphereMeshes(gfx);
    });
}

}  // namespace eve::hexmap
