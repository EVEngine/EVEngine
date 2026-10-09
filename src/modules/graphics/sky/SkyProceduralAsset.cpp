#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>
#include "graphics/sky/SkyWispsAssetData.h"

namespace eve::graphics {
namespace {
uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
float random(uint32_t x) { return float(hash(x) >> 8) / 16777216.f; }
float noise(float x, float y, uint32_t period, uint32_t seed) {
    auto  ix = static_cast<uint32_t>(std::floor(x)), iy = static_cast<uint32_t>(std::floor(y));
    float u = x - std::floor(x), v = y - std::floor(y);
    u                 = u * u * (3 - 2 * u);
    v                 = v * v * (3 - 2 * v);
    const auto sample = [&](uint32_t a, uint32_t b) {
        return random(seed ^ hash(a % period) ^ hash((b % period) + 7351));
    };
    return std::lerp(std::lerp(sample(ix, iy), sample(ix + 1, iy), u),
                     std::lerp(sample(ix, iy + 1), sample(ix + 1, iy + 1), u), v);
}
float fractal(float u, float v, uint32_t seed) {
    float value = 0, weight = .5f;
    for (uint32_t frequency = 4; frequency <= 64; frequency *= 2) {
        value += weight * noise(u * float(frequency), v * float(frequency), frequency, seed);
        weight *= .5f;
    }
    return value;
}
std::byte channel(float x) { return std::byte(static_cast<unsigned char>(std::clamp(x, 0.f, 1.f) * 255.f + .5f)); }
void      mips(std::vector<std::byte>& pixels, ShaderImageInput& image, uint32_t size, bool repeat) {
    image.width = image.height = size;
    image.format               = ShaderImageFormat::RGBA8;
    size_t offset              = 0;
    image.mipLevels            = 1;
    for (uint32_t previous = size; previous > 1; previous /= 2) {
        const uint32_t next       = previous / 2;
        const size_t   nextOffset = pixels.size();
        pixels.resize(nextOffset + size_t(next) * next * 4);
        for (uint32_t y = 0; y < next; ++y)
            for (uint32_t x = 0; x < next; ++x)
                for (size_t c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for (uint32_t dy = 0; dy < 2; ++dy)
                        for (uint32_t dx = 0; dx < 2; ++dx)
                            sum += std::to_integer<unsigned>(
                                pixels[offset + ((size_t(y * 2 + dy) * previous) + x * 2 + dx) * 4 + c]);
                    pixels[nextOffset + (size_t(y) * next + x) * 4 + c] = std::byte((sum + 2) / 4);
                }
        offset = nextOffset;
        ++image.mipLevels;
    }
    image.bytes           = pixels;
    image.sampler         = TextureSampler::linearMipmap();
    image.sampler.repeatU = image.sampler.repeatV = repeat;
}
}  // namespace
Result<SkyWispsAsset> SkyWispsAsset::generate(uint32_t seed) {
    try {
        auto               data      = std::make_shared<Impl>();
        auto&              w         = data->layer;
        constexpr float    pi        = std::numbers::pi_v<float>;
        constexpr uint32_t longitude = 96, latitude = 48, cloudSize = 512, starSize = 1024, moonSize = 256;
        const auto         vertex = [&](uint32_t x, uint32_t y) {
            const float                u = float(x) / float(longitude), v = float(y) / float(latitude);
            const float                elevation = (v - .5f) * pi, azimuth = u * 2 * pi;
            const float                z = std::sin(elevation);
            const std::array<float, 9> p{100 * std::cos(elevation) * std::cos(azimuth),
                                         100 * std::cos(elevation) * std::sin(azimuth),
                                         100 * z,
                                         u,
                                         v,
                                         std::max(0.f, z),
                                         0,
                                         0,
                                         1};
            data->corners.insert(data->corners.end(), p.begin(), p.end());
        };
        for (uint32_t y = 0; y < latitude; ++y)
            for (uint32_t x = 0; x < longitude; ++x) {
                vertex(x, y);
                vertex(x + 1, y);
                vertex(x + 1, y + 1);
                vertex(x, y);
                vertex(x + 1, y + 1);
                vertex(x, y + 1);
            }
        data->pixels.resize(cloudSize * cloudSize);
        for (uint32_t y = 0; y < cloudSize; ++y)
            for (uint32_t x = 0; x < cloudSize; ++x) {
                const float u = float(x) / float(cloudSize), v = float(y) / float(cloudSize);
                const float n                           = fractal(u * 2, v, seed ^ 0x434c4f55U);
                data->pixels[size_t(y) * cloudSize + x] = channel(std::max(0.f, n - .36f) * 2.4f);
            }
        auto& density = data->texture;
        density.width = density.height = cloudSize;
        density.format                 = ShaderImageFormat::R8;
        density.bytes                  = data->pixels;
        density.sampler                = TextureSampler::linear();
        density.sampler.repeatU = density.sampler.repeatV = true;
        for (size_t i = 0; i < 4; ++i) {
            const uint32_t size   = i == 0 ? starSize : (i == 1 ? 64 : moonSize);
            auto&          pixels = data->celestialPixels[i];
            pixels.resize(size_t(size) * size * 4);
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x) {
                    const size_t at = (size_t(y) * size + x) * 4;
                    float        r = 0, g = 0, b = 0, a = 1;
                    if (i == 0) {
                        const float sample = random(seed ^ 0x53544152U ^ hash(y * size + x));
                        r = g = b = sample > .9985f ? .35f + .65f * random(y * size + x) : 0;
                    } else if (i == 1) {
                        r = g = b = random(seed ^ 0x4e4f4953U ^ hash(y * size + x));
                    } else {
                        const float u = (float(x) + .5f) / float(size), v = (float(y) + .5f) / float(size);
                        const float nx = 2 * u - 1, nz = 2 * v - 1, rr = nx * nx + nz * nz;
                        const float depth = std::sqrt(std::max(0.f, 1 - rr));
                        a                 = rr <= 1 ? 1.f : 0.f;
                        if (i == 2) {
                            // Independently generated maria and crater mottling, not an astronomical map.
                            const float terrain = fractal(u, v, seed ^ 0x4d4f4f4eU);
                            const float fine    = noise(u * 64, v * 64, 64, seed ^ 0x43524154U);
                            r = g = b = std::clamp(.28f + terrain * .65f + (fine - .5f) * .12f, .15f, .9f);
                        } else {
                            r = (nx + 1) * .5f;
                            g = (nz + 1) * .5f;
                            b = (1 - depth) * .5f;
                        }
                    }
                    pixels[at]     = channel(r);
                    pixels[at + 1] = channel(g);
                    pixels[at + 2] = channel(b);
                    pixels[at + 3] = channel(a);
                }
            mips(pixels, data->celestialImages[i], size, i < 2);
        }
        w.corners           = data->corners;
        w.densityTexture    = &density;
        w.starsTexture      = &data->celestialImages[0];
        w.starsNoiseTexture = &data->celestialImages[1];
        w.moonColorTexture  = &data->celestialImages[2];
        w.moonNormalTexture = &data->celestialImages[3];
        w.daylightEnabled = w.opticalCycleEnabled = w.authoredSkyLighting = true;
        w.materialContrast                                                = {.1f, .85f, .375f};
        w.sunDiskColor                                                    = {1068, 934, 681};
        w.daylight.moonDiskColor[3]                                       = 1;
        for (size_t i = 0; i < w.daylight.scatteringCurve.size(); ++i) {
            const float t                 = float(i) / float(w.daylight.scatteringCurve.size() - 1);
            const float value             = t * t * (3 - 2 * t);
            w.daylight.scatteringCurve[i] = {80 + 100 * t, value, value, value};
        }
        auto valid = w.daylight.validate();
        if (!valid) return Result<SkyWispsAsset>::failure(valid.status());
        return Result<SkyWispsAsset>::success(SkyWispsAsset(std::move(data)));
    } catch (const std::exception& e) {
        return Result<SkyWispsAsset>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what(), "sky.procedural-v1"));
    }
}
}  // namespace eve::graphics
