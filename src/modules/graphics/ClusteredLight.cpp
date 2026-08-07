#include "graphics/ClusteredLight.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eve::graphics {

ClusteredLightingUpload buildClusteredLighting(const std::vector<ClusteredLightGpu> &points,
                                               const std::vector<ClusteredLightGpu> &dirs,
                                               const glm::mat4 &view, float nearZ, float farZ,
                                               int screenW, int screenH, float fovYRad,
                                               const glm::vec4 &ambient) {
    ClusteredLightingUpload out{};
    out.ambient = ambient;
    out.view = view;
    out.clipInfo = glm::vec4(nearZ, farZ, float(std::max(screenW, 1)), float(std::max(screenH, 1)));
    out.gridInfo = glm::vec4(float(ClusteredLightConfig::kTilesX), float(ClusteredLightConfig::kTilesY),
                             float(ClusteredLightConfig::kSlices), 0.f);

    if (!dirs.empty()) {
        glm::vec3 d(dirs[0].posRadius);
        if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
        else d = glm::normalize(d);
        out.primaryDir = glm::vec4(d, 1.f);
        out.primaryColor = dirs[0].color;
    } else {
        out.primaryDir = glm::vec4(0.f, 1.f, 0.f, 0.f);
        out.primaryColor = glm::vec4(0.f);
    }

    const int maxPts = std::min(int(points.size()), ClusteredLightConfig::kMaxLights);
    out.lights.assign(points.begin(), points.begin() + maxPts);
    out.gridInfo.w = float(out.lights.size());

    const int C = ClusteredLightConfig::kClusterCount;
    out.clusterTable.assign(size_t(C), ClusterTableEntry{});
    auto perCluster = std::vector<std::vector<uint32_t>>(size_t(C));
    for (auto &v : perCluster) v.reserve(4);

    const float nx = nearZ;
    const float fx = std::max(farZ, nearZ + 1e-3f);
    const float tanHalfFov = std::tan(std::max(fovYRad, 1e-3f) * 0.5f);
    const float aspect = out.clipInfo.z / std::max(out.clipInfo.w, 1.f);

    for (uint32_t li = 0; li < uint32_t(out.lights.size()); ++li) {
        const auto &L = out.lights[li];
        const glm::vec3 worldPos(L.posRadius);
        const float radius = std::max(L.posRadius.w, 0.01f);
        const glm::vec4 vp = view * glm::vec4(worldPos, 1.f);
        const glm::vec3 viewPos(vp);
        const float zCenter = -viewPos.z;  // positive depth in front of camera (RH)
        const float z0 = zCenter - radius;
        const float z1 = zCenter + radius;
        if (z1 < nx || z0 > fx) continue;

        const float zMin = std::max(z0, nx);
        const float zMax = std::min(z1, fx);
        const int slice0 = std::clamp(
            int(std::floor((zMin - nx) / (fx - nx) * float(ClusteredLightConfig::kSlices))), 0,
            ClusteredLightConfig::kSlices - 1);
        const int slice1 = std::clamp(
            int(std::floor((zMax - nx) / (fx - nx) * float(ClusteredLightConfig::kSlices))), 0,
            ClusteredLightConfig::kSlices - 1);

        const float depth = std::max(zCenter, nx);
        const float halfH = depth * tanHalfFov;
        const float halfW = halfH * aspect;

        auto toTileX = [&](float x) {
            float u = (x + halfW) / std::max(2.f * halfW, 1e-3f);
            return std::clamp(int(std::floor(u * float(ClusteredLightConfig::kTilesX))), 0,
                              ClusteredLightConfig::kTilesX - 1);
        };
        auto toTileY = [&](float y) {
            float v = 1.f - (y + halfH) / std::max(2.f * halfH, 1e-3f);
            return std::clamp(int(std::floor(v * float(ClusteredLightConfig::kTilesY))), 0,
                              ClusteredLightConfig::kTilesY - 1);
        };

        const int tx0 = toTileX(viewPos.x - radius);
        const int tx1 = toTileX(viewPos.x + radius);
        const int tyA = toTileY(viewPos.y - radius);
        const int tyB = toTileY(viewPos.y + radius);
        const int ty0 = std::min(tyA, tyB);
        const int ty1 = std::max(tyA, tyB);

        for (int sz = slice0; sz <= slice1; ++sz) {
            for (int ty = ty0; ty <= ty1; ++ty) {
                for (int tx = tx0; tx <= tx1; ++tx) {
                    const int cid =
                        (sz * ClusteredLightConfig::kTilesY + ty) * ClusteredLightConfig::kTilesX + tx;
                    perCluster[size_t(cid)].push_back(li);
                }
            }
        }
    }

    out.lightIndices.clear();
    out.lightIndices.reserve(size_t(C) * 4);
    for (int ci = 0; ci < C; ++ci) {
        auto &list = perCluster[size_t(ci)];
        if (list.size() > size_t(ClusteredLightConfig::kMaxLightsPerCluster)) {
            std::stable_sort(list.begin(), list.end(), [&](uint32_t a, uint32_t b) {
                const float ia = glm::length(glm::vec3(out.lights[a].color));
                const float ib = glm::length(glm::vec3(out.lights[b].color));
                return ia > ib;
            });
            list.resize(size_t(ClusteredLightConfig::kMaxLightsPerCluster));
        }
        out.clusterTable[size_t(ci)].offset = uint32_t(out.lightIndices.size());
        out.clusterTable[size_t(ci)].count = uint32_t(list.size());
        out.lightIndices.insert(out.lightIndices.end(), list.begin(), list.end());
    }
    if (out.lightIndices.empty()) out.lightIndices.push_back(0);
    out.active = true;
    return out;
}

}  // namespace eve::graphics
