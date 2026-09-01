#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>

namespace eve::graphics {

namespace cubemap_prefilter_detail {

inline glm::vec3 faceDirection(uint32_t face, float u, float v) {
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.f, -v, -u));
    case 1: return glm::normalize(glm::vec3(-1.f, -v, u));
    case 2: return glm::normalize(glm::vec3(u, 1.f, v));
    case 3: return glm::normalize(glm::vec3(u, -1.f, -v));
    case 4: return glm::normalize(glm::vec3(u, -v, 1.f));
    default: return glm::normalize(glm::vec3(-u, -v, -1.f));
    }
}

inline void directionFaceUv(const glm::vec3 &d, uint32_t &face, float &u, float &v) {
    const glm::vec3 a = glm::abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        if (d.x >= 0.f) {
            face = 0;
            u = -d.z / a.x;
            v = -d.y / a.x;
        } else {
            face = 1;
            u = d.z / a.x;
            v = -d.y / a.x;
        }
    } else if (a.y >= a.z) {
        if (d.y >= 0.f) {
            face = 2;
            u = d.x / a.y;
            v = d.z / a.y;
        } else {
            face = 3;
            u = d.x / a.y;
            v = -d.z / a.y;
        }
    } else if (d.z >= 0.f) {
        face = 4;
        u = d.x / a.z;
        v = -d.y / a.z;
    } else {
        face = 5;
        u = -d.x / a.z;
        v = -d.y / a.z;
    }
}

inline glm::vec4 sampleBase(const uint8_t *faces, uint32_t size, const glm::vec3 &direction) {
    uint32_t face = 0;
    float u = 0.f;
    float v = 0.f;
    directionFaceUv(direction, face, u, v);
    const float x = (u * 0.5f + 0.5f) * float(size) - 0.5f;
    const float y = (v * 0.5f + 0.5f) * float(size) - 0.5f;
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = x - float(x0);
    const float ty = y - float(y0);
    auto load = [&](int px, int py) {
        const float tapU = (2.f * (float(px) + 0.5f) / float(size)) - 1.f;
        const float tapV = (2.f * (float(py) + 0.5f) / float(size)) - 1.f;
        const glm::vec3 tapDirection = faceDirection(face, tapU, tapV);
        uint32_t tapFace = 0;
        float resolvedU = 0.f;
        float resolvedV = 0.f;
        directionFaceUv(tapDirection, tapFace, resolvedU, resolvedV);
        const uint32_t resolvedX = uint32_t(std::clamp(
            int((resolvedU * 0.5f + 0.5f) * float(size)), 0, int(size) - 1));
        const uint32_t resolvedY = uint32_t(std::clamp(
            int((resolvedV * 0.5f + 0.5f) * float(size)), 0, int(size) - 1));
        const size_t faceBase = size_t(tapFace) * size_t(size) * size_t(size) * 4u;
        const size_t i = faceBase + (size_t(resolvedY) * size + resolvedX) * 4u;
        return glm::vec4(faces[i], faces[i + 1u], faces[i + 2u], faces[i + 3u]);
    };
    return glm::mix(glm::mix(load(x0, y0), load(x1, y0), tx),
                    glm::mix(load(x0, y1), load(x1, y1), tx), ty);
}

inline float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

inline glm::vec3 importanceSample(float x, float y, const glm::vec3 &n, float roughness) {
    constexpr float kTau = 6.2831853071795864769f;
    const float a = std::max(roughness * roughness, 0.001f);
    const float a2 = a * a;
    const float phi = kTau * x;
    const float cosTheta = std::sqrt((1.f - y) / std::max(1.f + (a2 - 1.f) * y, 1e-6f));
    const float sinTheta = std::sqrt(std::max(1.f - cosTheta * cosTheta, 0.f));
    const glm::vec3 hT(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
    const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.f, 0.f, 1.f)
                                                : glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
    const glm::vec3 bitangent = glm::cross(n, tangent);
    return glm::normalize(tangent * hT.x + bitangent * hT.y + n * hT.z);
}

inline glm::vec3 cosineSample(float x, float y, const glm::vec3 &n) {
    constexpr float kTau = 6.2831853071795864769f;
    const float phi = kTau * x;
    const float radius = std::sqrt(y);
    const glm::vec3 local(radius * std::cos(phi), radius * std::sin(phi),
                          std::sqrt(std::max(1.f - y, 0.f)));
    const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.f, 0.f, 1.f)
                                                : glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
    const glm::vec3 bitangent = glm::cross(n, tangent);
    return glm::normalize(tangent * local.x + bitangent * local.y + n * local.z);
}

}  // namespace cubemap_prefilter_detail

