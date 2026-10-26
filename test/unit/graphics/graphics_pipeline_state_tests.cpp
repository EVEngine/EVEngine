// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "BloomSettings.cpp",
    "ClusteredLighting.cpp",
    "CubemapPrefilter.cpp",
    "Lighting2D.cpp",
    "MeshInstanceRange.cpp",
    "ParallaxMap.cpp",
    "Quad.cpp",
    "RenderSceneEffects.cpp",
    "RenderSystem3D.cpp",
    "Shadow3D.cpp",
    "ShadowScheme.cpp",
    "SkyAtmosphereLuts.cpp",
    "Volumetric.cpp",
    "atmosphere_volume.cpp",
    "depth_of_field_focus.cpp",
    "diffuse_light_probe.cpp",
    "graphics_camera_factory.cpp",
    "graphics_display_output.cpp",
    "graphics_pbr_surface.cpp",
    "graphics_primitives.cpp",
    "graphics_skin_palette.cpp",
    "graphics_vegetation_extras.cpp",
    "graphics_webgpu_pbr_variant.cpp",
    "grass_billboard.cpp",
    "grass.cpp",
    "grass_wind.cpp",
    "hair.cpp",
    "indirect_builder.cpp",
    "reflection_probe_culling.cpp",
    "tree_wind.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_BloomSettings)
#include "graphics/Bloom.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_bloom_settings {

TEST_CASE("graphics.bloomSettings.filterAndBoundsValidateWithoutMutation") {
    using namespace eve::graphics;
    SUB_CASE("all named reconstruction filters accept default numeric settings") {
        for (const auto filter : {BloomFilter::KarisTent, BloomFilter::GaussianScatter, BloomFilter::GaussianPyramid}) {
            BloomFilterSettings settings;
            settings.filter = filter;
            REQUIRE(validateBloomFilterSettings(settings).ok());
        }
    };
    SUB_CASE("closed scatter and iteration bounds accept endpoints") {
        BloomFilterSettings settings;
        for (const float scatter : {0.f, 1.f}) {
            settings.scatter = scatter;
            REQUIRE(validateBloomFilterSettings(settings).ok());
        }
        for (const int iterations : {1, 16}) {
            settings.maxIterations = iterations;
            REQUIRE(validateBloomFilterSettings(settings).ok());
        }
        settings.clamp = 65504.f;
        REQUIRE(validateBloomFilterSettings(settings).ok());
    };
    SUB_CASE("invalid enum, nonfinite values, and out-of-range bounds return failure") {
        BloomFilterSettings settings;
        settings.filter = static_cast<BloomFilter>(99);
        CHECK(!validateBloomFilterSettings(settings).ok());
        settings = {};
        for (const float scatter :
             {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            settings.scatter = scatter;
            CHECK(!validateBloomFilterSettings(settings).ok());
        }
        settings = {};
        for (const int iterations : {0, 17}) {
            settings.maxIterations = iterations;
            CHECK(!validateBloomFilterSettings(settings).ok());
        }
        settings = {};
        for (const float clamp :
             {0.f, -1.f, 65505.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            settings.clamp = clamp;
            CHECK(!validateBloomFilterSettings(settings).ok());
        }
    };
}
}  // namespace test_bloom_settings
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ClusteredLighting)
#include "graphics/ClusteredLight.h"
#include "zeroerr/unittest.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace test_clustered_lighting {
using namespace eve::graphics;

ClusteredLightGpu point(float x, float y, float z, float radius) {
    ClusteredLightGpu light{};
    light.posRadius = {x, y, z, radius};
    light.color     = {1.f, 1.f, 1.f, 1.f};
    return light;
}

TEST_CASE("graphics.clusteredLighting.emptyFrameStillPublishesFixedGridAndDirectionalFallback") {
    const auto upload =
        buildClusteredLighting({}, {}, glm::mat4(1.f), 0.1f, 100.f, 0, 0, 1.f, glm::vec4(0.1f, 0.2f, 0.3f, 0.f));
    REQUIRE(upload.active);
    CHECK_EQ(upload.lights.size(), 0u);
    CHECK_EQ(upload.clusterTable.size(), std::size_t(ClusteredLightConfig::kClusterCount));
    CHECK_EQ(upload.clipInfo.z, 1.f);
    CHECK_EQ(upload.clipInfo.w, 1.f);
    CHECK_EQ(upload.primaryDir.w, 0.f);
    CHECK_EQ(upload.ambient.y, 0.2f);
}

TEST_CASE("graphics.clusteredLighting.pointsPopulateFixedGridAndDirectionalInputBecomesPrimary") {
    std::vector<ClusteredLightGpu> points;
    for (int i = 0; i < 32; ++i) points.push_back(point(float(i % 8) * 0.2f, 0.f, 0.f, 2.f));
    ClusteredLightGpu sun = point(0.f, 1.f, 0.f, 0.f);
    sun.color             = {0.2f, 0.3f, 0.4f, 1.f};
    const auto view       = glm::lookAtRH(glm::vec3(0.f, 0.f, 3.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    const auto upload     = buildClusteredLighting(points, {sun}, view, 0.1f, 100.f, 320, 240, 1.f, glm::vec4(0.1f));
    REQUIRE(upload.active);
    CHECK_EQ(upload.lights.size(), 32u);
    CHECK_EQ(upload.clusterTable.size(), std::size_t(ClusteredLightConfig::kClusterCount));
    CHECK_GT(upload.lightIndices.size(), 0u);
    CHECK_EQ(upload.primaryDir.w, 1.f);
    CHECK_EQ(upload.primaryColor.y, 0.3f);
    uint32_t references = 0;
    for (const auto& entry : upload.clusterTable) references += entry.count;
    CHECK_GT(references, 0u);
}

TEST_CASE("graphics.clusteredLighting.frameAndClusterBudgetsCountDiscardedReferences") {
    std::vector<ClusteredLightGpu> points(300, point(0.f, 0.f, 0.f, 4.f));
    const auto view   = glm::lookAtRH(glm::vec3(0.f, 0.f, 3.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    const auto upload = buildClusteredLighting(points, {}, view, 0.1f, 100.f, 1920, 1080, 1.f, glm::vec4(0.f));
    REQUIRE(upload.active);
    CHECK_EQ(upload.lights.size(), std::size_t(ClusteredLightConfig::kMaxLights));
    CHECK_EQ(upload.truncatedLightCount, 44u);
    CHECK_GT(upload.overflowClusterCount, 0u);
    CHECK_GT(upload.droppedLightReferenceCount, 0u);
    for (const auto& entry : upload.clusterTable)
        CHECK_LE(entry.count, uint32_t(ClusteredLightConfig::kMaxLightsPerCluster));
}
}  // namespace test_clustered_lighting
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_CubemapPrefilter)
#include "graphics/CubemapPrefilter.h"
#include "zeroerr/unittest.h"

#include <cstddef>
#include <cstdint>
#include <vector>
namespace test_cubemap_prefilter {
using namespace eve::graphics;

constexpr uint32_t kSize      = 4;
constexpr size_t   kFaceBytes = size_t(kSize) * kSize * 4u;

std::vector<uint8_t> solidFaces(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> faces(kFaceBytes * 6u);
    for (size_t i = 0; i < faces.size(); i += 4u) {
        faces[i]      = r;
        faces[i + 1u] = g;
        faces[i + 2u] = b;
        faces[i + 3u] = a;
    }
    return faces;
}

TEST_CASE("graphics.cubemapPrefilter.invalidDimensionsAndNullSourceYieldNoMipData") {
    const uint8_t pixel[4] = {0u, 0u, 0u, 255u};
    REQUIRE(buildGgxCubemapMipChain(nullptr, kSize, 3u).empty());
    CHECK(buildGgxCubemapMipChain(pixel, 0u, 1u).empty());
    CHECK(buildGgxCubemapMipChain(pixel, 1u, 0u).empty());
}

TEST_CASE("graphics.cubemapPrefilter.uniformSixFacesKeepChannelsAndMipLayout") {
    const auto faces = solidFaces(37u, 91u, 173u, 229u);
    const auto chain = buildGgxCubemapMipChain(faces.data(), kSize, 3u, 16u);
    REQUIRE_EQ(chain.size(), size_t(6u * 4u * (16u + 4u + 1u)));
    for (size_t i = 0; i < chain.size(); i += 4u) {
        CHECK_EQ(chain[i], 37u);
        CHECK_EQ(chain[i + 1u], 91u);
        CHECK_EQ(chain[i + 2u], 173u);
        CHECK_EQ(chain[i + 3u], 229u);
    }
}

TEST_CASE("graphics.cubemapPrefilter.faceLightingBlendsAcrossRoughMipAndPreservesDirection") {
    SUB_CASE("distinct source faces mix in roughest mip") {
        auto faces = solidFaces(0u, 0u, 0u, 255u);
        for (uint32_t face = 0; face < 6u; ++face) {
            for (size_t i = size_t(face) * kFaceBytes; i < size_t(face + 1u) * kFaceBytes; i += 4u)
                faces[i] = uint8_t(face * 40u);
        }
        const auto   chain   = buildGgxCubemapMipChain(faces.data(), kSize, 3u, 32u);
        const size_t lastMip = kFaceBytes * 6u + size_t(2u * 2u * 4u * 6u);
        bool         mixed   = false;
        for (uint32_t face = 0; face < 6u; ++face) mixed |= chain[lastMip + size_t(face) * 4u] != uint8_t(face * 40u);
        CHECK(mixed);
    };
    SUB_CASE("positive Y source gives greater positive Y irradiance") {
        auto faces = solidFaces(0u, 0u, 0u, 255u);
        for (size_t i = 2u * kFaceBytes; i < 3u * kFaceBytes; i += 4u) {
            faces[i] = faces[i + 1u] = faces[i + 2u] = 255u;
        }
        const auto   chain   = buildGgxCubemapMipChain(faces.data(), kSize, 3u, 32u);
        const size_t lastMip = kFaceBytes * 6u + size_t(2u * 2u * 4u * 6u);
        REQUIRE_GT(chain[lastMip + 2u * 4u], chain[lastMip + 3u * 4u]);
    };
}

TEST_CASE("graphics.cubemapPrefilter.bilinearTapNearCubeEdgeSamplesNeighborFace") {
    auto faces = solidFaces(0u, 0u, 0u, 255u);
    for (size_t i = 4u * kFaceBytes; i < 5u * kFaceBytes; i += 4u) faces[i] = 200u;
    const glm::vec4 edge =
        cubemap_prefilter_detail::sampleBase(faces.data(), kSize, glm::normalize(glm::vec3(1.f, 0.f, 1.f)));
    REQUIRE_GT(edge.r, 0.f);
    CHECK_LT(edge.r, 200.f);
}
}  // namespace test_cubemap_prefilter
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Lighting2D)
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "zeroerr/unittest.h"

namespace test_lighting_2d {
using namespace eve::graphics;

TEST_CASE("graphics.light2d.factorySelectsTypesAndClampsSpotParameters") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            point       = Light2D::createLight("point");
    auto*            directional = Light2D::createLight("dir");
    auto*            spot        = Light2D::createLight("spot");
    REQUIRE(point != nullptr);
    REQUIRE(directional != nullptr);
    REQUIRE(spot != nullptr);
    CHECK_EQ(point->getType(), std::string("point"));
    CHECK_EQ(directional->getType(), std::string("dir"));
    CHECK_EQ(spot->getType(), std::string("spot"));
    directional->setDirection(1.f, 0.f);
    CHECK_EQ(directional->getDirX(), 1.f);
    spot->setSpotAngle(25.f);
    spot->setSpotSoftness(0.5f);
    CHECK_EQ(spot->getSpotAngle(), 25.f);
    CHECK_EQ(spot->getSpotSoftness(), 0.5f);
    spot->setSpotAngle(120.f);
    spot->setSpotSoftness(-1.f);
    CHECK_EQ(spot->getSpotAngle(), 89.f);
    CHECK_EQ(spot->getSpotSoftness(), 0.f);
    point->setType("unknown");
    CHECK_EQ(point->getType(), std::string("point"));
    ecs::DestroyEntity(point);
    ecs::DestroyEntity(directional);
    ecs::DestroyEntity(spot);
}

TEST_CASE("graphics.camera2d.ambientValuesAreOwnedByCreatedCamera") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            camera = Camera2D::createCamera();
    REQUIRE(camera != nullptr);
    camera->setAmbient(0.2f, 0.3f, 0.4f);
    CHECK_EQ(camera->data()->ambientR, 0.2f);
    CHECK_EQ(camera->data()->ambientG, 0.3f);
    CHECK_EQ(camera->data()->ambientB, 0.4f);
    ecs::DestroyEntity(camera);
}
}  // namespace test_lighting_2d
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_MeshInstanceRange)
#include "graphics/MeshInstanceRange.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_mesh_instance_range {
TEST_CASE("graphics.instanceRange.visibleBoundsRespectClipDepthAndHorizontalDistance") {
    using namespace eve::graphics;
    MeshInstanceRange range{0, 3, {-0.2f, -0.2f, 0.2f}, {0.2f, 0.2f, 0.8f}, 0};
    REQUIRE(validateMeshInstanceRange(range).ok());
    const glm::mat4 identity(1.f);
    SUB_CASE("identity bounds lie inside the Vulkan clip volume") {
        CHECK(meshInstanceRangeVisible(range, identity, identity, glm::vec3(0.f)));
    };
    SUB_CASE("transformed bounds overlap the side edge, then leave it and the near plane") {
        auto model  = identity;
        model[3][0] = 1.1f;
        CHECK(meshInstanceRangeVisible(range, model, identity, glm::vec3(0.f)));
        model[3][0] = 1.3f;
        CHECK(!meshInstanceRangeVisible(range, model, identity, glm::vec3(0.f)));
        model       = identity;
        model[3][2] = -1.f;
        CHECK(!meshInstanceRangeVisible(range, model, identity, glm::vec3(0.f)));
    };
    SUB_CASE("distance ignores camera height and zero instances are invisible") {
        range.maximumHorizontalDistance = 1.f;
        CHECK(!meshInstanceRangeVisible(range, identity, identity, glm::vec3(10.f, 0.f, 0.f)));
        CHECK(meshInstanceRangeVisible(range, identity, identity, glm::vec3(0.f, 100.f, 0.f)));
        range.count = 0;
        CHECK(!meshInstanceRangeVisible(range, identity, identity, glm::vec3(0.f)));
    };
}
TEST_CASE("graphics.instanceRange.invalidIndexBoundsAndDistanceFailValidation") {
    using namespace eve::graphics;
    MeshInstanceRange range;
    range.first = UINT32_MAX;
    range.count = 1;
    REQUIRE(!validateMeshInstanceRange(range).ok());
    range            = {};
    range.minimum[0] = 1;
    CHECK(!validateMeshInstanceRange(range).ok());
    range                           = {};
    range.maximumHorizontalDistance = -1.f;
    CHECK(!validateMeshInstanceRange(range).ok());
    range            = {};
    range.maximum[1] = std::numeric_limits<float>::infinity();
    CHECK(!validateMeshInstanceRange(range).ok());
}
}  // namespace test_mesh_instance_range
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ParallaxMap)
#include "graphics/ParallaxMap.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_parallax_map {
using namespace eve::graphics;

TEST_CASE("graphics.parallax.offsetUsesHeightAndZeroScaleIsIdentity") {
    float u = -1.f;
    float v = -1.f;
    parallaxOffsetUV(0.4f, 0.6f, 0.2f, 0.5f, -0.3f, 0.8f, 0.f, u, v);
    REQUIRE_LT(std::fabs(u - 0.4f), 1e-6f);
    CHECK_LT(std::fabs(v - 0.6f), 1e-6f);

    float lowU = 0.f, lowV = 0.f;
    float highU = 0.f, highV = 0.f;
    parallaxOffsetUV(0.5f, 0.5f, 0.f, 0.4f, 0.f, 0.9f, 0.1f, lowU, lowV);
    parallaxOffsetUV(0.5f, 0.5f, 1.f, 0.4f, 0.f, 0.9f, 0.1f, highU, highV);
    CHECK_LT(lowU, highU);
    CHECK_LT(std::fabs(lowV - 0.5f), 1e-5f);
    CHECK_LT(std::fabs(highV - 0.5f), 1e-5f);
}

TEST_CASE("graphics.parallax.parametersClampScaleAndLayerOrder") {
    ParallaxParams params;
    params.scale     = 1.f;
    params.minLayers = 0.f;
    params.maxLayers = 100.f;
    clampParallaxParams(params);
    REQUIRE_EQ(params.scale, 0.25f);
    CHECK_EQ(params.minLayers, 1.f);
    CHECK_EQ(params.maxLayers, 64.f);

    params.scale     = -1.f;
    params.minLayers = 16.f;
    params.maxLayers = 8.f;
    clampParallaxParams(params);
    CHECK_EQ(params.scale, 0.f);
    CHECK_EQ(params.maxLayers, 16.f);
}

TEST_CASE("graphics.parallax.chartCoverageIncludesClosedEdgesAndOptionalPadding") {
    SUB_CASE("hard chart coverage includes its closed unit rectangle") {
        for (const float edge : {0.f, 0.5f, 1.f}) REQUIRE_EQ(silPomCoverage(edge, edge, 0.f), 1.f);
        CHECK_EQ(silPomCoverage(-0.01f, 0.5f, 0.f), 0.f);
        CHECK_EQ(silPomCoverage(0.5f, 1.01f, 0.f), 0.f);
        CHECK_EQ(silPomCoverage(-0.01f, 0.5f, 0.02f), 1.f);
    };
    SUB_CASE("soft coverage fades at edge and horizon rejects low grazing height") {
        CHECK_EQ(silPomCoverageSoft(0.5f, 0.5f, 0.05f), 1.f);
        CHECK_EQ(silPomCoverageSoft(-0.01f, 0.5f, 0.05f), 0.f);
        const float edge = silPomCoverageSoft(0.02f, 0.5f, 0.05f);
        CHECK_GT(edge, 0.f);
        CHECK_LT(edge, 1.f);
        CHECK_EQ(silPomHorizonTrim(0.1f, 1.f, 0.45f), 1.f);
        CHECK_EQ(silPomHorizonTrim(0.1f, 0.05f, 0.45f), 0.f);
        CHECK_EQ(silPomHorizonTrim(0.95f, 0.05f, 0.45f), 1.f);
    };
}

TEST_CASE("graphics.parallax.screenSpaceOffsetScalesWithHeightAndCoverage") {
    float x0 = 0.f, y0 = 0.f;
    float x1 = 0.f, y1 = 0.f;
    ssdmScreenOffset(0.f, 1.f, 0.f, 0.05f, x0, y0);
    ssdmScreenOffset(1.f, 1.f, 0.f, 0.05f, x1, y1);
    REQUIRE_LT(std::fabs(x0), 1e-6f);
    CHECK_LT(std::fabs(y0), 1e-6f);
    CHECK_LT(std::fabs(x1 - 0.05f), 1e-5f);
    CHECK_LT(std::fabs(y1), 1e-6f);
    CHECK_EQ(ssdmCoverage(0.9f, 0.5f, 0.15f, 0.f, 0.f), 0.f);
    CHECK_EQ(ssdmCoverage(0.9f, 0.5f, 0.05f, 0.f, 0.f), 1.f);
}
}  // namespace test_parallax_map
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Quad)
#include "graphics/Quad.h"
#include "zeroerr/unittest.h"
namespace test_quad {
using namespace eve::graphics;

TEST_CASE("graphics.quad.pixelRectangleConvertsToUVAndInvalidExtentUsesFullTexture") {
    float u0 = -1.f, v0 = -1.f, u1 = -1.f, v1 = -1.f;
    SUB_CASE("pixel origin and extent divide by logical texture dimensions") {
        Quad quad(16, 8, 32, 16);
        quad.getUV(64, 64, u0, v0, u1, v1);
        REQUIRE_EQ(u0, 0.25f);
        CHECK_EQ(v0, 0.125f);
        CHECK_EQ(u1, 0.75f);
        CHECK_EQ(v1, 0.375f);
        quad.setViewport(0, 0, 64, 64);
        quad.getUV(64, 64, u0, v0, u1, v1);
        CHECK_EQ(u0, 0.f);
        CHECK_EQ(v0, 0.f);
        CHECK_EQ(u1, 1.f);
        CHECK_EQ(v1, 1.f);
    };
    SUB_CASE("nonpositive viewport or texture size returns full UV rectangle") {
        Quad quad(5, 7, -4, 0);
        CHECK_EQ(quad.getWidth(), 0);
        CHECK_EQ(quad.getHeight(), 0);
        quad.getUV(64, 64, u0, v0, u1, v1);
        CHECK_EQ(u0, 0.f);
        CHECK_EQ(v0, 0.f);
        CHECK_EQ(u1, 1.f);
        CHECK_EQ(v1, 1.f);
        quad.setViewport(1, 2, 3, 4);
        quad.getUV(0, 64, u0, v0, u1, v1);
        CHECK_EQ(u0, 0.f);
        CHECK_EQ(v0, 0.f);
        CHECK_EQ(u1, 1.f);
        CHECK_EQ(v1, 1.f);
    };
}
}  // namespace test_quad
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_RenderSceneEffects)
#include "graphics/ClipSpace.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_render_scene_projection {
TEST_CASE("graphics.clipSpace.perspectiveAndOrthographicProjectionFlipYForVulkan") {
    using namespace eve::graphics;
    SUB_CASE("perspective Y has opposite sign while other diagonal elements stay equal") {
        const glm::mat4 gl = glm::perspectiveRH_ZO(1.f, 1.f, 0.1f, 100.f);
        const glm::mat4 vk = perspectiveVulkanRH_ZO(1.f, 1.f, 0.1f, 100.f);
        REQUIRE_LT(std::fabs(gl[1][1] + vk[1][1]), 1e-5f);
        CHECK_EQ(gl[0][0], vk[0][0]);
        CHECK_LT(vk[1][1], 0.f);
    };
    SUB_CASE("orthographic camera helper uses the same Y convention") {
        const glm::mat4 direct = orthoVulkanRH_ZO(-2.f, 2.f, -1.f, 1.f, 0.1f, 20.f);
        const glm::mat4 camera = cameraProjectionVulkanRH_ZO(true, 1.f, 2.f, 2.f, 0.1f, 20.f);
        CHECK_EQ(camera[0][0], direct[0][0]);
        CHECK_EQ(camera[1][1], direct[1][1]);
        CHECK_LT(camera[1][1], 0.f);
    };
}
}  // namespace test_render_scene_projection
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_RenderSystem3D)
#include "graphics/RenderSystem3D.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_render_system_3d {
using namespace eve::graphics;

