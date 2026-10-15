#include <cmath>
#include <limits>
#include <memory>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "graphics/VegetationDetails.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationFieldGpu.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationRender.h"
#include "graphics/editing/VegetationFieldTarget.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// graphics_vegetation.cpp
using namespace eve::graphics;

TEST_CASE("graphics.vegetation.mesh_highlight_ownership_and_failure") {
    Mesh mesh;
    mesh.gpuVertexCount = 2;
    std::vector<float> values{.2f, .8f};
    ASSERT(mesh.adoptMotionHighlights(std::move(values)).ok());
    ASSERT(values.empty());
    ASSERT(mesh.motionHighlights().size() == 2);
    std::vector<float> invalid{.4f};
    ASSERT(!mesh.adoptMotionHighlights(std::move(invalid)).ok());
    ASSERT(invalid.size() == 1);
    ASSERT(mesh.motionHighlights()[1] == .8f);
    invalid = {0, std::numeric_limits<float>::infinity()};
    ASSERT(!mesh.adoptMotionHighlights(std::move(invalid)).ok());
    ASSERT(mesh.motionHighlights()[0] == .2f);
    invalid = {0, -.1f};
    ASSERT(!mesh.adoptMotionHighlights(std::move(invalid)).ok());
    std::vector<float> clear;
    ASSERT(mesh.adoptMotionHighlights(std::move(clear)).ok());
    ASSERT(mesh.motionHighlights().empty());
    ASSERT(clear.size() == 2);
}

