#pragma once

#include "voxel/FaceDir.h"
#include "voxel/VoxelPack.h"

namespace eve::voxel {

/** One decoded face rectangle in world space (mirrors voxel_rect.vert). */
struct DecodedRect {
    float corners[4][3]{};  // (0,0), (1,0), (1,1), (0,1)
    float normal[3]{};
    float uv[4][2]{};
    int tex = 0;
    float width = 1.f;
    float height = 1.f;
};

/**
 * CPU mirror of the instanced voxel vertex shader placement / UV logic.
 * Used by unit tests to validate packing ↔ geometry without a GPU.
 */
inline DecodedRect decodePackedRect(PackedRect rect, FaceDir dir, float originX, float originY,
                                    float originZ, int tilesPerRow = 16) {
    DecodedRect out;
    const float ix = float(rect.x());
    const float iy = float(rect.y());
    const float iz = float(rect.z());
    const float w = float(rect.width());
    const float h = float(rect.height());
    out.width = w;
    out.height = h;
    out.tex = rect.tex();

    const float corners2[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
    for (int i = 0; i < 4; ++i) {
        const float cu = corners2[i][0];
        const float cv = corners2[i][1];
        float x = ix, y = iy, z = iz;
        float nx = 0.f, ny = 0.f, nz = 0.f;
        switch (dir) {
            case FaceDir::PosX:
                x = ix + 1.f;
                y = iy + cv * h;
                z = iz + cu * w;
                nx = 1.f;
                break;
            case FaceDir::NegX:
                x = ix;
                y = iy + cv * h;
                z = iz + (1.f - cu) * w;
                nx = -1.f;
                break;
            case FaceDir::PosY:
                x = ix + cu * w;
                y = iy + 1.f;
                z = iz + cv * h;
                ny = 1.f;
                break;
            case FaceDir::NegY:
                x = ix + cu * w;
                y = iy;
                z = iz + (1.f - cv) * h;
                ny = -1.f;
                break;
            case FaceDir::PosZ:
                x = ix + (1.f - cu) * w;
                y = iy + cv * h;
                z = iz + 1.f;
                nz = 1.f;
                break;
            case FaceDir::NegZ:
            default:
                x = ix + cu * w;
                y = iy + cv * h;
                z = iz;
                nz = -1.f;
                break;
        }
        out.corners[i][0] = x + originX;
        out.corners[i][1] = y + originY;
        out.corners[i][2] = z + originZ;
        out.normal[0] = nx;
        out.normal[1] = ny;
        out.normal[2] = nz;

        const float tiles = float(tilesPerRow < 1 ? 1 : tilesPerRow);
        const float tileU = 1.f / tiles;
        const float col = float(out.tex % (tilesPerRow < 1 ? 1 : tilesPerRow));
        const float row = float(out.tex / (tilesPerRow < 1 ? 1 : tilesPerRow));
        out.uv[i][0] = (col + cu) * tileU;
        out.uv[i][1] = (row + cv) * tileU;
    }
    return out;
}

/** True when triangle (0,2,1) winding matches outward normal (matches GPU index order). */
inline bool decodedWindingMatchesNormal(const DecodedRect &q) {
    // GPU draws 0-2-1 and 0-3-2 (see ensureVoxelUnitQuad).
    const float e1x = q.corners[2][0] - q.corners[0][0];
    const float e1y = q.corners[2][1] - q.corners[0][1];
    const float e1z = q.corners[2][2] - q.corners[0][2];
    const float e2x = q.corners[1][0] - q.corners[0][0];
    const float e2y = q.corners[1][1] - q.corners[0][1];
    const float e2z = q.corners[1][2] - q.corners[0][2];
    const float cx = e1y * e2z - e1z * e2y;
    const float cy = e1z * e2x - e1x * e2z;
    const float cz = e1x * e2y - e1y * e2x;
    return cx * q.normal[0] + cy * q.normal[1] + cz * q.normal[2] > 0.f;
}

}  // namespace eve::voxel
