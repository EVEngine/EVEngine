// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "decal.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "RenderImageAudit.h"
#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "decal/Decal.h"
#include "decal/DecalManager.h"
#include "decal/ProceduralDecal.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace eve::decal;
using namespace eve::graphics;

namespace {
[[maybe_unused]] eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g, uint8_t b);
[[maybe_unused]] eve::graphics::Mesh    *makePlane(eve::graphics::Graphics *gfx, float size);
}  // namespace

TEST_CASE("decal.proceduralBakeIsDeterministicAndChannelPacked") {
    auto preset = proceduralDecalPreset("blood-wet", 1729u);
    REQUIRE(preset.ok());
    preset.value().width  = 64;
    preset.value().height = 48;
    auto first            = bakeProceduralDecal(preset.value());
    auto second           = bakeProceduralDecal(preset.value());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().albedo, second.value().albedo);
    CHECK_EQ(first.value().normal, second.value().normal);
    CHECK_EQ(first.value().params, second.value().params);
    REQUIRE_EQ(first.value().albedo.size(), static_cast<std::size_t>(64 * 48 * 4));
    bool hasCoverage        = false;
    bool hasHeight          = false;
    bool hasPerturbedNormal = false;
    for (std::size_t i = 0; i < first.value().albedo.size(); i += 4) {
        hasCoverage        = hasCoverage || first.value().albedo[i + 3] > 0;
        hasHeight          = hasHeight || first.value().params[i + 3] > 0;
        hasPerturbedNormal = hasPerturbedNormal || first.value().normal[i] != 128 || first.value().normal[i + 1] != 128;
        CHECK_EQ(first.value().normal[i + 3], first.value().albedo[i + 3]);
    }
    CHECK(hasCoverage);
    CHECK(hasHeight);
    CHECK(hasPerturbedNormal);
}

TEST_CASE("decal.proceduralPresetsCoverReferenceMaterialFamilies") {
    const char *names[] = {"blood-wet", "blood-dried", "damage", "dirt", "rust",
                           "puddle",    "paint",       "moss",   "mold", "lichen"};
    for (const char *name : names) {
        auto preset = proceduralDecalPreset(name, 7u);
        REQUIRE(preset.ok());
        preset.value().width  = 16;
        preset.value().height = 16;
        auto baked            = bakeProceduralDecal(preset.value());
        REQUIRE(baked.ok());
        CHECK_EQ(baked.value().albedo.size(), static_cast<std::size_t>(16 * 16 * 4));
    }
    auto unknown = proceduralDecalPreset("not-a-preset", 1u);
    CHECK(!unknown.ok());
}

TEST_CASE("decal.proceduralLayerControlsChangeMaterialOutputs") {
    ProceduralDecalRecipe recipe;
    recipe.width            = 32;
    recipe.height           = 32;
    recipe.seed             = 99u;
    recipe.layerA.pattern   = DecalPattern::Puddle;
    recipe.layerA.amount    = 1.f;
    recipe.layerA.blur      = 0.f;
    recipe.layerA.color     = {0.8f, 0.1f, 0.05f};
    recipe.layerA.roughness = 0.15f;
    recipe.layerA.metallic  = 0.1f;
    recipe.layerA.emissive  = 0.35f;
    recipe.layerA.height    = 0.2f;
    recipe.layerB.enabled   = false;
    auto first              = bakeProceduralDecal(recipe);
    REQUIRE(first.ok());

    recipe.layerA.pattern        = DecalPattern::Cracks;
    recipe.layerA.rotation       = 0.7f;
    recipe.layerA.contrast       = 2.2f;
    recipe.layerA.color          = {0.05f, 0.2f, 0.9f};
    recipe.layerA.roughness      = 0.85f;
    recipe.layerA.metallic       = 0.75f;
    recipe.layerA.emissive       = 0.9f;
    recipe.layerA.height         = 0.8f;
    recipe.layerA.normalStrength = 5.f;
    auto second                  = bakeProceduralDecal(recipe);
    REQUIRE(second.ok());

    CHECK(first.value().albedo != second.value().albedo);
    CHECK(first.value().normal != second.value().normal);
    CHECK(first.value().params != second.value().params);
    bool hasEmissive = false;
    for (std::size_t index = 2; index < second.value().params.size(); index += 4)
        hasEmissive = hasEmissive || second.value().params[index] > 0;
    CHECK(hasEmissive);
}