TEST_CASE("graphics.camera3d.screenToRayUsesPerspectiveEyeOrOrthographicNearPlane") {
    SUB_CASE("perspective center ray starts at eye and points down normalized view direction") {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto*            camera = Camera3D::createCamera();
        REQUIRE(camera != nullptr);
        camera->setEye(0.f, 0.f, 5.f);
        camera->setTarget(0.f, 0.f, 0.f);
        camera->setUp(0.f, 1.f, 0.f);
        camera->setFov(60.f);
        camera->screenToRay(160.f, 120.f, 320.f, 240.f);
        CHECK_LT(std::fabs(camera->getScreenRayOriginX()), 1e-4f);
        CHECK_LT(std::fabs(camera->getScreenRayOriginZ() - 5.f), 1e-4f);
        CHECK_LT(std::fabs(camera->getScreenRayDirX()), 0.05f);
        CHECK_LT(camera->getScreenRayDirZ(), -0.9f);
        const float length = std::sqrt(camera->getScreenRayDirX() * camera->getScreenRayDirX() +
                                       camera->getScreenRayDirY() * camera->getScreenRayDirY() +
                                       camera->getScreenRayDirZ() * camera->getScreenRayDirZ());
        CHECK_LT(std::fabs(length - 1.f), 1e-4f);
        camera->screenToRay(0.f, 0.f, 0.f, 240.f);
        CHECK_EQ(camera->getScreenRayOriginZ(), 5.f);
        CHECK_EQ(camera->getScreenRayDirZ(), -1.f);
        ecs::DestroyEntity(camera);
    };
    SUB_CASE("orthographic rays from separate pixels shift origin but retain parallel direction") {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        auto*            camera = Camera3D::createCamera();
        REQUIRE(camera != nullptr);
        camera->setEye(0.f, 0.f, 10.f);
        camera->setTarget(0.f, 0.f, 0.f);
        camera->setOrthographic(8.f);
        camera->screenToRay(20.f, 60.f, 160.f, 120.f);
        const float leftOrigin = camera->getScreenRayOriginX();
        const float directionX = camera->getScreenRayDirX();
        const float directionZ = camera->getScreenRayDirZ();
        camera->screenToRay(140.f, 60.f, 160.f, 120.f);
        CHECK_GT(camera->getScreenRayOriginX(), leftOrigin + 1.f);
        CHECK_LT(std::fabs(camera->getScreenRayDirX() - directionX), 1e-4f);
        CHECK_LT(std::fabs(camera->getScreenRayDirZ() - directionZ), 1e-4f);
        CHECK_LT(camera->getScreenRayDirZ(), -0.99f);
        ecs::DestroyEntity(camera);
    };
}

TEST_CASE("graphics.renderSystem3d.shadowDrawerRegistrationIssuesUniqueTokensAndRemovalIsIdempotent") {
    CHECK_EQ(RenderSystem3D::addShadowExtraDrawer({}), 0u);
    const auto first  = RenderSystem3D::addShadowExtraDrawer([](auto&, const auto&, const auto&) {});
    const auto second = RenderSystem3D::addShadowExtraDrawer([](auto&, const auto&, const auto&) {});
    REQUIRE(first != 0u);
    REQUIRE(second != 0u);
    CHECK_NE(first, second);
    RenderSystem3D::removeShadowExtraDrawer(first);
    RenderSystem3D::removeShadowExtraDrawer(first);
    RenderSystem3D::removeShadowExtraDrawer(second);
    RenderSystem3D::removeShadowExtraDrawer(0u);
}
}  // namespace test_render_system_3d
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Shadow3D)
#include "graphics/Shadow.h"
#include "graphics/ShadowScheme.h"
#include "zeroerr/unittest.h"

namespace test_shadow_csm {
using namespace eve::graphics;

struct RestoreScheme {
    ShadowSchemeSettings saved = ShadowSchemeSettings::current();
    ~RestoreScheme() {
        ShadowSchemeSettings::current() = saved;
        resetShadowLocalPageCache();
    }
};

ShadowUpload sampleCsm() {
    return buildDirectionalCSM(glm::vec3(0.2f, 1.f, 0.1f), glm::vec3(0.f, 2.f, 8.f), glm::vec3(0.f, 1.f, 0.f),
                               glm::vec3(0.f, 1.f, 0.f), 1.047f, 16.f / 9.f, 0.1f, 500.f, 0.002f, 1.f);
}

TEST_CASE("graphics.shadowCsm.splitsClampCameraFarToPracticalViewDistance") {
    RestoreScheme restore;
    auto&         scheme          = ShadowSchemeSettings::current();
    scheme.directionalMapSize     = ShadowConfig::kMapSize;
    scheme.localMapSize           = 1024;
    scheme.directionalMaxDistance = ShadowConfig::kMaxDistance;
    const auto upload             = sampleCsm();
    REQUIRE(upload.active);
    CHECK_GT(upload.ubo.splits.x, 0.1f);
    CHECK_LT(upload.ubo.splits.x, upload.ubo.splits.y);
    CHECK_LE(upload.ubo.splits.y, upload.ubo.splits.z + 1e-4f);
    CHECK_LE(upload.ubo.splits.z, ShadowConfig::kMaxDistance + 1e-3f);
    CHECK_LT(upload.ubo.splits.z, 100.f);
    CHECK_GE(upload.ubo.bias.x, 0.002f);
}

TEST_CASE("graphics.shadowScheme.mapResolutionAndFarDistanceChangeCsmWithoutInvalidMutation") {
    RestoreScheme restore;
    auto&         scheme = ShadowSchemeSettings::current();
    resetShadowLocalPageCache();
    CHECK_EQ(clampShadowMapSize(100), 256);
    CHECK_EQ(clampShadowMapSize(900), 1024);
    CHECK_EQ(clampShadowMapSize(3000), 2048);
    CHECK_EQ(clampShadowMapSize(8000), 4096);
    CHECK_EQ(shadowMapSizeForResolutionTier(0), 512);
    CHECK_EQ(shadowMapSizeForResolutionTier(3), 4096);
    REQUIRE(setShadowSchemeMapSizes(4096, 512).ok());
    CHECK_EQ(scheme.directionalMapSize, 4096);
    CHECK_EQ(scheme.localMapSize, 512);
    CHECK_EQ(scheme.resolvedAtlasMapSize(), 4096);
    REQUIRE(setShadowSchemeDirectionalMaxDistance(128.f).ok());
    CHECK_EQ(scheme.resolvedDirectionalMaxDistance(), 128.f);
    CHECK(!setShadowSchemeMapSizes(0, -1).ok());
    CHECK(!setShadowSchemeDirectionalMaxDistance(0.f).ok());
    CHECK_EQ(scheme.directionalMapSize, 4096);
    CHECK_EQ(scheme.directionalMaxDistance, 128.f);

    scheme.enableAdaptiveCascadeResolution = false;
    scheme.directionalMapSize = scheme.localMapSize = 1024;
    scheme.directionalMaxDistance                   = 64.f;
    const auto low                                  = sampleCsm();
    scheme.directionalMapSize                       = 4096;
    const auto high                                 = sampleCsm();
    CHECK_LT(high.ubo.cascadeTexel.x, low.ubo.cascadeTexel.x);
    scheme.directionalMapSize     = 2048;
    scheme.directionalMaxDistance = 128.f;
    const auto far                = sampleCsm();
    CHECK_GT(far.ubo.splits.z, 64.f);
    CHECK_LE(far.ubo.splits.z, 128.001f);
}

TEST_CASE("graphics.shadowCsm.adaptiveCascadeKeepsNearCoverageDenseAndFarCascadeCoarse") {
    RestoreScheme restore;
    auto&         scheme = ShadowSchemeSettings::current();
    resetShadowLocalPageCache();
    scheme.enableAdaptiveCascadeResolution = true;
    scheme.directionalMapSize              = 2048;
    scheme.localMapSize                    = 1024;
    scheme.directionalMaxDistance          = 64.f;
    const float splits[4]                  = {0.1f, 4.f, 16.f, 64.f};
    const float nearCoverage[3]            = {0.9f, 0.4f, 0.1f};
    const auto  allocation                 = allocateAdaptiveCascadeResolutions(2048, splits, nearCoverage, scheme);
    CHECK_EQ(allocation.atlasSize, 2048);
    CHECK_GE(allocation.sizes[0], allocation.sizes[1]);
    CHECK_GE(allocation.sizes[1], allocation.sizes[2]);
    CHECK_GE(allocation.sizes[0], 1024);
    CHECK_LT(allocation.sizes[2], allocation.sizes[0]);
    const float farCoverage[3] = {0.1f, 0.2f, 0.95f};
    resetShadowLocalPageCache();
    const auto farAllocation = allocateAdaptiveCascadeResolutions(2048, splits, farCoverage, scheme);
    CHECK_GE(farAllocation.sizes[0], farAllocation.sizes[1]);
    CHECK_GE(farAllocation.sizes[1], farAllocation.sizes[2]);
    resetShadowLocalPageCache();
    const auto upload = sampleCsm();
    REQUIRE(upload.active);
    CHECK_EQ(upload.atlasMapSize, 2048);
    CHECK_GE(upload.cascadeMapSizes[0], upload.cascadeMapSizes[1]);
    CHECK_GE(upload.cascadeMapSizes[1], upload.cascadeMapSizes[2]);
    CHECK_EQ(upload.ubo.cascadeUvScale.w, 2048.f);
    if (upload.cascadeMapSizes[0] > upload.cascadeMapSizes[2])
        CHECK_LT(upload.ubo.cascadeTexel.x, upload.ubo.cascadeTexel.z);
}
}  // namespace test_shadow_csm
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ShadowScheme)
#include "graphics/ShadowScheme.h"
#include "zeroerr/unittest.h"

#include <string>
#include <vector>

namespace test_shadow_scheme {
using namespace eve::graphics;

struct CacheScope {
    CacheScope() { resetShadowLocalPageCache(); }
    ~CacheScope() { resetShadowLocalPageCache(); }
};

Light3D* spot(float x, float intensity) {
    auto* light = Light3D::createLight("spot");
    light->setPosition(x, 2.f, 0.f);
    light->setDirection(0.f, -1.f, 0.f);
    light->setColor(1.f, 1.f, 1.f, intensity);
    light->setRadius(10.f);
    light->setSpotAngle(35.f);
    light->setCastShadow(true);
    light->setShadowMethod("perspective");
    return light;
}

ShadowPagingView originView() {
    ShadowPagingView view{};
    view.eye      = {0.f, 2.f, 8.f};
    view.viewProj = glm::mat4(1.f);
    view.valid    = true;
    return view;
}

TEST_CASE("graphics.shadowMethod.namesResolveByLightTypeAndRejectIncompatibleExplicitMethod") {
    auto csm         = parseShadowMethod("csm");
    auto perspective = parseShadowMethod("perspective");
    auto cube        = parseShadowMethod("cube");
    REQUIRE(csm.ok());
    REQUIRE(perspective.ok());
    REQUIRE(cube.ok());
    CHECK_EQ(csm.value(), ShadowMethod::CascadedDirectional);
    CHECK_EQ(perspective.value(), ShadowMethod::PerspectiveSpot);
    CHECK_EQ(cube.value(), ShadowMethod::CubePoint);
    CHECK(!parseShadowMethod("unknown").ok());
    CHECK_EQ(resolveShadowMethod("dir", ShadowMethod::Auto).value(), ShadowMethod::CascadedDirectional);
    CHECK_EQ(resolveShadowMethod("spot", ShadowMethod::Auto).value(), ShadowMethod::PerspectiveSpot);
    CHECK_EQ(resolveShadowMethod("point", ShadowMethod::Auto).value(), ShadowMethod::CubePoint);
    CHECK(!resolveShadowMethod("dir", ShadowMethod::PerspectiveSpot).ok());
    CHECK(!resolveShadowMethod("spot", ShadowMethod::CascadedDirectional).ok());
    CHECK_EQ(std::string(shadowMethodName(ShadowMethod::PerspectiveSpot)), std::string("perspective"));
}

TEST_CASE("graphics.shadowScheme.selectsEligibleDirectionalAndSpotWhilePointCubeDisabled") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    CacheScope       cache;
    auto*            sun = Light3D::createLight("dir");
    sun->setDirection(-0.4f, 0.9f, 0.2f);
    sun->setColor(1.f, 1.f, 1.f, 2.f);
    sun->setCastShadow(true);
    sun->setShadowMethod("csm");
    auto* local = spot(0.f, 3.f);
    auto* point = Light3D::createLight("point");
    point->setPosition(2.f, 2.f, 2.f);
    point->setRadius(6.f);
    point->setCastShadow(true);
    point->setShadowMethod("cube");
    std::vector<Light3D::Data*> lights{&*sun->data(), &*local->data(), &*point->data()};
    const std::vector<bool>     isPoint{false, true, true};
    ShadowSchemeSettings        settings     = ShadowSchemeSettings::current();
    settings.enablePointCube                 = false;
    settings.maxSpotShadowCasters            = 2;
    settings.enableLocalPaging               = false;
    Light3D::Data*               directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, ShadowPagingView{}, directional, locals);
    CHECK_EQ(directional, &*sun->data());
    REQUIRE_EQ(locals.size(), 1u);
    CHECK_EQ(locals[0].light, &*local->data());
    CHECK_EQ(locals[0].layer, ShadowConfig::kCascades);
    CHECK(locals[0].needsUpdate);
    CHECK_EQ(point->data()->shadowLocalSlot, -1);
    const glm::mat4 vp = buildSpotShadowVP(glm::vec3(0.f, 4.f, 0.f), glm::vec3(0.f, -1.f, 0.f), 12.f, 35.f);
    CHECK_GT((vp * glm::vec4(0.f, 0.f, 0.f, 1.f)).w, 0.f);
    CHECK(buildPointShadowFaceVP(glm::vec3(0.f), 0, 8.f).ok());
    CHECK(!buildPointShadowFaceVP(glm::vec3(0.f), 6, 8.f).ok());
}

TEST_CASE("graphics.shadowScheme.pagesOnlyConfiguredSpotBudgetByImportance") {
    ecs::Table           table;
    ecs::ScopedTable     scope(table);
    CacheScope           cache;
    ShadowSchemeSettings settings  = ShadowSchemeSettings::current();
    settings.enableDirectionalCsm  = false;
    settings.enableSpotPerspective = true;
    settings.maxSpotShadowCasters  = 2;
    settings.enableLocalPaging     = true;
    settings.hysteresisBonus       = 0.f;
    auto*                        a = spot(0.f, 10.f);
    auto*                        b = spot(1.f, 8.f);
    auto*                        c = spot(2.f, 6.f);
    auto*                        d = spot(3.f, 4.f);
    auto*                        e = spot(4.f, 2.f);
    std::vector<Light3D::Data*>  lights{&*a->data(), &*b->data(), &*c->data(), &*d->data(), &*e->data()};
    std::vector<bool>            isPoint(5u, true);
    Light3D::Data*               directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, originView(), directional, locals);
    REQUIRE_EQ(locals.size(), 2u);
    CHECK_EQ(locals[0].light, &*a->data());
    CHECK_EQ(locals[1].light, &*b->data());
    CHECK_EQ(a->data()->shadowLocalSlot, 0);
    CHECK_EQ(b->data()->shadowLocalSlot, 1);
    CHECK_EQ(c->data()->shadowLocalSlot, -1);
}

TEST_CASE("graphics.shadowScheme.hysteresisRetainsResidentAndUpdateBudgetSkipsStableSlots") {
    SUB_CASE("sticky bonus retains resident against modestly brighter challenger") {
        ecs::Table           table;
        ecs::ScopedTable     scope(table);
        CacheScope           cache;
        ShadowSchemeSettings settings         = ShadowSchemeSettings::current();
        settings.maxSpotShadowCasters         = 1;
        settings.enableLocalPaging            = false;
        settings.hysteresisBonus              = 0.5f;
        auto*                        resident = spot(0.f, 5.f);
        std::vector<Light3D::Data*>  lights{&*resident->data()};
        std::vector<bool>            isPoint{true};
        Light3D::Data*               directional = nullptr;
        std::vector<LocalShadowSlot> locals;
        selectShadowCasters(lights, isPoint, settings, ShadowPagingView{}, directional, locals);
        REQUIRE_EQ(locals.size(), 1u);
        auto* challenger = spot(1.f, 6.f);
        lights.push_back(&*challenger->data());
        isPoint.push_back(true);
        locals.clear();
        selectShadowCasters(lights, isPoint, settings, ShadowPagingView{}, directional, locals);
        REQUIRE_EQ(locals.size(), 1u);
        CHECK_EQ(locals[0].light, &*resident->data());
    };
    SUB_CASE("one redraw budget updates one new layer then none until light moves") {
        ecs::Table           table;
        ecs::ScopedTable     scope(table);
        CacheScope           cache;
        ShadowSchemeSettings settings    = ShadowSchemeSettings::current();
        settings.maxSpotShadowCasters    = 2;
        settings.enableLocalPaging       = true;
        settings.maxLocalUpdatesPerFrame = 1;
        auto*                        a   = spot(0.f, 10.f);
        auto*                        b   = spot(1.f, 9.f);
        std::vector<Light3D::Data*>  lights{&*a->data(), &*b->data()};
        const std::vector<bool>      isPoint{true, true};
        Light3D::Data*               directional = nullptr;
        std::vector<LocalShadowSlot> locals;
        const auto                   countUpdates = [&] {
            int count = 0;
            for (const auto& slot : locals)
                if (slot.needsUpdate) ++count;
            return count;
        };
        selectShadowCasters(lights, isPoint, settings, originView(), directional, locals);
        REQUIRE_EQ(locals.size(), 2u);
        CHECK_EQ(countUpdates(), 1);
        locals.clear();
        selectShadowCasters(lights, isPoint, settings, originView(), directional, locals);
        CHECK_EQ(countUpdates(), 0);
        a->setPosition(0.2f, 2.f, 0.f);
        locals.clear();
        selectShadowCasters(lights, isPoint, settings, originView(), directional, locals);
        CHECK_EQ(countUpdates(), 1);
    };
}

TEST_CASE("graphics.light3d.spotMethodSetterClampsAngleAndRetainsLastValidMethod") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            light = Light3D::createLight("spot");
    REQUIRE(light != nullptr);
    CHECK_EQ(light->getType(), std::string("spot"));
    light->setSpotAngle(40.f);
    light->setSpotSoftness(0.5f);
    CHECK_EQ(light->getSpotAngle(), 40.f);
    CHECK_EQ(light->getSpotSoftness(), 0.5f);
    light->setShadowMethod("perspective");
    CHECK_EQ(light->getShadowMethod(), std::string("perspective"));
    light->setShadowMethod("auto");
    light->setShadowMethod("not-a-method");
    CHECK_EQ(light->getShadowMethod(), std::string("auto"));
}
}  // namespace test_shadow_scheme
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkyAtmosphereLuts)
#include "graphics/sky/SkyAtmosphereLuts.h"
#include "graphics/sky/SkyAtmospherePass.h"
#include "graphics/sky/SkyOpticalCycle.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace test_sky_atmosphere_luts {
using namespace eve::graphics;

TEST_CASE("graphics.skyLuts.vacuumTransmitsAllLightAndGeneratesNoScatteredEnergy") {
    SkyAtmosphereParameters parameters;
    parameters.rayleigh = parameters.mieScattering = parameters.mieAbsorption = parameters.ozoneAbsorption =
        parameters.groundAlbedo                                               = {0.f, 0.f, 0.f};
    auto baked = detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(baked.ok());
    const auto& tables = baked.value();
    REQUIRE_EQ(tables.transmittance.size(), 256u * 64u * 4u);
    REQUIRE_EQ(tables.multiScattering.size(), 32u * 32u * 4u);
    CHECK(std::all_of(tables.transmittance.begin(), tables.transmittance.end(),
                      [](float value) { return value == 1.f; }));
    bool scattered = false;
    for (std::size_t i = 0; i < tables.multiScattering.size(); ++i)
        if (i % 4u != 3u && tables.multiScattering[i] != 0.f) scattered = true;
    CHECK(!scattered);
    auto ambient = detail::bakeDistantSkyAmbient(parameters, tables, {0.f, 1.f, 0.f});
    REQUIRE(ambient.ok());
    for (float channel : ambient.value()) CHECK_EQ(channel, 0.f);
}

TEST_CASE("graphics.skyLuts.multiScatteringAndSolarElevationChangeAmbientWithoutChangingTransmission") {
    SkyAtmosphereParameters parameters;
    auto                    enabled = detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(enabled.ok());
    parameters.multiScatteringFactor = 0.f;
    auto disabled                    = detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(disabled.ok());
    CHECK_EQ(enabled.value().transmittance, disabled.value().transmittance);
    double scatteringEnergy = 0.0;
    for (std::size_t i = 0; i < enabled.value().multiScattering.size(); ++i) {
        if (i % 4u == 3u) continue;
        const float value = enabled.value().multiScattering[i];
        CHECK(std::isfinite(value));
        CHECK_GE(value, 0.f);
        CHECK_EQ(disabled.value().multiScattering[i], 0.f);
        scatteringEnergy += value;
    }
    CHECK_GT(scatteringEnergy, 0.0);
    parameters.multiScatteringFactor = 1.f;
    auto day                         = detail::bakeDistantSkyAmbient(parameters, enabled.value(), {0.f, 1.f, 0.f});
    auto night                       = detail::bakeDistantSkyAmbient(parameters, enabled.value(), {0.f, -1.f, 0.f});
    REQUIRE(day.ok());
    REQUIRE(night.ok());
    for (std::size_t axis = 0; axis < 3u; ++axis) {
        CHECK_GT(day.value()[axis], night.value()[axis]);
        CHECK_GE(night.value()[axis], 0.f);
    }
    CHECK(!detail::bakeDistantSkyAmbient(parameters, enabled.value(), {0.f, 0.f, 0.f}).ok());
    auto malformed = enabled.value();
    malformed.multiScattering.pop_back();
    CHECK(!detail::bakeDistantSkyAmbient(parameters, malformed, {0.f, 1.f, 0.f}).ok());
    parameters.groundAlbedo[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!detail::bakeSkyAtmosphereLuts(parameters).ok());
}

