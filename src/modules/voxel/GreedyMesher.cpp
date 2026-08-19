#include "voxel/GreedyMesher.h"

#include <cstring>

namespace eve::voxel {

uint8_t GreedyMesher::at(const uint8_t *v, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize) return 0;
    return v[x + y * kChunkSize + z * kChunkSize * kChunkSize];
}

uint8_t GreedyMesher::neighborAt(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                                 int chunkX, int chunkY, int chunkZ, int nx, int ny, int nz) {
    if (nx >= 0 && ny >= 0 && nz >= 0 && nx < kChunkSize && ny < kChunkSize && nz < kChunkSize)
        return voxels[nx + ny * kChunkSize + nz * kChunkSize * kChunkSize];
    if (sampler) return sampler(userData, chunkX, chunkY, chunkZ, nx, ny, nz);
    return 0;
}

uint8_t GreedyMesher::outsideVoxel(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                                   int chunkX, int chunkY, int chunkZ, FaceDir dir, int slice,
                                   int gu, int gv) {
    int x = 0, y = 0, z = 0;
    switch (dir) {
        case FaceDir::PosX: x = slice + 1; y = gv; z = gu; break;
        case FaceDir::NegX: x = slice - 1; y = gv; z = gu; break;
        case FaceDir::PosY: x = gu; y = slice + 1; z = gv; break;
        case FaceDir::NegY: x = gu; y = slice - 1; z = gv; break;
        case FaceDir::PosZ: x = gu; y = gv; z = slice + 1; break;
        case FaceDir::NegZ:
        default: x = gu; y = gv; z = slice - 1; break;
    }
    return neighborAt(voxels, sampler, userData, chunkX, chunkY, chunkZ, x, y, z);
}

uint8_t GreedyMesher::vertexAO(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                               int chunkX, int chunkY, int chunkZ, FaceDir dir, int slice, int gu,
                               int gv) {
    const uint8_t e1 = outsideVoxel(voxels, sampler, userData, chunkX, chunkY, chunkZ, dir, slice,
                                    gu - 1, gv);
    const uint8_t e2 = outsideVoxel(voxels, sampler, userData, chunkX, chunkY, chunkZ, dir, slice,
                                    gu, gv - 1);
    const uint8_t d = outsideVoxel(voxels, sampler, userData, chunkX, chunkY, chunkZ, dir, slice,
                                   gu - 1, gv - 1);
    if (isSolid(e1) && isSolid(e2)) return 0;
    return uint8_t(3 - (isSolid(e1) ? 1 : 0) - (isSolid(e2) ? 1 : 0) - (isSolid(d) ? 1 : 0));
}

uint32_t GreedyMesher::rectAOWord(const uint8_t *voxels, ChunkSampler sampler, void *userData,
                                  int chunkX, int chunkY, int chunkZ, FaceDir dir, int slice,
                                  int x, int y, int z, int w, int h) {
    // Grid vertices in shader corner order (see decodePackedRect mirror).
    int gu[4], gv[4];
    switch (dir) {
        case FaceDir::PosX:
            gu[0] = z; gv[0] = y; gu[1] = z + w; gv[1] = y;
            gu[2] = z + w; gv[2] = y + h; gu[3] = z; gv[3] = y + h;
            break;
        case FaceDir::NegX:
            gu[0] = z + w; gv[0] = y; gu[1] = z; gv[1] = y;
            gu[2] = z; gv[2] = y + h; gu[3] = z + w; gv[3] = y + h;
            break;
        case FaceDir::PosY:
            gu[0] = x; gv[0] = z; gu[1] = x + w; gv[1] = z;
            gu[2] = x + w; gv[2] = z + h; gu[3] = x; gv[3] = z + h;
            break;
        case FaceDir::NegY:
            gu[0] = x; gv[0] = z + h; gu[1] = x + w; gv[1] = z + h;
            gu[2] = x + w; gv[2] = z; gu[3] = x; gv[3] = z;
            break;
        case FaceDir::PosZ:
            gu[0] = x + w; gv[0] = y; gu[1] = x; gv[1] = y;
            gu[2] = x; gv[2] = y + h; gu[3] = x + w; gv[3] = y + h;
            break;
        case FaceDir::NegZ:
        default:
            gu[0] = x; gv[0] = y; gu[1] = x + w; gv[1] = y;
            gu[2] = x + w; gv[2] = y + h; gu[3] = x; gv[3] = y + h;
            break;
    }
    uint32_t word = 0;
    for (int i = 0; i < 4; ++i) {
        const uint32_t ao =
            uint32_t(vertexAO(voxels, sampler, userData, chunkX, chunkY, chunkZ, dir, slice,
                              gu[i], gv[i]));
        word |= ao << (2 * i);
    }
    return word;
}