TEST_CASE("decal.proceduralRejectsInvalidRecipeWithoutOutput") {
    ProceduralDecalRecipe recipe;
    recipe.width     = 0;
    auto invalidSize = bakeProceduralDecal(recipe);
    CHECK(!invalidSize.ok());
    recipe.width         = 32;
    recipe.schemaVersion = 99;
    auto invalidVersion  = bakeProceduralDecal(recipe);
    CHECK(!invalidVersion.ok());
    recipe.schemaVersion = ProceduralDecalRecipe::kSchemaVersion;
    recipe.layerA.scale  = 0.f;
    auto invalidScale    = bakeProceduralDecal(recipe);
    CHECK(!invalidScale.ok());
    recipe.layerA.scale = 4097.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.scale = 1.f;
    recipe.layerA.blur  = 2.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.blur           = 0.f;
    recipe.layerA.normalSoftness = 0.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.normalSoftness  = 0.08f;
    recipe.layerA.normalThickness = -1.f;
    CHECK(!bakeProceduralDecal(recipe).ok());
    recipe.layerA.normalThickness = 1.f;
    recipe.layerA.contrast        = std::numeric_limits<float>::max();
    CHECK(!bakeProceduralDecal(recipe).ok());
}


TEST_CASE("decal.managerProjectRemove") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    CHECK(mgr.count() == 0);

    const int id = mgr.project(1.f, 2.f, 3.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f,
                               0.f, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(mgr.count() == 1);
    CHECK(mgr.instances()[0].size == 0.5f);
    CHECK(mgr.instances()[0].ny == 1.f);

    CHECK(mgr.remove(id));
    CHECK(mgr.count() == 0);
    CHECK(!mgr.remove(id));
}

TEST_CASE("decal.managerLifetimeFadeExpiry") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0, 0.1f, 1.f, 0.5f, 0.f, 0.f, 0.f, 0.f);
    mgr.update(0.5f);
    CHECK(mgr.count() == 1);
    CHECK(mgr.instances()[0].age >= 0.5f);
    // age = 0.5 + 1.2 = 1.7 >= lifetime(1) + fadeOut(0.5) -> expired.
    mgr.update(1.2f);
    CHECK(mgr.count() == 0);
}

TEST_CASE("decal.managerPersistentNeverExpires") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    mgr.update(1000.f);
    CHECK(mgr.count() == 1);
}

TEST_CASE("decal.managerKindQuotaEvictsOldest") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.setLimit("blood", 2);
    const int first  = mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f,
                                   0.f, 0.f, 0.f, 0.f);
    const int second = mgr.project(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f,
                                   0.f, 0.f, 0.f, 0.f);
    const int third  = mgr.project(2.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f,
                                   0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.count() == 2);
    // The oldest ("first") was evicted to make room for "third".
    CHECK(!mgr.remove(first));
    CHECK(mgr.remove(second));
    CHECK(mgr.remove(third));
}

TEST_CASE("decal.managerOtherKindNotEvicted") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.setLimit("blood", 1);
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                0.f);
    const int dirt = mgr.project(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "dirt", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f,
                                 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.count() == 2);
    CHECK(mgr.remove(dirt));
}