/**
 * @brief Build a mip-major RGBA8 IBL chain: GGX specular mips plus final diffuse irradiance.
 * @param rgbaFaces Six contiguous base faces ordered +X, -X, +Y, -Y, +Z, -Z.
 * @param faceSize Width and height of every base face.
 * @param mipLevels Number of output mip levels, including the unchanged base.
 * @param sampleCount Importance samples per output texel for levels above zero.
 * @return Packed bytes ordered by mip, then face, then row-major texel.
 */
inline std::vector<uint8_t> buildGgxCubemapMipChain(const uint8_t *rgbaFaces, uint32_t faceSize,
                                                    uint32_t mipLevels,
                                                    uint32_t sampleCount = 32u) {
    if (!rgbaFaces || faceSize == 0u || mipLevels == 0u) return {};
    sampleCount = std::max(sampleCount, 1u);
    struct CacheEntry {
        uint64_t key = 0;
        uint32_t faceSize = 0;
        uint32_t mipLevels = 0;
        uint32_t sampleCount = 0;
        std::vector<uint8_t> bytes;
    };
    static std::mutex cacheMutex;
    static std::vector<CacheEntry> cache;
    const size_t sourceBytes = size_t(faceSize) * faceSize * 4u * 6u;
    uint64_t key = 1469598103934665603ull;
    for (size_t i = 0; i < sourceBytes; ++i) {
        key ^= uint64_t(rgbaFaces[i]);
        key *= 1099511628211ull;
    }
    key ^= uint64_t(faceSize) | (uint64_t(mipLevels) << 32u);
    key *= 1099511628211ull;
    key ^= uint64_t(sampleCount);
    if (mipLevels > 1u) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const CacheEntry &entry : cache) {
            if (entry.key == key && entry.faceSize == faceSize &&
                entry.mipLevels == mipLevels && entry.sampleCount == sampleCount)
                return entry.bytes;
        }
    }

    std::vector<uint8_t> packed;
    size_t totalBytes = 0;
    for (uint32_t level = 0, size = faceSize; level < mipLevels;
         ++level, size = std::max(size >> 1u, 1u))
        totalBytes += size_t(size) * size * 4u * 6u;
    packed.reserve(totalBytes);
    packed.insert(packed.end(), rgbaFaces, rgbaFaces + sourceBytes);
    if (mipLevels <= 1u) return packed;

    for (uint32_t level = 1, size = std::max(faceSize >> 1u, 1u); level < mipLevels;
         ++level, size = std::max(size >> 1u, 1u)) {
        const float roughness = float(level) / float(mipLevels - 1u);
        const bool diffuseIrradiance = mipLevels >= 3u && level + 1u == mipLevels;
        const uint32_t levelSamples =
            diffuseIrradiance
                ? std::max(64u, sampleCount * 2u)
                : std::max(8u, uint32_t(std::ceil(float(sampleCount) * roughness)));
        for (uint32_t face = 0; face < 6u; ++face) {
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    const float u = (2.f * (float(x) + 0.5f) / float(size)) - 1.f;
                    const float v = (2.f * (float(y) + 0.5f) / float(size)) - 1.f;
                    const glm::vec3 n = cubemap_prefilter_detail::faceDirection(face, u, v);
                    glm::vec4 sum(0.f);
                    float weight = 0.f;
                    for (uint32_t i = 0; i < levelSamples; ++i) {
                        const float xiX = (float(i) + 0.5f) / float(levelSamples);
                        const float xiY = cubemap_prefilter_detail::radicalInverse(i);
                        if (diffuseIrradiance) {
                            const glm::vec3 l =
                                cubemap_prefilter_detail::cosineSample(xiX, xiY, n);
                            sum += cubemap_prefilter_detail::sampleBase(rgbaFaces, faceSize, l);
                            weight += 1.f;
                            continue;
                        }
                        const glm::vec3 h = cubemap_prefilter_detail::importanceSample(
                            xiX, xiY, n, roughness);
                        const glm::vec3 l = glm::normalize(2.f * glm::dot(n, h) * h - n);
                        const float noL = std::max(glm::dot(n, l), 0.f);
                        if (noL <= 0.f) continue;
                        sum += cubemap_prefilter_detail::sampleBase(rgbaFaces, faceSize, l) * noL;
                        weight += noL;
                    }
                    const glm::vec4 c = sum / std::max(weight, 1e-6f);
                    for (int channel = 0; channel < 4; ++channel)
                        packed.push_back(static_cast<uint8_t>(
                            std::clamp(std::lround(c[channel]), 0l, 255l)));
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cache.size() >= 2u) cache.erase(cache.begin());
        cache.push_back(CacheEntry{key, faceSize, mipLevels, sampleCount, packed});
    }
    return packed;
}

}  // namespace eve::graphics