TEST_CASE("graphics.vegetation.extras_gpu_atlas_tracks_revision_and_rejects_half_overflow") {
    using namespace eve::graphics;
    VegetationField   field;
    VegetationGlobals globals;
    globals.extras = glm::vec4(0.5f, -2.f, 3.f, 1.f);
    REQUIRE(field.replace(globals, {}).ok());

    auto* gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    auto atlas = uploadVegetationExtrasAtlas(*gfx, field, {0.13f, 0.f, -0.13f}, {2.f, 1.f, 2.f}, 2, 2);
    REQUIRE(atlas.ok());
    CHECK(atlas.value().texture != nullptr);
    CHECK(atlas.value().fieldRevision == field.revision());
    CHECK(atlas.value().layers == 9);
    CHECK(atlas.value().center.x == 0.f);
    CHECK(atlas.value().center.z == 0.f);
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
    auto motion = uploadVegetationMotionAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1);
    auto vertex = uploadVegetationVertexAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1);
    REQUIRE(motion.ok());
    REQUIRE(vertex.ok());
    CHECK(motion.value().layers == 9);
    CHECK(vertex.value().fieldRevision == field.revision());
    REQUIRE(gfx->releaseTexture(motion.value().texture));
    REQUIRE(gfx->releaseTexture(vertex.value().texture));

    globals.extras.x = 65505.f;
    REQUIRE(field.replace(globals, {}).ok());
    auto overflow = uploadVegetationExtrasAtlas(*gfx, field, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, 1, 1);
    REQUIRE(!overflow.ok());
    CHECK(overflow.error()->code() == eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE("graphics.vegetation.gpu_field_binding_is_coherent_stale_safe_and_atomic") {
    VegetationField   field;
    VegetationGlobals globals;
    REQUIRE(field.replace(globals, {}).ok());
    auto* gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    auto fields = uploadVegetationGpuFieldSet(*gfx, field, {2, 0, -4}, {4, 1, 8}, 1, 1, false);
    REQUIRE(fields.ok());
    const std::array<std::uint8_t, 4> noisePixel{128, 128, 128, 255};
    auto*                             noise = gfx->newTexture(1, 1, noisePixel.data(), true, true);
    REQUIRE(noise != nullptr);
    PbrSurface surface;
    surface.vegetationMotion.bending = .37f;
    VegetationGpuRuntime runtime;
    runtime.motionNoise        = noise;
    runtime.motionDirection    = {.6f, -.8f};
    runtime.worldOrigin        = {10, 2, 30};
    runtime.time               = 7.25;
    runtime.globalBending      = .8f;
    runtime.globalBranch       = .7f;
    runtime.globalFlutter      = .6f;
    runtime.noiseTiling        = 2.5f;
    runtime.motionFadeDistance = 75.f;
    REQUIRE(bindVegetationGpuFields(surface, field, fields.value(), runtime).ok());
    REQUIRE_EQ(surface.vegetationExtras.texture, fields.value().extras.texture);
    REQUIRE_EQ(surface.vegetationColors.texture, fields.value().colors.texture);
    REQUIRE_EQ(surface.vegetationMotion.texture, fields.value().motion.texture);
    REQUIRE_EQ(surface.vegetationVertex.texture, fields.value().vertex.texture);
    REQUIRE_EQ(surface.vegetationExtras.coords[0], .125f);
    REQUIRE_EQ(surface.vegetationExtras.coords[1], .0625f);
    REQUIRE_EQ(surface.vegetationExtras.coords[2], .25f);
    REQUIRE_EQ(surface.vegetationExtras.coords[3], .75f);
    REQUIRE_EQ(surface.vegetationMotion.mode, PbrVegetationMotionMode::Object);
    REQUIRE_EQ(surface.vegetationVertex.source, PbrVegetationDeformationSource::GpuFields);
    REQUIRE_EQ(surface.vegetationMotion.bending, .37f);
    REQUIRE_EQ(surface.vegetationMotion.time, 7.25);

    const auto before = surface;
    globals.season    = 1;
    REQUIRE(field.replace(globals, {}).ok());
    REQUIRE(!bindVegetationGpuFields(surface, field, fields.value(), runtime).ok());
    REQUIRE_EQ(surface.vegetationExtras.texture, before.vegetationExtras.texture);
    REQUIRE_EQ(surface.vegetationMotion.time, before.vegetationMotion.time);
    REQUIRE(releaseVegetationGpuFieldSet(*gfx, fields.value()).ok());
    REQUIRE(fields.value().extras.texture == nullptr);
    REQUIRE(fields.value().colors.texture == nullptr);
    REQUIRE(fields.value().motion.texture == nullptr);
    REQUIRE(fields.value().vertex.texture == nullptr);
    REQUIRE(gfx->releaseTexture(noise));
}

TEST_CASE("graphics.vegetation.prebaked_layers_upload_bind_and_release_atomically") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    std::array<VegetationChannelAtlas, 9> colors, extras, motion, vertex;
    auto initialize = [](auto& layers, std::uint32_t width, std::uint32_t height, glm::vec3 center, glm::vec3 extent,
                         float marker) {
        for (std::size_t layer = 0; layer < layers.size(); ++layer) {
            layers[layer].width  = width;
            layers[layer].height = height;
            layers[layer].center = center;
            layers[layer].extent = extent;
            layers[layer].pixels.assign(std::size_t(width) * height, glm::vec4(float(layer), marker, .5f, 1.f));
        }
    };
    initialize(colors, 1, 1, {2.f, 0.f, -4.f}, {4.f, 1.f, 8.f}, 0.f);
    initialize(extras, 2, 1, {-2.f, 0.f, 4.f}, {2.f, 1.f, 4.f}, 1.f);
    initialize(motion, 1, 2, {1.f, 0.f, 2.f}, {8.f, 1.f, 2.f}, 2.f);
    initialize(vertex, 2, 2, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, 3.f);
    auto fields = uploadVegetationGpuFieldSet(*gfx, colors, extras, motion, vertex, 77);
    REQUIRE(fields.ok());
    CHECK_EQ(fields.value().extras.fieldRevision, std::uint64_t(77));
    CHECK_EQ(fields.value().motion.layers, std::uint32_t(9));
    PbrSurface           surface;
    VegetationGpuRuntime runtime;
    runtime.enableMotion = false;
    REQUIRE(bindVegetationGpuFields(surface, fields.value(), 77, runtime).ok());
    CHECK_EQ(surface.vegetationColors.texture, fields.value().colors.texture);
    CHECK_EQ(fields.value().colors.width, std::uint32_t(1));
    CHECK_EQ(fields.value().extras.width, std::uint32_t(2));
    CHECK_EQ(fields.value().motion.height, std::uint32_t(2));
    CHECK_EQ(fields.value().vertex.width, std::uint32_t(2));
    CHECK_EQ(surface.vegetationColors.coords[0], .125f);
    CHECK_EQ(surface.vegetationExtras.coords[0], .25f);
    CHECK_EQ(surface.vegetationMotion.coords[0], .0625f);
    CHECK_EQ(surface.vegetationVertex.coords[0], .5f);
    const auto before = surface;
    REQUIRE(!bindVegetationGpuFields(surface, fields.value(), 78, runtime).ok());
    CHECK_EQ(surface.vegetationColors.texture, before.vegetationColors.texture);
    REQUIRE(releaseVegetationGpuFieldSet(*gfx, fields.value()).ok());
}

