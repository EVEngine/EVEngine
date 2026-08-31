#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/CubemapPrefilter.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using eve::graphics::buildGgxCubemapMipChain;

TEST_CASE("graphics.cubemapPrefilter.rejectsInvalidInput") {
    CHECK(buildGgxCubemapMipChain(nullptr, 4u, 3u).empty());
    const uint8_t pixel[4] = {0u, 0u, 0u, 255u};
    CHECK(buildGgxCubemapMipChain(pixel, 0u, 1u).empty());
    CHECK(buildGgxCubemapMipChain(pixel, 1u, 0u).empty());
}

TEST_CASE("graphics.cubemapPrefilter.uniformRadianceAndLayout") {
    constexpr uint32_t size = 4;
    constexpr uint32_t levels = 3;
    constexpr uint8_t rgba[4] = {37, 91, 173, 229};
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u);
    for (size_t i = 0; i < faces.size(); i += 4u) {
        faces[i] = rgba[0];
        faces[i + 1u] = rgba[1];
        faces[i + 2u] = rgba[2];
        faces[i + 3u] = rgba[3];
    }

    const std::vector<uint8_t> chain = buildGgxCubemapMipChain(faces.data(), size, levels, 16u);
    const size_t expectedBytes = 6u * 4u * (size_t(4u * 4u) + size_t(2u * 2u) + 1u);
    REQUIRE(chain.size() == expectedBytes);
    REQUIRE(chain.size() >= faces.size());
    for (size_t i = 0; i < faces.size(); ++i) CHECK(chain[i] == faces[i]);
    for (size_t i = faces.size(); i < chain.size(); i += 4u) {
        CHECK(chain[i] == rgba[0]);
        CHECK(chain[i + 1u] == rgba[1]);
        CHECK(chain[i + 2u] == rgba[2]);
        CHECK(chain[i + 3u] == rgba[3]);
    }
}

TEST_CASE("graphics.cubemapPrefilter.roughMipsCrossFaceSample") {
    constexpr uint32_t size = 4;
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u, 255u);
    for (uint32_t face = 0; face < 6u; ++face) {
        const uint8_t value = uint8_t(face * 40u);
        const size_t begin = size_t(face) * size * size * 4u;
        for (size_t i = begin; i < begin + size_t(size) * size * 4u; i += 4u) {
            faces[i] = value;
            faces[i + 1u] = value;
            faces[i + 2u] = value;
        }
    }

    const std::vector<uint8_t> chain = buildGgxCubemapMipChain(faces.data(), size, 3u, 32u);
    const size_t lastMip = size_t(size) * size * 4u * 6u + size_t(2u) * 2u * 4u * 6u;
    bool mixed = false;
    for (uint32_t face = 0; face < 6u; ++face) {
        const uint8_t original = uint8_t(face * 40u);
        if (chain[lastMip + size_t(face) * 4u] != original) mixed = true;
    }
    CHECK(mixed);
}

TEST_CASE("graphics.cubemapPrefilter.diffuseIrradianceKeepsDirection") {
    constexpr uint32_t size = 4;
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u, 255u);
    for (size_t i = 0; i < faces.size(); i += 4u) {
        faces[i] = 0u;
        faces[i + 1u] = 0u;
        faces[i + 2u] = 0u;
    }
    const size_t positiveY = size_t(2u) * size * size * 4u;
    for (size_t i = positiveY; i < positiveY + size_t(size) * size * 4u; i += 4u) {
        faces[i] = 255u;
        faces[i + 1u] = 255u;
        faces[i + 2u] = 255u;
    }

    const std::vector<uint8_t> chain = buildGgxCubemapMipChain(faces.data(), size, 3u, 32u);
    const size_t lastMip = size_t(size) * size * 4u * 6u + size_t(2u) * 2u * 4u * 6u;
    const uint8_t towardLight = chain[lastMip + size_t(2u) * 4u];
    const uint8_t awayFromLight = chain[lastMip + size_t(3u) * 4u];
    CHECK(towardLight > awayFromLight);
}

TEST_CASE("graphics.cubemapPrefilter.bilinearTapCrossesFaceEdge") {
    constexpr uint32_t size = 4;
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u, 255u);
    for (size_t i = 0; i < faces.size(); i += 4u) {
        faces[i] = 0u;
        faces[i + 1u] = 0u;
        faces[i + 2u] = 0u;
    }
    const size_t positiveZ = size_t(4u) * size * size * 4u;
    for (size_t i = positiveZ; i < positiveZ + size_t(size) * size * 4u; i += 4u)
        faces[i] = 200u;

    const glm::vec4 edge = eve::graphics::cubemap_prefilter_detail::sampleBase(
        faces.data(), size, glm::normalize(glm::vec3(1.f, 0.f, 1.f)));
    CHECK(edge.r > 0.f);
    CHECK(edge.r < 200.f);
}
