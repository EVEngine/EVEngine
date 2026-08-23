#include "fluids/FluidSurfaceBinding.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceWetnessField.h"

#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using namespace eve::fluids;

namespace {

struct Rgb {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
};

Rgb mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {glm::mix(a.r, b.r, t), glm::mix(a.g, b.g, t), glm::mix(a.b, b.b, t)};
}

void blend(std::vector<Rgb>& image, int width, int height, int x, int y, Rgb color, float alpha) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    image[size_t(y * width + x)] = mix(image[size_t(y * width + x)], color, alpha);
}

}  // namespace

int main(int argc, char** argv) {
    constexpr int columns = 42;
    constexpr int rows = 30;
    constexpr int width = 960;
    constexpr int height = 640;
    const std::string output = argc > 1 ? argv[1] : "surface-fluid-dynamic.ppm";

    std::vector<glm::vec3> rest;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> indices;
    rest.reserve(columns * rows);
    uvs.reserve(columns * rows);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const glm::vec2 uv(float(x) / float(columns - 1), float(y) / float(rows - 1));
            rest.emplace_back(glm::mix(-1.65f, 1.65f, uv.x), glm::mix(-1.08f, 1.08f, uv.y), 0.f);
            uvs.push_back(uv);
        }
    }
    for (int y = 0; y + 1 < rows; ++y) {
        for (int x = 0; x + 1 < columns; ++x) {
            const uint32_t a = uint32_t(y * columns + x);
            const uint32_t b = a + 1u;
            const uint32_t c = a + uint32_t(columns);
            const uint32_t d = c + 1u;
            indices.insert(indices.end(), {a, b, c, c, b, d});
        }
    }

    FluidSurfaceBinding binding;
    if (!binding.build(rest, indices, uvs)) return 2;
    SurfaceWetnessField wetness;
    if (!wetness.build(binding)) return 3;
    SurfaceDropletParams params;
    params.gravity = glm::vec3(0.32f, -3.1f, 0.f);
    params.friction = 2.8f;
    params.adhesionAcceleration = 18.f;
    params.contactAngleDegrees = 68.f;
    params.mergeRadiusScale = 0.82f;
    params.trailDeposition = 520.f;
    params.reattachDistance = 0.045f;
    SurfaceDropletSimulation simulation(&binding, params, &wetness);

    const glm::vec2 seeds[] = {
        {0.16f, 0.90f}, {0.24f, 0.82f}, {0.31f, 0.94f}, {0.43f, 0.84f}, {0.48f, 0.91f},
        {0.58f, 0.87f}, {0.67f, 0.95f}, {0.72f, 0.79f}, {0.82f, 0.91f}, {0.88f, 0.84f},
        {0.38f, 0.72f}, {0.62f, 0.70f}, {0.76f, 0.68f},
    };
    for (size_t i = 0; i < std::size(seeds); ++i) {
        SurfaceLocation location;
        const glm::vec3 point(glm::mix(-1.65f, 1.65f, seeds[i].x),
                              glm::mix(-1.08f, 1.08f, seeds[i].y), 0.f);
        if (binding.project(point, 0.2f, location)) {
            const float volume = 0.00115f + 0.00032f * float(i % 4u);
            simulation.addDroplet(location, volume, glm::vec3(0.08f * std::sin(float(i)), 0.f, 0.f));
            wetness.deposit(location, 0.22f);
        }
    }

    std::vector<glm::vec3> pose = rest;
    SurfaceWetnessParams wetParams;
    wetParams.diffusion = 0.34f;
    wetParams.evaporation = 0.012f;
    for (int frame = 0; frame < 62; ++frame) {
        const float time = float(frame) / 60.f;
        for (size_t i = 0; i < rest.size(); ++i) {
            const glm::vec2 uv = uvs[i];
            pose[i] = rest[i];
            pose[i].z = 0.105f * std::sin(uv.x * 7.2f + time * 2.1f) *
                        std::sin(uv.y * 4.1f + time * 1.4f);
            pose[i].x += 0.035f * std::sin(time * 1.7f + uv.y * 3.f);
        }
        binding.setDeformedPositions(pose);
        simulation.step(1.f / 60.f);
        wetness.step(1.f / 60.f, wetParams);
    }

    std::vector<Rgb> image(size_t(width * height));
    for (int y = 0; y < height; ++y) {
        const float v = float(y) / float(height - 1);
        const Rgb top{0.035f, 0.065f, 0.10f};
        const Rgb bottom{0.005f, 0.012f, 0.025f};
        for (int x = 0; x < width; ++x) {
            const float glow = std::exp(-std::pow((float(x) / width - 0.68f) * 2.4f, 2.f) -
                                        std::pow((v - 0.30f) * 2.1f, 2.f));
            image[size_t(y * width + x)] = mix(top, bottom, v * 0.82f);
            image[size_t(y * width + x)] = mix(image[size_t(y * width + x)], {0.10f, 0.22f, 0.31f}, glow * 0.18f);
        }
    }

    const int left = 78, right = width - 78, top = 54, bottom = height - 48;
    for (int py = top; py <= bottom; ++py) {
        const float v = 1.f - float(py - top) / float(bottom - top);
        const float gy = v * float(rows - 1);
        const int y0 = std::min(rows - 2, int(gy));
        const float fy = gy - float(y0);
        for (int px = left; px <= right; ++px) {
            const float u = float(px - left) / float(right - left);
            const float gx = u * float(columns - 1);
            const int x0 = std::min(columns - 2, int(gx));
            const float fx = gx - float(x0);
            const size_t i00 = size_t(y0 * columns + x0);
            const size_t i10 = i00 + 1u;
            const size_t i01 = i00 + size_t(columns);
            const size_t i11 = i01 + 1u;
            const float w0 = glm::mix(wetness.values()[i00], wetness.values()[i10], fx);
            const float w1 = glm::mix(wetness.values()[i01], wetness.values()[i11], fx);
            const float wet = std::clamp(glm::mix(w0, w1, fy) * 7.2f, 0.f, 1.f);
            const float z0 = glm::mix(pose[i00].z, pose[i10].z, fx);
            const float z1 = glm::mix(pose[i01].z, pose[i11].z, fx);
            const float curve = glm::mix(z0, z1, fy);
            const float diagonal = std::pow(std::max(0.f, 1.f - std::fabs(u - v * 0.43f - 0.35f) * 7.f), 4.f);
            Rgb glass = {0.055f + curve * 0.15f, 0.105f + curve * 0.20f, 0.145f + curve * 0.28f};
            glass = mix(glass, {0.08f, 0.31f, 0.43f}, wet * 0.70f);
            glass = mix(glass, {0.34f, 0.65f, 0.76f}, diagonal * (0.05f + wet * 0.16f));
            image[size_t(py * width + px)] = glass;
        }
    }

    for (const SurfaceDroplet& drop : simulation.droplets()) {
        const SurfaceSample sample = binding.evaluate(drop.location, 1.f / 60.f);
        const int cx = left + int((sample.position.x + 1.65f) / 3.3f * float(right - left));
        const int cy = bottom - int((sample.position.y + 1.08f) / 2.16f * float(bottom - top));
        const int radius = std::max(4, int(simulation.dropletRadius(drop.volume) / 3.3f * float(right - left) * 1.45f));
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const float nx = float(dx) / float(radius);
                const float ny = float(dy) / float(radius);
                const float d2 = nx * nx + ny * ny;
                if (d2 > 1.f) continue;
                const float rim = std::pow(std::clamp((d2 - 0.48f) / 0.52f, 0.f, 1.f), 1.8f);
                const float highlight = std::exp(-((nx + 0.34f) * (nx + 0.34f) +
                                                   (ny + 0.38f) * (ny + 0.38f)) * 26.f);
                blend(image, width, height, cx + dx, cy + dy,
                      mix({0.07f, 0.23f, 0.31f}, {0.68f, 0.92f, 1.f}, highlight + rim * 0.55f),
                      0.34f + rim * 0.52f + highlight * 0.55f);
            }
        }
    }

    for (int x = left; x <= right; ++x) {
        blend(image, width, height, x, top, {0.38f, 0.66f, 0.74f}, 0.65f);
        blend(image, width, height, x, bottom, {0.18f, 0.40f, 0.48f}, 0.55f);
    }
    for (int y = top; y <= bottom; ++y) {
        blend(image, width, height, left, y, {0.38f, 0.66f, 0.74f}, 0.65f);
        blend(image, width, height, right, y, {0.18f, 0.40f, 0.48f}, 0.55f);
    }

    std::ofstream file(output, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const Rgb& color : image) {
        const auto channel = [](float value) {
            return uint8_t(std::clamp(std::pow(std::max(0.f, value), 1.f / 2.2f), 0.f, 1.f) * 255.f);
        };
        const uint8_t rgb[3] = {channel(color.r), channel(color.g), channel(color.b)};
        file.write(reinterpret_cast<const char*>(rgb), 3);
    }
    return file ? 0 : 4;
}