TEST_CASE("graphics.skyOpticalCycle.noonDawnNightCoefficientsAndOneLutMidpointAreBounded") {
    SkyDaylightLayer        daylight;
    SkyAtmosphereParameters base;
    const auto              noon  = detail::opticalSample(base, daylight, 1.f);
    const auto              dawn  = detail::opticalSample(base, daylight, 0.f);
    const auto              night = detail::opticalSample(base, daylight, -1.f);
    CHECK_LT(std::fabs(noon.rayleigh[0] - 0.00674508f), 1e-7f);
    CHECK_LT(std::fabs(dawn.rayleigh[2] - 0.03366377f), 1e-7f);
    CHECK_LT(std::fabs(night.rayleigh[2] - 0.024375f), 1e-7f);
    CHECK_LT(std::fabs(noon.ozoneAbsorption[0] - 0.001794476f), 1e-8f);
    CHECK_LT(std::fabs(dawn.ozoneAbsorption[1] - 0.002927907f), 1e-8f);
    CHECK_LT(std::fabs(night.ozoneAbsorption[1] - 0.003396696f), 1e-8f);
    constexpr float elevation = 0.0025f;
    auto            low    = detail::bakeSkyAtmosphereLuts(detail::opticalSample(base, daylight, elevation - 0.0025f));
    auto            high   = detail::bakeSkyAtmosphereLuts(detail::opticalSample(base, daylight, elevation + 0.0025f));
    auto            center = detail::bakeSkyAtmosphereLuts(detail::opticalSample(base, daylight, elevation));
    REQUIRE(low.ok());
    REQUIRE(high.ok());
    REQUIRE(center.ok());
    const auto bounded = [](const auto& a, const auto& b, const auto& mid) {
        for (std::size_t i = 0; i < mid.size(); ++i) {
            if (i % 4u == 3u) continue;
            const int  mantissaBits = i % 4u == 2u ? 5 : 6;
            const auto halfUlp      = [mantissaBits](float value) {
                int         exponent = 0;
                const float fraction = std::frexp(value, &exponent);
                if (fraction == 0.f) exponent = -13;
                return std::ldexp(1.f, std::max(-14, exponent - 1) - mantissaBits - 1);
            };
            const float storageTolerance = 0.5f * (halfUlp(a[i]) + halfUlp(b[i])) + halfUlp(mid[i]);
            if (std::fabs((a[i] + b[i]) * 0.5f - mid[i]) >= 0.002f + storageTolerance) return false;
        }
        return true;
    };
    CHECK(bounded(low.value().transmittance, high.value().transmittance, center.value().transmittance));
    CHECK(bounded(low.value().multiScattering, high.value().multiScattering, center.value().multiScattering));
}
}  // namespace test_sky_atmosphere_luts
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Volumetric)
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "zeroerr/unittest.h"

namespace test_volumetric_flags {
using namespace eve::graphics;

TEST_CASE("graphics.volumetric.drawableOcclusionDefaultsOnAndCanBeDisabledPerObject") {
    Texture texture;
    Mesh    mesh;
    CHECK(texture.getCastOcclusion());
    CHECK(mesh.getCastOcclusion());
    texture.setCastOcclusion(false);
    mesh.setCastOcclusion(false);
    CHECK(!texture.getCastOcclusion());
    CHECK(!mesh.getCastOcclusion());

    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            two   = Renderable2D::create();
    auto*            three = Renderable3D::create();
    REQUIRE(two != nullptr);
    REQUIRE(three != nullptr);
    CHECK(two->getCastOcclusion());
    CHECK(three->getCastOcclusion());
    two->setCastOcclusion(false);
    three->setCastOcclusion(false);
    CHECK(!two->getCastOcclusion());
    CHECK(!three->getCastOcclusion());
    ecs::DestroyEntity(two);
    ecs::DestroyEntity(three);
}

TEST_CASE("graphics.volumetric.lightControlsAndEmissiveProxiesStayVolumeOnly") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            two   = Light2D::createLight("point");
    auto*            three = Light3D::createLight("dir");
    REQUIRE(two != nullptr);
    REQUIRE(three != nullptr);
    CHECK(!two->getVolumetric());
    CHECK(!three->getVolumetric());
    two->setVolumetric(true);
    two->setVolumetricIntensity(0.75f);
    three->setVolumetric(true);
    CHECK(two->getVolumetric());
    CHECK(three->getVolumetric());
    CHECK_EQ(two->getVolumetricIntensity(), 0.75f);
    two->setVolumetricOnly(true);
    CHECK(two->getVolumetricOnly());
    CHECK(!three->getVolumetricOnly());
    ecs::DestroyEntity(two);
    ecs::DestroyEntity(three);

    auto* glow2 = Light2D::createEmissiveProxy(40.f, 30.f, 1.f, 0.4f, 0.1f, 2.f, 80.f);
    auto* glow3 = Light3D::createEmissiveProxy(1.f, 2.f, 3.f, 0.2f, 0.8f, 1.f, 4.f, 6.f);
    REQUIRE(glow2 != nullptr);
    REQUIRE(glow3 != nullptr);
    CHECK(glow2->getVolumetric());
    CHECK(glow2->getVolumetricOnly());
    CHECK_EQ(glow2->getX(), 40.f);
    CHECK(glow3->getVolumetric());
    CHECK(glow3->getVolumetricOnly());
    CHECK(!glow3->getCastShadow());
    CHECK_EQ(glow3->getRadius(), 6.f);
    ecs::DestroyEntity(glow2);
    ecs::DestroyEntity(glow3);
}
}  // namespace test_volumetric_flags
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_atmosphere_volume)
#include "graphics/AtmosphereClipmap.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/FogVolume.h"
#include "graphics/SparseVolumeTexture.h"
#include "graphics/VolumeDensityGraph.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_atmosphere_volume {
using namespace eve::graphics;

TEST_CASE("graphics.atmosphereVolume.logSlicesRoundTripAndUniformExtinctionObeysBeerLambert") {
    SUB_CASE("slice index converts through logarithmic physical distance") {
        AtmosphereVolume volume;
        volume.resize(4, 3, 32);
        volume.setDepthRange(0.1f, 1000.f);
        for (int z = 0; z < volume.getDepth(); ++z) REQUIRE_EQ(volume.sliceForDistance(volume.sliceDistance(z)), z);
    };
    SUB_CASE("constant extinction integrates to exponential transmittance") {
        AtmosphereVolume volume;
        volume.resize(1, 1, 64);
        volume.setDepthRange(1.f, 101.f);
        for (int z = 0; z < volume.getDepth(); ++z) {
            volume.at(0, 0, z).extinction = 0.02f;
            volume.at(0, 0, z).scattering = glm::vec3(0.02f);
        }
        volume.integrate(glm::vec3(1.f));
        const float expected = std::exp(-0.02f * (volume.sliceDistance(63) - 1.f));
        CHECK_LT(std::fabs(volume.integratedAt(0, 0, 63).a - expected), 1e-5f);
    };
}

TEST_CASE("graphics.atmosphereVolume.heightFogUsesWorldHeightAndLocalMediaCanCarve") {
    SUB_CASE("ordinary height fog fades upward") {
        AtmosphereVolume volume;
        volume.resize(2, 8, 4);
        volume.injectHeightFog(0.2f, glm::vec3(0.8f), 0.f, 0.5f, 0.f, 8.f);
        REQUIRE_GT(volume.at(0, 0, 0).extinction, volume.at(0, 7, 0).extinction);
    };
    SUB_CASE("frustum fog uses reconstructed world height rather than screen row") {
        AtmosphereVolume volume;
        volume.resize(1, 2, 4);
        volume.setDepthRange(0.1f, 1.f);
        glm::mat4 inverse(1.f);
        inverse[1] = glm::vec4(0.f, 0.f, 1.f, 0.f);
        inverse[2] = glm::vec4(0.f, 1.f, 0.f, 0.f);
        volume.injectHeightFogFrustum(0.2f, glm::vec3(0.8f), 0.f, 2.f, -2.f, 2.f, inverse);
        CHECK_LT(std::fabs(volume.at(0, 0, 0).extinction - volume.at(0, 1, 0).extinction), 1e-6f);
        CHECK_GT(volume.at(0, 0, 0).extinction, volume.at(0, 0, 3).extinction);
    };
    SUB_CASE("additive sphere raises local density and negative sphere carves it") {
        AtmosphereVolume volume;
        volume.resize(8, 8, 8);
        volume.injectHeightFog(0.1f, glm::vec3(1.f), 0.f, 0.f, -4.f, 4.f);
        FogVolume sphere;
        sphere.setShape("sphere");
        sphere.setSize(4.f, 4.f, 4.f);
        sphere.setExtinction(0.2f);
        sphere.setEdgeFalloff(0.f);
        volume.injectLocalVolume(sphere, glm::vec3(-4.f), glm::vec3(4.f));
        CHECK_GT(volume.at(4, 4, 4).extinction, volume.at(0, 0, 0).extinction);
        sphere.setExtinction(-1.f);
        volume.injectLocalVolume(sphere, glm::vec3(-4.f), glm::vec3(4.f));
        CHECK_EQ(volume.at(4, 4, 4).extinction, 0.f);
        CHECK_GT(volume.at(0, 0, 0).extinction, 0.f);
    };
}

TEST_CASE("graphics.atmosphereVolume.visibilityHistoryAndLocalLightsAffectIntegratedScattering") {
    SUB_CASE("shadow visibility lowers scattering without altering media") {
        AtmosphereVolume lit;
        lit.resize(1, 1, 8);
        lit.setDepthRange(1.f, 20.f);
        for (int z = 0; z < 8; ++z) {
            lit.at(0, 0, z).extinction = 0.1f;
            lit.at(0, 0, z).scattering = glm::vec3(0.08f);
        }
        AtmosphereVolume shadowed = lit;
        for (int z = 0; z < 8; ++z) shadowed.setLightVisibility(0, 0, z, 0.f);
        lit.integrate(glm::vec3(1.f));
        shadowed.integrate(glm::vec3(1.f));
        REQUIRE_GT(lit.integratedAt(0, 0, 7).r, shadowed.integratedAt(0, 0, 7).r);
    };
    SUB_CASE("lighting discontinuity rejects stale history") {
        AtmosphereVolume current;
        current.resize(2, 1, 2);
        current.setDepthRange(1.f, 4.f);
        for (int z = 0; z < 2; ++z) {
            current.at(0, 0, z).emissive = glm::vec3(2.f);
            current.at(1, 0, z).emissive = glm::vec3(0.2f);
        }
        current.integrate(glm::vec3(0.f));
        AtmosphereVolume history = current;
        history.clear();
        history.integrate(glm::vec3(0.f));
        CHECK_EQ(current.blendHistory(history, 0.9f, 0.2f), 4);
        CHECK_GT(current.integratedAt(0, 0, 1).r, 0.f);
    };
    SUB_CASE("local red light makes distant accumulation redder and less transmissive") {
        AtmosphereVolume volume;
        volume.resize(4, 4, 16);
        volume.setDepthRange(0.5f, 30.f);
        volume.injectHeightFog(0.04f, glm::vec3(0.8f), 0.f, 0.f, -4.f, 4.f);
        VolumetricLight light;
        light.position = glm::vec3(0.f);
        light.radius   = 6.f;
        light.color    = glm::vec3(1.f, 0.2f, 0.1f);
        volume.integrateLocalLights({light}, glm::vec3(-8.f), glm::vec3(8.f), glm::vec3(0.02f));
        const glm::vec4 nearFog = volume.sampleIntegrated(0.5f, 0.5f, 1.f);
        const glm::vec4 farFog  = volume.sampleIntegrated(0.5f, 0.5f, 20.f);
        CHECK_LT(farFog.a, nearFog.a);
        CHECK_GT(farFog.r, farFog.b);
    };
}

TEST_CASE("graphics.atmosphereClipmap.centerSnapsAndFinestContainingLevelWins") {
    AtmosphereClipmap clipmap;
    clipmap.configure(3, 8, 8.f);
    clipmap.updateCenter(glm::vec3(1.1f, 0.f, 0.f));
    const glm::vec3 first = clipmap.getLevel(0).center;
    clipmap.updateCenter(glm::vec3(1.9f, 0.f, 0.f));
    REQUIRE_EQ(clipmap.getLevel(0).center, first);
    CHECK_EQ(clipmap.levelForPosition(glm::vec3(4.f, 0.f, 0.f)), 0);
    CHECK_EQ(clipmap.levelForPosition(glm::vec3(12.f, 0.f, 0.f)), 1);
    CHECK_EQ(clipmap.levelForPosition(glm::vec3(28.f, 0.f, 0.f)), 2);
    CHECK_EQ(clipmap.levelForPosition(glm::vec3(80.f, 0.f, 0.f)), -1);
}

TEST_CASE("graphics.fogVolume.seededNoiseIsRepeatableAndDisabledNoiseIsUniform") {
    FogVolume volume;
    volume.setShape("box");
    volume.setSize(20.f, 20.f, 20.f);
    volume.setExtinction(1.f);
    volume.setEdgeFalloff(0.f);
    volume.setNoise(1.f, 0.7f, 42);
    const glm::vec3 point(0.25f, 0.5f, 0.75f);
    const float     first = volume.sampleExtinction(point);
    REQUIRE_EQ(volume.sampleExtinction(point), first);
    CHECK_GT(std::fabs(first - volume.sampleExtinction(glm::vec3(1.75f, 0.5f, 0.75f))), 1e-4f);
    volume.setNoise(0.f, 0.7f, 42);
    CHECK_LT(std::fabs(volume.sampleExtinction(point) - 1.f), 1e-6f);
}

TEST_CASE("graphics.volumeDensityGraph.sphereNoiseAndSparseBakeProduceOccupiedBricksOnly") {
    VolumeDensityGraph graph;
    const int          sphere = graph.addNode(VolumeDensityGraph::Op::sphere, -1, -1, glm::vec4(0.f, 0.f, 0.f, 4.f));
    const int          noise  = graph.addNode(VolumeDensityGraph::Op::noise, -1, -1, glm::vec4(2.f, 0.2f, 0.f, 7.f));
    graph.setOutput(graph.addNode(VolumeDensityGraph::Op::multiply, sphere, noise));
    REQUIRE_GT(graph.evaluate(glm::vec3(0.f), 1.f), 0.f);
    CHECK_EQ(graph.evaluate(glm::vec3(10.f), 1.f), 0.f);
    VolumeDensityGraph sparseGraph;
    sparseGraph.addNode(VolumeDensityGraph::Op::sphere, -1, -1, glm::vec4(0.f, 0.f, 0.f, 2.f));
    SparseVolumeTexture sparse;
    sparse.resize(32, 32, 32, 8);
    sparse.bake(sparseGraph, glm::vec3(-16.f), glm::vec3(16.f), 0.2f, glm::vec3(0.7f));
    CHECK_GT(sparse.getBrickCount(), 0);
    CHECK_LT(sparse.getBrickCount(), 64);
    CHECK_EQ(sparse.sample(0.f, 0.f, 0.f).extinction, 0.f);
    CHECK_GT(sparse.sample(0.5f, 0.5f, 0.5f).extinction, 0.f);
    AtmosphereVolume volume;
    volume.resize(16, 16, 16);
    volume.injectSparseVolume(sparse);
    // Cell centers map to sparse voxels 15 and 17: (-.5,-.5,-.5) lies
    // inside radius 2, while (1.5,1.5,1.5) lies outside it.
    CHECK_GT(volume.at(7, 7, 7).extinction, 0.f);
    CHECK_EQ(volume.at(8, 8, 8).extinction, 0.f);
    CHECK_EQ(volume.at(0, 0, 0).extinction, 0.f);
}
}  // namespace test_atmosphere_volume
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_depth_of_field_focus)
#include "graphics/DepthOfFieldFocus.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_depth_of_field_focus {
using namespace eve::graphics;

TEST_CASE("graphics.depthOfFieldFocus.screenHitPlayerAndFixedOffsetTrackByExplicitDt") {
    DepthOfFieldFocusState    state;
    DepthOfFieldFocusOutput   output;
    DepthOfFieldFocusSettings settings;
    DepthOfFieldFocusInput    input;
    input.hasRayHit      = true;
    input.rayHitDistance = 20.f;
    input.deltaSeconds   = 0.1f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &output, &settings, &input).ok());
    CHECK(output.active);
    CHECK_EQ(output.focusDistance, 72.f);
    CHECK_EQ(output.aperture, 7.5f);
    CHECK_EQ(output.focalLength, 30.f);
    input.hitPlayer = true;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &output, &settings, &input).ok());
    CHECK_EQ(output.focusDistance, 81.8f);
    settings.tracking    = int(DepthOfFieldTracking::FixedOffset);
    settings.focusOffset = 12.f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &output, &settings, &input).ok());
    CHECK_EQ(output.focusDistance, 12.f);
}

TEST_CASE("graphics.depthOfFieldFocus.followTargetAddsOffsetAndClampsResponseStep") {
    DepthOfFieldFocusState    state;
    DepthOfFieldFocusOutput   output;
    DepthOfFieldFocusSettings settings;
    DepthOfFieldFocusInput    input;
    settings.tracking    = int(DepthOfFieldTracking::FollowTarget);
    settings.focusOffset = 2.f;
    settings.response    = 10.f;
    input.hasTarget      = true;
    input.targetDistance = 8.f;
    input.deltaSeconds   = 1.f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &output, &settings, &input).ok());
    CHECK_EQ(output.focusDistance, 10.f);
    CHECK(!state.maximumExceeded);
}

TEST_CASE("graphics.depthOfFieldFocus.invalidInputAndNullObjectPreserveStateAndOutput") {
    DepthOfFieldFocusState    state;
    DepthOfFieldFocusOutput   output;
    DepthOfFieldFocusSettings settings;
    DepthOfFieldFocusInput    input;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &output, &settings, &input).ok());
    const auto beforeState   = state;
    const auto beforeOutput  = output;
    settings.maximumDistance = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateDepthOfFieldFocus(&state, &output, &settings, &input).ok());
    settings.maximumDistance = 100.f;
    CHECK(!evaluateDepthOfFieldFocus(nullptr, &output, &settings, &input).ok());
    CHECK_EQ(state.focusDistance, beforeState.focusDistance);
    CHECK_EQ(state.initialized, beforeState.initialized);
    CHECK_EQ(output.focusDistance, beforeOutput.focusDistance);
    CHECK_EQ(output.active, beforeOutput.active);
}
}  // namespace test_depth_of_field_focus
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_diffuse_light_probe)
#include "common/ProcgenProbeSink.h"
#include "graphics/DiffuseLightProbeRegistry.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace test_diffuse_light_probe {
using namespace eve;
using namespace eve::graphics;

TEST_CASE("graphics.lightProbe.batchReplacementBlendsOverlappingIrradianceAndRemovalClearsSamples") {
    ProcgenProbeDesc red;
    red.sourcePointId = 1;
    red.type          = 1;
    red.x             = -1.f;
    red.extentX = red.extentY = red.extentZ = 4.f;
    red.irradianceR                         = 1.f;
    red.irradianceG                         = 0.f;
    red.irradianceB                         = 0.f;
    ProcgenProbeDesc blue                   = red;
    blue.sourcePointId                      = 2;
    blue.x                                  = 1.f;
    blue.irradianceR                        = 0.f;
    blue.irradianceB                        = 1.f;
    auto& registry                          = DiffuseLightProbeRegistry::instance();
    registry.replaceBatch("light-probe-contract", {red, blue});
    auto sample = registry.sample(glm::vec3(0.f), 4);
    REQUIRE(sample.ok());
    const glm::vec3 rgb = glm::vec3(sample.value().coefficients[0]) * 0.2820947918f;
    CHECK_GT(rgb.r, 0.49f);
    CHECK_LT(rgb.r, 0.51f);
    CHECK_GT(rgb.b, 0.49f);
    CHECK_LT(rgb.b, 0.51f);
    CHECK(!registry.sample(glm::vec3(20.f), 4).ok());
    registry.removeBatch("light-probe-contract");
    CHECK(!registry.sample(glm::vec3(0.f), 4).ok());
}

TEST_CASE("graphics.lightProbe.volumeSelectsEightBracketingCornersOrSparseFallback") {
    auto& registry = DiffuseLightProbeRegistry::instance();
    SUB_CASE("regular grid selects the containing trilinear cell") {
        std::vector<ProcgenProbeDesc> probes;
        std::uint64_t                 id = 100;
        for (float z : {-1.f, 1.f})
            for (float y : {-1.f, 1.f})
                for (float x : {-2.f, 0.f, 2.f}) {
                    ProcgenProbeDesc probe;
                    probe.sourcePointId = id++;
                    probe.type          = 1;
                    probe.x             = x;
                    probe.y             = y;
                    probe.z             = z;
                    probe.extentX = probe.extentY = probe.extentZ = 2.f;
                    probes.push_back(probe);
                }
        registry.replaceBatch("probe-grid-contract", probes);
        auto selected = registry.selectVolume(glm::vec3(0.5f, 0.25f, -0.25f));
        REQUIRE(selected.ok());
        CHECK(selected.value().trilinearCell);
        CHECK_EQ(selected.value().count, 8);
        for (int i = 0; i < selected.value().count; ++i) {
            const glm::vec3 position(selected.value().positions[std::size_t(i)]);
            const bool      xIsCorner = position.x == 0.f || position.x == 2.f;
            CHECK(xIsCorner);
            const bool yIsCorner = position.y == -1.f || position.y == 1.f;
            CHECK(yIsCorner);
            const bool zIsCorner = position.z == -1.f || position.z == 1.f;
            CHECK(zIsCorner);
        }
        registry.removeBatch("probe-grid-contract");
    };
    SUB_CASE("missing corner falls back to present sparse probes") {
        std::vector<ProcgenProbeDesc> probes;
        std::uint64_t                 id = 200;
        for (const glm::vec3 p : {glm::vec3(-1.f, -1.f, 0.f), glm::vec3(1.f, -1.f, 0.f), glm::vec3(-1.f, 1.f, 0.f)}) {
            ProcgenProbeDesc probe;
            probe.sourcePointId = id++;
            probe.type          = 1;
            probe.x             = p.x;
            probe.y             = p.y;
            probe.z             = p.z;
            probe.extentX = probe.extentY = probe.extentZ = 2.f;
            probes.push_back(probe);
        }
        registry.replaceBatch("probe-sparse-contract", probes);
        auto selected = registry.selectVolume(glm::vec3(0.f));
        REQUIRE(selected.ok());
        CHECK(!selected.value().trilinearCell);
        CHECK_EQ(selected.value().count, 3);
        registry.removeBatch("probe-sparse-contract");
    };
}

TEST_CASE("graphics.lightProbe.renderableOwnsCustomCoefficientsAndRejectsInvalidWrites") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            renderable = Renderable3D::create();
    REQUIRE(renderable != nullptr);
    CHECK(!renderable->setCustomLightProbe(-1.f, 0.f, 0.f).ok());
    CHECK(!renderable->setCustomLightProbe(std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    REQUIRE(renderable->setCustomLightProbe(0.25f, 0.5f, 0.75f).ok());
    CHECK(renderable->meshRenderer()->customLightProbe);
    CHECK_LT(std::fabs(renderable->meshRenderer()->customLightProbeSh[0].x * 0.2820947918f - 0.25f), 1e-5f);
    REQUIRE(renderable->setCustomLightProbeCoefficient(3, 0.5f, -0.25f, 0.1f).ok());
    CHECK_EQ(renderable->meshRenderer()->customLightProbeSh[3].x, 0.5f);
    CHECK_EQ(renderable->meshRenderer()->customLightProbeSh[3].y, -0.25f);
    CHECK_EQ(renderable->meshRenderer()->customLightProbeSh[3].z, 0.1f);
    CHECK(!renderable->setCustomLightProbeCoefficient(9, 0.f, 0.f, 0.f).ok());
    renderable->clearCustomLightProbe();
    CHECK(!renderable->meshRenderer()->customLightProbe);
    ecs::DestroyEntity(renderable);
}
}  // namespace test_diffuse_light_probe
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_camera_factory)
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/unittest.h"