TEST_CASE("graphics.vegetation.deformation_stream_validates_and_publishes_atomically") {
    Mesh mesh;
    mesh.gpuVertexCount = 1;
    std::vector<float> valid{1, 2, 3, .2f, .3f, .4f, .5f, 6, 7};
    REQUIRE(mesh.adoptVegetationDeformationFactors(std::move(valid)).ok());
    const auto before =
        std::vector<float>(mesh.vegetationDeformationFactors().begin(), mesh.vegetationDeformationFactors().end());
    std::vector<float> invalid{1, 2, 3, 1.1f, .3f, .4f, .5f, 6, 7};
    REQUIRE(!mesh.adoptVegetationDeformationFactors(std::move(invalid)).ok());
    REQUIRE_EQ(
        std::vector<float>(mesh.vegetationDeformationFactors().begin(), mesh.vegetationDeformationFactors().end()),
        before);
    std::vector<float> clear;
    REQUIRE(mesh.adoptVegetationDeformationFactors(std::move(clear)).ok());
    REQUIRE(mesh.vegetationDeformationFactors().empty());
}

TEST_CASE("graphics.vegetation.mesh_factor_ownership_and_failure") {
    Mesh mesh;
    mesh.gpuVertexCount = 2;
    std::vector<float> values{.2f, .8f, 1.f, 2.f, -1.f, 1.f, 0.f, .5f, 3.f, 4.f};
    ASSERT(mesh.adoptVegetationFactors(std::move(values)).ok());
    ASSERT(values.empty());
    ASSERT(mesh.vegetationFactors().size() == 10);
    std::vector<float> invalid{.4f, .5f};
    ASSERT(!mesh.adoptVegetationFactors(std::move(invalid)).ok());
    ASSERT(invalid.size() == 2);
    ASSERT(mesh.vegetationFactors()[1] == .8f);
    invalid = {0, 1, 0, std::numeric_limits<float>::infinity(), 0, 0, 1, 0, 0, 0};
    ASSERT(!mesh.adoptVegetationFactors(std::move(invalid)).ok());
    invalid = {0, 1, 1.1f, 0, 0, 0, 1, 0, 0, 0};
    ASSERT(!mesh.adoptVegetationFactors(std::move(invalid)).ok());
    std::vector<float> clear;
    ASSERT(mesh.adoptVegetationFactors(std::move(clear)).ok());
    ASSERT(mesh.vegetationFactors().empty());
    ASSERT(clear.size() == 10);
}

TEST_CASE("graphics.vegetation.layers_and_atomic_replace") {
    VegetationField field;
    ASSERT(field.revision() == 0);
    VegetationElement element;
    element.value  = {0.2f, 0.4f, 0.1f, 0.5f};
    element.layers = 1u << 3;
    ASSERT(field.replace({}, std::span(&element, 1)).ok());
    ASSERT(field.revision() == 1);
    auto defaultLayer = field.sample({0, 0, 0});
    auto selected     = field.sample({0, 0, 0}, {3, 0, 0, 0});
    ASSERT(defaultLayer.ok());
    ASSERT(selected.ok());
    ASSERT(defaultLayer.value().color.x == 1.f);
    ASSERT(selected.value().color.x == 0.2f);
    element.extents.x = 0.f;
    ASSERT(!field.replace({}, std::span(&element, 1)).ok());
    ASSERT(field.revision() == 1);
    auto preserved = field.sample({0, 0, 0}, {3, 0, 0, 0});
    ASSERT(preserved.ok());
    ASSERT(preserved.value().color.x == 0.2f);
    ASSERT(!field.sample({0, 0, 0}, {9, 0, 0, 0}).ok());
}

TEST_CASE("graphics.vegetation.season_wrap_and_mask_ownership") {
    VegetationField   field;
    VegetationElement e;
    e.seasonal = true;
    e.seasons  = {glm::vec4(0.f), glm::vec4(1.f), glm::vec4(2.f), glm::vec4(3.f)};
    e.mask     = {1, 1, {glm::vec4(1.f, 1.f, 1.f, 0.5f)}};
    VegetationGlobals globals;
    globals.season = 3.5f;
    ASSERT(field.replace(globals, std::span(&e, 1)).ok());
    e.mask.pixels[0].a = 0.f;
    auto middle        = field.sample({0, 0, 0});
    ASSERT(middle.ok());
    ASSERT(std::abs(middle.value().color.x - 1.25f) < 1e-6f);
    e.mask         = {};
    globals.season = 4.f;
    ASSERT(field.replace(globals, std::span(&e, 1)).ok());
    auto winter = field.sample({0, 0, 0});
    ASSERT(winter.ok());
    ASSERT(winter.value().color.x == 0.f);
}

