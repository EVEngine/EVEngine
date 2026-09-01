#include "voxel/Voxel.h"

#include "voxel/FaceDir.h"

#include "data/ByteData.h"
#include "graphics/Graphics.h"
#include "procgen/heightmap/TerrainSampler.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::voxel {

Module_IMPL(Voxel, new Voxel());

CubeTypeRegistry *Voxel::newCubeTypes() { return new CubeTypeRegistry(); }

VoxelWorld *Voxel::newWorld(const CubeTypeRegistry *types) {
    return types ? new VoxelWorld(*types) : new VoxelWorld();
}

void Voxel::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Voxel::create, false);
    expose(cls);

    auto cubeTypes = table.addClass<CubeTypeRegistry>(
        "VoxelCubeTypes", std::function<CubeTypeRegistry *()>(
                              []() -> CubeTypeRegistry * { return new CubeTypeRegistry(); }),
        true);
    cubeTypes.addFunc("loadFromJson",
                      std::function<int(CubeTypeRegistry *, const std::string &)>(
                          [](CubeTypeRegistry *r, const std::string &json) {
                              return r->loadFromJson(json, nullptr);
                          }));
    cubeTypes.addFunc("count", &CubeTypeRegistry::count);
    cubeTypes.addFunc("variantCount", &CubeTypeRegistry::variantCount);
    cubeTypes.addFunc("clear", &CubeTypeRegistry::clear);

    auto world = table.addClass<VoxelWorld>(
        "VoxelWorld", std::function<VoxelWorld *()>([]() -> VoxelWorld * { return nullptr; }), true);
    world.addFunc("hasChunk", &VoxelWorld::hasChunk);
    world.addFunc("removeChunk", &VoxelWorld::removeChunk);
    world.addFunc("unloadChunksOutside", &VoxelWorld::unloadChunksOutside);
    world.addFunc(
        "streamAround",
        std::function<int(VoxelWorld *, int, int, int, int)>(
            [](VoxelWorld *w, int cx, int cy, int cz, int radius) -> int {
                return w ? w->streamAround(cx, cy, cz, radius).created : 0;
            }));
    world.addFunc(
        "setTerrain",
        std::function<void(VoxelWorld *, int, int, int, int, float, float, float)>(
            [](VoxelWorld *w, int seed, int top, int sub, int stone, float baseHeight,
               float amplitude, float scale) {
                if (!w) return;
                w->setTerrainParams(uint32_t(seed), uint8_t(top), uint8_t(sub), uint8_t(stone),
                                    baseHeight, amplitude, scale);
            }));
    world.addFunc("setTerrainParam", &VoxelWorld::setTerrainParam);
    world.addFunc("terrainHeightAt", &VoxelWorld::terrainHeightAt);
    world.addFunc(
        "loadTerrainAsset",
        std::function<bool(VoxelWorld *, data::ByteData *, float, float)>(
            [](VoxelWorld *w, data::ByteData *bytes, float offset, float scale) {
                return w && w->loadTerrainAsset(bytes, offset, scale);
            }));
    world.addFunc(
        "streamTerrainAssetAround",
        std::function<int(VoxelWorld *, int, int, int, int)>(
            [](VoxelWorld *w, int wx, int wz, int radius, int maxLoads) {
                return w ? w->streamTerrainAssetAround(wx, wz, radius, maxLoads).loaded : 0;
            }));
    world.addFunc(
        "setTerrainAssetMaterials",
        std::function<void(VoxelWorld *, int, int, int, int, int)>(
            [](VoxelWorld *w, int vegetation, int sand, int snow, int alpine, int riverbed) {
                if (w) w->setTerrainAssetMaterials(uint8_t(vegetation), uint8_t(sand),
                                                   uint8_t(snow), uint8_t(alpine),
                                                   uint8_t(riverbed));
            }));
    world.addFunc("getTerrainAssetResidentCount", &VoxelWorld::getTerrainAssetResidentCount);
    world.addFunc("disableTerrain", &VoxelWorld::disableTerrain);
    world.addFunc("saveWorld", &VoxelWorld::saveWorld);
    world.addFunc("loadWorld", &VoxelWorld::loadWorld);
    world.addFunc("clear", &VoxelWorld::clear);
    world.addFunc("getChunkCount", &VoxelWorld::getChunkCount);
    world.addFunc("getRevision", &VoxelWorld::getRevision);
    world.addFunc("remeshDirty",
                  std::function<int(VoxelWorld *)>([](VoxelWorld *w) -> int {
                      return w ? w->remeshDirty() : 0;
                  }));
    world.addFunc("getVoxel", &VoxelWorld::getVoxel);
    world.addFunc("setVoxel", &VoxelWorld::setVoxel);
    world.addFunc("setVoxelByName", &VoxelWorld::setVoxelByName);
    world.addFunc("getCubeTypeName", &VoxelWorld::getCubeTypeName);
    world.addFunc("getCubeTypeTex", &VoxelWorld::getCubeTypeTex);
    world.addFunc(
        "raycast",
        std::function<bool(VoxelWorld *, float, float, float, float, float, float, float)>(
            [](VoxelWorld *w, float ox, float oy, float oz, float dx, float dy, float dz,
               float maxDist) -> bool {
                return w && w->raycastScript(ox, oy, oz, dx, dy, dz, maxDist);
            }));
    world.addFunc("lastRaycastHit", &VoxelWorld::lastRaycastHit);
    world.addFunc("getRaycastHitX", &VoxelWorld::lastRaycastHitX);
    world.addFunc("getRaycastHitY", &VoxelWorld::lastRaycastHitY);
    world.addFunc("getRaycastHitZ", &VoxelWorld::lastRaycastHitZ);
    world.addFunc("getRaycastPrevX", &VoxelWorld::lastRaycastPrevX);
    world.addFunc("getRaycastPrevY", &VoxelWorld::lastRaycastPrevY);
    world.addFunc("getRaycastPrevZ", &VoxelWorld::lastRaycastPrevZ);
    world.addFunc("getRaycastFaceX", &VoxelWorld::lastRaycastFaceX);
    world.addFunc("getRaycastFaceY", &VoxelWorld::lastRaycastFaceY);
    world.addFunc("getRaycastFaceZ", &VoxelWorld::lastRaycastFaceZ);
    world.addFunc("getVisibleBatchCount", &VoxelWorld::getVisibleBatchCount);
    world.addFunc("getVisibleChunkCount", &VoxelWorld::getVisibleChunkCount);
    world.addFunc("getVisibleRectCount", &VoxelWorld::getVisibleRectCount);
    world.addFunc(
        "selectVisible",
        std::function<void(VoxelWorld *, float, float, float, float, float, float, float, float,
                           float, float, float, float, float, float, float, float, float, float,
                           float, float, bool)>(
            [](VoxelWorld *w, float m0, float m1, float m2, float m3, float m4, float m5, float m6,
               float m7, float m8, float m9, float m10, float m11, float m12, float m13, float m14,
               float m15, float eyeX, float eyeY, float eyeZ, float viewRange, bool faceCull) {
                if (!w) return;
                float vp[16] = {m0, m1, m2,  m3,  m4,  m5,  m6,  m7,
                                m8, m9, m10, m11, m12, m13, m14, m15};
                w->selectVisible(vp, eyeX, eyeY, eyeZ, viewRange, faceCull);
            }));
    world.addFunc("drawVisible", &VoxelWorld::drawVisible);
}

void Voxel::expose(ssq::Class &cls) {
    cls.addFunc("getChunkSize", &Voxel::getChunkSize);
    cls.addFunc("newCubeTypes", &Voxel::newCubeTypes);
    cls.addFunc("newWorld", std::function<VoxelWorld *(Voxel *)>(
                                [](Voxel *m) -> VoxelWorld * { return m->newWorld(nullptr); }));
    cls.addFunc("newWorldWithTypes",
                std::function<VoxelWorld *(Voxel *, CubeTypeRegistry *)>(
                    [](Voxel *m, CubeTypeRegistry *types) -> VoxelWorld * {
                        return m->newWorld(types);
                    }));
}

}  // namespace eve::voxel