namespace test_graphics_camera_factory {
template <class Camera>
std::size_t countIn(ecs::Table& table) {
    std::size_t count = 0;
    for (auto&& entity : ecs::View<Camera, typename Camera::Data>(table)) {
        (void)entity;
        ++count;
    }
    return count;
}

TEST_CASE("graphics.cameraFactory.usesCurrentCallerTableAndDestroysOnlyOwnedEntities") {
    ecs::Table               first;
    ecs::Table               second;
    eve::graphics::Camera2D* two   = nullptr;
    eve::graphics::Camera3D* three = nullptr;
    {
        ecs::ScopedTable scope(first);
        two   = eve::graphics::Camera2D::createCamera();
        three = eve::graphics::Camera3D::createCamera();
        REQUIRE(two != nullptr);
        REQUIRE(three != nullptr);
        CHECK_EQ(two->data()->entity, two);
        CHECK_EQ(three->data()->entity, three);
        CHECK(two->data()->active);
        CHECK(three->data()->active);
        CHECK_EQ(countIn<eve::graphics::Camera2D>(first), 1u);
        CHECK_EQ(countIn<eve::graphics::Camera3D>(first), 1u);
    }
    {
        ecs::ScopedTable scope(second);
        CHECK_EQ(countIn<eve::graphics::Camera2D>(second), 0u);
        CHECK_EQ(countIn<eve::graphics::Camera3D>(second), 0u);
    }
    {
        ecs::ScopedTable scope(first);
        ecs::DestroyEntity(two);
        ecs::DestroyEntity(three);
        CHECK_EQ(countIn<eve::graphics::Camera2D>(first), 0u);
        CHECK_EQ(countIn<eve::graphics::Camera3D>(first), 0u);
    }
}
}  // namespace test_graphics_camera_factory
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_display_output)
#include "graphics/DisplayOutputEncoding.h"
#include "zeroerr/unittest.h"

#include <cmath>

namespace test_graphics_display_output {
using namespace eve::graphics;
using namespace eve::graphics::display;

TEST_CASE("graphics.displayOutput.calibrationPackRoundTripsQuantizedNitsAndClampsBounds") {
    float paper = 0.f, peak = 0.f;
    unpackNits(packNits(200.f, 1000.f), paper, peak);
    REQUIRE_LT(std::fabs(paper - 200.f), 0.15f);
    CHECK_LT(std::fabs(peak - 1000.f), 1.f);
    unpackNits(packNits(1.f, 50.f), paper, peak);
    CHECK_EQ(paper, 80.f);
    CHECK_EQ(peak, 200.f);
    unpackNits(packNits(500.f, 20000.f), paper, peak);
    CHECK_EQ(paper, 400.f);
    CHECK_EQ(peak, 10000.f);
}

TEST_CASE("graphics.displayOutput.hdr10AndScRgbEncodePaperWhiteWithHeadroom") {
    SUB_CASE("PQ black stays black and 200 nit white becomes a mid code") {
        float r = 1.f, g = 1.f, b = 1.f;
        encodeHdr10(r, g, b, 200.f, 1000.f);
        REQUIRE_GT(r, 0.4f);
        CHECK_LT(r, 0.7f);
        CHECK_LT(std::fabs(r - g), 1e-4f);
        CHECK_LT(std::fabs(g - b), 1e-4f);
        // The PQ float offset still quantizes to HDR10 black code zero.
        CHECK_EQ(std::lround(linearToPq(0.f) * 1023.f), 0L);
        CHECK_GT(linearToPq(1.f), 0.9f);
    };
    SUB_CASE("scRGB maps paper white to 200 over 80 and peak remains representable") {
        float r = 1.f, g = 1.f, b = 1.f;
        encodeScRgb(r, g, b, 200.f, 1000.f);
        CHECK_LT(std::fabs(r - 2.5f), 1e-4f);
        CHECK_EQ(r, g);
        CHECK_EQ(g, b);
        r = g = b = 5.f;
        encodeScRgb(r, g, b, 200.f, 1000.f);
        CHECK_LT(std::fabs(r - 12.5f), 1e-3f);
    };
    SUB_CASE("ACES keeps linear HDR brighter than SDR but within peak ratio") {
        float r = 8.f, g = 8.f, b = 8.f;
        acesToDisplayLinear(r, g, b, 200.f, 1000.f);
        CHECK_GT(r, 1.f);
        CHECK_LE(r, 5.001f);
    };
}

TEST_CASE("graphics.displayOutput.resolveTintsEncodeActiveAndComposeModes") {
    const Color sdr = sceneResolveTint(true, ActiveColorSpace::Sdr, false, 200.f, 1000.f);
    REQUIRE_GE(sdr.r, 0.5f);
    CHECK_EQ(sdr.g, 0.f);
    CHECK_GE(sdr.a, 65536.f);
    const Color hdr = sceneResolveTint(true, ActiveColorSpace::Hdr10, false, 200.f, 1000.f);
    CHECK_EQ(hdr.g, 2.f);
    CHECK_LT(hdr.a, 1.f);
    const Color ui = uiResolveTint(ActiveColorSpace::ScRgb, 200.f, 1000.f);
    CHECK_LT(ui.r, 0.f);
    CHECK_EQ(ui.g, 1.f);
    const Color compose = sceneComposeTint(true, 200.f, 1000.f);
    CHECK(isComposeLinearMode(compose.g));
    CHECK_LT(compose.a, 1.f);
}
}  // namespace test_graphics_display_output
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_pbr_surface)
#include "graphics/Shader.h"
#include "zeroerr/unittest.h"

#include <cstdint>

namespace test_graphics_mesh_texture_slots {
TEST_CASE("graphics.shader.meshTextureSlotsBorrowPointersAndRejectOutOfRangeAtomically") {
    using namespace eve::graphics;
    Shader   shader;
    Texture* borrowed = reinterpret_cast<Texture*>(std::uintptr_t(16));
    REQUIRE(shader.setMeshTexture(3u, borrowed).ok());
    CHECK_EQ(shader.meshTexture(3u), borrowed);
    CHECK_EQ(shader.meshTexture(0u), nullptr);
    REQUIRE(!shader.setMeshTexture(4u, nullptr).ok());
    CHECK_EQ(shader.meshTexture(3u), borrowed);
    CHECK_EQ(shader.meshTexture(4u), nullptr);
    REQUIRE(shader.setMeshTexture(3u, nullptr).ok());
    CHECK_EQ(shader.meshTexture(3u), nullptr);
}
}  // namespace test_graphics_mesh_texture_slots
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_primitives)
#include "graphics/Batcher.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitivePath.h"
#include "graphics/PrimitiveScene.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/PrimitiveTypes.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace test_graphics_primitives {
using namespace eve::graphics;

SceneDrawContext view3d() {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    return context;
}

TEST_CASE("graphics.primitiveRecorder.pathDistancesAndCapacityAreDeterministicAndAtomic") {
    DashPattern dash{{4.f, 2.f, 1.f, 3.f}, 0.5f, DashSpace::WorldUnits};
    CHECK_EQ(dash.period(), 10.f);
    PrimitivePaint paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{3.f, 2.f}, 0.f, DashSpace::ScreenPixels};
    PrimitiveCanvas2D canvas(1);
    const std::array  path{glm::vec2{0.f, 0.f}, glm::vec2{3.f, 4.f}, glm::vec2{6.f, 4.f}};
    REQUIRE(canvas.tryDrawPolyline(path, false, paint).ok());
    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK_EQ(canvas.commands()[0].cumulativeLengths[1], 5.f);
    CHECK_EQ(canvas.commands()[0].cumulativeLengths[2], 8.f);
    CHECK_EQ(canvas.statistics().segmentCount, 2u);
    CHECK(!canvas.tryDrawPolyline(path, false, paint).ok());
    CHECK_EQ(canvas.commands().size(), 1u);
    CHECK_EQ(canvas.statistics().droppedCommands, 1u);
    ScenePrimitivePaint spatial;
    spatial.mode = PaintMode::Stroke;
    PrimitiveSceneCanvas3D scene(view3d(), 1);
    const std::array       triangle{glm::vec3{0.f, 0.f, -2.f}, glm::vec3{3.f, 0.f, -2.f}, glm::vec3{3.f, 4.f, -2.f}};
    REQUIRE(scene.tryDrawPolyline(triangle, true, spatial).ok());
    CHECK_EQ(scene.commands()[0].cumulativeWorldLengths.back(), 12.f);
    CHECK(!scene.tryDrawPolyline(triangle, true, spatial).ok());
    CHECK_EQ(scene.commands().size(), 1u);
}

TEST_CASE("graphics.primitivePath.flatteningAndFillRulesPreserveContourGeometry") {
    Path2D curve;
    curve.moveTo({0.f, 0.f}).quadTo({5.f, 10.f}, {10.f, 0.f}).cubicTo({12.f, -4.f}, {18.f, -4.f}, {20.f, 0.f}).close();
    const auto coarse = curve.flatten(2.f);
    const auto fine   = curve.flatten(0.05f);
    REQUIRE_EQ(coarse.size(), 1u);
    CHECK(coarse[0].closed);
    CHECK_GT(fine[0].points.size(), coarse[0].points.size());
    CHECK_EQ(fine[0].points.front().x, 0.f);
    CHECK_EQ(fine[0].points.back().x, 20.f);

    Path2D concave;
    concave.moveTo({0.f, 0.f}).lineTo({8.f, 0.f}).lineTo({8.f, 8.f}).lineTo({4.f, 3.f}).lineTo({0.f, 8.f}).close();
    PrimitivePaint fill;
    fill.mode      = PaintMode::FillAndStroke;
    fill.antialias = false;
    PrimitiveCanvas2D canvas;
    canvas.drawPath(concave, fill);
    CHECK_EQ(canvas.triangles().size(), 3u);
    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK(canvas.commands()[0].closed);

    const auto appendSquare = [](Path2D& path, float lo, float hi, bool clockwise) {
        if (clockwise)
            path.moveTo({lo, lo}).lineTo({lo, hi}).lineTo({hi, hi}).lineTo({hi, lo}).close();
        else
            path.moveTo({lo, lo}).lineTo({hi, lo}).lineTo({hi, hi}).lineTo({lo, hi}).close();
    };
    const auto area = [](const PrimitiveCanvas2D& drawing) {
        float sum = 0.f;
        for (const auto& triangle : drawing.triangles()) {
            const glm::vec2 ab = triangle.points[1] - triangle.points[0];
            const glm::vec2 ac = triangle.points[2] - triangle.points[0];
            sum += std::fabs(ab.x * ac.y - ab.y * ac.x) * 0.5f;
        }
        return sum;
    };
    fill.mode = PaintMode::Fill;
    Path2D evenOdd;
    appendSquare(evenOdd, 0.f, 10.f, false);
    appendSquare(evenOdd, 3.f, 7.f, false);
    evenOdd.setFillRule(PathFillRule::EvenOdd);
    PrimitiveCanvas2D hole;
    hole.drawPath(evenOdd, fill);
    CHECK_LT(std::fabs(area(hole) - 84.f), 1e-4f);
    evenOdd.setFillRule(PathFillRule::NonZero);
    PrimitiveCanvas2D solid;
    solid.drawPath(evenOdd, fill);
    CHECK_LT(std::fabs(area(solid) - 100.f), 1e-4f);
}

TEST_CASE("graphics.primitive2d.fillStrokeDashAntialiasAndBlendProduceExpectedTriangles") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode      = PaintMode::Fill;
    paint.antialias = false;
    canvas.drawRect({10.f, 20.f}, {30.f, 40.f}, paint);
    CHECK_EQ(canvas.triangles().size(), 2u);
    CHECK(canvas.commands().empty());
    canvas.reset();
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    const auto solid = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(solid.vertices.size(), 6u);
    CHECK_EQ(solid.statistics.triangleCount, 2u);
    CHECK_LT(std::fabs(std::fabs(solid.vertices[2].clipPosition.y - solid.vertices[0].clipPosition.y) - 0.08f), 1e-5f);
    canvas.reset();
    paint.antialias = true;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    const auto aa = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK_EQ(aa.vertices.size(), 18u);
    CHECK(
        std::any_of(aa.vertices.begin(), aa.vertices.end(), [](const auto& vertex) { return vertex.color.a == 0.f; }));
    canvas.reset();
    paint.antialias   = false;
    paint.stroke.dash = DashPattern{{6.f, 4.f}, 0.f, DashSpace::ScreenPixels};
    const std::array points{glm::vec2{0.f, 10.f}, glm::vec2{5.f, 10.f}, glm::vec2{10.f, 10.f}};
    canvas.drawPolyline(points, false, paint);
    const auto dashed = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK_EQ(dashed.vertices.size(), 12u);
    float furthest = 0.f;
    for (const auto& vertex : dashed.vertices) furthest = std::max(furthest, vertex.pathDistance);
    CHECK_EQ(furthest, 6.f);
    canvas.reset();
    paint.stroke.dash.reset();
    paint.blend = BlendMode::Opaque;
    canvas.drawLine({0.f, 0.f}, {10.f, 0.f}, paint);
    paint.blend = BlendMode::Additive;
    canvas.drawLine({0.f, 2.f}, {10.f, 2.f}, paint);
    const auto ordered = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(ordered.batches2D.size(), 2u);
    CHECK_EQ(ordered.batches2D[0].blend, BlendMode::Opaque);
    CHECK_EQ(ordered.batches2D[1].blend, BlendMode::Additive);
    CHECK_EQ(ordered.batches2D[0].sequence, 0u);
    CHECK_EQ(ordered.batches2D[1].sequence, 1u);
}

TEST_CASE("graphics.primitive2d.radialShapesCapsAndQualityHaveDistinctGeometry") {
    PrimitivePaint paint;
    paint.mode      = PaintMode::FillAndStroke;
    paint.antialias = false;
    PrimitiveCanvas2D canvas;
    canvas.drawCircle({50.f, 50.f}, 20.f, paint, 8);
    REQUIRE_EQ(canvas.triangles().size(), 8u);
    CHECK_EQ(canvas.commands().size(), 1u);
    canvas.drawEllipse({20.f, 20.f}, {10.f, 5.f}, paint, 8);
    CHECK_EQ(canvas.triangles().size(), 16u);
    canvas.drawArc({20.f, 20.f}, {8.f, 8.f}, 0.f, glm::pi<float>(), paint, 4);
    CHECK_EQ(canvas.triangles().size(), 20u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::Low, 0.75f, 0}, 100.f), 16u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::High, 0.75f, 0}, 100.f), 64u);
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    canvas.reset();
    paint.stroke.cap = LineCap::Butt;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(canvas, {100, 100}).vertices.size(), 6u);
    canvas.reset();
    paint.stroke.cap = LineCap::Square;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(canvas, {100, 100}).vertices.size(), 18u);
    canvas.reset();
    paint.stroke.cap = LineCap::Round;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(canvas, {100, 100}).vertices.size(), 54u);
}

TEST_CASE("graphics.primitive3d.recordedWireShapesAndNearPlaneClippingRemainFinite") {
    SceneDrawContext       context = view3d();
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{0.5f, 0.25f}, 0.f, DashSpace::WorldUnits};
    canvas.drawAabb({-1.f, -2.f, -3.f}, {1.f, 2.f, 3.f}, paint);
    CHECK_EQ(canvas.commands().size(), 12u);
    canvas.drawSphere({0.f, 0.f, -4.f}, 2.f, paint, 8);
    CHECK_EQ(canvas.commands().size(), 15u);
    for (const auto& command : canvas.commands()) CHECK(command.paint.stroke.dash.has_value());
    canvas.reset();
    paint.stroke.dash.reset();
    paint.stroke.width      = 3.f;
    paint.stroke.widthSpace = WidthSpace::ScreenPixels;
    paint.antialias         = false;
    canvas.drawLine({-1.f, 0.f, -1.f}, {1.f, 0.f, 0.f}, paint);
    canvas.drawLine({-1.f, 1.f, 1.f}, {1.f, 1.f, 2.f}, paint);
    const auto clipped = resolvePrimitiveStrokes3D(canvas);
    REQUIRE_EQ(clipped.vertices.size(), 6u);
    for (const auto& vertex : clipped.vertices) {
        CHECK(std::isfinite(vertex.clipPosition.x));
        CHECK_GT(vertex.clipPosition.w, 0.f);
    }
    canvas.reset();
    paint.stroke.widthSpace = WidthSpace::WorldUnits;
    paint.stroke.width      = 0.2f;
    canvas.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);
    const auto world = resolvePrimitiveStrokes3D(canvas);
    REQUIRE_EQ(world.vertices.size(), 6u);
    CHECK_GT(std::fabs(world.vertices[0].clipPosition.y - world.vertices[2].clipPosition.y), 1e-5f);
}

TEST_CASE("graphics.primitiveScene.staleCrossOwnerAndInvalidUpdatesPreserveCommittedDescriptors") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry = PrimitiveSphere3D{{0.f, 0.f, 0.f}, 1.f, 8};
    CHECK(scene.isStale({}));
    auto first = scene.add(descriptor);
    REQUIRE(first.ok());
    REQUIRE(scene.tryGet(first.value()) != nullptr);
    auto removed = scene.remove(first.value());
    REQUIRE(removed.ok());
    CHECK(scene.isStale(first.value()));
    auto reused = scene.add(descriptor);
    REQUIRE(reused.ok());
    CHECK_EQ(reused.value().index(), first.value().index());
    CHECK_NE(reused.value().generation(), first.value().generation());
    CHECK(!scene.update(first.value(), descriptor).ok());
    PrimitiveScene another;
    auto           foreign = another.add(descriptor);
    REQUIRE(foreign.ok());
    CHECK_NE(foreign.value().owner(), reused.value().owner());
    CHECK(another.tryGet(reused.value()) == nullptr);
    PrimitiveDescriptor3D invalid = descriptor;
    invalid.geometry              = PrimitiveSphere3D{{0.f, 0.f, 0.f}, -1.f, 8};
    CHECK(!scene.update(reused.value(), invalid).ok());
    CHECK(!scene.add(invalid).ok());
    REQUIRE(scene.tryGet(reused.value()) != nullptr);
    const auto* sphere = std::get_if<PrimitiveSphere3D>(&scene.tryGet(reused.value())->geometry);
    REQUIRE(sphere != nullptr);
    CHECK_EQ(sphere->radius, 1.f);
    CHECK_EQ(scene.size(), 1u);
}

TEST_CASE("graphics.primitiveScene.rejectsDegenerateSpatialGeometryAndInvalidPaintBeforeMutation") {
    PrimitiveScene                           scene;
    const std::array<PrimitiveGeometry3D, 4> invalid{{
        PrimitiveAabb3D{{1.f, 0.f, 0.f}, {0.f, 1.f, 1.f}},
        PrimitiveDisk3D{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 1.f, 8},
        PrimitiveCylinder3D{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 1.f, 8},
        PrimitiveArrow3D{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 0.2f, 0.1f},
    }};
    for (const auto& geometry : invalid) {
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = geometry;
        CHECK(!scene.add(descriptor).ok());
    }
    CHECK_EQ(scene.size(), 0u);
    PrimitiveDescriptor3D valid;
    valid.geometry = PrimitiveSphere3D{{0.f, 0.f, 0.f}, 1.f, 8};
    auto added     = scene.add(valid);
    REQUIRE(added.ok());
    valid.paint.stroke.width = -1.f;
    CHECK(!scene.update(added.value(), valid).ok());
    REQUIRE(scene.tryGet(added.value()) != nullptr);
    CHECK_EQ(scene.tryGet(added.value())->paint.stroke.width, 1.f);
    valid.paint.stroke.width = 1.f;
    valid.paint.stroke.dash  = DashPattern{{1.f, 0.f}, 0.f, DashSpace::WorldUnits};
    CHECK(!scene.update(added.value(), valid).ok());
    CHECK(!scene.tryGet(added.value())->paint.stroke.dash.has_value());
}

TEST_CASE("graphics.primitiveScene.batchMutationIsAtomicAndRenderingSkipsHiddenDescriptors") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D visible;
    visible.geometry  = PrimitiveArrow3D{{0.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, 0.4f, 0.2f};
    visible.transform = glm::translate(glm::mat4(1.f), glm::vec3(4.f, 0.f, 0.f));
    auto first        = scene.add(visible);
    REQUIRE(first.ok());
    PrimitiveDescriptor3D second = visible;
    second.visible               = false;
    auto stale                   = scene.add(second);
    REQUIRE(stale.ok());
    REQUIRE(scene.remove(stale.value()).ok());
    PrimitiveDescriptor3D hide = visible;
    hide.visible               = false;
    const std::array updates{PrimitiveBatchUpdate{first.value(), hide}, PrimitiveBatchUpdate{stale.value(), hide}};
    CHECK(!scene.updateMany(updates).ok());
    REQUIRE(scene.tryGet(first.value()) != nullptr);
    CHECK(scene.tryGet(first.value())->visible);
    PrimitiveSceneCanvas3D canvas(view3d());
    scene.render(canvas);
    CHECK(!canvas.commands().empty());
    for (const auto& command : canvas.commands()) CHECK_LT(std::fabs(command.transform[3].x - 4.f), 1e-5f);
    scene.clear();
    CHECK_EQ(scene.size(), 0u);
    CHECK(scene.isStale(first.value()));
}
}  // namespace test_graphics_primitives
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_skin_palette)
#include "graphics/Mesh.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <vector>