TEST_CASE("graphics.vegetation.priority_and_channel_isolation") {
    VegetationField                  field;
    std::array<VegetationElement, 3> elements;
    elements[0].value    = glm::vec4(4.f);
    elements[0].priority = 10;
    elements[1].value    = glm::vec4(2.f);
    elements[1].priority = -10;
    elements[2].channel  = VegetationChannel::Extras;
    elements[2].value    = glm::vec4(0.25f);
    ASSERT(field.replace({}, elements).ok());
    auto value = field.sample({0, 0, 0});
    ASSERT(value.ok());
    ASSERT(value.value().color.x == 4.f);
    ASSERT(value.value().extras.x == 0.25f);
    ASSERT(value.value().motion.z == 0.5f);
    elements[1].priority = 10;
    ASSERT(field.replace({}, elements).ok());
    auto stable = field.sample({0, 0, 0});
    ASSERT(stable.ok());
    ASSERT(stable.value().color.x == 2.f);
}

TEST_CASE("graphics.vegetation.rotated_volume_and_edge") {
    VegetationField   field;
    VegetationElement e;
    e.shape    = VegetationShape::Box;
    e.extents  = {2, 1, 0.5f};
    e.yaw      = 1.57079632679f;
    e.value    = glm::vec4(0.f);
    e.edgeFade = 0.5f;
    ASSERT(field.replace({}, std::span(&e, 1)).ok());
    auto inside  = field.sample({0, 0, 1});
    auto outside = field.sample({1, 0, 0});
    auto edge    = field.sample({0, 0, 1.5f});
    ASSERT(inside.ok());
    ASSERT(outside.ok());
    ASSERT(edge.ok());
    ASSERT(inside.value().color.x == 0.f);
    ASSERT(outside.value().color.x == 1.f);
    ASSERT(std::abs(edge.value().color.x - 0.5f) < 1e-6f);
}

TEST_CASE("graphics.vegetation.atlas_texel_centers_and_snap") {
    VegetationField   field;
    VegetationElement e;
    e.extents = {2, 2, 2};
    e.value   = glm::vec4(0.2f);
    ASSERT(field.replace({}, std::span(&e, 1)).ok());
    auto atlas = field.bake({0.1f, 0, 0.1f}, {2, 1, 2}, 4, 4);
    ASSERT(atlas.ok());
    ASSERT(atlas.value().center.x == 0.f);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x) {
            auto direct = field.sample({float(x) - 1.5f, 0, float(y) - 1.5f});
            ASSERT(direct.ok());
            ASSERT(atlas.value().channels[0][y * 4 + x] == direct.value().color);
        }
    ASSERT(!field.bake({}, {1, 1, 1}, 0, 4).ok());
}

TEST_CASE("graphics.vegetation.wind_anchor_time_and_interaction") {
    VegetationField                 field;
    std::array<VegetationVertex, 2> vertices;
    vertices[1].position = {0, 1, 0};
    vertices[1].bending  = 1.f;
    VegetationMotion motion;
    auto             first = deformVegetation(field, vertices, motion);
    motion.time            = 0.7;
    auto second            = deformVegetation(field, vertices, motion);
    auto replay            = deformVegetation(field, vertices, motion);
    ASSERT(first.ok());
    ASSERT(second.ok());
    ASSERT(replay.ok());
    ASSERT(first.value().positions[0] == 0.f);
    ASSERT(second.value().positions[0] == 0.f);
    ASSERT(std::abs(first.value().positions[3] - second.value().positions[3]) > 1e-5f);
    ASSERT(second.value().positions == replay.value().positions);
    VegetationGlobals globals;
    globals.motion = {0, 1, 0, 1};
    ASSERT(field.replace(globals, {}).ok());
    auto interaction = deformVegetation(field, vertices, motion);
    ASSERT(interaction.ok());
    const float angle = 2.f * (0.9999f / 1.0001f);
    ASSERT(std::abs(interaction.value().positions[5] - std::sin(angle)) < 1e-5f);
    ASSERT(std::abs(interaction.value().positions[4] - std::cos(angle)) < 1e-5f);
}

TEST_CASE("graphics.vegetation.motion_input_failure_and_zero_size") {
    VegetationField  field;
    VegetationVertex vertex;
    vertex.position = {0, 1, 0};
    VegetationGlobals globals;
    globals.vertex.w = 0.f;
    ASSERT(field.replace(globals, {}).ok());
    auto collapsed = deformVegetation(field, std::span(&vertex, 1), {});
    ASSERT(collapsed.ok());
    ASSERT(collapsed.value().positions[1] == 0.f);
    ASSERT(std::isfinite(collapsed.value().normals[1]));
    vertex.flutter = std::numeric_limits<float>::quiet_NaN();
    ASSERT(!deformVegetation(field, std::span(&vertex, 1), {}).ok());
    ASSERT(!field.sample({std::numeric_limits<float>::infinity(), 0, 0}).ok());
}

