#include "procgen/algorithms/CaveSupport.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace eve::procgen {

CaveDetachmentResult detachUnsupportedCaveFragments(std::vector<float>& density, int nx, int ny, int nz,
                                                    float strength) {
    CaveDetachmentResult result;
    if (strength <= 0.f || nx <= 0 || ny <= 0 || nz <= 0) return result;

    const auto index = [nx, ny](int x, int y, int z) {
        return size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
    };
    std::vector<uint8_t> anchored(density.size(), uint8_t(0));
    std::deque<size_t>   frontier;
    auto                 anchor = [&](int x, int y, int z) {
        const size_t voxel = index(x, y, z);
        if (density[voxel] <= 0.f || anchored[voxel] != 0) return;
        anchored[voxel] = 1;
        frontier.push_back(voxel);
    };
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            anchor(0, y, z);
            anchor(nx - 1, y, z);
        }
    }
    for (int z = 0; z < nz; ++z) {
        for (int x = 0; x < nx; ++x) {
            anchor(x, 0, z);
            anchor(x, ny - 1, z);
        }
    }
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            anchor(x, y, 0);
            anchor(x, y, nz - 1);
        }
    }

    while (!frontier.empty()) {
        const size_t voxel = frontier.front();
        frontier.pop_front();
        const int z = int(voxel / (size_t(nx) * size_t(ny)));
        const int y = int((voxel / size_t(nx)) % size_t(ny));
        const int x = int(voxel % size_t(nx));
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx == 0 && dy == 0 && dz == 0) || x + dx < 0 || x + dx >= nx || y + dy < 0 || y + dy >= ny ||
                        z + dz < 0 || z + dz >= nz)
                        continue;
                    anchor(x + dx, y + dy, z + dz);
                }
            }
        }
    }

    for (size_t voxel = 0; voxel < density.size(); ++voxel) {
        if (density[voxel] <= 0.f || anchored[voxel] != 0) continue;
        ++result.unsupportedVoxels;
        const float detached = -std::fabs(density[voxel]) - 1e-4f;
        density[voxel] += (detached - density[voxel]) * strength;
        if (density[voxel] <= 0.f) ++result.detachedVoxels;
    }
    return result;
}

}  // namespace eve::procgen