TEST_CASE("decal.managerAtlasBlendSetters") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    const int id = mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f, 0.f,
                               0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(mgr.setUvRect(id, 0.25f, 0.25f, 0.5f, 0.5f));
    CHECK(mgr.instances()[0].uvRect[0] == 0.25f);
    CHECK(mgr.instances()[0].uvRect[3] == 0.5f);

    CHECK(mgr.setBlend(id, "add"));
    CHECK(mgr.instances()[0].blendMode == 1);
    CHECK(mgr.setBlend(id, "over"));
    CHECK(mgr.instances()[0].blendMode == 0);
    CHECK(!mgr.setBlend(id, "bogus"));  // unknown mode rejected

    CHECK(mgr.setProjection(id, "triplanar", 8.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 1);
    CHECK(mgr.instances()[0].blendSharpness == 8.f);
    CHECK(mgr.setProjection(id, "planar", 4.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 0);
    CHECK(mgr.setProjection(id, "spherical", 4.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 2);
    CHECK(mgr.setProjection(id, "world", 4.f) == DecalProjectionStatus::Applied);
    CHECK(mgr.instances()[0].projectionMode == 3);
    CHECK(mgr.setProjection(id, "bogus", 4.f) == DecalProjectionStatus::InvalidMode);
    CHECK(mgr.setProjection(id, "triplanar", 0.f) == DecalProjectionStatus::InvalidSharpness);
    CHECK(mgr.setProjection(99999, "triplanar", 4.f) == DecalProjectionStatus::UnknownId);
    CHECK(mgr.setParallax(id, 0.06f, 8.f, 32.f) == DecalParallaxStatus::Applied);
    CHECK(mgr.instances()[0].parallaxScale == 0.06f);
    CHECK(mgr.instances()[0].parallaxMinLayers == 8.f);
    CHECK(mgr.instances()[0].parallaxMaxLayers == 32.f);
    CHECK(mgr.setParallax(id, -0.1f, 8.f, 32.f) == DecalParallaxStatus::InvalidScale);
    CHECK(mgr.setParallax(id, 0.1f, 32.f, 8.f) == DecalParallaxStatus::InvalidLayers);
    CHECK(mgr.setParallax(99999, 0.1f, 8.f, 32.f) == DecalParallaxStatus::UnknownId);
    CHECK(mgr.setEdgeFade(id, 0.12f) == DecalEdgeFadeStatus::Applied);
    CHECK(mgr.instances()[0].edgeFadeWidth == 0.12f);
    CHECK(mgr.setEdgeFade(id, 0.5f) == DecalEdgeFadeStatus::InvalidWidth);
    CHECK(mgr.setEdgeFade(99999, 0.1f) == DecalEdgeFadeStatus::UnknownId);

    CHECK(mgr.setTextures(id, nullptr, nullptr));
    CHECK(!mgr.setUvRect(99999, 0.f, 0.f, 1.f, 1.f));  // unknown id
}

TEST_CASE("decal.capabilityProvidedToConsumers") {
    // Instantiate the decal module (registers the drawer + capability).
    auto *const decalModule = eve::ModuleManager::requireInstance<eve::decal::Decal>("Decal");
    REQUIRE(decalModule != nullptr);
    auto *q = eve::cap::query<eve::IDecalQuery>();
    REQUIRE(q != nullptr);
    q->clearAll();
    CHECK(q->count() == 0);
    q->setLimit("blood", 1);
    const int id = q->project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(q->count() == 1);
    CHECK(q->remove(id));
    CHECK(q->count() == 0);
}

TEST_CASE("decal.renderControlFeatureGatesPass") {
    eve::graphics::RenderControl rc;
    CHECK(rc.supports("decal"));
    CHECK(!rc.isEnabled("decal"));

    rc.enable("decal");
    CHECK(rc.isEnabled("decal"));
    CHECK(rc.isEnabled("gbuffer"));  // decal implies the G-buffer (depth/normal inputs)

    rc.compile();
    CHECK(rc.hasPass("decal"));
    // Order: shadow -> gbuffer -> decal -> forward -> hair.
    CHECK(rc.getPassName(1) == "gbuffer");
    CHECK(rc.getPassName(2) == "decal");
    CHECK(rc.getPassName(3) == "forward");

    rc.disable("gbuffer");
    CHECK(!rc.isEnabled("decal"));  // dropping the G-buffer drops decals too
}

namespace {

[[maybe_unused]] eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const int            size = 16;
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i]     = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Mesh *makePlane(eve::graphics::Graphics *gfx, float size) {
    const float                 h   = size * 0.5f;
    const std::vector<float>    pos = {-h, -h, 0.f, h, -h, 0.f, h, h, 0.f, -h, h, 0.f};
    const std::vector<float>    nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const std::vector<float>    uv  = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), 4, idx.data(), 6);
}

}  // namespace

// End-to-end smoke: a red decal projected onto a white plane must change the
// screen-center pixel toward red (the decal layer is written between the
// G-buffer and forward passes, and mesh3d.frag blends it before lighting).


// Emissive decals use the additive layer pipeline: a red glow on a dark plane
// must visibly brighten the screen center vs. the unlit corner.


// Headless verification of the decal pass itself: queue a G-buffer plane + one
// decal, then read the DecalLayer back to CPU. Works without a swapchain, so it
// also runs when the interactive GPU/surface is busy.


// Triplanar mode paints a wall that classic planar projection discards (facing
// nearly orthogonal to the decal forward). Planar leaves the wall near the
// base albedo color.