TEST_CASE("graphics.vegetation.motion_highlight_uses_rest_height_wind_and_fade") {
    VegetationField  field;
    VegetationMotion motion;
    motion.noise = {1, 1, {glm::vec4(.5f, .5f, .5f, .4f)}};
    std::array<VegetationVertex, 3> vertices;
    vertices[0].bending = 0;
    vertices[1].bending = .5f;
    vertices[2].bending = 1;
    auto near           = deformVegetation(field, vertices, motion);
    ASSERT(near.ok());
    ASSERT(near.value().motionHighlights.size() == vertices.size());
    ASSERT(near.value().motionHighlights[0] == 0);
    ASSERT(std::abs(near.value().motionHighlights[1] - .15f) < 1e-6f);
    ASSERT(std::abs(near.value().motionHighlights[2] - .3f) < 1e-6f);
    // The fade samples the rest vertex, not its wind-deformed position or
    // the finite-difference positions used for the normal Jacobian.
    motion.camera  = {0, 0, 75};
    motion.bending = 2;
    auto middle    = deformVegetation(field, vertices, motion);
    ASSERT(middle.ok());
    const float fadeEnd  = motion.fadeDistance + .01f;
    const float expected = .3f * (75.f - fadeEnd) / (-fadeEnd * .5f + .0001f);
    ASSERT(std::abs(middle.value().motionHighlights[2] - expected) < 1e-6f);
    auto repeated = deformVegetation(field, vertices, motion);
    ASSERT(repeated.ok());
    ASSERT(repeated.value().motionHighlights == middle.value().motionHighlights);
    motion.camera = {0, 0, 200};
    auto far      = deformVegetation(field, vertices, motion);
    ASSERT(far.ok());
    ASSERT(far.value().motionHighlights[2] == 0);
    motion.camera = {};
    VegetationGlobals globals;
    globals.motion.z = 0;
    ASSERT(field.replace(globals, {}).ok());
    auto still = deformVegetation(field, vertices, motion);
    ASSERT(still.ok());
    ASSERT(still.value().motionHighlights[2] == 0);
}

TEST_CASE("graphics.vegetation.snapshot_restore_unknown_fields") {
    VegetationField   field;
    VegetationElement e;
    e.value = {0.4f, 0.5f, 0.6f, 0.f};
    ASSERT(field.replace({}, std::span(&e, 1)).ok());
    auto snapshot = field.snapshot();
    ASSERT(snapshot.ok());
    VegetationField restored;
    ASSERT(restored.restore(snapshot.value()).ok());
    auto sample = restored.sample({0, 0, 0});
    ASSERT(sample.ok());
    ASSERT(sample.value().color == e.value);
    snapshot.value().set("version", 2);
    auto future = restored.restore(snapshot.value());
    ASSERT(!future.ok());
    ASSERT(future.error()->code() == eve::DiagnosticCode::UnknownVersion);
    snapshot.value().set("version", 1);
    snapshot.value().set("unexpected", 7);
    ASSERT(!restored.restore(snapshot.value()).ok());
    auto preserved = restored.sample({0, 0, 0});
    ASSERT(preserved.ok());
    ASSERT(preserved.value().color == e.value);
}

TEST_CASE("graphics.vegetation.surface_idempotence_and_failure") {
    Material          material;
    VegetationSample  sample{{0.5f, 1.f, 0.5f, 0.f}, {1, 1, 0, 0.8f}, {}, {}};
    VegetationSurface base;
    base.albedo = {0.5f, 0.8f, 0.2f, 1.f};
    ASSERT(applyVegetationSurface(material, sample, base).ok());
    const float r = material.getTintR();
    ASSERT(applyVegetationSurface(material, sample, base).ok());
    ASSERT(material.getTintR() == r);
    ASSERT(material.getRoughness() == base.roughness);
    ASSERT(material.pbrSurface().vegetationColor.overlay == 0.f);
    ASSERT(material.pbrSurface().vegetationColor.wetness == 1.f);
    ASSERT(material.pbrSurface().vegetationColor.wetnessContrast == .5f);
    ASSERT(material.pbrSurface().vegetationColor.overlaySubsurface == .5f);
    ASSERT(material.pbrSurface().vegetationColor.overlayColor == base.overlayColor);
    ASSERT(material.pbrSurface().vegetationColor.fieldColor == (std::array<float, 4>{.5f, 1.f, .5f, 0.f}));
    ASSERT(material.getTintR() == base.albedo[0]);
    ASSERT(material.getDoubleSided());
    ASSERT(material.getSurfaceMode() == "masked");
    base.roughness = -1.f;
    ASSERT(!applyVegetationSurface(material, sample, base).ok());
    ASSERT(material.getTintR() == r);
    base.roughness         = .5f;
    base.overlaySubsurface = 1.1f;
    ASSERT(!applyVegetationSurface(material, sample, base).ok());
    ASSERT(material.getTintR() == r);
}