namespace test_graphics_skin_palette {
TEST_CASE("graphics.skinPalette.copiesDynamicMatrixArrayAndReplacesPreviousCount") {
    eve::graphics::Mesh mesh;
    for (const int count : {1, 129, 4096, 17}) {
        std::vector<float> input(std::size_t(count) * 16u, 0.f);
        for (int matrix = 0; matrix < count; ++matrix) {
            input[std::size_t(matrix) * 16u + 12u] = float(matrix);
            input[std::size_t(matrix) * 16u + 15u] = 1.f;
        }
        REQUIRE(mesh.setSkinPalette(input.data(), count).ok());
        input.back() = 5.f;
        CHECK_EQ(mesh.getSkinPaletteCount(), count);
        CHECK_EQ(mesh.skinPalette().size(), std::size_t(count) * 16u);
        CHECK_EQ(mesh.skinPalette().back(), 1.f);
    }
}

TEST_CASE("graphics.skinPalette.invalidPointerCountOrNonfiniteMatrixPreservesLastGoodPalette") {
    eve::graphics::Mesh mesh;
    std::vector<float>  valid(16u, 0.f);
    valid[15] = 1.f;
    REQUIRE(mesh.setSkinPalette(valid.data(), 1).ok());
    const auto before = mesh.skinPalette();
    CHECK(!mesh.setSkinPalette(nullptr, 1).ok());
    CHECK(!mesh.setSkinPalette(valid.data(), 0).ok());
    CHECK(!mesh.setSkinPalette(valid.data(), -1).ok());
    valid[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!mesh.setSkinPalette(valid.data(), 1).ok());
    CHECK_EQ(mesh.skinPalette(), before);
    CHECK_EQ(mesh.getSkinPaletteCount(), 1);
}
}  // namespace test_graphics_skin_palette
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_vegetation_extras)
#include "graphics/PbrSurface.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_graphics_vegetation_gradient {
TEST_CASE("graphics.vegetationGradient.unitEndpointsAndFiniteNonnegativeColorsValidate") {
    using namespace eve::graphics;
    PbrSurface surface;
    surface.vegetationGradient.enabled = true;
    surface.vegetationGradient.minimum = 0.f;
    surface.vegetationGradient.maximum = 1.f;
    REQUIRE(validatePbrSurface(surface).ok());
    surface.vegetationGradient.colorOne = {0.f, 1.f, 2.f};
    surface.vegetationGradient.colorTwo = {2.f, 1.f, 0.f};
    CHECK(validatePbrSurface(surface).ok());
}

TEST_CASE("graphics.vegetationGradient.negativeNonfiniteOrOutOfRangeInputsFailValidation") {
    using namespace eve::graphics;
    PbrSurface surface;
    surface.vegetationGradient.enabled = true;
    surface.vegetationGradient.minimum = -0.01f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationGradient         = {};
    surface.vegetationGradient.maximum = 1.01f;
    CHECK(!validatePbrSurface(surface).ok());
    surface.vegetationGradient             = {};
    surface.vegetationGradient.colorOne[0] = -1.f;
    CHECK(!validatePbrSurface(surface).ok());
    surface.vegetationGradient             = {};
    surface.vegetationGradient.colorTwo[1] = std::numeric_limits<float>::infinity();
    CHECK(!validatePbrSurface(surface).ok());
}
}  // namespace test_graphics_vegetation_gradient
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_webgpu_pbr_variant)
#include "common/config.h"
#ifdef EVENGINE_WEBGPU
#include "graphics/PbrSurface.h"
#include "graphics/webgpu/PbrVariantPlan.h"
#include "graphics/webgpu/PbrVariantSource.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string_view>

namespace test_webgpu_pbr_variant {
using namespace eve::graphics;
using namespace eve::graphics::webgpu;

Texture* marker(std::uintptr_t value) { return reinterpret_cast<Texture*>(value); }

TEST_CASE("graphics.webgpuPbrVariant.reachableMaterialResourcesDetermineStableFragmentPlan") {
    PbrSurface surface;
    auto       empty = planPbrVariant(surface, false, false);
    REQUIRE(empty.ok());
    CHECK_EQ(empty.value().fragmentTextures, 0u);
    CHECK_EQ(empty.value().vertexTextures, 0u);
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1u);
    surface.textures[std::size_t(PbrTextureSlot::Normal)].texture    = marker(2u);
    surface.vegetationDetail.value                                   = 1.f;
    surface.vegetationDetail.textures[0].texture                     = marker(3u);
    surface.vegetationDetail.textures[1].texture                     = marker(4u);
    auto detail                                                      = planPbrVariant(surface, true, true);
    REQUIRE(detail.ok());
    CHECK_EQ(detail.value().canonicalTextureMask, 0b101u);
    CHECK_EQ(detail.value().detailTextureMask, 0b011u);
    CHECK_EQ(detail.value().fragmentTextures, 6u);
    CHECK_EQ(detail.value().fragmentSamplers, 3u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[0], 0u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[2], 0u);
    surface.vegetationDetail.maskMode            = 1;
    surface.vegetationDetail.textures[2].texture = marker(5u);
    auto masked                                  = planPbrVariant(surface, true, true);
    REQUIRE(masked.ok());
    CHECK_EQ(masked.value().detailTextureMask, 0b111u);
    CHECK_EQ(masked.value().fragmentTextures, 7u);
}

TEST_CASE("graphics.webgpuPbrVariant.vertexBudgetIsIndependentAndOverflowIsStructured") {
    PbrSurface surface;
    surface.vegetationVertex.source  = PbrVegetationDeformationSource::GpuFields;
    surface.vegetationVertex.texture = marker(1u);
    surface.vegetationMotion.mode    = PbrVegetationMotionMode::Object;
    surface.vegetationMotion.texture = marker(2u);
    surface.vegetationMotion.noise   = marker(3u);
    auto vertex                      = planPbrVariant(surface, false, false, {3u, 3u});
    REQUIRE(vertex.ok());
    CHECK_EQ(vertex.value().vertexTextures, 3u);
    CHECK_EQ(vertex.value().fragmentTextures, 0u);
    CHECK(!planPbrVariant(surface, false, false, {2u, 3u}).ok());
    for (std::size_t slot = 0; slot < std::size_t(PbrTextureSlot::Count); ++slot)
        surface.textures[slot].texture = marker(slot + 10u);
    surface.vegetationExtras.texture = marker(30u);
    surface.vegetationColors.texture = marker(31u);
    REQUIRE(planPbrVariant(surface, true, true, {17u, 16u}).ok());
    surface.vegetationDetail.value               = 1.f;
    surface.vegetationDetail.textures[0].texture = marker(33u);
    surface.vegetationDetail.textures[1].texture = marker(34u);
    auto overflow                                = planPbrVariant(surface, true, true, {17u, 16u});
    REQUIRE(!overflow.ok());
    CHECK_EQ(overflow.error()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("graphics.webgpuPbrVariant.specializationRemovesUnreachableTextureDeclarations") {
    const auto base = pbrVariantBaseSources();
    CHECK(base.vertex.find("@vertex") != std::string_view::npos);
    CHECK(base.fragment.find("@fragment") != std::string_view::npos);
    CHECK(base.vertex.find("vertexField_image") != std::string_view::npos);
    CHECK(base.fragment.find("detailMap2_image") != std::string_view::npos);
    auto empty = planPbrVariant(PbrSurface{}, false, false);
    REQUIRE(empty.ok());
    const auto specialized = specializePbrVariantSources(empty.value());
    CHECK(specialized.vertex.find("vertexField_image") == std::string::npos);
    CHECK(specialized.fragment.find("detailMap2_image") == std::string::npos);
}
}  // namespace test_webgpu_pbr_variant
#endif
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_grass_billboard)
#include "graphics/Grass.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <vector>

namespace test_grass_billboard {
using namespace eve::graphics;

TEST_CASE("graphics.vegetationWind.packetUsesExplicitTimeAndBillboardFlags") {
    VegetationWindState   state{{1.f, -0.5f, 0.2f}, 0.8f, 0.17f};
    VegetationWindProfile profile;
    auto                  disabled = packVegetationWind(state, profile, 2.0);
    REQUIRE(disabled.ok());
    CHECK_EQ(disabled.value()[5], 0.f);
    profile.enabled   = true;
    profile.billboard = true;
    profile.alphaTest = false;
    auto billboard    = packVegetationWind(state, profile, 2.0);
    REQUIRE(billboard.ok());
    const auto data = billboard.value();
    CHECK_EQ(data[0], state.direction.x);
    CHECK_EQ(data[3], state.strength);
    CHECK_EQ(data[4], state.phase);
    CHECK_EQ(data[5], -100.f);
    CHECK_EQ(data[8], 0.f);
    CHECK_LT(std::fabs(data[12] - 0.479425539f), 1e-6f);
    CHECK_LT(std::fabs(data[13] - 0.909297427f), 1e-6f);
    profile.billboard = false;
    profile.alphaTest = true;
    auto alpha        = packVegetationWind(state, profile, 2.0);
    REQUIRE(alpha.ok());
    CHECK_EQ(alpha.value()[5], 100.f);
    CHECK_EQ(alpha.value()[8], profile.flex.z);
}

TEST_CASE("graphics.vegetationWind.invalidPacketInputFailsEvenWhenProfileDisabled") {
    VegetationWindState   state;
    VegetationWindProfile profile;
    REQUIRE(!packVegetationWind(state, profile, std::numeric_limits<double>::infinity()).ok());
    profile.maximumDistance = 0.f;
    CHECK(!packVegetationWind(state, profile, 0.0).ok());
    profile.maximumDistance = 100.f;
    profile.frequency.x     = std::numeric_limits<float>::quiet_NaN();
    CHECK(!packVegetationWind(state, profile, 0.0).ok());
    profile.frequency.x = 0.25f;
    state.strength      = 2.f;
    CHECK(!packVegetationWind(state, profile, 0.0).ok());
    CHECK(!profile.enabled);
    CHECK_EQ(state.strength, 2.f);
}

TEST_CASE("graphics.vegetationWind.advanceWrapsPhaseAndInvalidDtPreservesSnapshot") {
    VegetationWindState state;
    REQUIRE(advanceVegetationWind(state, {1.f, 0.f, 0.f}, 1.f, 4.f).ok());
    CHECK_EQ(state.direction.x, 1.f);
    CHECK_LT(std::fabs(state.phase - 0.4f), 1e-6f);
    state.phase = state.updatePhase = 99.9f;
    REQUIRE(advanceVegetationWind(state, {1.f, 0.f, 0.f}, 1.f, 4.f).ok());
    CHECK_LT(std::fabs(state.phase - 0.3f), 1e-5f);
    const auto saved = state;
    CHECK(!advanceVegetationWind(state, {1.f, 0.f, 0.f}, 1.f, -1.f).ok());
    CHECK_EQ(state.phase, saved.phase);
    CHECK_EQ(state.updatePhase, saved.updatePhase);
}

TEST_CASE("graphics.grass.billboardGeometryEncodesIndependentWidthScaleAndTint") {
    std::vector<grass::Point> points(1);
    points[0].position   = {3.f, 4.f, 5.f};
    points[0].id         = 11;
    points[0].scale      = 2.f;
    points[0].widthScale = 0.25f;
    const auto standard  = grass::buildBillboards(points, 1.f, 1.f, false);
    const auto dark      = grass::buildBillboards(points, 1.f, 1.f, true);
    REQUIRE_EQ(standard.posXYZ.size(), 12u);
    for (int vertex = 0; vertex < 4; ++vertex) {
        CHECK_EQ(standard.posXYZ[std::size_t(vertex) * 3u], 3.f);
        CHECK_EQ(standard.nrmXYZ[std::size_t(vertex) * 3u + 2u], -0.25f);
        CHECK_EQ(dark.nrmXYZ[std::size_t(vertex) * 3u + 2u], 1.25f);
    }
    points[0].widthScale = 0.f;
    const auto legacy    = grass::buildBillboards(points, 1.f, 1.f, false);
    CHECK_EQ(legacy.nrmXYZ[2], 0.f);
    CHECK_EQ(legacy.uvST, standard.uvST);
    CHECK_EQ(legacy.indices, standard.indices);
    points[0].tint        = {0.2f, 0.4f, 0.8f};
    const auto     tinted = grass::buildBillboards(points, 1.f, 1.f, false);
    const uint32_t packed = (51u << 16) | (102u << 8) | 204u;
    CHECK_EQ(tinted.nrmXYZ[0], -float(packed + 1u));
}

TEST_CASE("graphics.vegetationWind.evaluateKeepsRootAndCullsDistantBillboard") {
    VegetationWindInput input;
    VegetationWindState wind;
    wind.direction = {1.f, 0.f, 0.f};
    wind.strength  = 1.f;
    auto root      = evaluateVegetationWind(input, wind);
    REQUIRE(root.ok());
    CHECK_EQ(root.value().x, 0.f);
    CHECK_EQ(root.value().y, 0.f);
    CHECK_EQ(root.value().z, 0.f);
    input.position  = {0.f, 1.f, 0.f};
    input.billboard = true;
    input.flex      = {1.f, 0.f, 0.f};
    auto bent       = evaluateVegetationWind(input, wind);
    REQUIRE(bent.ok());
    CHECK_LT(std::fabs(bent.value().x - float(1.05 / 2.1025)), 1e-6f);
    input.cameraPosition = {100.f, 0.f, 0.f};
    auto far             = evaluateVegetationWind(input, wind);
    REQUIRE(far.ok());
    CHECK_EQ(far.value(), input.position);
    input.objectToWorld = glm::mat3(0.f);
    CHECK(!evaluateVegetationWind(input, wind).ok());
}
}  // namespace test_grass_billboard
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_grass)
#include "graphics/Grass.h"
#include "graphics/Shader.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace test_grass_unit {
using namespace eve::graphics;

namespace {
void plane(std::vector<float>& positions, std::vector<float>& normals, std::vector<uint32_t>& indices) {
    grass::makePlane(4.f, 4.f, 4, 4, positions, normals, indices);
}

float nearestDistance(const std::vector<grass::Point>& points) {
    float nearest = 1e9f;
    for (size_t i = 0; i < points.size(); ++i) {
        for (size_t j = i + 1; j < points.size(); ++j) {
            nearest = std::min(nearest, glm::length(points[i].position - points[j].position));
        }
    }
    return nearest;
}
}  // namespace

TEST_CASE("graphics.grass.shaderParameters: defaults plus foliage profile and layer flag") {
    Shader shader;
    shader.setKind(Shader::Kind::eMesh3D);
    grass::bindDefaults(&shader);
    REQUIRE_EQ(grass::paramCount(), 32);
    CHECK_EQ(shader.usedFloats(), 32);
    CHECK_EQ(shader.getUniformIndex("time"), 0);
    CHECK_EQ(shader.getUniformIndex("alwaysDark"), 5);
    CHECK_EQ(shader.getUniformIndex("lightGreen"), 6);
    CHECK_EQ(shader.getUniformIndex("frameCount"), 12);
    CHECK_EQ(shader.getUniformIndex("windGlobals"), 18);
    CHECK_EQ(shader.getUniformIndex("windSineTime"), 30);
    CHECK_EQ(grass::paramName(17), std::string("leafRowOffset"));

    // Input: a foliage profile. Output: fixed push constant slots retain their meanings.
    {
        grass::GrassFoliageSettings foliage;
        foliage.baseR             = 0.2f;
        foliage.normalStrength    = 1.7f;
        foliage.renderDistance    = 91.f;
        foliage.fadeRange         = 13.f;
        foliage.snowMinimumHeight = 8.f;
        foliage.snowFadeDistance  = 17.f;
        foliage.snowProgress      = 0.75f;
        grass::bindFoliage(&shader, foliage);
        CHECK_EQ(shader.pushConstantData()[0], 8.f);
        CHECK_EQ(shader.pushConstantData()[1], 1.7f);
        CHECK_EQ(shader.pushConstantData()[6], 0.2f);
        CHECK_EQ(shader.pushConstantData()[12], -1.f);
        CHECK_EQ(shader.pushConstantData()[13], 91.f);
        CHECK_EQ(shader.pushConstantData()[14], 13.f);
        CHECK_EQ(shader.pushConstantData()[15], 17.f);
        CHECK_EQ(shader.pushConstantData()[16], 0.75f);
    }
    // Input: both layer modes. Output: the flag is rewritten on each call.
    {
        float dark = -1.f;
        grass::bindLayer(&shader, true);
        REQUIRE_EQ(shader.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
        CHECK(dark > 0.5f);
        grass::bindLayer(&shader, false);
        REQUIRE_EQ(shader.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
        CHECK(dark < 0.5f);
    }
}

TEST_CASE("graphics.grass.terrainDetail: valid overwrite and rejected density") {
    grass::GrassFoliageSettings           foliage;
    grass::TerrainDetailOverwriteSettings detail;
    detail.pcgDetailDistance        = -4;
    detail.pcgFadeoutDistance       = 12;
    detail.unityDetailDistance      = 140;
    detail.unityDetailDensity       = 0.35f;
    detail.detailResolutionPerPatch = 4;
    auto applied                    = grass::applyTerrainDetailOverwrite(foliage, detail);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), static_cast<int>(grass::TerrainDetailQuality::VeryHigh4));
    CHECK_EQ(foliage.renderDistance, 0.f);
    CHECK_EQ(foliage.fadeRange, 12.f);
    CHECK_EQ(foliage.hardRenderDistance, 140.f);
    CHECK_EQ(foliage.density, 0.35f);
    const auto previous       = foliage;
    detail.unityDetailDensity = 2.f;
    CHECK(!grass::applyTerrainDetailOverwrite(foliage, detail).ok());
    CHECK_EQ(foliage.renderDistance, previous.renderDistance);
    CHECK_EQ(foliage.density, previous.density);
    CHECK(grass::terrainDetailQuality(7) == grass::TerrainDetailQuality::VeryLow64);
}

TEST_CASE("graphics.grass.sampling: flat mesh yields bounded separated points") {
    std::vector<float>    positions, normals;
    std::vector<uint32_t> indices;
    plane(positions, normals, indices);
    SUB_CASE("Poisson radius is a minimum separation") {
        grass::SampleParams params;
        params.radius      = 0.45f;
        params.maxPoints   = 200;
        params.seed        = 7;
        params.minSlopeDot = 0.1f;
        const auto points  = grass::samplePoisson(positions.data(), normals.data(), int(positions.size() / 3),
                                                  indices.data(), int(indices.size()), params);
        REQUIRE(points.size() >= 8);
        CHECK(points.size() <= size_t(params.maxPoints));
        CHECK(nearestDistance(points) + 1e-4f >= params.radius);
        for (const auto& point : points) {
            CHECK(std::abs(point.position.y) < 1e-4f);
            CHECK(point.position.x >= -2.01f);
            CHECK(point.position.x <= 2.01f);
            CHECK(point.position.z >= -2.01f);
            CHECK(point.position.z <= 2.01f);
        }
    };
    SUB_CASE("Halton count, ids and coverage") {
        const auto points = grass::sampleHalton(positions.data(), normals.data(), int(positions.size() / 3),
                                                indices.data(), int(indices.size()), 64, 3, 0.1f);
        REQUIRE_EQ(points.size(), 64u);
        CHECK_EQ(points.front().id, 0u);
        CHECK_EQ(points.back().id, 63u);
        float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
        for (const auto& point : points) {
            minX = std::min(minX, point.position.x);
            maxX = std::max(maxX, point.position.x);
            minZ = std::min(minZ, point.position.z);
            maxZ = std::max(maxZ, point.position.z);
        }
        CHECK(maxX - minX > 2.f);
        CHECK(maxZ - minZ > 2.f);
    };
    SUB_CASE("larger Poisson radius produces fewer points") {
        grass::SampleParams dense;
        dense.radius     = 0.3f;
        dense.maxPoints  = 400;
        dense.seed       = 1;
        auto sparse      = dense;
        sparse.radius    = 1.2f;
        sparse.maxPoints = 80;
        const auto many  = grass::samplePoisson(positions.data(), normals.data(), int(positions.size() / 3),
                                                indices.data(), int(indices.size()), dense);
        const auto few   = grass::samplePoisson(positions.data(), normals.data(), int(positions.size() / 3),
                                                indices.data(), int(indices.size()), sparse);
        CHECK(many.size() > few.size());
        CHECK(!few.empty());
    };
}

TEST_CASE("graphics.grass.billboard: root data plus topology and darkness metadata") {
    grass::Point root;
    root.position   = glm::vec3(3.f, 1.5f, -2.f);
    root.id         = 11;
    root.scale      = 1.25f;
    const auto mesh = grass::buildBillboards({root}, 0.4f, 0.8f, true);
    REQUIRE_EQ(mesh.posXYZ.size(), 12u);
    REQUIRE_EQ(mesh.nrmXYZ.size(), 12u);
    CHECK_EQ(mesh.uvST.size(), 8u);
    CHECK_EQ(mesh.indices.size(), 6u);
    for (size_t vertex = 0; vertex < 4; ++vertex) {
        CHECK_EQ(mesh.posXYZ[vertex * 3], 3.f);
        CHECK_EQ(mesh.posXYZ[vertex * 3 + 1], 1.5f);
        CHECK_EQ(mesh.posXYZ[vertex * 3 + 2], -2.f);
        CHECK_EQ(mesh.nrmXYZ[vertex * 3], 11.f);
        CHECK_EQ(mesh.nrmXYZ[vertex * 3 + 1], 1.25f);
        CHECK(mesh.nrmXYZ[vertex * 3 + 2] > 0.5f);
    }
    CHECK_EQ(mesh.uvST[1], 0.f);
    CHECK_EQ(mesh.uvST[3], 0.f);
}

TEST_CASE("graphics.grass.sway: instance phase and atlas frames") {
    SUB_CASE("instance id changes phase and time advances it") {
        const int first  = grass::swayFrame(0.f, 0.12f, 0, 4);
        const int second = grass::swayFrame(0.f, 0.12f, 1, 4);
        const int third  = grass::swayFrame(0.f, 0.12f, 2, 4);
        CHECK(first >= 0);
        CHECK(first < 4);
        CHECK(second >= 0);
        CHECK(second < 4);
        CHECK(third >= 0);
        CHECK(third < 4);
        const bool desynchronized = first != second || second != third;
        CHECK(desynchronized);
        CHECK(grass::swayFrame(0.36f, 0.12f, 0, 4) != first);
    };
    SUB_CASE("four generated frames retain roots and vary their silhouette") {
        std::vector<uint8_t> rgba;
        grass::makeSwayAtlasRGBA(32, 32, 4, rgba);
        REQUIRE_EQ(grass::swayAtlasWidth(32, 4), 128);
        REQUIRE_EQ(grass::swayAtlasHeight(32), 32);
        REQUIRE_EQ(rgba.size(), 128u * 32u * 4u);
        int perFrame[4] = {};
        int roots = 0, tips = 0, differences = 0;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 128; ++x) {
                const auto alpha = rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u];
                if (alpha > 16) ++perFrame[x / 32];
                if (x < 32 && alpha > 16) {
                    if (y >= 24) ++roots;
                    if (y < 8) ++tips;
                }
                if (x < 32 && alpha != rgba[(size_t(y) * 128u + size_t(x + 96)) * 4u + 3u]) ++differences;
            }
        }
        for (int count : perFrame) CHECK(count > 10);
        CHECK(roots > tips);
        CHECK(differences > 8);
    };
}
}  // namespace test_grass_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_grass_wind)
#include "graphics/Grass.h"
#include "graphics/GrassWind.h"
#include "graphics/Shader.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace test_grass_wind {
using namespace eve::graphics;

std::array<float, 32> snapshot(const Shader& shader) {
    std::array<float, 32> values{};
    std::copy_n(shader.pushConstantData(), values.size(), values.begin());
    return values;
}