namespace {

[[maybe_unused]] float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Procedural decal art so the gallery needs no external assets.
[[maybe_unused]] eve::graphics::Texture *makeChecker(eve::graphics::Graphics *gfx, int size, int cells) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool    dark = ((x * cells / size) + (y * cells / size)) % 2 == 0;
            const uint8_t v    = dark ? 150 : 205;
            size_t        i    = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]              = v;
            px[i + 1]          = v;
            px[i + 2]          = v;
            px[i + 3]          = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeBloodSplat(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u   = (float(x) + 0.5f) / float(size);
            const float v   = (float(y) + 0.5f) / float(size);
            const float dx  = u - 0.5f;
            const float dy  = v - 0.5f;
            const float ang = std::atan2(dy, dx);
            const float r   = std::sqrt(dx * dx + dy * dy) * 2.f;
            // Irregular splat edge: three-lobe wobble + inner core.
            const float wob  = 0.72f + 0.22f * std::sin(3.f * ang + 1.3f) + 0.10f * std::sin(5.f * ang + 4.2f);
            const float a    = 1.f - smoothstep(0.30f, wob, r);
            const float core = 1.f - smoothstep(0.0f, 0.34f, r);
            size_t      i    = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]            = 130;
            px[i + 1]        = 14;
            px[i + 2]        = 24;
            px[i + 3]        = uint8_t(std::clamp((a * 0.55f + core * 0.45f) * 255.f, 0.f, 255.f));
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeDirt(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    uint32_t             seed = 12345u;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            seed          = seed * 1664525u + 1013904223u;
            const float n = float(seed % 10000u) / 10000.f;
            const float a = n < 0.62f ? 0.25f + 0.35f * n : 0.f;
            size_t      i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]         = 118;
            px[i + 1]     = 102;
            px[i + 2]     = 82;
            px[i + 3]     = uint8_t(a * 255.f);
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeDentNormal(eve::graphics::Graphics *gfx, int size) {
    // Concave dent: tangent-space normal tilts inward toward the center.
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u    = (float(x) + 0.5f) / float(size);
            const float v    = (float(y) + 0.5f) / float(size);
            const float dx   = (u - 0.5f) * 2.f;
            const float dy   = (v - 0.5f) * 2.f;
            const float r    = std::sqrt(dx * dx + dy * dy);
            float       tilt = 1.6f * std::exp(-r * r * 2.6f);
            // Raised rim just outside the crater.
            tilt -= 0.8f * std::exp(-(r - 0.55f) * (r - 0.55f) * 60.f);
            glm::vec3 n = glm::normalize(glm::vec3(-dx * tilt, -dy * tilt, 1.f));
            size_t    i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]       = uint8_t((n.x * 0.5f + 0.5f) * 255.f);
            px[i + 1]   = uint8_t((n.y * 0.5f + 0.5f) * 255.f);
            px[i + 2]   = uint8_t((n.z * 0.5f + 0.5f) * 255.f);
            px[i + 3]   = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeDentAlbedo(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float   u      = (float(x) + 0.5f) / float(size);
            const float   v      = (float(y) + 0.5f) / float(size);
            const float   dx     = (u - 0.5f) * 2.f;
            const float   dy     = (v - 0.5f) * 2.f;
            const float   r      = std::sqrt(dx * dx + dy * dy);
            const float   crater = 1.f - smoothstep(0.f, 0.55f, r);
            const float   ring   = smoothstep(0.42f, 0.55f, r) * (1.f - smoothstep(0.55f, 0.68f, r));
            const uint8_t val    = uint8_t(std::clamp((68.f + crater * -28.f + ring * 42.f), 0.f, 255.f));
            size_t        i      = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i]                = val;
            px[i + 1]            = uint8_t(val * 0.93f);
            px[i + 2]            = uint8_t(val * 0.88f);
            px[i + 3]            = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

[[maybe_unused]] eve::graphics::Texture *makeRoughParams(eve::graphics::Graphics *gfx, int size, float rough) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i]     = uint8_t(rough * 255.f);  // R = target roughness
        px[i + 1] = 0;                       // G = metallic
        px[i + 2] = 0;                       // B = emissive
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

}  // namespace

// Gallery render: blood / dirt / dent decals projected onto a checker floor,
// saved as PNG for visual review (build/<plat>/test/out/decal/).