void GreedyMesher::greedy2D(int mask[kChunkSize][kChunkSize], FaceDir dir, int slice,
                            std::vector<PackedRect> &out, std::vector<uint32_t> *aoOut,
                            const uint8_t *voxels, ChunkSampler sampler, void *userData,
                            int chunkX, int chunkY, int chunkZ) {
    for (int v = 0; v < kChunkSize; ++v) {
        for (int u = 0; u < kChunkSize;) {
            const int tex = mask[v][u];
            if (tex < 0) {  // 空
                ++u;
                continue;
            }

            int w = 1;
            while (u + w < kChunkSize && mask[v][u + w] == tex) ++w;

            int h = 1;
            bool grow = true;
            while (v + h < kChunkSize && grow) {
                for (int k = 0; k < w; ++k) {
                    if (mask[v + h][u + k] != tex) {
                        grow = false;
                        break;
                    }
                }
                if (grow) ++h;
            }

            for (int dv = 0; dv < h; ++dv)
                for (int du = 0; du < w; ++du) mask[v + dv][u + du] = -1;

            int x = 0, y = 0, z = 0;
            int rw = w, rh = h;
            switch (dir) {
                case FaceDir::PosX:
                case FaceDir::NegX:
                    x = slice;
                    y = v;
                    z = u;
                    rw = w;  // along Z
                    rh = h;  // along Y
                    break;
                case FaceDir::PosY:
                case FaceDir::NegY:
                    x = u;
                    y = slice;
                    z = v;
                    rw = w;  // along X
                    rh = h;  // along Z
                    break;
                case FaceDir::PosZ:
                case FaceDir::NegZ:
                default:
                    x = u;
                    y = v;
                    z = slice;
                    rw = w;  // along X
                    rh = h;  // along Y
                    break;
            }
            out.push_back(PackedRect::pack(x, y, z, rw, rh, tex));
            if (aoOut) {
                aoOut->push_back(rectAOWord(voxels, sampler, userData, chunkX, chunkY, chunkZ,
                                            dir, slice, x, y, z, rw, rh));
            }
            u += w;
        }
    }
}

void GreedyMesher::meshFace(const uint8_t *voxels, FaceDir dir, std::vector<PackedRect> &out,
                            const CubeTypeRegistry &types, ChunkSampler sampler,
                            void *samplerUserData, int chunkX, int chunkY, int chunkZ,
                            std::vector<uint32_t> *aoOut) {
    int mask[kChunkSize][kChunkSize];

    for (int s = 0; s < kChunkSize; ++s) {
        for (int a = 0; a < kChunkSize; ++a)
            for (int b = 0; b < kChunkSize; ++b) mask[a][b] = -1;

        for (int a = 0; a < kChunkSize; ++a) {
            for (int b = 0; b < kChunkSize; ++b) {
                int x = 0, y = 0, z = 0;
                int nx = 0, ny = 0, nz = 0;
                int u = 0, v = 0;
                switch (dir) {
                    case FaceDir::PosX:
                        x = s;
                        y = a;
                        z = b;
                        nx = s + 1;
                        ny = a;
                        nz = b;
                        u = b;
                        v = a;
                        break;
                    case FaceDir::NegX:
                        x = s;
                        y = a;
                        z = b;
                        nx = s - 1;
                        ny = a;
                        nz = b;
                        u = b;
                        v = a;
                        break;
                    case FaceDir::PosY:
                        x = b;
                        y = s;
                        z = a;
                        nx = b;
                        ny = s + 1;
                        nz = a;
                        u = b;
                        v = a;
                        break;
                    case FaceDir::NegY:
                        x = b;
                        y = s;
                        z = a;
                        nx = b;
                        ny = s - 1;
                        nz = a;
                        u = b;
                        v = a;
                        break;
                    case FaceDir::PosZ:
                        x = b;
                        y = a;
                        z = s;
                        nx = b;
                        ny = a;
                        nz = s + 1;
                        u = b;
                        v = a;
                        break;
                    case FaceDir::NegZ:
                        x = b;
                        y = a;
                        z = s;
                        nx = b;
                        ny = a;
                        nz = s - 1;
                        u = b;
                        v = a;
                        break;
                    default:
                        break;
                }

                const uint8_t id = at(voxels, x, y, z);
                if (!isSolid(id)) continue;
                if (isSolid(neighborAt(voxels, sampler, samplerUserData, chunkX, chunkY, chunkZ,
                                       nx, ny, nz)))
                    continue;
                mask[v][u] = int(resolveFaceTex(types, id, dir));
            }
        }

        greedy2D(mask, dir, s, out, aoOut, voxels, sampler, samplerUserData, chunkX, chunkY,
                 chunkZ);
    }
}

void GreedyMesher::meshChunk(const uint8_t *voxels, std::vector<PackedRect> outFaces[6],
                             const CubeTypeRegistry &types, ChunkSampler sampler,
                             void *samplerUserData, int chunkX, int chunkY, int chunkZ,
                             std::vector<uint32_t> aoOut[6]) {
    for (int i = 0; i < faceDirCount(); ++i) {
        outFaces[i].clear();
        if (aoOut) aoOut[i].clear();
        meshFace(voxels, FaceDir(i), outFaces[i], types, sampler, samplerUserData, chunkX, chunkY,
                 chunkZ, aoOut ? &aoOut[i] : nullptr);
    }
}

}  // namespace eve::voxel
