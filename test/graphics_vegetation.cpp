#include <cmath>
#include <limits>
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationFieldGpu.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationRender.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    VegetationField field;
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
    auto* noise = gfx->newTexture(1, 1, noisePixel.data(), true, true);
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
    auto initialize = [](auto& layers, std::uint32_t width, std::uint32_t height, glm::vec3 center,
                         glm::vec3 extent, float marker) {
        for (std::size_t layer = 0; layer < layers.size(); ++layer) {
            layers[layer].width = width;
            layers[layer].height = height;
            layers[layer].center = center;
            layers[layer].extent = extent;
            layers[layer].pixels.assign(std::size_t(width) * height,
                                        glm::vec4(float(layer), marker, .5f, 1.f));
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
    PbrSurface surface;
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
    const auto before = std::vector<float>(mesh.vegetationDeformationFactors().begin(),
                                           mesh.vegetationDeformationFactors().end());
    std::vector<float> invalid{1, 2, 3, 1.1f, .3f, .4f, .5f, 6, 7};
    REQUIRE(!mesh.adoptVegetationDeformationFactors(std::move(invalid)).ok());
    REQUIRE_EQ(std::vector<float>(mesh.vegetationDeformationFactors().begin(),
                                  mesh.vegetationDeformationFactors().end()),
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
    VegetationField   field;
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
