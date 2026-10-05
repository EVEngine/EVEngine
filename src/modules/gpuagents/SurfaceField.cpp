#include "gpuagents/SurfaceField.h"

#include "common/Assert.h"

#include <algorithm>
#include <cmath>

namespace eve::gpuagents {

void SurfaceField::initFlat(const glm::vec3& originMin, float size, int res, float planeY) {
    EV_PARAM_CHECK(res > 1);
    EV_PARAM_CHECK(size > 0.f);
    origin     = originMin;
    worldSize  = size;
    resolution = res;
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    height.assign(n, planeY);
    normals.assign(n, glm::vec3(0.f, 1.f, 0.f));
    lifeField.assign(n, glm::vec4(0.f));
}

glm::vec2 SurfaceField::worldToUv(const glm::vec3& p) const {
    return glm::vec2((p.x - origin.x) / worldSize, (p.z - origin.z) / worldSize);
}

int SurfaceField::index(int x, int z) const {
    return x + resolution * z;
}

bool SurfaceField::inBounds(int x, int z) const {
    return x >= 0 && z >= 0 && x < resolution && z < resolution;
}

float SurfaceField::sampleHeight(float x, float z) const {
    if (height.empty()) return 0.f;
    const glm::vec2 uv = worldToUv(glm::vec3(x, 0.f, z));
    const float     fx = uv.x * static_cast<float>(resolution - 1);
    const float     fz = uv.y * static_cast<float>(resolution - 1);
    const int       x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, resolution - 1);
    const int       z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, resolution - 1);
    const int       x1 = std::min(x0 + 1, resolution - 1);
    const int       z1 = std::min(z0 + 1, resolution - 1);
    const float     tx = fx - static_cast<float>(x0);
    const float     tz = fz - static_cast<float>(z0);
    const float     h00 = height[static_cast<size_t>(index(x0, z0))];
    const float     h10 = height[static_cast<size_t>(index(x1, z0))];
    const float     h01 = height[static_cast<size_t>(index(x0, z1))];
    const float     h11 = height[static_cast<size_t>(index(x1, z1))];
    return glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz);
}

glm::vec3 SurfaceField::sampleNormal(float x, float z) const {
    if (normals.empty()) return glm::vec3(0.f, 1.f, 0.f);
    const glm::vec2 uv = worldToUv(glm::vec3(x, 0.f, z));
    const int       ix = std::clamp(static_cast<int>(std::floor(uv.x * resolution)), 0, resolution - 1);
    const int       iz = std::clamp(static_cast<int>(std::floor(uv.y * resolution)), 0, resolution - 1);
    return normals[static_cast<size_t>(index(ix, iz))];
}

glm::vec4 SurfaceField::sampleLifeTexel(float u, float v) const {
    const float fx = u * static_cast<float>(resolution - 1);
    const float fz = v * static_cast<float>(resolution - 1);
    const int   x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, resolution - 1);
    const int   z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, resolution - 1);
    const int   x1 = std::min(x0 + 1, resolution - 1);
    const int   z1 = std::min(z0 + 1, resolution - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const auto  at = [&](int x, int z) { return lifeField[static_cast<size_t>(index(x, z))]; };
    return glm::mix(glm::mix(at(x0, z0), at(x1, z0), tx), glm::mix(at(x0, z1), at(x1, z1), tx), tz);
}

glm::vec4 SurfaceField::sampleLife(float x, float z) const {
    if (lifeField.empty()) return glm::vec4(0.f);
    const glm::vec2 uv = worldToUv(glm::vec3(x, 0.f, z));
    return sampleLifeTexel(std::clamp(uv.x, 0.f, 1.f), std::clamp(uv.y, 0.f, 1.f));
}

glm::vec3 SurfaceField::project(const glm::vec3& p) const {
    return glm::vec3(p.x, sampleHeight(p.x, p.z), p.z);
}

void SurfaceField::stepLife(float dt, float decayRate, float halfLife, float diffusion,
                            const std::vector<glm::vec3>& deposits, float depositStrength) {
    if (lifeField.empty()) return;
    EV_PARAM_CHECK(dt >= 0.f);
    const float decay = std::exp(-decayRate * dt);
    const float freshDecay =
        halfLife > 1e-4f ? std::exp(-std::log(2.f) * dt / halfLife) : 0.f;

    std::vector<glm::vec4> next = lifeField;
    const int              res  = resolution;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(index(x, z));
            glm::vec4    c = lifeField[i];
            c.r *= decay;
            c.a *= freshDecay;
            if (diffusion > 0.f) {
                glm::vec4 sum(0.f);
                int       count = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        const int nx = x + dx;
                        const int nz = z + dz;
                        if (!inBounds(nx, nz)) continue;
                        sum += lifeField[static_cast<size_t>(index(nx, nz))];
                        ++count;
                    }
                }
                if (count > 0) {
                    const glm::vec4 avg = sum / static_cast<float>(count);
                    c.r = glm::mix(c.r, avg.r, diffusion);
                    c.g = glm::mix(c.g, avg.g, diffusion * 0.5f);
                    c.b = glm::mix(c.b, avg.b, diffusion * 0.25f);
                }
            }
            next[i] = c;
        }
    }

    for (const glm::vec3& p : deposits) {
        const glm::vec2 uv = worldToUv(p);
        if (uv.x < 0.f || uv.y < 0.f || uv.x >= 1.f || uv.y >= 1.f) continue;
        const int x = std::clamp(static_cast<int>(uv.x * res), 0, res - 1);
        const int z = std::clamp(static_cast<int>(uv.y * res), 0, res - 1);
        auto&     c = next[static_cast<size_t>(index(x, z))];
        c.r = std::min(c.r + depositStrength, 1.f);
        c.a = 1.f;
    }
    lifeField.swap(next);
}

}  // namespace eve::gpuagents
