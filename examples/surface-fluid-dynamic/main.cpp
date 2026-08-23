#include "fluids/FluidSurfaceBinding.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceWetnessField.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace eve::fluids;

namespace {

struct Rgb { float r = 0.f, g = 0.f, b = 0.f; };
struct TrailMark { SurfaceLocation location; float strength = 0.f; };

Rgb mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {glm::mix(a.r, b.r, t), glm::mix(a.g, b.g, t), glm::mix(a.b, b.b, t)};
}

void blend(std::vector<Rgb>& image, int width, int height, int x, int y, Rgb color, float alpha) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    image[size_t(y * width + x)] = mix(image[size_t(y * width + x)], color, alpha);
}

std::pair<std::vector<glm::vec3>, std::vector<uint32_t>> makeSphere(int subdivisions) {
    const float t = (1.f + std::sqrt(5.f)) * 0.5f;
    std::vector<glm::vec3> vertices = {
        glm::normalize(glm::vec3(-1.f, t, 0.f)), glm::normalize(glm::vec3(1.f, t, 0.f)),
        glm::normalize(glm::vec3(-1.f, -t, 0.f)), glm::normalize(glm::vec3(1.f, -t, 0.f)),
        glm::normalize(glm::vec3(0.f, -1.f, t)), glm::normalize(glm::vec3(0.f, 1.f, t)),
        glm::normalize(glm::vec3(0.f, -1.f, -t)), glm::normalize(glm::vec3(0.f, 1.f, -t)),
        glm::normalize(glm::vec3(t, 0.f, -1.f)), glm::normalize(glm::vec3(t, 0.f, 1.f)),
        glm::normalize(glm::vec3(-t, 0.f, -1.f)), glm::normalize(glm::vec3(-t, 0.f, 1.f)),
    };
    std::vector<uint32_t> triangles = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4,
        11, 10, 2, 10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8,
        3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1,
    };
    for (int level = 0; level < subdivisions; ++level) {
        std::vector<uint32_t> next;
        next.reserve(triangles.size() * 4u);
        for (size_t i = 0; i < triangles.size(); i += 3u) {
            const uint32_t a = triangles[i], b = triangles[i + 1u], c = triangles[i + 2u];
            const uint32_t ab = uint32_t(vertices.size());
            vertices.push_back(glm::normalize(vertices[a] + vertices[b]));
            const uint32_t bc = uint32_t(vertices.size());
            vertices.push_back(glm::normalize(vertices[b] + vertices[c]));
            const uint32_t ca = uint32_t(vertices.size());
            vertices.push_back(glm::normalize(vertices[c] + vertices[a]));
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        triangles.swap(next);
    }
    return {std::move(vertices), std::move(triangles)};
}

}  // namespace

