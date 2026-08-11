#include "voxel/Voxel.h"

#include "voxel/FaceDir.h"
#include "voxel/GreedyMesher.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstring>
#include <functional>
#include <vector>

namespace eve::voxel {

Module_IMPL(Voxel, new Voxel());

namespace {
std::vector<PackedRect> g_meshFaces[6];
}

VoxelWorld *Voxel::newWorld() { return new VoxelWorld(); }

Chunk *Voxel::newChunk(int cx, int cy, int cz) { return new Chunk(cx, cy, cz); }

uint32_t Voxel::packRect(int x, int y, int z, int width, int height, int tex) const {
    return PackedRect::pack(x, y, z, width, height, tex).bits;
}

int Voxel::unpackRectX(uint32_t bits) const { return PackedRect{bits}.x(); }
int Voxel::unpackRectY(uint32_t bits) const { return PackedRect{bits}.y(); }
int Voxel::unpackRectZ(uint32_t bits) const { return PackedRect{bits}.z(); }
int Voxel::unpackRectWidth(uint32_t bits) const { return PackedRect{bits}.width(); }
int Voxel::unpackRectHeight(uint32_t bits) const { return PackedRect{bits}.height(); }
int Voxel::unpackRectTex(uint32_t bits) const { return PackedRect{bits}.tex(); }

void Voxel::meshVoxels(const uint8_t *voxels, int byteCount) {
    if (!voxels || byteCount < kChunkSize * kChunkSize * kChunkSize)
        throw Exception("Voxel.meshVoxels: need at least 32*32*32 bytes");
    GreedyMesher::meshChunk(voxels, g_meshFaces);
}

int Voxel::getMeshFaceCount(const std::string &faceDir) const {
    FaceDir d;
    if (!faceDirFromName(faceDir, d)) return 0;
    return int(g_meshFaces[int(d)].size());
}

uint32_t Voxel::getMeshFacePacked(const std::string &faceDir, int index) const {
    FaceDir d;
    if (!faceDirFromName(faceDir, d)) return 0;
    const auto &v = g_meshFaces[int(d)];
    if (index < 0 || index >= int(v.size())) return 0;
    return v[size_t(index)].bits;
}

void Voxel::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Voxel::create, false);
    expose(cls);

    auto chunk = table.addClass<Chunk>(
        "VoxelChunk", std::function<Chunk *()>([]() -> Chunk * { return nullptr; }), true);
    chunk.addFunc("getCx", &Chunk::cx);
    chunk.addFunc("getCy", &Chunk::cy);
    chunk.addFunc("getCz", &Chunk::cz);
    chunk.addFunc("getOriginX", &Chunk::originX);
    chunk.addFunc("getOriginY", &Chunk::originY);
    chunk.addFunc("getOriginZ", &Chunk::originZ);
    chunk.addFunc("get", &Chunk::get);
    chunk.addFunc("set", &Chunk::set);
    chunk.addFunc("fill", &Chunk::fill);
    chunk.addFunc("clear", &Chunk::clear);
    chunk.addFunc("isDirty", &Chunk::isDirty);
    chunk.addFunc("remesh", &Chunk::remesh);
    chunk.addFunc("ensureMeshed", &Chunk::ensureMeshed);
    chunk.addFunc("getFaceRectCount",
                  std::function<int(Chunk *, const std::string &)>([](Chunk *c, const std::string &n) {
                      if (!c) return 0;
                      FaceDir d;
                      if (!faceDirFromName(n, d)) return 0;
                      return c->faceRectCount(d);
                  }));
    chunk.addFunc("getTotalRectCount", &Chunk::totalRectCount);

    auto world = table.addClass<VoxelWorld>(
        "VoxelWorld", std::function<VoxelWorld *()>([]() -> VoxelWorld * { return nullptr; }), true);
    world.addFunc("getOrCreateChunk", &VoxelWorld::getOrCreateChunk);
    world.addFunc("getChunk",
                  static_cast<Chunk *(VoxelWorld::*)(int, int, int)>(&VoxelWorld::getChunk));
    world.addFunc("hasChunk", &VoxelWorld::hasChunk);
    world.addFunc("removeChunk", &VoxelWorld::removeChunk);
    world.addFunc("clear", &VoxelWorld::clear);
    world.addFunc("getChunkCount", &VoxelWorld::getChunkCount);
    world.addFunc("remeshDirty", &VoxelWorld::remeshDirty);
    world.addFunc("getVoxel", &VoxelWorld::getVoxel);
    world.addFunc("setVoxel", &VoxelWorld::setVoxel);
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
    cls.addFunc("newWorld", &Voxel::newWorld);
    cls.addFunc("newChunk", &Voxel::newChunk);
    cls.addFunc("packRect", &Voxel::packRect);
    cls.addFunc("unpackRectX", &Voxel::unpackRectX);
    cls.addFunc("unpackRectY", &Voxel::unpackRectY);
    cls.addFunc("unpackRectZ", &Voxel::unpackRectZ);
    cls.addFunc("unpackRectWidth", &Voxel::unpackRectWidth);
    cls.addFunc("unpackRectHeight", &Voxel::unpackRectHeight);
    cls.addFunc("unpackRectTex", &Voxel::unpackRectTex);
    cls.addFunc("getMeshFaceCount", &Voxel::getMeshFaceCount);
    cls.addFunc("getMeshFacePacked", &Voxel::getMeshFacePacked);
}

}  // namespace eve::voxel