// graphics_vegetation_color.cpp
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.color_mask_requires_linear_binding_and_atomic_validity") {
    Material   material;
    PbrSurface surface;
    REQUIRE(material.setPbrSurface(surface).ok());
    surface.colorMaskEnabled = true;
    REQUIRE(!material.setPbrSurface(surface).ok());
    REQUIRE(!material.pbrSurface().colorMaskEnabled);
    surface.colorMaskEnabled = false;
    surface.colorMaskMin     = -.1f;
    surface.colorMaskMax     = .2f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.colorMaskMin          = 0;
    surface.colorMaskMax          = 1;
    surface.albedoTextureStrength = 2;
    REQUIRE(!validatePbrSurface(surface).ok());
}
TEST_CASE("graphics.vegetation.color_mask_remaps_after_gpu_filtering") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {.4f, .5f, .4f, .5f, .4f, .5f, .4f, .5f};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto*          mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t pixels[] = {255, 255, 0, 0, 255, 255, 0, 255};
    auto*         mask     = gfx->newTexture(2, 1, pixels);
    REQUIRE(mask != nullptr);
    PbrSurface surface;
    surface.unlit              = true;
    surface.colorMaskEnabled   = true;
    surface.colorMaskSecondary = {0, 0, 1};
    surface.colorMaskMin       = .25f;
    surface.colorMaskMax       = .75f;
    auto& binding              = surface.textures[size_t(PbrTextureSlot::MetallicRoughness)];
    binding.texture            = mask;
    binding.srgbDecode         = false;
    binding.minFilter          = 9729;
    binding.magFilter          = 9729;
    REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
    auto render = [&](const PbrSurface& current) {
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&current).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(1, 0, 0, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    const auto mixed = render(surface);
    // UV .4 filters the two alpha endpoints to .3; remapping afterwards yields .09998.
    CHECK(std::abs(mixed.r - .1f) < .015f);
    CHECK(std::abs(mixed.b - .9f) < .015f);
    surface.colorMaskMin = .75f;
    surface.colorMaskMax = .25f;
    const auto inverted  = render(surface);
    CHECK(std::abs(inverted.r - .9f) < .015f);
    CHECK(std::abs(inverted.b - .1f) < .015f);
    surface.colorMaskMin     = .25f;
    surface.colorMaskMax     = .75f;
    surface.colorMaskEnabled = false;
    const auto primary       = render(surface);
    CHECK(primary.r > .98f);
    CHECK(primary.b < .02f);
    const uint8_t albedoPixel[] = {64, 255, 255, 255};
    auto*         albedo        = gfx->newTexture(1, 1, albedoPixel);
    REQUIRE(albedo != nullptr);
    surface.textures[0].texture    = albedo;
    surface.textures[0].srgbDecode = false;
    surface.albedoTextureStrength  = .5f;
    const auto halfway             = render(surface);
    CHECK(std::abs(halfway.r - .62549f) < .01f);
    surface.albedoTextureStrength = 0;
    CHECK(render(surface).r > .98f);
    surface.albedoTextureStrength = 1;
    CHECK(std::abs(render(surface).r - 64.f / 255.f) < .01f);
    surface.textures[0].texture = nullptr;
    surface.colorMaskEnabled    = true;
    const auto again            = render(surface);
    CHECK(std::abs(again.r - mixed.r) < .001f);
    CHECK(std::abs(again.b - mixed.b) < .001f);
}
TEST_CASE("graphics.vegetation.global_colors_matches_tve_linear_fragment_formula") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto*          mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    PbrSurface surface;
    surface.unlit                                = true;
    surface.vegetationColor.fieldColor           = {.1f, .2f, .3f, .5f};
    surface.vegetationColor.colorsMask           = 0;
    surface.vegetationColor.colorsVariation      = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.4f, .4f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto pixel = image->getPixel(32, 32);
    CHECK(std::abs(pixel.r - .4f * .1f * 4.594794f) < .015f);
    CHECK(std::abs(pixel.g - .4f * .2f * 4.594794f) < .015f);
    CHECK(std::abs(pixel.b - .4f * .3f * 4.594794f) < .015f);

    surface.vegetationColor.globalColorMaskMinimum = .6f;
    surface.vegetationColor.globalColorMaskMaximum = .8f;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.4f, .4f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> remappedImage(canvas->newImageData());
    REQUIRE(remappedImage != nullptr);
    const auto remapped = remappedImage->getPixel(32, 32);
    CHECK(std::abs(remapped.r - .4f) < .015f);
    CHECK(std::abs(remapped.g - .4f) < .015f);
    CHECK(std::abs(remapped.b - .4f) < .015f);
}
TEST_CASE("graphics.vegetation.detail_layer_matches_tve_replace_formula") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto*          mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t albedoPixel[] = {128, 64, 32, 255};
    const uint8_t normalPixel[] = {128, 128, 255, 255};
    auto*         albedo        = gfx->newTexture(1, 1, albedoPixel);
    auto*         normal        = gfx->newTexture(1, 1, normalPixel);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    PbrSurface surface;
    surface.unlit              = true;
    auto& detail               = surface.vegetationDetail;
    detail.value               = 1;
    detail.blendMode           = 1;
    detail.blendMinimum        = 0;
    detail.blendMaximum        = 0;
    detail.textures[0].texture = albedo;
    detail.textures[1].texture = normal;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.2f, .3f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto pixel = image->getPixel(32, 32);
    auto       srgb  = [](float c) { return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); };
    CHECK(std::abs(pixel.r - srgb(128.f / 255.f)) < .015f);
    CHECK(std::abs(pixel.g - srgb(64.f / 255.f)) < .015f);
    CHECK(std::abs(pixel.b - srgb(32.f / 255.f)) < .015f);
}