TEST_CASE("graphics.grassWind.applyPublishesPacketAtomicallyAndValidatesEntireSchema") {
    SUB_CASE("canonical layout updates only fourteen wind floats and disabled mode clears flag") {
        Shader shader;
        grass::bindDefaults(&shader);
        REQUIRE_EQ(shader.usedFloats(), 32);
        const auto            original = snapshot(shader);
        VegetationWindState   state{{1.f, -0.5f, 0.2f}, 0.8f, 0.17f};
        VegetationWindProfile profile;
        profile.enabled = true;
        auto packed     = packVegetationWind(state, profile, 2.0);
        REQUIRE(packed.ok());
        REQUIRE(applyGrassWind(shader, state, profile, 2.0).ok());
        const auto applied = snapshot(shader);
        for (int i = 0; i < 18; ++i) CHECK_EQ(applied[std::size_t(i)], original[std::size_t(i)]);
        for (int i = 0; i < 14; ++i) CHECK_EQ(applied[std::size_t(i + 18)], packed.value()[std::size_t(i)]);
        profile.maximumDistance = 0.f;
        CHECK(!applyGrassWind(shader, state, profile, 3.0).ok());
        CHECK_EQ(snapshot(shader), applied);
        profile.maximumDistance = 100.f;
        profile.enabled         = false;
        REQUIRE(applyGrassWind(shader, state, profile, 3.0).ok());
        CHECK_EQ(shader.pushConstantData()[23], 0.f);
    };
    SUB_CASE("wrong final uniform fails without writing an earlier valid prefix") {
        Shader shader;
        for (int i = 0; i < 18; ++i) shader.declareFloat("prefix" + std::to_string(i));
        shader.declareVec4("windGlobals");
        shader.declareVec2("windPhaseDistance");
        shader.declareVec3("windFlex");
        shader.declareVec3("windFrequency");
        shader.declareVec2("wrongLastUniform");
        shader.sendVec4("windGlobals", 9.f, 8.f, 7.f, 6.f);
        const auto            before = snapshot(shader);
        VegetationWindState   state{{1.f, 0.f, 0.f}, 1.f, 0.f};
        VegetationWindProfile profile;
        profile.enabled = true;
        CHECK(!applyGrassWind(shader, state, profile, 0.0).ok());
        CHECK_EQ(snapshot(shader), before);
    };
}

TEST_CASE("graphics.vegetationWind.instantStateAndAudioControllerUseInjectedTime") {
    SUB_CASE("instant initialization applies direction now and failed input preserves every field") {
        VegetationWindState state{{9.f, 8.f, 7.f}, 0.25f, 4.f};
        REQUIRE(initializeVegetationWind(state, {0.6f, 0.8f, 0.f}, 2.f).ok());
        CHECK_EQ(state.direction.x, 0.6f);
        CHECK_EQ(state.direction.y, -0.8f);
        CHECK_EQ(state.direction.z, 0.f);
        CHECK_EQ(state.strength, 1.2f);
        CHECK_LT(std::fabs(state.phase - 0.1331f), 1e-6f);
        CHECK_EQ(state.updatePhase, 0.f);
        REQUIRE(advanceVegetationWind(state, {0.6f, 0.8f, 0.f}, 1.2f, 1.f).ok());
        CHECK_LT(std::fabs(state.updatePhase - 0.1331f), 1e-6f);
        const auto before = state;
        CHECK(!initializeVegetationWind(state, {std::numeric_limits<float>::infinity(), 0.f, 0.f}, 1.f).ok());
        CHECK_EQ(state.direction, before.direction);
        CHECK_EQ(state.phase, before.phase);
    };
    SUB_CASE("audio fades to controller speed and rejects zero span without mutation") {
        VegetationWindAudioState state;
        REQUIRE(advanceVegetationWindAudio(state, 1.f, 5.f, 1.f, true, true).ok());
        CHECK_EQ(state.volume, 0.2f);
        CHECK(state.processing);
        CHECK(state.playing);
        for (int i = 0; i < 20 && state.processing; ++i)
            REQUIRE(advanceVegetationWindAudio(state, 1.f, 5.f, 1.f, true, true).ok());
        CHECK_EQ(state.volume, 1.f);
        const auto before = state;
        CHECK(!advanceVegetationWindAudio(state, 1.f, 0.f, 1.f, true, true).ok());
        CHECK_EQ(state.volume, before.volume);
        CHECK_EQ(state.processing, before.processing);
    };
}
}  // namespace test_grass_wind
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hair)
#include "graphics/HairCards.h"
#include "graphics/HairShader.h"
#include "graphics/Shader.h"
#include "graphics/hair/Binding.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/Guides.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/Simulation.h"
#include "graphics/hair/StrandsDatas.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

namespace test_hair_unit {
using namespace eve::graphics;
using namespace eve::graphics::hair;

namespace {
StrandsDatas straightStrands() {
    StrandsDatas strands;
    strands.setPoints(
        {{{0.f, 0.f, 0.f}, 0.002f, 0.f}, {{0.f, 0.05f, 0.f}, 0.0015f, 0.5f}, {{0.f, 0.1f, 0.f}, 0.001f, 1.f}});
    strands.setCurves({{0u, 3u, 0.1f}});
    return strands;
}

eve::Result<StrandsDatas> grown(int count, int points, uint32_t seed) {
    ProceduralParams params;
    params.strandCount     = count;
    params.pointsPerStrand = points;
    params.seed            = seed;
    return generateOnPlane(0.3f, 0.3f, params);
}

GroomGroup group(uint32_t id, const std::string& name) {
    GroomGroup item;
    item.groupId   = id;
    item.name      = name;
    auto generated = grown(8, 4, id);
    if (generated.ok()) item.strands = std::move(generated).value();
    item.lods = {{1.f, Representation::Strands, 1.f, 1.f}, {0.2f, Representation::None, 0.25f, 2.f}};
    return item;
}
}  // namespace

TEST_CASE("graphics.hair.shader: named parameters map to stable push constants") {
    Shader shader;
    shader.setKind(Shader::Kind::eMesh3D);
    hair::bindDefaults(&shader);
    REQUIRE_EQ(hair::paramCount(), 15);
    CHECK_EQ(shader.usedFloats(), 15);
    CHECK_EQ(shader.getUniformIndex("specExp"), 0);
    CHECK_EQ(shader.getUniformIndex("strandDir"), 6);
    CHECK_EQ(shader.getUniformIndex("marschnerR"), 9);
    CHECK_EQ(shader.getUniformIndex("selfShadowStrength"), 12);
    CHECK_EQ(hair::paramName(8), std::string("strandDirZ"));
    CHECK_EQ(hair::paramName(14), std::string("rootAoStrength"));
}

TEST_CASE("graphics.hair.strands: valid and bad ranges plus clear") {
    StrandsDatas strands;
    CHECK(!strands.validate().ok());
    CHECK(strands.curvePoints(0).empty());
    strands = straightStrands();
    REQUIRE(strands.validate().ok());
    CHECK_EQ(strands.curveCount(), 1u);
    CHECK_EQ(strands.pointCount(), 3u);
    CHECK_EQ(strands.curvePoints(0).size(), 3u);
    CHECK(strands.curvePoints(1).empty());
    glm::vec3 minimum, maximum;
    strands.computeBounds(minimum, maximum);
    CHECK_EQ(minimum.y, 0.f);
    CHECK_EQ(maximum.y, 0.1f);
    SUB_CASE("an out of bounds curve is rejected") {
        strands.setCurves({{2u, 3u, 0.1f}});
        CHECK(!strands.validate().ok());
        CHECK(strands.curvePoints(0).empty());
    };
    SUB_CASE("clear removes both owned arrays") {
        strands.clear();
        CHECK_EQ(strands.pointCount(), 0u);
        CHECK_EQ(strands.curveCount(), 0u);
    };
}

TEST_CASE("graphics.hair.proceduralPlane: counts plus repeatability and invalid parameters") {
    auto first  = grown(24, 5, 42);
    auto second = grown(24, 5, 42);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value().validate().ok());
    CHECK_EQ(first.value().curveCount(), 24u);
    CHECK_EQ(first.value().pointCount(), 120u);
    for (size_t index : {size_t(0), size_t(20), size_t(119)}) {
        CHECK_EQ(first.value().points()[index].position.x, second.value().points()[index].position.x);
        CHECK_EQ(first.value().points()[index].position.y, second.value().points()[index].position.y);
    }
    ProceduralParams bad;
    bad.strandCount = 0;
    CHECK(!generateOnPlane(0.3f, 0.3f, bad).ok());
}

TEST_CASE("graphics.hair.ribbon: triangle topology and rebased append") {
    auto a = grown(4, 3, 11);
    auto b = grown(4, 3, 12);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RibbonParams parameters;
    parameters.widthScale = 2.f;
    auto first            = buildRibbons(a.value(), parameters);
    auto second           = buildRibbons(b.value(), parameters);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value().vertexCount() > 0);
    CHECK_EQ(first.value().indexCount() % 3, 0);
    CHECK_EQ(first.value().posXYZ.size(), first.value().nrmXYZ.size());
    CHECK_EQ(first.value().uvST.size(), size_t(first.value().vertexCount()) * 2u);
    auto      combined     = first.value();
    const int baseVertices = combined.vertexCount();
    const int baseIndices  = combined.indexCount();
    appendRibbonMesh(combined, second.value());
    CHECK_EQ(combined.vertexCount(), baseVertices + second.value().vertexCount());
    CHECK_EQ(combined.indexCount(), baseIndices + second.value().indexCount());
    for (size_t index = size_t(baseIndices); index < combined.indices.size(); ++index) {
        CHECK(combined.indices[index] >= uint32_t(baseVertices));
        CHECK(combined.indices[index] < uint32_t(combined.vertexCount()));
    }
}

TEST_CASE("graphics.hair.cards: one card per segment or supplied root") {
    const auto single = buildCard(0.2f, 0.5f);
    REQUIRE_EQ(single.positions.size(), 12u);
    CHECK_EQ(single.normals.size(), 12u);
    CHECK_EQ(single.uvs.size(), 8u);
    CHECK_EQ(single.indices.size(), 6u);
    CardRoot   roots[2]  = {{0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.1f, 0.4f}, {0.2f, 0.f, 0.f, 0.f, 1.f, 0.1f, 0.08f, 0.35f}};
    const auto fromRoots = buildCardsFromRoots(roots, 2);
    CHECK_EQ(fromRoots.indices.size(), 12u);
    const float polyline[]   = {0.f, 0.f, 0.f, 0.f, 0.2f, 0.f, 0.f, 0.45f, 0.05f};
    const auto  fromPolyline = buildCardsAlongPolyline(polyline, 3, 0.1f);
    CHECK_EQ(fromPolyline.indices.size(), 12u);
}

TEST_CASE("graphics.hair.asset: groups are owned by id and LOD follows screen size") {
    GroomAsset asset;
    REQUIRE(asset.addGroup(group(1, "scalp")).ok());
    REQUIRE(asset.addGroup(group(2, "fringe")).ok());
    REQUIRE(asset.validate().ok());
    CHECK_EQ(asset.groupCount(), 2u);
    REQUIRE(asset.findGroupById(1) != nullptr);
    CHECK_EQ(asset.findGroupById(1)->name, std::string("scalp"));
    CHECK(asset.groupAt(2) == nullptr);
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.9f), 0u);
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.1f), 1u);
    CHECK(asset.groupAt(0)->lods[1].representation == Representation::None);
    CHECK(!asset.addGroup(group(1, "duplicate")).ok());
    CHECK_EQ(asset.groupCount(), 2u);
    asset.clear();
    CHECK_EQ(asset.groupCount(), 0u);
}

TEST_CASE("graphics.hair.clusterGrid: spatial groups cull by camera and filter curves") {
    StrandsDatas             strands;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    for (const glm::vec3 root : {glm::vec3(-2.f, 0.f, 0.f), glm::vec3(-1.9f, 0.f, 0.05f), glm::vec3(2.f, 0.f, 0.f),
                                 glm::vec3(2.1f, 0.f, -0.05f)}) {
        curves.push_back({uint32_t(points.size()), 2u, 0.1f});
        points.push_back({root, 0.002f, 0.f});
        points.push_back({root + glm::vec3(0.f, 0.1f, 0.f), 0.001f, 1.f});
    }
    strands.setPoints(std::move(points));
    strands.setCurves(std::move(curves));
    REQUIRE(strands.validate().ok());
    ClusterGrid grid;
    REQUIRE(grid.build(strands, 0.5f).ok());
    REQUIRE(grid.clusterCount() >= 2u);
    glm::vec3 minimum, maximum;
    grid.computeBounds(minimum, maximum);
    CHECK(minimum.x < -1.f);
    CHECK(maximum.x > 1.f);
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(-2.f, 0.4f, 1.5f), glm::vec3(-2.f, 0.05f, 0.f), glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 projection     = glm::perspectiveRH_ZO(glm::radians(35.f), 1.f, 0.1f, 4.f);
    const glm::mat4 viewProjection = projection * view;
    auto            visible        = grid.cullClusters(reinterpret_cast<const float*>(&viewProjection[0][0]));
    REQUIRE(visible.ok());
    CHECK(!visible.value().empty());
    CHECK(visible.value().size() < grid.clusterCount());
    auto ids = grid.collectCurveIndices(visible.value());
    REQUIRE(ids.ok());
    auto subset = filterStrandsByCurves(strands, ids.value());
    REQUIRE(subset.ok());
    CHECK(subset.value().curveCount() > 0u);
    CHECK(subset.value().curveCount() < strands.curveCount());
}

TEST_CASE("graphics.hair.skinBinding: a translated scalp moves roots and tips") {
    StrandsDatas strands;
    auto points = std::vector<StrandPoint>{{{-0.2f, 0.f, 0.f}, 0.002f, 0.f},    {{-0.2f, 0.05f, 0.f}, 0.0015f, 0.5f},
                                           {{-0.2f, 0.1f, 0.f}, 0.001f, 1.f},   {{0.2f, 0.f, 0.f}, 0.002f, 0.f},
                                           {{0.2f, 0.05f, 0.f}, 0.0015f, 0.5f}, {{0.2f, 0.1f, 0.f}, 0.001f, 1.f}};
    strands.setPoints(std::move(points));
    strands.setCurves({{0u, 3u, 0.1f}, {3u, 3u, 0.1f}});
    REQUIRE(strands.validate().ok());
    float        restPositions[] = {-0.5f, 0.f, -0.5f, 0.5f, 0.f, -0.5f, 0.5f, 0.f, 0.5f, -0.5f, 0.f, 0.5f};
    uint32_t     triangles[]     = {0, 1, 2, 0, 2, 3};
    SkinTriMesh  rest{restPositions, 4, triangles, 6};
    GroomBinding binding;
    REQUIRE(binding.build(strands, rest).ok());
    CHECK_EQ(binding.rootCount(), 2u);
    REQUIRE(binding.rootAt(0) != nullptr);
    CHECK(binding.rootAt(0)->triangleIndex < 2u);
    float translated[12];
    for (int i = 0; i < 4; ++i) {
        translated[i * 3]     = restPositions[i * 3];
        translated[i * 3 + 1] = restPositions[i * 3 + 1] + 0.25f;
        translated[i * 3 + 2] = restPositions[i * 3 + 2];
    }
    SkinTriMesh deformed{translated, 4, triangles, 6};
    SUB_CASE("rigid deformation follows the root translation") {
        auto moved = binding.deform(strands, deformed, BindingDeformMode::Rigid);
        REQUIRE(moved.ok());
        CHECK_EQ(moved.value().curveCount(), 2u);
        const float rootDelta = moved.value().curvePoints(0)[0].position.y - strands.curvePoints(0)[0].position.y;
        const float tipDelta  = moved.value().curvePoints(0)[2].position.y - strands.curvePoints(0)[2].position.y;
        CHECK(std::abs(rootDelta - 0.25f) < 0.01f);
        CHECK(std::abs(tipDelta - 0.25f) < 0.01f);
    };
    SUB_CASE("offset deformation preserves topology") {
        auto moved = binding.deform(strands, deformed, BindingDeformMode::Offset);
        REQUIRE(moved.ok());
        CHECK_EQ(moved.value().curveCount(), 2u);
        CHECK_EQ(moved.value().pointCount(), 6u);
    };
}

TEST_CASE("graphics.hair.guides: extraction plus normalized weights and translated interpolation") {
    auto source = grown(24, 5, 9);
    REQUIRE(source.ok());
    auto guides = extractGuides(source.value(), 0.25f);
    REQUIRE(guides.ok());
    REQUIRE(guides.value().curveCount() > 0u);
    CHECK(guides.value().curveCount() < source.value().curveCount());
    auto weights = buildGuideWeights(source.value(), guides.value(), 3, InterpolationMode::Offset);
    REQUIRE(weights.ok());
    REQUIRE_EQ(weights.value().size(), source.value().curveCount());
    float sum = 0.f;
    for (int i = 0; i < weights.value()[0].count; ++i) sum += weights.value()[0].influencers[i].weight;
    CHECK(std::abs(sum - 1.f) < 0.01f);
    auto                     shifted = guides.value();
    std::vector<StrandPoint> translated(shifted.points().begin(), shifted.points().end());
    for (auto& point : translated) point.position.x += 0.1f;
    shifted.setPoints(std::move(translated));
    // Rigid interpolation follows guide translation at the roots.
    {
        auto output =
            interpolateStrands(source.value(), guides.value(), shifted, weights.value(), InterpolationMode::Rigid);
        REQUIRE(output.ok());
        const float delta = output.value().curvePoints(0)[0].position.x - source.value().curvePoints(0)[0].position.x;
        CHECK(delta > 0.05f);
    }
    // Both alternate interpolation modes retain source topology.
    {
        auto offset =
            interpolateStrands(source.value(), guides.value(), shifted, weights.value(), InterpolationMode::Offset);
        auto smooth =
            interpolateStrands(source.value(), guides.value(), shifted, weights.value(), InterpolationMode::Smooth);
        REQUIRE(offset.ok());
        REQUIRE(smooth.ok());
        CHECK_EQ(offset.value().curveCount(), source.value().curveCount());
        CHECK_EQ(smooth.value().curveCount(), source.value().curveCount());
    }
}

TEST_CASE("graphics.hair.simulation: roots stay pinned while tips fall and invalid dt is rejected") {
    StrandsDatas             guides;
    std::vector<StrandPoint> points;
    for (float rootX : {-0.1f, 0.1f}) {
        for (int index = 0; index < 4; ++index) {
            points.push_back({glm::vec3(rootX, 0.5f + 0.04f * float(index), 0.f), 0.001f, float(index) / 3.f});
        }
    }
    guides.setPoints(std::move(points));
    guides.setCurves({{0u, 4u, 0.12f}, {4u, 4u, 0.12f}});
    REQUIRE(guides.validate().ok());
    GuideSimParams params;
    params.gravity    = glm::vec3(0.f, -20.f, 0.f);
    params.damping    = 0.98f;
    params.iterations = 6;
    params.compliance = 0.f;
    params.collisionY = 0.f;
    GuideSimulator simulator;
    CHECK(!simulator.isReady());
    REQUIRE(simulator.reset(guides, params).ok());
    REQUIRE(simulator.isReady());
    CHECK_EQ(simulator.curveCount(), 2u);
    CHECK_EQ(simulator.particleCount(), 8u);
    CHECK(!simulator.step(0.f).ok());
    CHECK(!simulator.step(-0.1f).ok());
    for (int index = 0; index < 30; ++index) REQUIRE(simulator.step(1.f / 60.f).ok());
    auto state = simulator.snapshot();
    REQUIRE(state.ok());
    CHECK_EQ(state.value().curvePoints(0)[0].position.y, 0.5f);
    CHECK_EQ(state.value().curvePoints(1)[0].position.y, 0.5f);
    CHECK(state.value().curvePoints(0)[3].position.y < 0.48f);
    CHECK(state.value().curvePoints(0)[3].position.y >= -0.001f);
    simulator.clear();
    CHECK(!simulator.isReady());
}
}  // namespace test_hair_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_indirect_builder)
#include "graphics/IndirectBuilder.h"
#include "zeroerr/unittest.h"

#include <vector>

namespace test_indirect_builder {
using namespace eve::graphics;

GpuMeshRecord mesh(uint32_t indices) {
    GpuMeshRecord record{};
    record.indexCount = indices;
    record.indexType  = 1;
    return record;
}

TEST_CASE("graphics.indirectBuilder.groupsMeshMaterialPipelineAndMapsStableInstanceOrder") {
    const std::vector<GpuMeshRecord> meshes{mesh(36u), mesh(72u)};
    IndirectBuilder                  builder;
    builder.add(7u, 0u, 0u, 0u);
    builder.add(3u, 0u, 0u, 0u);
    builder.add(9u, 1u, 0u, 0u);
    builder.add(10u, 0u, 1u, 0u);
    builder.add(11u, 0u, 0u, 1u);
    REQUIRE_EQ(builder.build(meshes), 4u);
    const auto& commands = builder.commands();
    const auto& order    = builder.sortedInstanceOrder();
    REQUIRE_EQ(commands.size(), 4u);
    REQUIRE_EQ(order.size(), 5u);
    CHECK_EQ(commands[0].instanceCount, 2u);
    CHECK_EQ(commands[0].indexCount, 36u);
    CHECK_EQ(commands[0].firstInstance, 0u);
    CHECK_EQ(order[0], 1u);  // sequence 3 precedes sequence 7 in one bucket
    CHECK_EQ(order[1], 0u);
    CHECK_EQ(commands[1].indexCount, 72u);
    CHECK_EQ(commands[2].instanceCount, 1u);
    CHECK_EQ(commands[3].instanceCount, 1u);
}

TEST_CASE("graphics.indirectBuilder.unknownMeshDoesNotProduceCommand") {
    const std::vector<GpuMeshRecord> meshes{mesh(12u)};
    IndirectBuilder                  builder;
    builder.add(0u, 0u, 0u, 0u);
    builder.add(1u, 99u, 0u, 0u);
    REQUIRE_EQ(builder.build(meshes), 1u);
    REQUIRE_EQ(builder.commands().size(), 1u);
    CHECK_EQ(builder.commands()[0].instanceCount, 1u);
    CHECK_EQ(builder.sortedInstanceOrder()[0], 0u);
}
}  // namespace test_indirect_builder
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_reflection_probe_culling)
#include "graphics/ReflectionProbeCapture.h"
#include "graphics/ReflectionProbeRegistry.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_reflection_probe_registry {
using namespace eve::graphics;

TEST_CASE("graphics.reflectionProbeRegistry.registrationDeduplicatesAndRemovalChangesCount") {
    ReflectionProbeRegistry registry;
    ReflectionProbeCapture  probe(nullptr);
    registry.add(&probe);
    registry.add(&probe);
    REQUIRE_EQ(registry.getCount(), 1);
    registry.remove(&probe);
    CHECK_EQ(registry.getCount(), 0);
    registry.add(&probe);
    registry.clear();
    CHECK_EQ(registry.getCount(), 0);
}

