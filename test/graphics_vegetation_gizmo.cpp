#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <memory>
#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationMaskPacking.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationNormal.h"
#include "graphics/VegetationPacking.h"
#include "graphics/editing/VegetationFieldGizmo.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// graphics_vegetation_gizmo.cpp
using namespace eve;
using namespace eve::graphics;
using namespace eve::graphics_editing;

TEST_CASE("graphics.vegetation.gizmo_preserves_rotated_shapes_channels_and_revision") {
    VegetationElement box;
    box.channel  = VegetationChannel::Motion;
    box.shape    = VegetationShape::Box;
    box.center   = {1.f, 2.f, 3.f};
    box.extents  = {4.f, 5.f, 6.f};
    box.yaw      = 0.75f;
    box.opacity  = 0.6f;
    box.priority = 2;
    box.mask     = {1, 1, {{1.f, 1.f, 1.f, 1.f}}};

    VegetationElement ellipsoid;
    ellipsoid.channel  = VegetationChannel::Color;
    ellipsoid.shape    = VegetationShape::Ellipsoid;
    ellipsoid.center   = {-2.f, 0.5f, 7.f};
    ellipsoid.extents  = {1.f, 2.f, 3.f};
    ellipsoid.yaw      = -0.25f;
    ellipsoid.priority = 1;

    VegetationField  field;
    const std::array elements{box, ellipsoid};
    REQUIRE(field.replace({}, elements).ok());
    VegetationFieldGizmoBuilder builder;
    auto                        result = builder.build("vegetation-volume", field.revision(), field, 2);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().status, StatusCode::Applied);
    CHECK_EQ(result.value().target, std::string("vegetation-volume"));
    CHECK_EQ(result.value().targetRevision, field.revision());
    REQUIRE_EQ(result.value().primitives.size(), 2u);

    const auto& first = result.value().primitives[0];
    CHECK_EQ(first.kind, std::string("ellipsoid"));
    CHECK_EQ(first.position[0], -2.0);
    CHECK_EQ(first.size[1], 4.0);
    CHECK_EQ(first.yaw, -0.25);
    CHECK_EQ(first.color[1], 0.85);
    CHECK(!first.dashed);

    const auto& second = result.value().primitives[1];
    CHECK_EQ(second.kind, std::string("obb"));
    CHECK_EQ(second.position[2], 3.0);
    CHECK_EQ(second.size[0], 8.0);
    CHECK_EQ(second.yaw, 0.75);
    CHECK_EQ(second.color[2], 1.0);
    CHECK(second.dashed);
}

TEST_CASE("graphics.vegetation.gizmo_rejects_stale_revision_and_budget_without_partial_output") {
    VegetationElement element;
    VegetationField   field;
    REQUIRE(field.replace({}, std::span(&element, 1)).ok());
    VegetationFieldGizmoBuilder builder;

    auto stale = builder.build("vegetation-volume", field.revision() - 1, field);
    CHECK_EQ(stale.code(), StatusCode::Conflict);
    auto overBudget = builder.build("vegetation-volume", field.revision(), field, 0);
    CHECK_EQ(overBudget.code(), StatusCode::Rejected);
}

