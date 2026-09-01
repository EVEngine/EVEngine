#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/shaders/ReflectionProbeWgsl.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

glm::vec3 faceDirection(int face, float u, float v) {
    switch (face) {
        case 0: return glm::normalize(glm::vec3(1.f, -v, -u));
        case 1: return glm::normalize(glm::vec3(-1.f, -v, u));
        case 2: return glm::normalize(glm::vec3(u, 1.f, v));
        case 3: return glm::normalize(glm::vec3(u, -1.f, -v));
        case 4: return glm::normalize(glm::vec3(u, -v, 1.f));
        default: return glm::normalize(glm::vec3(-u, -v, -1.f));
    }
}

std::array<glm::vec3, 24 * 33> allFaceEdges() {
    std::array<glm::vec3, 24 * 33> result{};
    size_t out = 0;
    for (int face = 0; face < 6; ++face) {
        for (int edge = 0; edge < 4; ++edge) {
            for (int sample = 0; sample < 33; ++sample) {
                const float t = float(sample) / 16.f - 1.f;
                const float u = edge < 2 ? (edge == 0 ? -1.f : 1.f) : t;
                const float v = edge >= 2 ? (edge == 2 ? -1.f : 1.f) : t;
                result[out++] = faceDirection(face, u, v);
            }
        }
    }
    return result;
}

}  // namespace

TEST_CASE("graphics.reflectionProbe.hammersleyDeterministic") {
    CHECK(std::abs(radicalInverse(0u) - 0.f) < 1e-7f);
    CHECK(std::abs(radicalInverse(1u) - 0.5f) < 1e-7f);
    CHECK(std::abs(radicalInverse(2u) - 0.25f) < 1e-7f);
    CHECK(std::abs(radicalInverse(3u) - 0.75f) < 1e-7f);
    for (uint32_t i = 0; i < 512; ++i) {
        CHECK(float(i) / 512.f >= 0.f);
        CHECK(float(i) / 512.f < 1.f);
        CHECK(radicalInverse(i) >= 0.f);
        CHECK(radicalInverse(i) < 1.f);
    }
}

TEST_CASE("graphics.reflectionProbe.cubemapEdgesAreContinuous") {
    const auto edges = allFaceEdges();
    constexpr size_t samplesPerFace = 4u * 33u;
    for (size_t face = 0; face < 6; ++face) {
        for (size_t local = 0; local < samplesPerFace; ++local) {
            const glm::vec3 direction = edges[face * samplesPerFace + local];
            float bestAdjacentDot = -1.f;
            for (size_t otherFace = 0; otherFace < 6; ++otherFace) {
                if (otherFace == face) continue;
                for (size_t other = 0; other < samplesPerFace; ++other)
                    bestAdjacentDot = std::max(
                        bestAdjacentDot,
                        glm::dot(direction, edges[otherFace * samplesPerFace + other]));
            }
            CHECK(bestAdjacentDot > 0.999999f);
        }
    }
}

TEST_CASE("graphics.reflectionProbe.diffuseWhiteFurnaceNormalization") {
    // Cosine-distributed hemisphere samples estimate irradiance / PI directly.
    // For L(z) = 0.5 + 0.5z, E_cosine[z] = 2/3, hence the analytic result is 5/6.
    constexpr int sampleCount = 512;
    float estimate = 0.f;
    for (int i = 0; i < sampleCount; ++i) {
        const float xiX = float(i) / float(sampleCount);
        const float xiY = radicalInverse(uint32_t(i));
        const float phi = 2.f * kPi * xiX;
        const float radius = std::sqrt(xiY);
        const glm::vec3 l(std::cos(phi) * radius, std::sin(phi) * radius,
                          std::sqrt(std::max(1.f - xiY, 0.f)));
        estimate += 0.5f + 0.5f * l.z;
    }
    estimate /= float(sampleCount);
    CHECK(std::abs(estimate - 5.f / 6.f) < 0.003f);

    const std::string_view wgsl(eve::graphics::shaders::kReflectionProbeFilterWgsl);
    CHECK(wgsl.find("let sampleWeight = select(noL, 1.0, params.data.z > 0.5)") !=
          std::string_view::npos);

    const std::string glslPath =
        (std::string(__FILE__).substr(0, std::string(__FILE__).find_last_of("/\\")) +
         "/../src/modules/graphics/shaders/reflection_probe_filter.comp");
    std::ifstream glslFile(glslPath);
    REQUIRE(glslFile.good());
    const std::string glsl((std::istreambuf_iterator<char>(glslFile)),
                           std::istreambuf_iterator<char>());
    CHECK(glsl.find("params.diffuseMode != 0u ? 1.0 : noL") != std::string::npos);
}
