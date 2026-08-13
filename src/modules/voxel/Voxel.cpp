#include "voxel/Voxel.h"

#include "voxel/FaceDir.h"

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
    world.addFunc("clear", &VoxelWorld::clear);
    world.addFunc("getChunkCount", &VoxelWorld::getChunkCount);
    world.addFunc("remeshDirty", &VoxelWorld::remeshDirty);
    world.addFunc("getVoxel", &VoxelWorld::getVoxel);
    world.addFunc("setVoxel", &VoxelWorld::setVoxel);
    world.addFunc("setVoxelByName", &VoxelWorld::setVoxelByName);
    world.addFunc("getCubeTypeName", &VoxelWorld::getCubeTypeName);
    world.addFunc("getCubeTypeTex", &VoxelWorld::getCubeTypeTex);
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