// graphics_vegetation_mask.cpp
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.mask_orm_uses_green_ao_alpha_smoothness_and_retains_blue") {
    VegetationMask source{2, 1, {glm::vec4(.9f, .2f, .4f, .7f), glm::vec4(.1f, .8f, .6f, .3f)}};
    auto           encoded = packVegetationOrm(source, .5f, .8f);
    REQUIRE(encoded.ok());
    REQUIRE_EQ(encoded.value().width, 2u);
    const auto a = encoded.value().pixels[0];
    REQUIRE(std::abs(a.r - .6f) < 1e-6f);
    REQUIRE(std::abs(a.g - .44f) < 1e-6f);
    REQUIRE_EQ(a.b, 0.f);
    REQUIRE_EQ(a.a, .4f);
    REQUIRE(std::abs(encoded.value().pixels[1].r - .9f) < 1e-6f);
    REQUIRE(std::abs(encoded.value().pixels[1].g - .76f) < 1e-6f);
    auto disabled = packVegetationOrm(source, 0, 0);
    REQUIRE(disabled.ok());
    REQUIRE_EQ(disabled.value().pixels[0], glm::vec4(1, 1, 0, .4f));
    source.pixels[0] = glm::vec4(0);
    REQUIRE_EQ(encoded.value().pixels[0], a);
}
TEST_CASE("graphics.vegetation.mask_orm_rejects_malformed_linear_inputs") {
    VegetationMask source{1, 1, {glm::vec4(1)}};
    REQUIRE(!packVegetationOrm(source, -.1f, 1).ok());
    REQUIRE(!packVegetationOrm(source, 1, 1.1f).ok());
    source.width = 2;
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
    source.width       = 1;
    source.pixels[0].g = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
    source.width  = UINT32_MAX;
    source.height = UINT32_MAX;
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
}

// graphics_vegetation_motion.cpp
using namespace eve::graphics;
namespace {
VegetationMotion isolated() {
    VegetationMotion motion;
    motion.noise   = {1, 1, {glm::vec4(0.5f, 0.5f, 1.f, 1.f)}};
    motion.bending = motion.branch = motion.rolling = motion.flutter = 0.f;
    motion.facing                                                    = 0.f;
    return motion;
}
VegetationField fullWind() {
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {1, 0, 1, 0};
    field.replace(globals, {}).expect("test full wind");
    return field;
}
glm::vec3 evaluate(const VegetationField& field, const VegetationVertex& vertex, const VegetationMotion& motion) {
    auto result = deformVegetation(field, std::span(&vertex, 1), motion);
    REQUIRE(result.ok());
    return {result.value().positions[0], result.value().positions[1], result.value().positions[2]};
}
void near(glm::vec3 actual, glm::vec3 expected, float tolerance = 1e-5f) {
    CHECK(glm::length(actual - expected) < tolerance);
}
}  // namespace

TEST_CASE("graphics.vegetation.motion_bending_rotates_about_pivot") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.bending = 0.25f;
    VegetationVertex v;
    v.pivot    = {2, 0, 3};
    v.position = {2, 2, 3};
    v.bending  = 1.f;
    // Neutral RG noise, full wind: source angle = 0.6 * 2 * amplitude.
    auto actual = evaluate(field, v, motion);
    near(actual, v.pivot + glm::vec3(2.f * std::sin(0.3f), 2.f * std::cos(0.3f), 0));
    CHECK(std::abs(glm::length(actual - v.pivot) - 2.f) < 1e-5f);
}

TEST_CASE("graphics.vegetation.motion_global_wind_is_not_local_interaction_direction") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.bending = 0.25f;
    VegetationElement element;
    element.channel = VegetationChannel::Motion;
    element.value   = {0, 1, 1, 0};
    REQUIRE(field.replace(field.globalValues(), std::span(&element, 1)).ok());
    VegetationVertex v;
    v.position = {0, 1, 0};
    v.bending  = 1;
    near(evaluate(field, v, motion), {std::sin(0.3f), std::cos(0.3f), 0});
}

TEST_CASE("graphics.vegetation.motion_interaction_uses_fourth_power") {
    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {0, 1, 0, 0.5f};
    REQUIRE(field.replace(globals, {}).ok());
    auto             motion = isolated();
    VegetationVertex v;
    v.position        = {0, 1, 0};
    v.bending         = 0.5f;
    const float angle = (1.f / 1.0001f) / 16.f;
    near(evaluate(field, v, motion), {0, std::cos(angle), std::sin(angle)});
}

