#include "voxel/GreedyMesher.h"

#include <cstring>

namespace eve::voxel {

uint8_t GreedyMesher::at(const uint8_t *v, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize) return 0;
    return v[x + y * kChunkSize + z * kChunkSize * kChunkSize];
}

void GreedyMesher::greedy2D(int mask[kChunkSize][kChunkSize], FaceDir dir, int slice,
                            std::vector<PackedRect> &out) {
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
            u += w;
        }
    }
}

void GreedyMesher::meshFace(const uint8_t *voxels, FaceDir dir, std::vector<PackedRect> &out,
                            const CubeTypeRegistry &types) {
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
                if (isSolid(at(voxels, nx, ny, nz))) continue;
                mask[v][u] = int(resolveFaceTex(types, id, dir));
            }
        }

        greedy2D(mask, dir, s, out);
    }
}

void GreedyMesher::meshChunk(const uint8_t *voxels, std::vector<PackedRect> outFaces[6],
                             const CubeTypeRegistry &types) {
    for (int i = 0; i < faceDirCount(); ++i) {
        outFaces[i].clear();
        meshFace(voxels, FaceDir(i), outFaces[i], types);
    }
}

}  // namespace eve::voxel