TEST_CASE("graphics.reflectionProbeRegistry.distanceQualificationTracksCameraAndRejectsInvalidRadiusAtomically") {
    ReflectionProbeRegistry registry;
    ReflectionProbeCapture  nearProbe(nullptr), farProbe(nullptr);
    nearProbe.configure(3.f, 0.f, 0.f);
    farProbe.configure(30.f, 0.f, 0.f);
    registry.add(&nearProbe);
    registry.add(&farProbe);
    auto* camera = Camera3D::createCamera();
    REQUIRE(camera != nullptr);
    camera->setEye(0.f, 0.f, 0.f);
    camera->setTarget(0.f, 0.f, -1.f);
    REQUIRE(registry.setMaxRenderDistance(10.f).ok());
    registry.setDistanceCullingEnabled(true);
    CHECK_EQ(registry.updateCamera(camera), registry.getLastSelectedCount());
    CHECK_EQ(registry.getLastDistanceCulledCount(), 1);
    camera->setEye(25.f, 0.f, 0.f);
    camera->setTarget(25.f, 0.f, -1.f);
    registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(), 1);
    CHECK(!registry.setMaxRenderDistance(0.f).ok());
    CHECK(!registry.setMaxRenderDistance(std::numeric_limits<float>::infinity()).ok());
    CHECK_EQ(registry.getMaxRenderDistance(), 10.f);
    registry.setDistanceCullingEnabled(false);
    registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(), 0);
    ecs::DestroyEntity(camera);
}
}  // namespace test_reflection_probe_registry
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tree_wind)
#include "graphics/Shader.h"
#include "graphics/TreeWind.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <limits>

namespace test_tree_wind {
using namespace eve::graphics;

void declareCanonicalLayout(Shader& shader) {
    shader.declareVec4("windGlobals");
    shader.declareVec2("windPhaseDistance");
    shader.declareVec3("windFlex");
    shader.declareVec3("windFrequency");
    shader.declareVec2("windSineTime");
    shader.declareVec2("treeDimensions");
}

std::array<float, 16> snapshot(const Shader& shader) {
    std::array<float, 16> values{};
    std::copy_n(shader.pushConstantData(), values.size(), values.begin());
    return values;
}

TEST_CASE("graphics.treeWind.validatedSnapshotScalesFlexAndPublishesPrototypeDimensions") {
    Shader shader;
    declareCanonicalLayout(shader);
    const VegetationWindState state{{1.f, -0.5f, 0.25f}, 0.8f, 0.17f};
    VegetationWindProfile     vegetation;
    vegetation.enabled = true;
    const TreeWindProfile tree{{4.f, 9.f}, 0.35f};
    auto                  base = packVegetationWind(state, vegetation, 2.0);
    REQUIRE(base.ok());
    REQUIRE(applyTreeWind(shader, state, vegetation, tree, 2.0).ok());
    const auto values = snapshot(shader);
    CHECK_EQ(shader.usedFloats(), 16);
    CHECK_EQ(values[0], base.value()[0]);
    CHECK_EQ(values[6], base.value()[6] * tree.bendFactor);
    CHECK_EQ(values[7], base.value()[7] * tree.bendFactor);
    CHECK_EQ(values[8], base.value()[8] * tree.bendFactor);
    CHECK_EQ(values[14], 4.f);
    CHECK_EQ(values[15], 9.f);
}

TEST_CASE("graphics.treeWind.invalidGeometryOrSchemaPreservesAllShaderParameters") {
    Shader shader;
    declareCanonicalLayout(shader);
    const VegetationWindState state{{1.f, 0.f, 0.f}, 1.f, 0.2f};
    VegetationWindProfile     vegetation;
    vegetation.enabled = true;
    TreeWindProfile tree{{2.f, 6.f}, 0.5f};
    REQUIRE(applyTreeWind(shader, state, vegetation, tree, 0.0).ok());
    const auto before  = snapshot(shader);
    tree.widthHeight.y = 0.f;
    CHECK(!applyTreeWind(shader, state, vegetation, tree, 0.0).ok());
    CHECK_EQ(snapshot(shader), before);
    tree = {{2.f, 6.f}, std::numeric_limits<float>::infinity()};
    CHECK(!applyTreeWind(shader, state, vegetation, tree, 0.0).ok());
    CHECK_EQ(snapshot(shader), before);
    tree = {{2.f, 6.f}, 0.5f};
    CHECK(!applyTreeWind(shader, state, vegetation, tree, std::numeric_limits<double>::quiet_NaN()).ok());
    CHECK_EQ(snapshot(shader), before);

    Shader wrongLayout;
    wrongLayout.declareVec4("windGlobals");
    wrongLayout.declareVec2("windPhaseDistance");
    wrongLayout.declareVec3("windFlex");
    wrongLayout.declareVec3("windFrequency");
    wrongLayout.declareVec2("wrongSineTime");
    wrongLayout.declareVec2("treeDimensions");
    const auto wrongBefore = snapshot(wrongLayout);
    CHECK(!applyTreeWind(wrongLayout, state, vegetation, tree, 0.0).ok());
    CHECK_EQ(snapshot(wrongLayout), wrongBefore);
}
}  // namespace test_tree_wind
#endif

#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_MaterialRenderControl)
#include "PrivateTestScope.h"
#include "graphics/GBuffer.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>
#include <string>

namespace test_MaterialRenderControl {
using namespace eve::graphics;

TEST_CASE("Material surface contract") {
    SUB_CASE("Material shading model selects lighting policy and clamps physical inputs") {
        Material material;
        REQUIRE_EQ(material.getShadingModel(), std::string("pbr"));
        material.setShadingModel("unlit");
        CHECK(!material.getReceiveLight());
        material.setShadingModel("hair");
        CHECK(material.isTransparentHair());
        material.setShadingModel("unsupported");
        CHECK_EQ(material.getShadingModel(), std::string("pbr"));
        material.setMetallic(1.5f);
        material.setRoughness(0.01f);
        CHECK_EQ(material.getMetallic(), 1.f);
        CHECK(material.getRoughness() >= 0.04f);
        material.setFloat("outlineWidth", 1.25f);
        CHECK(material.hasParam("outlineWidth"));
        CHECK(std::fabs(material.getFloat("outlineWidth") - 1.25f) < 1e-5f);
    };

    SUB_CASE("Material masked and transparent surfaces retain independent alpha settings") {
        Material material;
        CHECK_EQ(material.getSurfaceMode(), std::string("opaque"));
        material.setSurfaceMode("masked");
        material.setAlphaCutoff(0.37f);
        material.setDoubleSided(true);
        CHECK_EQ(material.getSurfaceMode(), std::string("masked"));
        CHECK(std::fabs(material.getAlphaCutoff() - 0.37f) < 1e-5f);
        CHECK(material.getDoubleSided());
        material.setSurfaceMode("transparent");
        material.setBlendMode("premultiplied");
        material.setAlphaTechnique("dither");
        material.setSortPriority(12);
        CHECK_EQ(material.getBlendMode(), std::string("premultiplied"));
        CHECK_EQ(material.getAlphaTechnique(), std::string("dither"));
        CHECK_EQ(material.getSortPriority(), 12);
    };
}

TEST_CASE("RenderControl feature compilation and lighting contract") {
    SUB_CASE("RenderControl compiles feature dependencies into ordered passes") {
        RenderControl control;
        CHECK(control.supports("gbuffer"));
        CHECK(control.supports("clusteredDeferred"));
        CHECK(!control.supports("deferred"));
        control.compile();
        CHECK(control.isCompiled());
        CHECK(control.hasPass("shadow"));
        CHECK(control.hasPass("forward"));
        CHECK(control.hasPass("gbuffer"));
        int shadow = -1, gbuffer = -1, forward = -1;
        for (int i = 0; i < control.getPassCount(); ++i) {
            if (control.getPassName(i) == "shadow") shadow = i;
            if (control.getPassName(i) == "gbuffer") gbuffer = i;
            if (control.getPassName(i) == "forward") forward = i;
        }
        CHECK(shadow >= 0);
        CHECK(gbuffer > shadow);
        CHECK(forward > gbuffer);
        control.enable("gbufferAlbedo");
        CHECK(control.isEnabled("gbuffer"));
        control.disable("gbuffer");
        CHECK(!control.isEnabled("gbufferAlbedo"));
        CHECK(control.isDirty());
        control.compile();
        CHECK(!control.hasPass("gbuffer"));
    };

    SUB_CASE("RenderControl hybrid request exposes fallback without deferred backend") {
        RenderControl control;
        REQUIRE(control.setLightingMode(LightingMode::Hybrid).ok());
        CHECK(control.isEnabled("clusteredDeferred"));
        CHECK(control.isEnabled("gbuffer"));
        control.compile();
        CHECK(control.getLightingMode() == LightingMode::Hybrid);
        CHECK(control.getEffectiveLightingMode() == LightingMode::ForwardPlus);
        CHECK(control.hasHybridLightingFallback());
        CHECK(!control.hasPass("deferredLighting"));
        CHECK(!control.setLightingMode("unsupported").ok());
        CHECK(control.getLightingMode() == LightingMode::Hybrid);
        REQUIRE(control.setLightingMode("forwardPlus").ok());
        control.compile();
        CHECK(!control.hasHybridLightingFallback());
    };

    SUB_CASE("RenderControl named lighting presets reject unknown names without replacing active preset") {
        RenderControl control;
        REQUIRE(control.applyLightingPreset("mobile").ok());
        CHECK(control.getLightingPreset() == LightingPreset::Mobile);
        CHECK(control.getLightingMode() == LightingMode::ForwardPlus);
        REQUIRE(control.applyLightingPreset(LightingPreset::Ci).ok());
        CHECK(control.getLightingPreset() == LightingPreset::Ci);
        REQUIRE(control.applyLightingPreset("desktop").ok());
        CHECK(control.getLightingPreset() == LightingPreset::Desktop);
        CHECK(!control.applyLightingPreset("unknown").ok());
        CHECK(control.getLightingPreset() == LightingPreset::Desktop);
    };

    SUB_CASE("RenderControl atmosphere and reflection chains enable and release prerequisites") {
        RenderControl control;
        control.enable("fogTemporal");
        control.compile();
        CHECK(control.isEnabled("atmosphere"));
        CHECK(control.isEnabled("volumetricFog"));
        CHECK(control.hasPass("fogMedia"));
        CHECK(control.hasPass("fogTemporal"));
        control.disable("volumetricFog");
        CHECK(!control.isEnabled("fogTemporal"));
        control.enable("reflectionChain");
        CHECK(control.isEnabled("gbuffer"));
        CHECK(control.isEnabled("taa"));
        CHECK(control.isEnabled("ssr"));
        CHECK(!control.isEnabled("msaa"));
        control.disable("reflectionChain");
        CHECK(!control.isEnabled("ssr"));
        CHECK(!control.isEnabled("taa"));
    };
}

TEST_CASE("Graphics probe part and buffer binding contract") {
    SUB_CASE("Camera probe rejects nonpositive extents and clear removes authored state") {
        Camera3D* camera = Camera3D::createCamera();
        REQUIRE(camera != nullptr);
        CHECK(!camera->hasEnvProbe());
        camera->setEnvProbe(1, 2, 3, 4, -5, 6);
        CHECK(!camera->hasEnvProbe());
        camera->setEnvProbe(1, 2, 3, 4, 5, 6);
        CHECK(camera->hasEnvProbe());
        CHECK_EQ(camera->getEnvProbeCenterY(), 2.f);
        CHECK_EQ(camera->getEnvProbeExtentY(), 5.f);
        camera->clearEnvProbe();
        CHECK(!camera->hasEnvProbe());
    };

    SUB_CASE("Renderable parts require a mesh and retain per-part materials until clear") {
        Renderable3D* renderable = Renderable3D::create();
        REQUIRE(renderable != nullptr);
        Material body, hair;
        hair.setShadingModel("hair");
        renderable->setMaterial(&body);
        CHECK(renderable->getMaterial() == &body);
        renderable->setPart(0, "missing", nullptr, &body);
        CHECK_EQ(renderable->getPartCount(), 0);
        Mesh mesh;
        renderable->setPart(0, "body", &mesh, &body);
        renderable->setPart(1, "hair", &mesh, &hair);
        CHECK_EQ(renderable->getPartCount(), 2);
        CHECK_EQ(renderable->getPartName(1), std::string("hair"));
        CHECK(renderable->getPartMaterial(1)->isTransparentHair());
        renderable->clearParts();
        CHECK_EQ(renderable->getPartCount(), 0);
    };

    SUB_CASE("GBuffer exposes only attached targets and clear removes all bindings") {
        GBuffer buffer;
        CHECK(!buffer.isValid());
        Texture depth, normal, albedo, hardware, pbr, emissive;
        buffer.setTargets(128, 96, &depth, &normal, &albedo);
        CHECK(buffer.isValid());
        CHECK_EQ(buffer.getWidth(), 128);
        CHECK(buffer.getBuffer("depth") == &depth);
        CHECK(buffer.getBuffer("hwDepth") == nullptr);
        buffer.setTargets(128, 96, &depth, &normal, &albedo, &hardware, &pbr, &emissive);
        CHECK(buffer.getBuffer("hwDepth") == &hardware);
        CHECK(buffer.getBuffer("pbrParams") == &pbr);
        CHECK(buffer.getBuffer("emissive") == &emissive);
        CHECK(buffer.getBuffer("missing") == nullptr);
        buffer.clear();
        CHECK(!buffer.isValid());
        CHECK(!buffer.hasBuffer("emissive"));
    };
}
}  // namespace test_MaterialRenderControl
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_MeshShaderRasterState)
#include <limits>
#include "PrivateTestScope.h"
#include "graphics/MeshShaderRasterState.h"
namespace test_MeshShaderRasterState {
TEST_CASE("Mesh raster state validation contract") {
    SUB_CASE("Mesh raster snapshot admits overlays and rejects out-of-range enum mask or nonfinite bias") {
        using namespace eve::graphics;
        MeshShaderRasterState state;
        REQUIRE(validateMeshShaderRasterState(state).ok());
        state.depthCompare      = MeshDepthCompare::Always;
        state.colorWriteMask    = 7;
        state.depthBiasConstant = -1;
        CHECK(validateMeshShaderRasterState(state).ok());
        state.colorWriteMask = 16;
        CHECK(!validateMeshShaderRasterState(state).ok());
        state.colorWriteMask = 15;
        state.depthBiasSlope = std::numeric_limits<float>::quiet_NaN();
        CHECK(!validateMeshShaderRasterState(state).ok());
        state.depthBiasSlope = 0;
        state.depthCompare   = static_cast<MeshDepthCompare>(999);
        CHECK(!validateMeshShaderRasterState(state).ok());
    };
}
}  // namespace test_MeshShaderRasterState
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkyTextureResources)
#include <array>
#include "PrivateTestScope.h"
#include "graphics/ShaderResources.h"
namespace test_SkyTextureResources {
TEST_CASE("Shader volume layout contract") {
    SUB_CASE("Shader volume regions halve XYZ at each mip with exact HDR byte offsets") {
        using namespace eve::graphics;
        std::array<std::byte, 296> bytes{};
        ShaderImageInput           image;
        image.dimension = ShaderImageDimension::Image3D;
        image.format    = ShaderImageFormat::RGBA16Float;
        image.width     = 2;
        image.height    = 4;
        image.depth     = 4;
        image.mipLevels = 3;
        image.bytes     = bytes;
        auto regions    = shaderImageRegions(image);
        REQUIRE(regions.ok());
        REQUIRE_EQ(regions.value().size(), 3u);
        CHECK_EQ(regions.value()[0].size, 256u);
        CHECK_EQ(regions.value()[1].offset, 256u);
        CHECK_EQ(regions.value()[1].depth, 2u);
        CHECK_EQ(regions.value()[2].offset, 288u);
        CHECK_EQ(regions.value()[2].size, 8u);
        image.bytes = std::span(bytes).first(295);
        CHECK(!shaderImageRegions(image).ok());
    };
    SUB_CASE("Shader volume rejects array layers compressed format and oversized extents") {
        using namespace eve::graphics;
        std::array<std::byte, 120> bytes{};
        ShaderImageInput           image;
        image.dimension = ShaderImageDimension::Image3D;
        image.format    = ShaderImageFormat::RGBA16Unorm;
        image.width     = 1;
        image.height    = 1;
        image.depth     = 8;
        image.mipLevels = 4;
        image.bytes     = bytes;
        REQUIRE(shaderImageRegions(image).ok());
        image.layers = 2;
        CHECK(!shaderImageRegions(image).ok());
        image.layers = 1;
        image.format = ShaderImageFormat::BC3;
        CHECK(!shaderImageRegions(image).ok());
        image.format = ShaderImageFormat::RGBA32Float;
        image.width = image.height = image.depth = 2048;
        CHECK(!shaderImageRegions(image).ok());
    };
}
}  // namespace test_SkyTextureResources
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_TextureCellBomb)
#include <cmath>
#include "PrivateTestScope.h"
#include "graphics/TextureCellBomb.h"
namespace test_TextureCellBomb_unit {
TEST_CASE("Texture cell transformation contract") {
    SUB_CASE("Texture cell hash is deterministic and bounded for signed grid coordinates") {
        for (int y = -3; y <= 5; ++y)
            for (int x = -3; x <= 5; ++x) {
                float a = 0, b = 0, againA = 0, againB = 0;
                eve::graphics::texBombHash22(float(x), float(y), a, b);
                eve::graphics::texBombHash22(float(x), float(y), againA, againB);
                REQUIRE_GE(a, 0.f);
                CHECK_LT(a, 1.f);
                CHECK_GE(b, 0.f);
                CHECK_LT(b, 1.f);
                CHECK_EQ(a, againA);
                CHECK_EQ(b, againB);
            }
    };
    SUB_CASE("Texture cell bomb zero strength is identity and strength perturbs adjacent cells differently") {
        eve::graphics::TexCellBombParams parameters;
        parameters.cellScale = 2;
        parameters.strength  = 0;
        parameters.rotAmount = 1;
        float u = 0, v = 0;
        eve::graphics::texCellBombSampleUV(0.1f, 0.1f, parameters, u, v);
        CHECK(std::fabs(u - 0.1f) < 1e-6f);
        CHECK(std::fabs(v - 0.1f) < 1e-6f);
        parameters.strength = 1;
        float aU = 0, aV = 0, bU = 0, bV = 0;
        eve::graphics::texCellBombSampleUV(0.1f, 0.1f, parameters, aU, aV);
        eve::graphics::texCellBombSampleUV(0.6f, 0.1f, parameters, bU, bV);
        CHECK(std::fabs((aU - 0.1f) - (bU - 0.6f)) + std::fabs(aV - bV) > 1e-4f);
    };
}
}  // namespace test_TextureCellBomb_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ShaderInstances) || \
    defined(EVE_TEST_ORIGIN_ShaderResources)