// graphics_vegetation_details.cpp
using namespace eve::graphics;

TEST_CASE("graphics.vegetationDetailsProjectsGlobalStateAtomically") {
    PbrSurface baseSurface;
    baseSurface.specularFactor = .25f;
    VegetationMotion baseMotion;
    baseMotion.time = 7;
    VegetationDetailSettings settings;
    settings.colorsLayer          = 2;
    settings.extrasLayer          = 3;
    settings.motionLayer          = 4;
    settings.globalColor          = .6f;
    settings.globalAlpha          = .7f;
    settings.globalOverlay        = .8f;
    settings.globalWetness        = .9f;
    settings.colorMaskMinimum     = .1f;
    settings.colorMaskMaximum     = .2f;
    settings.overlayMaskMinimum   = .3f;
    settings.overlayMaskMaximum   = .4f;
    settings.alphaThreshold       = .75f;
    settings.motionHighlight      = {2, 3, 4};
    settings.bendingAmplitude     = 1.2f;
    settings.bendingSpeed         = 11;
    settings.bendingScale         = 5;
    settings.flutterAmplitude     = .3f;
    settings.flutterSpeed         = 17;
    settings.flutterScale         = 8;
    settings.interactionAmplitude = 1.5f;
    settings.perspectivePush      = 1;
    settings.perspectiveNoise     = 2;
    settings.perspectiveAngle     = 3;
    auto projected                = configureVegetationDetails(baseSurface, baseMotion, settings);
    REQUIRE(projected.ok());
    REQUIRE_EQ(projected.value().surface.vegetationColors.layer, uint32_t(2));
    REQUIRE_EQ(projected.value().surface.vegetationExtras.layer, uint32_t(3));
    REQUIRE_EQ(projected.value().surface.vegetationMotion.layer, uint32_t(4));
    REQUIRE_EQ(projected.value().motion.motionLayer, uint8_t(4));
    REQUIRE_EQ(projected.value().surface.vegetationColor.colorsCoverage, .6f);
    REQUIRE_EQ(projected.value().surface.vegetationAlpha.global, .7f);
    REQUIRE_EQ(projected.value().surface.motionHighlightColor[2], 4.f);
    REQUIRE_EQ(projected.value().surface.vegetationMotion.bending, 1.2f);
    REQUIRE_EQ(projected.value().motion.flutterSpeed, 17.f);
    REQUIRE_EQ(projected.value().motion.interaction, 1.5f);
    REQUIRE_EQ(projected.value().colorMaskMinimum, .1f);
    REQUIRE_EQ(projected.value().overlayMaskMaximum, .4f);
    REQUIRE_EQ(projected.value().alphaThresholdOffset, .25f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalColorMaskMinimum, .1f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalColorMaskMaximum, .2f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalOverlayMaskMinimum, .3f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalOverlayMaskMaximum, .4f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalAlphaThresholdOffset, .25f);
    REQUIRE_EQ(projected.value().surface.specularFactor, .25f);
    REQUIRE_EQ(projected.value().motion.time, 7.0);

    settings.motionLayer = 9;
    REQUIRE(!configureVegetationDetails(baseSurface, baseMotion, settings).ok());
    REQUIRE_EQ(baseSurface.specularFactor, .25f);
    REQUIRE_EQ(baseMotion.time, 7.0);
}