int main(int argc, char** argv) {
    constexpr int width = 960, height = 640, centerX = 480, centerY = 324;
    constexpr float spherePixels = 252.f;
    const std::string output = argc > 1 ? argv[1] : "surface-fluid-dynamic.ppm";

    auto [rest, indices] = makeSphere(3);
    FluidSurfaceBinding binding;
    if (!binding.build(rest, indices)) return 2;
    SurfaceWetnessField wetness;
    if (!wetness.build(binding)) return 3;

    SurfaceDropletParams params;
    params.gravity = glm::vec3(0.18f, -2.8f, 0.f);
    params.friction = 3.2f;
    params.adhesionAcceleration = 24.f;
    params.contactAngleDegrees = 62.f;
    params.mergeRadiusScale = 0.55f;
    params.trailDeposition = 180.f;
    SurfaceDropletSimulation simulation(&binding, params, &wetness);

    const glm::vec2 seeds[] = {
        {-0.48f, 0.72f}, {-0.28f, 0.83f}, {-0.06f, 0.70f}, {0.18f, 0.86f},
        {0.39f, 0.69f}, {0.57f, 0.55f}, {-0.60f, 0.43f}, {-0.20f, 0.48f},
        {0.08f, 0.57f}, {0.34f, 0.42f}, {0.66f, 0.25f},
    };
    for (size_t i = 0; i < std::size(seeds); ++i) {
        const float z = -std::sqrt(std::max(0.f, 1.f - glm::dot(seeds[i], seeds[i])));
        SurfaceLocation location;
        if (binding.project(glm::vec3(seeds[i], z), 0.12f, location)) {
            const float volume = 0.000012f + 0.000004f * float(i % 3u);
            simulation.addDroplet(location, volume);
        }
    }

    std::vector<TrailMark> trails;
    SurfaceWetnessParams wetParams;
    wetParams.diffusion = 0.08f;
    wetParams.evaporation = 0.035f;
    for (int frame = 0; frame < 44; ++frame) {
        const float time = float(frame) / 60.f;
        const glm::mat4 pose = glm::rotate(glm::mat4(1.f), 0.10f * std::sin(time * 1.5f),
                                           glm::normalize(glm::vec3(0.3f, 1.f, 0.1f)));
        binding.setTransform(pose);
        simulation.step(1.f / 60.f);
        wetness.step(1.f / 60.f, wetParams);
        for (const SurfaceDroplet& drop : simulation.droplets()) trails.push_back({drop.location, 0.13f});
        for (TrailMark& trail : trails) trail.strength *= 0.992f;
    }

    std::vector<Rgb> image(size_t(width * height));
    for (int y = 0; y < height; ++y) {
        const float v = float(y) / float(height - 1);
        for (int x = 0; x < width; ++x) {
            const float u = float(x) / float(width - 1);
            const float halo = std::exp(-std::pow((u - 0.62f) * 2.2f, 2.f) -
                                        std::pow((v - 0.34f) * 2.0f, 2.f));
            image[size_t(y * width + x)] = mix({0.008f, 0.015f, 0.027f},
                                                {0.045f, 0.095f, 0.13f}, (1.f - v) * 0.72f);
            image[size_t(y * width + x)] = mix(image[size_t(y * width + x)],
                                                {0.10f, 0.25f, 0.33f}, halo * 0.16f);
        }
    }

    for (int py = centerY - int(spherePixels); py <= centerY + int(spherePixels); ++py) {
        for (int px = centerX - int(spherePixels); px <= centerX + int(spherePixels); ++px) {
            const float nx = float(px - centerX) / spherePixels;
            const float ny = -float(py - centerY) / spherePixels;
            const float r2 = nx * nx + ny * ny;
            if (r2 > 1.f) continue;
            const float nz = -std::sqrt(1.f - r2);
            const glm::vec3 normal(nx, ny, nz);
            const float facing = std::clamp(-nz, 0.f, 1.f);
            const float fresnel = std::pow(1.f - facing, 3.2f);
            const float band = std::pow(std::max(0.f, glm::dot(normal,
                glm::normalize(glm::vec3(-0.55f, 0.35f, -0.75f)))), 18.f);
            Rgb surface = mix({0.025f, 0.075f, 0.105f}, {0.08f, 0.19f, 0.24f}, 0.45f + ny * 0.18f);
            surface = mix(surface, {0.20f, 0.43f, 0.49f}, fresnel * 0.72f);
            surface = mix(surface, {0.48f, 0.74f, 0.76f}, band * 0.42f);
            image[size_t(py * width + px)] = surface;
        }
    }

    for (const TrailMark& trail : trails) {
        const SurfaceSample sample = binding.evaluate(trail.location, 1.f / 60.f);
        if (sample.position.z > 0.12f) continue;
        const int x = centerX + int(sample.position.x * spherePixels);
        const int y = centerY - int(sample.position.y * spherePixels);
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const float d2 = float(dx * dx + dy * dy);
                if (d2 > 4.5f) continue;
                blend(image, width, height, x + dx, y + dy, {0.22f, 0.55f, 0.62f},
                      trail.strength * std::exp(-d2 * 0.55f));
            }
        }
    }

    for (const SurfaceDroplet& drop : simulation.droplets()) {
        const SurfaceSample sample = binding.evaluate(drop.location, 1.f / 60.f);
        if (sample.position.z > 0.08f) continue;
        const int cx = centerX + int(sample.position.x * spherePixels);
        const int cy = centerY - int(sample.position.y * spherePixels);
        const float speed = glm::length(drop.relativeVelocity);
        const int rx = std::clamp(int(simulation.dropletRadius(drop.volume) * spherePixels * 0.72f), 4, 10);
        const int ry = std::clamp(int(float(rx) * (1.25f + speed * 0.22f)), 6, 17);
        for (int dy = -ry; dy <= ry; ++dy) {
            const float ny = float(dy) / float(ry);
            const float taper = std::clamp(0.72f + 0.28f * ny, 0.42f, 1.f);
            for (int dx = -rx; dx <= rx; ++dx) {
                const float nx = float(dx) / (float(rx) * taper);
                const float d2 = nx * nx + ny * ny;
                if (d2 > 1.f) continue;
                const float rim = std::pow(std::clamp((d2 - 0.60f) / 0.40f, 0.f, 1.f), 1.5f);
                const float highlight = std::exp(-((nx + 0.32f) * (nx + 0.32f) +
                                                   (ny + 0.40f) * (ny + 0.40f)) * 28.f);
                blend(image, width, height, cx + dx, cy + dy,
                      mix({0.035f, 0.13f, 0.17f}, {0.72f, 0.94f, 0.96f}, highlight + rim * 0.45f),
                      0.30f + rim * 0.46f + highlight * 0.65f);
            }
        }
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