#include <array>
#include <cstring>
#include <limits>
#include "PrivateTestScope.h"
#include "graphics/ShaderResources.h"
namespace test_graphics_shader_resources {
TEST_CASE("Shader resource validation contract") {
    SUB_CASE("Shader instance records require exact affine finite invertible matrices") {
        std::array<float, 16> matrix{1, 0, 0, 0, 0, 2, 0, 0, 0, 0, -3, 0, 100, 50, -20, 1};
        auto                  bytes = std::as_bytes(std::span(matrix));
        auto                  valid = eve::graphics::shaderInstanceMatrixCount(bytes);
        REQUIRE(valid.ok());
        CHECK_EQ(valid.value(), 1u);
        CHECK_EQ(eve::graphics::shaderInstanceMatrixCount({}).value(), 0u);
        CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes.first(63)).ok());
        matrix[0] = 0;
        CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
        matrix[0] = 1;
        matrix[3] = 1;
        CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
        matrix[3]  = 0;
        matrix[12] = std::numeric_limits<float>::infinity();
        CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
        matrix[12] = 100;
        std::array<std::byte, 65> unaligned{};
        std::memcpy(unaligned.data() + 1, matrix.data(), 64);
        CHECK(eve::graphics::shaderInstanceMatrixCount(std::span(unaligned).subspan(1)).ok());
    };
    SUB_CASE("Shader compressed array retains block-sized tail mips and layer boundaries") {
        using namespace eve::graphics;
        std::array<std::byte, 160> bytes{};
        ShaderImageInput           image;
        image.width     = 8;
        image.height    = 4;
        image.layers    = 2;
        image.mipLevels = 4;
        image.dimension = ShaderImageDimension::Array2D;
        image.format    = ShaderImageFormat::BC3;
        image.bytes     = bytes;
        auto regions    = shaderImageRegions(image);
        REQUIRE(regions.ok());
        REQUIRE_EQ(regions.value().size(), 8u);
        CHECK_EQ(regions.value()[0].size, 32u);
        CHECK_EQ(regions.value()[3].size, 16u);
        CHECK_EQ(regions.value()[4].offset, 80u);
        CHECK_EQ(regions.value()[7].offset + regions.value()[7].size, bytes.size());
        image.bytes = std::span(bytes).first(159);
        CHECK(!shaderImageRegions(image).ok());
    };
    SUB_CASE("Shader numeric image format preserves R16 byte width and rejects invalid shape") {
        using namespace eve::graphics;
        std::array<std::byte, 4> bytes{};
        ShaderImageInput         image;
        image.width  = 2;
        image.height = 1;
        image.format = ShaderImageFormat::R16;
        image.bytes  = bytes;
        auto regions = shaderImageRegions(image);
        REQUIRE(regions.ok());
        CHECK_EQ(regions.value()[0].size, 4u);
        image.mipLevels = 3;
        CHECK(!shaderImageRegions(image).ok());
        image.mipLevels = 1;
        image.dimension = ShaderImageDimension::Cube;
        CHECK(!shaderImageRegions(image).ok());
        image.dimension = ShaderImageDimension::Image2D;
        image.width     = 0;
        CHECK(!shaderImageRegions(image).ok());
    };
}
}  // namespace test_graphics_shader_resources
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_virtual_texture_blend)
#include <cmath>
#include <memory>
#include "PrivateTestScope.h"
#include "graphics/VirtualTextureBlend.h"
#include "image/ImageData.h"
namespace test_virtual_texture_blend_unit {
namespace {
std::shared_ptr<eve::image::ImageData> solid(float red, float green, float blue, float alpha = 1.f) {
    auto image = std::make_shared<eve::image::ImageData>(4, 4, "RGBA8");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) image->setPixel(x, y, {red, green, blue, alpha});
    return image;
}
}  // namespace
TEST_CASE("Virtual texture blend page and residency contract") {
    SUB_CASE("Virtual texture blend validates config and requires a layer before page requests") {
        using namespace eve::graphics;
        VirtualTextureBlendConfig bad;
        bad.tileSize = 2;
        CHECK(!VirtualTextureBlend::create(bad).ok());
        VirtualTextureBlendConfig valid{8, 8, 4, 1, 8};
        auto                      blend = VirtualTextureBlend::create(valid);
        REQUIRE(blend.ok());
        CHECK_EQ(blend.value()->pageCountX(), 2);
        CHECK_EQ(blend.value()->pageCountY(), 2);
        CHECK(!blend.value()->requestAlbedoPage(0, 0, 0).ok());
    };
    SUB_CASE("Virtual texture blend weights favor high alpha and normalize tangent-space normals") {
        using namespace eve::graphics;
        auto created = VirtualTextureBlend::create({8, 8, 4, 1, 8});
        REQUIRE(created.ok());
        auto                     blend = std::move(created).value();
        VirtualTextureBlendLayer red;
        red.albedo         = solid(1, 0, 0, 0.2f);
        red.normal         = solid(1, 0.5f, 0.5f);
        red.heightContrast = 1;
        VirtualTextureBlendLayer blue;
        blue.albedo         = solid(0, 0, 1, 0.8f);
        blue.normal         = solid(0.5f, 1, 0.5f);
        blue.heightContrast = 1;
        REQUIRE(blend->addLayer(red).ok());
        REQUIRE(blend->addLayer(blue).ok());
        auto albedo = blend->requestAlbedoPage(0, 0, 0);
        REQUIRE(albedo.ok());
        CHECK(albedo.value()->getPixel(2, 2).b > 0.95f);
        auto normal = blend->requestNormalPage(0, 0, 0);
        REQUIRE(normal.ok());
        const auto  encoded = normal.value()->getPixel(2, 2);
        const float x = encoded.r * 2 - 1, y = encoded.g * 2 - 1, z = encoded.b * 2 - 1;
        CHECK(std::fabs(std::sqrt(x * x + y * y + z * z) - 1) < 0.03f);
    };
    SUB_CASE("Virtual texture page receipt survives LRU eviction and atlas rejects duplicate resident coordinates") {
        using namespace eve::graphics;
        auto created = VirtualTextureBlend::create({12, 4, 4, 1, 1});
        REQUIRE(created.ok());
        auto                     blend = std::move(created).value();
        VirtualTextureBlendLayer layer;
        layer.albedo = solid(0.25f, 0.5f, 0.75f);
        REQUIRE(blend->addLayer(layer).ok());
        auto first = blend->requestAlbedoPage(0, 0, 0);
        REQUIRE(first.ok());
        CHECK_EQ(first.value()->getWidth(), 6);
        REQUIRE(blend->requestAlbedoPage(1, 0, 0).ok());
        CHECK_EQ(blend->stats().pageEvictions, 1u);
        CHECK(first.value()->getPixel(0, 0).g > 0.48f);
        CHECK(!blend->buildResidentSet({{0, 0}, {0, 0}}).ok());
        CHECK(!blend->buildResidentSet({{0, 0}, {1, 0}}).ok());
        auto resident = blend->buildResidentSet({{1, 0}});
        REQUIRE(resident.ok());
        CHECK_EQ(resident.value().residentPages, 1u);
        CHECK_EQ(resident.value().pageExtent, 6);
        CHECK(resident.value().pageTable->getPixel(1, 0).b > 0.95f);
        CHECK(resident.value().pageTable->getPixel(0, 0).b < 0.05f);
    };
}
}  // namespace test_virtual_texture_blend_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_RenderImageAudit)
#include <cstdint>
#include <memory>
#include "PrivateTestScope.h"
#include "RenderImageAudit.h"
#include "image/ImageData.h"
namespace test_image_audit {
namespace {
std::unique_ptr<eve::image::ImageData> solid(int width, int height, std::uint8_t red, std::uint8_t green,
                                             std::uint8_t blue) {
    auto  image  = std::make_unique<eve::image::ImageData>(width, height, "RGBA8");
    auto* pixels = static_cast<std::uint8_t*>(image->getData());
    for (int i = 0; i < width * height; ++i) {
        pixels[4 * i]     = red;
        pixels[4 * i + 1] = green;
        pixels[4 * i + 2] = blue;
        pixels[4 * i + 3] = 255;
    }
    return image;
}
void rectangle(eve::image::ImageData& image, int x0, int y0, int width, int height, std::uint8_t red,
               std::uint8_t green, std::uint8_t blue) {
    auto* pixels = static_cast<std::uint8_t*>(image.getData());
    for (int y = y0; y < y0 + height; ++y)
        for (int x = x0; x < x0 + width; ++x) {
            auto* p = pixels + (y * image.getWidth() + x) * 4;
            p[0]    = red;
            p[1]    = green;
            p[2]    = blue;
        }
}
bool contains(const RenderAuditResult& result, RenderDefect::Kind kind) {
    for (const auto& defect : result.defects)
        if (defect.kind == kind) return true;
    return false;
}
}  // namespace
TEST_CASE("Render image audit defect contract") {
    SUB_CASE("Render image audit distinguishes clean occupied render from empty frame") {
        const RenderAuditBg background{0.08f, 0.09f, 0.11f};
        RenderAuditConfig   config{"synthetic", "clean", "", 0};
        auto                filled = solid(64, 64, 180, 180, 185);
        auto                result = auditImage(*filled, config, background);
        REQUIRE(!result.empty);
        CHECK(result.defects.empty());
        CHECK(result.occupancy > 0.9f);
        auto empty = solid(64, 64, 20, 23, 28);
        result     = auditImage(*empty, config, background);
        CHECK(result.empty);
        CHECK(result.hasSevere());
    };
    SUB_CASE("Render image audit reports interior hole and magenta damage") {
        const RenderAuditBg background{0.08f, 0.09f, 0.11f};
        RenderAuditConfig   config{"synthetic", "defect", "", 0};
        auto                hole = solid(64, 64, 180, 180, 185);
        rectangle(*hole, 16, 16, 16, 16, 0, 0, 0);
        CHECK(contains(auditImage(*hole, config, background), RenderDefect::Kind::Incomplete));
        auto damaged = solid(64, 64, 180, 180, 185);
        rectangle(*damaged, 8, 8, 12, 12, 255, 0, 255);
        auto result = auditImage(*damaged, config, background);
        CHECK(contains(result, RenderDefect::Kind::Damage));
        CHECK(result.hasSevere());
    };
    SUB_CASE("Render image audit detects temporal flicker between same-size images") {
        const RenderAuditBg background{0.08f, 0.09f, 0.11f};
        RenderAuditConfig   config{"synthetic", "flicker", "", 0};
        auto                before = solid(64, 64, 180, 180, 185);
        auto                after  = solid(64, 64, 180, 180, 185);
        rectangle(*after, 16, 16, 24, 24, 40, 40, 40);
        CHECK(contains(auditFlicker(*before, *after, config, background), RenderDefect::Kind::Flicker));
    };
}
}  // namespace test_image_audit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_shader_parameters)
#include "graphics/Shader.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <limits>
#include <span>

// Shader's declared slots form a contiguous float ABI. Each case states the
// input block and the complete observable result of a write.

TEST_CASE("graphics.shader.float_block_layoutValidationAndAtomicWriteContract") {
    SUB_CASE("exact declared layout replaces every float") {
        eve::graphics::Shader shader;
        REQUIRE_EQ(shader.declareFloat("opacity"), 0);
        REQUIRE_EQ(shader.declareVec3("color"), 1);
        REQUIRE_EQ(shader.usedFloats(), 4);
        REQUIRE_EQ(shader.pushConstantSize(), 4U * sizeof(float));

        const std::array<float, 4> input{0.5f, 1.f, 2.f, 3.f};
        REQUIRE(shader.sendFloatBlock(input).ok());
        for (std::size_t i = 0; i < input.size(); ++i) CHECK_EQ(shader.pushConstantData()[i], input[i]);

        const std::array<float, 4> replacement{-2.f, 0.f, 0.25f, 9.f};
        REQUIRE(shader.sendFloatBlock(replacement).ok());
        for (std::size_t i = 0; i < replacement.size(); ++i) CHECK_EQ(shader.pushConstantData()[i], replacement[i]);
    };
    SUB_CASE("short and long inputs preserve the last valid block") {
        eve::graphics::Shader shader;
        shader.declareVec2("offset");
        const std::array<float, 2> initial{3.f, 7.f};
        REQUIRE(shader.sendFloatBlock(initial).ok());

        const std::array<float, 1> shortInput{11.f};
        const std::array<float, 3> longInput{11.f, 13.f, 17.f};
        CHECK(!shader.sendFloatBlock(shortInput).ok());
        CHECK(!shader.sendFloatBlock(longInput).ok());
        CHECK_EQ(shader.usedFloats(), 2);
        CHECK_EQ(shader.pushConstantData()[0], 3.f);
        CHECK_EQ(shader.pushConstantData()[1], 7.f);
    };
    SUB_CASE("each nonfinite position is rejected atomically") {
        eve::graphics::Shader shader;
        shader.declareVec3("direction");
        const std::array<float, 3> initial{2.f, 4.f, 8.f};
        REQUIRE(shader.sendFloatBlock(initial).ok());

        for (std::size_t position = 0; position < initial.size(); ++position) {
            for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity()}) {
                auto input      = initial;
                input[position] = invalid;
                CHECK(!shader.sendFloatBlock(input).ok());
                for (std::size_t i = 0; i < initial.size(); ++i) CHECK_EQ(shader.pushConstantData()[i], initial[i]);
            }
        }
    };
    SUB_CASE("empty layout accepts only empty input") {
        eve::graphics::Shader shader;
        CHECK_EQ(shader.usedFloats(), 0);
        CHECK(shader.sendFloatBlock(std::span<const float>{}).ok());
        const std::array<float, 1> nonempty{1.f};
        CHECK(!shader.sendFloatBlock(nonempty).ok());
        CHECK_EQ(shader.pushConstantSize(), 0U);
    };
}
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_virtual_texture_streaming)
#include "graphics/VirtualTextureStreaming.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <memory>
#include <span>

using eve::graphics::VirtualTextureBlendLayer;
using eve::graphics::VirtualTextureStreaming;
using eve::graphics::VirtualTextureStreamingConfig;
using eve::image::ImageData;

namespace {
std::shared_ptr<ImageData> solid() {
    auto image = std::make_shared<ImageData>(8, 8, "RGBA8");
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) image->setPixel(x, y, {0.2f, 0.4f, 0.8f, 1.f});
    return image;
}

VirtualTextureStreamingConfig config(std::size_t capacity = 2, std::size_t budget = 2) {
    VirtualTextureStreamingConfig value{};
    value.blend.width                = 8;
    value.blend.height               = 8;
    value.blend.tileSize             = 4;
    value.blend.borderSize           = 1;
    value.blend.residentPageCapacity = capacity;
    value.maxDecodesPerUpdate        = budget;
    return value;
}

std::unique_ptr<VirtualTextureStreaming> virtualTextureSession(std::size_t capacity = 2, std::size_t budget = 2) {
    auto created = VirtualTextureStreaming::create(config(capacity, budget));
    REQUIRE(created.ok());
    auto                     result = std::move(created).takeValue();
    VirtualTextureBlendLayer layer{};
    layer.albedo = solid();
    REQUIRE(result->addLayer(layer).ok());
    REQUIRE(result->initializePhysicalCache().ok());
    return result;
}

void publish(VirtualTextureStreaming& streaming) {
    REQUIRE(streaming.update().ok());
    auto batch = streaming.takePendingUploads();
    REQUIRE(batch.ok());
    REQUIRE(streaming.commitPublish(batch.value().expectedGeneration).ok());
}
}  // namespace

TEST_CASE("Virtual texture streaming feedback validation contract") {
    SUB_CASE("virtual texture contract: configuration and initialization gate feedback") {
        auto bad                = config();
        bad.maxDecodesPerUpdate = 0;
        REQUIRE(!VirtualTextureStreaming::create(bad).ok());
        bad                  = config();
        bad.feedbackCapacity = 0;
        REQUIRE(!VirtualTextureStreaming::create(bad).ok());
        auto created = VirtualTextureStreaming::create(config());
        REQUIRE(created.ok());
        auto streaming = std::move(created).takeValue();
        REQUIRE(!streaming->recordFeedback(0, 0).ok());
        REQUIRE(!streaming->update().ok());
        VirtualTextureBlendLayer layer{};
        layer.albedo = solid();
        REQUIRE(streaming->addLayer(layer).ok());
        REQUIRE(streaming->initializePhysicalCache().ok());
        REQUIRE_EQ(streaming->generation(), 0u);
        REQUIRE_EQ(streaming->stats().residentPages, 0u);
        REQUIRE(!streaming->takePendingUploads().ok());
    };

    SUB_CASE("virtual texture contract: wire format and coordinates have explicit bounds") {
        for (const auto sample :
             {VirtualTextureStreaming::packFeedback(0, 0, 0), VirtualTextureStreaming::packFeedback(4095, 4095, 255),
              VirtualTextureStreaming::packFeedback(3, 5, 2)}) {
            auto decoded = VirtualTextureStreaming::unpackFeedback(sample);
            REQUIRE(decoded.ok());
            REQUIRE_EQ(VirtualTextureStreaming::packFeedback(decoded.value().pageX, decoded.value().pageY,
                                                             decoded.value().mipLevel),
                       sample);
        }
        auto streaming = virtualTextureSession();
        REQUIRE(streaming->recordFeedback(0, 0).ok());
        REQUIRE(!streaming->recordFeedback(-1, 0).ok());
        REQUIRE(!streaming->recordFeedback(streaming->pageCountX(), 0).ok());
        REQUIRE(!streaming->recordFeedback(0, 0, 1).ok());
        REQUIRE(!streaming->recordFeedbackUv(std::numeric_limits<float>::infinity(), 0).ok());
        REQUIRE(!streaming->recordFeedbackGrid(0, 1).ok());
        REQUIRE_EQ(streaming->stats().feedbackSamples, 1u);
    };
}

TEST_CASE("Virtual texture streaming publication eviction and invalidation contract") {
    SUB_CASE("virtual texture contract: duplicate feedback produces one published mapping") {
        auto streaming = virtualTextureSession();
        REQUIRE(streaming->recordFeedback(0, 0).ok());
        REQUIRE(streaming->recordFeedback(0, 0).ok());
        REQUIRE_EQ(streaming->stats().pendingFeedback, 2u);
        REQUIRE(streaming->update().ok());
        auto batch = streaming->takePendingUploads();
        REQUIRE(batch.ok());
        REQUIRE_EQ(batch.value().albedoRegions.size(), 1u);
        REQUIRE_EQ(batch.value().normalRegions.size(), 1u);
        REQUIRE_EQ(batch.value().pageTableSets.size(), 1u);
        REQUIRE(!streaming->update().ok());
        REQUIRE(!streaming->commitPublish(batch.value().expectedGeneration + 1).ok());
        REQUIRE_EQ(streaming->generation(), 0u);
        REQUIRE(streaming->commitPublish(batch.value().expectedGeneration).ok());
        REQUIRE_EQ(streaming->generation(), 1u);
        REQUIRE_EQ(streaming->stats().residentPages, 1u);
        REQUIRE(streaming->pageTable().getPixel(0, 0).b > 0.95f);
    };

    SUB_CASE("virtual texture contract: abandon restores published mapping and eviction clears the victim") {
        auto streaming = virtualTextureSession(1, 1);
        REQUIRE(streaming->recordFeedback(0, 0).ok());
        publish(*streaming);
        REQUIRE(streaming->recordFeedback(1, 0).ok());
        REQUIRE(streaming->update().ok());
        auto pending = streaming->takePendingUploads();
        REQUIRE(pending.ok());
        REQUIRE_EQ(pending.value().pageTableClears.size(), 1u);
        REQUIRE(streaming->abandonPublish().ok());
        REQUIRE_EQ(streaming->generation(), 1u);
        REQUIRE(streaming->pageTable().getPixel(0, 0).b > 0.95f);
        REQUIRE(!streaming->abandonPublish().ok());
        REQUIRE(streaming->recordFeedback(1, 0).ok());
        publish(*streaming);
        REQUIRE(streaming->pageTable().getPixel(0, 0).b < 0.05f);
        REQUIRE(streaming->pageTable().getPixel(1, 0).b > 0.95f);
        REQUIRE(streaming->stats().pagesEvicted >= 1u);
    };

    SUB_CASE("virtual texture contract: invalidation redecodes published pages on the next update") {
        auto streaming = virtualTextureSession();
        REQUIRE(streaming->recordFeedback(0, 0).ok());
        publish(*streaming);
        const auto decoded = streaming->stats().pagesDecoded;
        REQUIRE(streaming->invalidatePage(0, 0).ok());
        publish(*streaming);
        REQUIRE(streaming->stats().pagesDecoded > decoded);
        REQUIRE(!streaming->invalidatePage(-1, 0).ok());
        REQUIRE(!streaming->invalidateRect(0, 0, std::numeric_limits<float>::quiet_NaN(), 1).ok());
    };

    SUB_CASE("virtual texture contract: higher mip feedback collapses to a mip-zero parent") {
        auto       streaming = virtualTextureSession();
        const auto word      = VirtualTextureStreaming::packFeedback(1, 1, 1);
        REQUIRE(streaming->ingestPackedFeedback(std::span<const std::uint32_t>(&word, 1)).ok());
        publish(*streaming);
        REQUIRE(streaming->pageTable().getPixel(0, 0).b > 0.95f);
    };
}
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_damage_numbers)
#include "graphics/DamageNumbers.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <string>

namespace test_graphics_damage_numbers {
using namespace eve::graphics;

TEST_CASE("graphics.DamageNumbers.spawn documents merge isolation and bounded admission") {
    SUB_CASE("same nonempty group and kind within the window produce one summed label") {
        DamageNumbers numbers;
        const auto    first = numbers.spawn({10, 100, 200, DamageNumberKind::Damage, "enemy"});
        REQUIRE(first.ok());
        REQUIRE_EQ(first.value(), DamageNumberAdmission::Spawned);
        const auto second = numbers.spawn({20, 100, 200, DamageNumberKind::Damage, "enemy"});
        REQUIRE(second.ok());
        REQUIRE_EQ(second.value(), DamageNumberAdmission::Merged);
        REQUIRE_EQ(numbers.count(), 1u);
        REQUIRE_EQ(numbers.snapshot().front().text, std::string("-30"));
    };

    SUB_CASE("kind, group and distance each prevent an otherwise eligible merge") {
        DamageNumbers numbers;
        REQUIRE(numbers.spawn({10, 100, 200, DamageNumberKind::Damage, "enemy"}).ok());
        REQUIRE(numbers.spawn({20, 100, 200, DamageNumberKind::Critical, "enemy"}).ok());
        REQUIRE(numbers.spawn({30, 100, 200, DamageNumberKind::Damage, "other"}).ok());
        REQUIRE(numbers.spawn({40, 900, 200, DamageNumberKind::Damage, "enemy"}).ok());
        REQUIRE_EQ(numbers.count(), 4u);
        REQUIRE(numbers.update(0.2).ok());
        const auto afterWindow = numbers.spawn({50, 100, 200, DamageNumberKind::Damage, "enemy"});
        REQUIRE(afterWindow.ok());
        REQUIRE_EQ(afterWindow.value(), DamageNumberAdmission::Spawned);
    };

    SUB_CASE("a full pool replaces its oldest entry without growing") {
        DamageNumbers numbers;
        for (std::size_t i = 0; i < numbers.capacity(); ++i)
            REQUIRE(numbers.spawn({static_cast<double>(i), 10, 20}).ok());
        const auto overflow = numbers.spawn({999, 10, 20});
        REQUIRE(overflow.ok());
        REQUIRE_EQ(overflow.value(), DamageNumberAdmission::ReplacedOldest);
        REQUIRE_EQ(numbers.count(), numbers.capacity());
        const auto samples = numbers.snapshot();
        REQUIRE_EQ(samples.front().text, std::string("-1"));
        REQUIRE_EQ(samples.back().text, std::string("-999"));
    };
}

TEST_CASE("graphics.DamageNumbers rejects invalid inputs without changing live samples") {
    SUB_CASE("negative and nonfinite elapsed time") {
        DamageNumbers numbers;
        REQUIRE(numbers.spawn({1, 10, 20}).ok());
        const auto before = numbers.snapshot();
        REQUIRE(!numbers.update(-0.01).ok());
        REQUIRE(!numbers.update(std::numeric_limits<double>::quiet_NaN()).ok());
        REQUIRE_EQ(numbers.count(), 1u);
        REQUIRE_EQ(numbers.snapshot().front().y, before.front().y);
    };
    SUB_CASE("negative and nonfinite requested amounts") {
        DamageNumbers numbers;
        REQUIRE(numbers.spawn({1, 10, 20}).ok());
        const auto before = numbers.snapshot();
        REQUIRE(!numbers.spawn({-1, 10, 20}).ok());
        REQUIRE(!numbers.spawn({std::numeric_limits<double>::infinity(), 10, 20}).ok());
        REQUIRE_EQ(numbers.count(), 1u);
        REQUIRE_EQ(numbers.snapshot().front().text, before.front().text);
    };
    SUB_CASE("nonpositive style lifetime") {
        DamageNumbers numbers;
        REQUIRE(numbers.spawn({1, 10, 20}).ok());
        const auto        before = numbers.snapshot();
        DamageNumberStyle invalid;
        invalid.lifetime = 0;
        REQUIRE(!numbers.setStyle(DamageNumberKind::Damage, invalid).ok());
        REQUIRE_EQ(numbers.count(), 1u);
        REQUIRE_EQ(numbers.snapshot().front().y, before.front().y);
    };
}

TEST_CASE("graphics.DamageNumbers uses injected time and copies style at spawn") {
    DamageNumbers a, b;
    REQUIRE(a.spawn({12, 100, 200, DamageNumberKind::Critical}).ok());
    REQUIRE(b.spawn({12, 100, 200, DamageNumberKind::Critical}).ok());
    REQUIRE(a.update(0.25).ok());
    REQUIRE(b.update(0.125).ok());
    REQUIRE(b.update(0.125).ok());
    REQUIRE(std::fabs(a.snapshot().front().y - b.snapshot().front().y) < 1e-4f);

    auto style = a.style(DamageNumberKind::Critical);
    REQUIRE(style.ok());
    style.value().scale = 2.0f;
    REQUIRE(a.setStyle(DamageNumberKind::Critical, style.value()).ok());
    REQUIRE_EQ(a.snapshot().front().scale, b.snapshot().front().scale);
    REQUIRE(a.spawn({13, 100, 200, DamageNumberKind::Critical}).ok());
    REQUIRE(a.snapshot().back().scale > a.snapshot().front().scale);

    REQUIRE(a.update(1.0).ok());
    REQUIRE(a.snapshot().front().alpha < 0.5f);
    REQUIRE(a.update(0.5).ok());
    REQUIRE_EQ(a.count(), 0u);
    a.clear();
    REQUIRE_EQ(a.count(), 0u);
}

}  // namespace test_graphics_damage_numbers
#endif