TEST_CASE("graphics.vegetation.motion_branch_squash_then_rolling") {
    auto field         = fullWind();
    auto motion        = isolated();
    motion.branch      = 0.4f;
    motion.rolling     = 0.3f;
    motion.branchScale = 0;
    motion.time        = 3.141592653589793 / 12.;
    VegetationVertex v;
    v.position     = {1, 2, 0};
    v.branch       = 1;
    v.boundsRadius = 2;
    near(evaluate(field, v, motion), {1.8f * std::cos(0.3f), 2.24f, -1.8f * std::sin(0.3f)});
}

TEST_CASE("graphics.vegetation.motion_flutter_texture_and_distance_fade") {
    auto field     = fullWind();
    auto motion    = isolated();
    motion.flutter = 0.2f;
    motion.noise   = {1, 1, {glm::vec4(1, 0, 0.25f, 1)}};
    VegetationVertex v;
    v.position            = {0, 1, 0};
    v.flutter             = 0.5f;
    const float amplitude = 0.1f * std::pow(0.25f, 0.6f);
    near(evaluate(field, v, motion), v.position + glm::vec3(1, -1, -0.5f) * amplitude);
    motion.camera = {0, 1000, 0};
    near(evaluate(field, v, motion), v.position);
}

TEST_CASE("graphics.vegetation.motion_batched_height_offsets_ignore_object_size") {
    auto field       = fullWind();
    auto globals     = field.globalValues();
    globals.vertex.w = 0;
    REQUIRE(field.replace(globals, {}).ok());
    auto motion      = isolated();
    motion.bending   = 0.25f;
    motion.batchMode = VegetationBatchMode::Batched;
    VegetationVertex v;
    v.position     = {0, 1, 0};
    v.bending      = 0.5f;
    v.boundsHeight = 3;
    near(evaluate(field, v, motion), {0.225f, 1, 0});
    motion.batchMode = VegetationBatchMode::Object;
    near(evaluate(field, v, motion), {0, 0, 0});
}

TEST_CASE("graphics.vegetation.motion_time_override_and_object_transform") {
    auto             field = fullWind();
    VegetationMotion motion;
    VegetationVertex v;
    v.position          = {0.1f, 1, 0.2f};
    v.bending           = 0.8f;
    v.branch            = 0.5f;
    v.flutter           = 0.5f;
    motion.time         = 4.5;
    const auto expected = evaluate(field, v, motion);
    motion.time         = 2;
    motion.timeScale    = 2;
    motion.timeOffset   = 0.5;
    motion.timeOverride = 1;
    near(evaluate(field, v, motion), expected);
    motion               = isolated();
    motion.objectToWorld = glm::translate(glm::mat4(1.f), glm::vec3(100000, 200000, 300000));
    motion.objectToWorld = glm::scale(motion.objectToWorld, glm::vec3(2, 3, 4));
    auto output          = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(output.ok());
    near({output.value().positions[0], output.value().positions[1], output.value().positions[2]},
         {100000.2f, 200003.f, 300000.8f}, 0.05f);
    near({output.value().normals[0], output.value().normals[1], output.value().normals[2]}, {0, 1, 0});
    motion.objectToWorld[1] = glm::vec4(0.f);
    REQUIRE(!deformVegetation(field, std::span(&v, 1), motion).ok());
}