TEST_CASE("graphics.vegetationDetailsPersistenceIsExactAndTransactional") {
    VegetationDetailSettings settings;
    settings.colorsLayer          = 8;
    settings.extrasLayer          = 7;
    settings.motionLayer          = 6;
    settings.globalColor          = .25f;
    settings.overlayMaskMinimum   = .15f;
    settings.overlayMaskMaximum   = .85f;
    settings.motionHighlight      = {2.f, 3.f, 4.f};
    settings.interactionAmplitude = 1.25f;
    auto document                 = snapshotVegetationDetails(settings);
    REQUIRE(document.ok());
    auto restored = restoreVegetationDetails(document.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().colorsLayer, uint8_t(8));
    CHECK_EQ(restored.value().extrasLayer, uint8_t(7));
    CHECK_EQ(restored.value().motionLayer, uint8_t(6));
    CHECK_EQ(restored.value().globalColor, .25f);
    CHECK_EQ(restored.value().overlayMaskMinimum, .15f);
    CHECK_EQ(restored.value().overlayMaskMaximum, .85f);
    CHECK_EQ(restored.value().motionHighlight[2], 4.f);
    CHECK_EQ(restored.value().interactionAmplitude, 1.25f);

    auto unknown                                     = document.value();
    (*unknown.getIf<eve::Value::Object>())["future"] = eve::Value(true);
    CHECK(!restoreVegetationDetails(unknown).ok());
    (*unknown.getIf<eve::Value::Object>()).erase("future");
    (*unknown.getIf<eve::Value::Object>())["version"] = eve::Value(2);
    auto future                                       = restoreVegetationDetails(unknown);
    REQUIRE(!future.ok());
    CHECK_EQ(future.error()->code(), eve::DiagnosticCode::UnknownVersion);

    settings.globalAlpha = 2.f;
    CHECK(!snapshotVegetationDetails(settings).ok());
}

// graphics_vegetation_field_target.cpp
using namespace eve::editing;
using namespace eve::graphics;
using namespace eve::graphics_editing;

namespace {
SelectionSnapshot select(const VegetationFieldTarget& target, const StableId& id) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Custom, target.targetId(), id, "vegetation-field-element"});
    return result;
}
}  // namespace

TEST_CASE("graphics.vegetation.field_target_property_and_gizmo_share_atomic_transform") {
    VegetationField   field;
    VegetationElement first;
    first.priority = 10;
    first.center   = {1.f, 2.f, 3.f};
    VegetationElement second;
    second.priority = -2;
    REQUIRE(field.replace({}, std::vector<VegetationElement>{first, second}).ok());
    auto created = VegetationFieldTarget::create("field", field);
    REQUIRE(created.ok());
    auto&      target = *created.value();
    const auto ids    = target.elementIds();
    REQUIRE_EQ(ids.size(), 2U);
    const auto selection = select(target, ids[0]);

    auto center = target.makeSet(selection, PropertyPath("element.center"), Value::Array{4.0, 5.0, 6.0},
                                 PropertySetMode::Absolute);
    REQUIRE(center.ok());
    auto staged = target.cloneDomainState();
    REQUIRE(staged->applyDomainOperation(center.value()).ok());
    CHECK_EQ(field.revision(), 1U);
    REQUIRE(target.commitDomainState(std::move(staged)).ok());
    CHECK_EQ(field.revision(), 2U);

    auto transform = target.makeTransform(selection, {7.f, 8.f, 9.f}, {2.f, 3.f, 4.f}, 0.75f);
    REQUIRE(transform.ok());
    REQUIRE(target.applyDomainOperation(transform.value()).ok());
    auto elements = field.snapshotElements();
    REQUIRE(elements.ok());
    REQUIRE_EQ(elements.value().size(), 2U);
    CHECK_EQ(elements.value()[0].center.x, 7.f);
    CHECK_EQ(elements.value()[0].extents.z, 4.f);
    CHECK_EQ(elements.value()[0].yaw, 0.75f);

    DomainOperation undo = transform.value();
    undo.payload         = transform.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    elements = field.snapshotElements();
    REQUIRE(elements.ok());
    CHECK_EQ(elements.value()[0].center.x, 4.f);
}

TEST_CASE("graphics.vegetation.field_target_rejects_stale_runtime_and_invalid_extents") {
    VegetationField field;
    REQUIRE(field.replace({}, std::vector<VegetationElement>{VegetationElement{}}).ok());
    auto created = VegetationFieldTarget::create("field", field);
    REQUIRE(created.ok());
    auto&      target    = *created.value();
    const auto selection = select(target, target.elementIds().front());

    auto invalid = target.makeSet(selection, PropertyPath("element.extents"), Value::Array{1.0, 0.0, 1.0},
                                  PropertySetMode::Absolute);
    CHECK(!invalid.ok());
    REQUIRE(field.replace({}, std::vector<VegetationElement>{VegetationElement{}}).ok());
    auto stale = target.makeTransform(selection, {}, {1.f, 1.f, 1.f}, 0.f);
    CHECK(!stale.ok());
    CHECK(!target.currentRevision(selection).ok());
}