TEST_CASE("graphics.vegetation.packing_retains_masks_bounds_uvs_and_pivots") {
    TvePackedVertex packed;
    packed.color     = {0.3f, 0.4f, 0.5f, 0.6f};
    packed.position  = {1, 2, 3};
    packed.texcoord0 = {0.2f, 0.7f, 1048063.f, 4194303.f};
    packed.texcoord1 = {0.1f, 0.2f, 0.3f, 0.4f};
    packed.texcoord3 = {7, 9, 8, 0};
    auto decoded     = decodeTveVegetationVertices(std::span(&packed, 1));
    REQUIRE(decoded.ok());
    const auto& v = decoded.value()[0];
    near(v.pivot, {7, 8, 9});
    CHECK(v.bending == 0.6f);
    CHECK(v.variation == 0.3f);
    CHECK(v.occlusion == 0.4f);
    CHECK(v.detail == 0.5f);
    CHECK(v.branch == 511.f / 2047.f);
    CHECK(v.flutter == 1535.f / 2047.f);
    CHECK(v.boundsHeight == 100.f);
    CHECK(v.boundsRadius == 100.f);
    CHECK(v.texcoord.x == 0.2f);
    CHECK(v.secondaryTexcoord.y == 0.2f);
    CHECK(v.detailCoord.x == 0.3f);
    packed.texcoord0.z = 1.5f;
    CHECK(!decodeTveVegetationVertices(std::span(&packed, 1)).ok());
    packed.texcoord0.z = 4194304.f;
    CHECK(!decodeTveVegetationVertices(std::span(&packed, 1)).ok());
}

TEST_CASE("graphics.vegetation.motion_invalid_noise_and_arithmetic_overflow") {
    auto             field  = fullWind();
    auto             motion = isolated();
    VegetationVertex v;
    motion.noise.width = 2;
    CHECK(!deformVegetation(field, std::span(&v, 1), motion).ok());
    motion               = isolated();
    v.position           = glm::vec3(std::numeric_limits<float>::max());
    motion.objectToWorld = glm::scale(glm::mat4(1.f), glm::vec3(100.f));
    CHECK(!deformVegetation(field, std::span(&v, 1), motion).ok());
}

TEST_CASE("graphics.vegetation.motion_authored_frame_follows_deformation_and_handedness") {
    auto             field  = fullWind();
    auto             motion = isolated();
    VegetationVertex v;
    v.position = {0, 2, 0};
    v.normal   = {0, 0, 1};
    v.bending  = 1;
    v.tangent  = glm::vec4(1, 0, 0, -1);
    auto rest  = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(rest.ok());
    REQUIRE_EQ(rest.value().tangents.size(), size_t(3));
    near({rest.value().tangents[0], rest.value().tangents[1], rest.value().tangents[2]}, {1, 0, 0}, .001f);
    near({rest.value().bitangents[0], rest.value().bitangents[1], rest.value().bitangents[2]}, {0, -1, 0}, .001f);
    motion.bending = .4f;
    auto bent      = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(bent.ok());
    glm::vec3 t(bent.value().tangents[0], bent.value().tangents[1], bent.value().tangents[2]);
    glm::vec3 b(bent.value().bitangents[0], bent.value().bitangents[1], bent.value().bitangents[2]);
    glm::vec3 n(bent.value().normals[0], bent.value().normals[1], bent.value().normals[2]);
    CHECK(std::abs(glm::dot(n, t)) < .002f);
    CHECK(std::abs(glm::dot(n, b)) < .002f);
    CHECK(glm::dot(glm::cross(t, b), n) < -.99f);
    CHECK(glm::length(t - glm::vec3(1, 0, 0)) + glm::length(b - glm::vec3(0, -1, 0)) > .05f);
    auto repeat = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(repeat.ok());
    REQUIRE_EQ(repeat.value().tangents, bent.value().tangents);
    v.tangent->w = 0;
    REQUIRE(!deformVegetation(field, std::span(&v, 1), motion).ok());
    v.tangent.reset();
    auto absent = deformVegetation(field, std::span(&v, 1), motion);
    REQUIRE(absent.ok());
    REQUIRE(absent.value().tangents.empty());
}

// graphics_vegetation_normal.cpp
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.normal_gpu_matches_filtered_cpu_reference") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]{-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]{.4f, .5f, .4f, .5f, .4f, .5f, .4f, .5f};
    const uint32_t ix[]{0, 2, 1, 0, 3, 2};
    auto*          mesh = gfx->newMeshFromArrays(p, n, uv, 4, ix, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t packed[]{255, 128, 0, 0, 0, 128, 0, 255};
    auto*         texture = gfx->newTexture(2, 1, packed);
    REQUIRE(texture != nullptr);
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.1f, .1f, .1f, 0};
    lighting.lights[0].posRadius = {.7f, .2f, 1, 0};
    lighting.lights[0].color     = {1, 1, 1, 0};
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({0, 0, 2});
    auto render = [&](const PbrSurface& surface) {
        auto drawSurface                               = surface;
        drawSurface.vegetationColor.backfaceNormalMode = PbrVegetationBackfaceNormalMode::Same;
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&drawSurface).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.6f, .6f, .6f, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    float minimum = 1, maximum = 0;
    for (unsigned layout = 0; layout < 3; ++layout)
        for (float strength : {-2.f, 0.f, 1.f, 2.f}) {
            auto decoded = decodeVegetationNormal({.7f, 128.f / 255.f, 0, .3f},
                                                  static_cast<VegetationNormalEncoding>(layout), strength);
            REQUIRE(decoded.ok());
            auto    direction = glm::normalize(decoded.value());
            uint8_t referencePixel[4]{0, 0, 0, 255};
            for (unsigned c = 0; c < 3; ++c)
                referencePixel[c] = uint8_t(std::lround((direction[c] * .5f + .5f) * 255.f));
            auto* referenceTexture = gfx->newTexture(1, 1, referencePixel);
            REQUIRE(referenceTexture != nullptr);
            PbrSurface surface;
            auto&      binding  = surface.textures[size_t(PbrTextureSlot::Normal)];
            binding.texture     = texture;
            binding.srgbDecode  = false;
            binding.magFilter   = 9729;
            binding.minFilter   = 9729;
            surface.normalMode  = static_cast<PbrNormalMode>(layout + 1);
            surface.normalScale = strength;
            REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
            const auto actual   = render(surface);
            minimum             = std::min(minimum, actual.r);
            maximum             = std::max(maximum, actual.r);
            surface.normalMode  = PbrNormalMode::TangentXYZ;
            surface.normalScale = 1;
            binding.texture     = referenceTexture;
            const auto expected = render(surface);
            CHECK(std::abs(actual.r - expected.r) < .015f);
            CHECK(std::abs(actual.g - expected.g) < .015f);
            CHECK(std::abs(actual.b - expected.b) < .015f);
            surface.normalMode = PbrNormalMode::VegetationRG;
            binding.srgbDecode = true;
            REQUIRE(!validatePbrSurface(surface).ok());
            binding.srgbDecode = false;
            binding.texture    = nullptr;
            REQUIRE(!validatePbrSurface(surface).ok());
        }
    CHECK(maximum - minimum > .05f);
    // A material UV mirror/rotation changes sampling, not the source TVE tangent frame.
    const float directionalUv[]{0, 0, 1, 0, 1, 1, 0, 1};
    mesh = gfx->newMeshFromArrays(p, n, directionalUv, 4, ix, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t directionalNormal[]{230, 128, 255, 255};
    auto*         directionalTexture = gfx->newTexture(1, 1, directionalNormal);
    REQUIRE(directionalTexture != nullptr);
    PbrSurface directional;
    directional.normalMode   = PbrNormalMode::VegetationRG;
    auto& normalBinding      = directional.textures[size_t(PbrTextureSlot::Normal)];
    normalBinding.texture    = directionalTexture;
    normalBinding.srgbDecode = false;
    const auto original      = render(directional);
    normalBinding.scale      = {-1, 2};
    const auto mirrored      = render(directional);
    CHECK(std::abs(original.r - mirrored.r) < .005f);
    normalBinding.rotation = 1.57079632679f;
    const auto rotated     = render(directional);
    CHECK(std::abs(original.r - rotated.r) < .005f);
    // The fixture is sensitive to the previous transformed-UV basis.
    normalBinding.rotation      = 0;
    directional.normalMode      = PbrNormalMode::TangentXYZ;
    const auto transformedBasis = render(directional);
    CHECK(std::abs(original.r - transformedBasis.r) > .05f);
    std::vector<float> tangents, bitangents;
    for (unsigned i = 0; i < 4; ++i) {
        tangents.insert(tangents.end(), {-1, 0, 0});
        bitangents.insert(bitangents.end(), {0, 1, 0});
    }
    REQUIRE(mesh->setTangentFrame(tangents, bitangents).ok());
    directional.normalMode = PbrNormalMode::VegetationRG;
    normalBinding.scale    = {1, 1};
    const auto authored    = render(directional);
    CHECK(std::abs(authored.r - transformedBasis.r) < .005f);
    CHECK(std::abs(authored.r - original.r) > .05f);
    auto invalid = tangents;
    invalid[0]   = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!mesh->setTangentFrame(invalid, bitangents).ok());
    REQUIRE_EQ(mesh->importedTangents(), tangents);
    CHECK(std::abs(render(directional).r - authored.r) < .005f);
    REQUIRE(!mesh->setTangentFrame(tangents, tangents).ok());
    REQUIRE(!mesh->setTangentFrame({}, bitangents).ok());
    REQUIRE(mesh->setTangentFrame({}, {}).ok());
    CHECK(std::abs(render(directional).r - original.r) < .005f);
    // An out-of-range UV must use the imported sampler without changing the tangent basis.
    const uint8_t strip[]{40, 128, 255, 255, 90, 128, 255, 255, 180, 128, 255, 255, 230, 128, 255, 255};
    auto*         stripTexture = gfx->newTexture(4, 1, strip);
    REQUIRE(stripTexture != nullptr);
    normalBinding.scale     = {0, 0};
    normalBinding.offset    = {1.375f, .5f};
    normalBinding.minFilter = normalBinding.magFilter = 9728;
    for (const auto wrap : {10497u, 33071u, 33648u}) {
        normalBinding.wrapS            = wrap;
        normalBinding.texture          = stripTexture;
        const auto     sampled         = render(directional);
        const unsigned pixel           = wrap == 10497u ? 1u : wrap == 33071u ? 3u : 2u;
        auto*          expectedTexture = gfx->newTexture(1, 1, strip + pixel * 4);
        REQUIRE(expectedTexture != nullptr);
        normalBinding.texture = expectedTexture;
        const auto expected   = render(directional);
        CHECK(std::abs(sampled.r - expected.r) < .005f);
        CHECK(std::abs(sampled.g - expected.g) < .005f);
        CHECK(std::abs(sampled.b - expected.b) < .005f);
    }
    std::vector<uint8_t> mipBytes;
    for (const auto level : {0, 1, 2}) {
        const unsigned size = 4u >> level;
        for (unsigned pixel = 0; pixel < size * size; ++pixel)
            mipBytes.insert(mipBytes.end(), {uint8_t(level == 2 ? 230 : 40), 128, 255, 255});
    }
    eve::asset_graphics::GraphicsImageFactoryAdapter imageFactory(*gfx);
    auto                                             mipped = imageFactory.uploadRgba8MipChain(4, 4, 3, mipBytes);
    REQUIRE(mipped.ok());
    REQUIRE_EQ(mipped.value()->getMipmapCount(), 3);
    normalBinding.texture   = mipped.value();
    normalBinding.scale     = {1024, 1024};
    normalBinding.offset    = {0, 0};
    normalBinding.minFilter = 9987;
    normalBinding.magFilter = 9729;
    const auto distant      = render(directional);
    normalBinding.texture   = directionalTexture;
    const auto lastLevel    = render(directional);
    CHECK(std::abs(distant.r - lastLevel.r) < .005f);
    CHECK(std::abs(distant.g - lastLevel.g) < .005f);
    CHECK(std::abs(distant.b - lastLevel.b) < .005f);
    normalBinding.texture   = mipped.value();
    normalBinding.scale     = {0, 0};
    normalBinding.minFilter = 9729;
    const auto baseLevel    = render(directional);
    CHECK(std::abs(baseLevel.r - distant.r) > .05f);
    REQUIRE(!imageFactory.uploadRgba8MipChain(4, 4, 2, mipBytes).ok());
    REQUIRE(!imageFactory.uploadRgba8MipChain(4, 4, 3, std::span(mipBytes).first(mipBytes.size() - 1)).ok());
    REQUIRE(!imageFactory.uploadRgba8MipChain(UINT32_MAX, 4, 3, mipBytes).ok());
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    REQUIRE(imageFactory.releaseImage(mipped.value()).ok());
}
TEST_CASE("graphics.vegetation.normal_source_layout_strength_and_unnormalized_z") {
    const glm::vec4 sample(.75f, .25f, .1f, .5f);
    for (float strength : {-8.f, -1.f, 0.f, .2f, 1.f, 8.f}) {
        auto rg  = decodeVegetationNormal(sample, VegetationNormalEncoding::RedGreen, strength);
        auto rag = decodeVegetationNormal(sample, VegetationNormalEncoding::RedAlphaGreen, strength);
        auto ag  = decodeVegetationNormal(sample, VegetationNormalEncoding::AlphaGreen, strength);
        REQUIRE(rg.ok());
        REQUIRE(rag.ok());
        REQUIRE(ag.ok());
        REQUIRE_EQ(rg.value(), glm::vec3(.5f * strength, -.5f * strength, 1));
        REQUIRE_EQ(rag.value(), glm::vec3(-.25f * strength, -.5f * strength, 1));
        REQUIRE_EQ(ag.value(), glm::vec3(0, -.5f * strength, 1));
    }
    // Filtering must happen before the nonlinear R*A decode.
    auto filtered = decodeVegetationNormal(glm::vec4(.5f, .5f, 0, .5f), VegetationNormalEncoding::RedAlphaGreen, 1);
    REQUIRE(filtered.ok());
    REQUIRE_EQ(filtered.value(), glm::vec3(-.5f, 0, 1));
    auto left  = decodeVegetationNormal(glm::vec4(1, .5f, 0, 0), VegetationNormalEncoding::RedAlphaGreen, 1);
    auto right = decodeVegetationNormal(glm::vec4(0, .5f, 0, 1), VegetationNormalEncoding::RedAlphaGreen, 1);
    REQUIRE(left.ok());
    REQUIRE(right.ok());
    REQUIRE_EQ((left.value().x + right.value().x) * .5f, -1.f);
}
TEST_CASE("graphics.vegetation.normal_rejects_invalid_input_without_clamping") {
    const auto layout = VegetationNormalEncoding::RedGreen;
    for (float value : {-8.01f, 8.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        REQUIRE(!decodeVegetationNormal(glm::vec4(.5f), layout, value).ok());
    for (unsigned c = 0; c < 4; ++c)
        for (float value : {-.01f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
            glm::vec4 sample(.5f);
            sample[c] = value;
            REQUIRE(!decodeVegetationNormal(sample, layout, 1).ok());
        }
    REQUIRE(!decodeVegetationNormal(glm::vec4(.5f), static_cast<VegetationNormalEncoding>(99), 1).ok());
}

TEST_CASE("graphics.vegetation.explicit_mip_provider_absence_is_observable") {
    struct BaseOnly final : IImageResourceFactory {
        unsigned              calls = 0;
        eve::Result<Texture*> uploadRgba8(uint32_t, uint32_t, const uint8_t*, bool) override {
            ++calls;
            return eve::Result<Texture*>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "fixture"));
        }
        eve::Result<void> releaseImage(Texture*) override { return eve::Result<void>::success(); }
    } factory;
    const std::array<uint8_t, 4> pixel{128, 128, 255, 255};
    auto                         result = factory.uploadRgba8MipChain(1, 1, 1, pixel);
    REQUIRE(!result.ok());
    REQUIRE_EQ(result.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    REQUIRE_EQ(factory.calls, 0u);
}
