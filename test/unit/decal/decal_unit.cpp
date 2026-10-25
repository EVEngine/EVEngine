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

#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "common/Module.h"
#include "decal/Decal.h"
#include "decal/DecalManager.h"
#include "decal/ProceduralDecal.h"
#include "graphics/RenderControl.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

using namespace eve::decal;

namespace {

int project(DecalManager& manager, const char* kind = "blood", float lifetime = 0.f, float fadeOut = 0.f) {
    return manager.project(1.f, 2.f, 3.f, 0.f, 1.f, 0.f, nullptr, kind, 0.5f, 0.15f, false, 0, 0.f, lifetime, fadeOut,
                           0.f, 0.f, 0.f, 0.f);
}

struct ClearDecals {
    ClearDecals() {
        auto& manager = DecalManager::inst();
        manager.clearAll();
        manager.setLimit("blood", 0);
    }
    ~ClearDecals() {
        auto& manager = DecalManager::inst();
        manager.clearAll();
        manager.setLimit("blood", 0);
    }
};

}  // namespace

TEST_CASE("decal.proceduralDecalPreset and bakeProceduralDecal: valid recipes yield deterministic packed channels") {
    auto preset = proceduralDecalPreset("blood-wet", 1729u);
    REQUIRE(preset.ok());
    preset.value().width  = 32;
    preset.value().height = 24;
    auto first            = bakeProceduralDecal(preset.value());
    auto again            = bakeProceduralDecal(preset.value());
    REQUIRE(first.ok());
    REQUIRE(again.ok());
    CHECK_EQ(first.value().albedo, again.value().albedo);
    CHECK_EQ(first.value().normal, again.value().normal);
    CHECK_EQ(first.value().params, again.value().params);
    REQUIRE_EQ(first.value().albedo.size(), std::size_t{32 * 24 * 4});
    bool hasCoverage = false;
    bool hasHeight   = false;
    for (std::size_t pixel = 0; pixel < first.value().albedo.size(); pixel += 4) {
        hasCoverage |= first.value().albedo[pixel + 3] > 0;
        hasHeight |= first.value().params[pixel + 3] > 0;
        CHECK_EQ(first.value().normal[pixel + 3], first.value().albedo[pixel + 3]);
    }
    CHECK(hasCoverage);
    CHECK(hasHeight);
}

TEST_CASE("decal.proceduralDecalPreset and bakeProceduralDecal: invalid input fails before publishing channels") {
    SUB_CASE("unknown preset is rejected") { CHECK(!proceduralDecalPreset("not-a-preset", 1u).ok()); };

    SUB_CASE("dimensions and schema version are validated") {
        ProceduralDecalRecipe recipe;
        recipe.width = 0;
        CHECK(!bakeProceduralDecal(recipe).ok());
        recipe.width         = 32;
        recipe.schemaVersion = 99;
        CHECK(!bakeProceduralDecal(recipe).ok());
    };

    SUB_CASE("layer scale and contrast must stay within finite supported bounds") {
        ProceduralDecalRecipe recipe;
        recipe.width = recipe.height = 16;
        recipe.layerA.scale          = 0.f;
        CHECK(!bakeProceduralDecal(recipe).ok());
        recipe.layerA.scale    = 1.f;
        recipe.layerA.contrast = std::numeric_limits<float>::max();
        REQUIRE(!bakeProceduralDecal(recipe).ok());
    };
}

TEST_CASE("decal.DecalManager project update remove: each id has an observable lifetime") {
    ClearDecals clean;
    auto*       manager = &DecalManager::inst();

    SUB_CASE("project returns a live id and remove clears only that instance") {
        const int id = project(*manager);
        REQUIRE_GT(id, 0);
        CHECK(manager->contains(id));
        CHECK_EQ(manager->count(), 1);
        CHECK_EQ(manager->instances().front().size, 0.5f);
        CHECK(manager->remove(id));
        CHECK(!manager->contains(id));
        CHECK(!manager->remove(id));
    };

    SUB_CASE("finite lifetime plus fade-out expires while zero lifetime persists") {
        const int temporary  = project(*manager, "temporary", 1.f, 0.5f);
        const int persistent = project(*manager, "persistent");
        manager->update(0.5f);
        CHECK(manager->contains(temporary));
        manager->update(1.2f);
        CHECK(!manager->contains(temporary));
        CHECK(manager->contains(persistent));
        CHECK_EQ(manager->count(), 1);
    };

    SUB_CASE("a per-kind quota evicts the oldest matching id without evicting other kinds") {
        manager->setLimit("blood", 2);
        const int oldest = project(*manager);
        const int newer  = project(*manager);
        const int other  = project(*manager, "dirt");
        const int newest = project(*manager);
        CHECK(!manager->contains(oldest));
        CHECK(manager->contains(newer));
        CHECK(manager->contains(other));
        CHECK(manager->contains(newest));
        CHECK_EQ(manager->count(), 3);
    };
}

TEST_CASE("decal.DecalManager setters: accepted values are visible and invalid ids or modes fail") {
    ClearDecals clean;
    auto*       manager = &DecalManager::inst();
    const int   id      = project(*manager);
    REQUIRE_GT(id, 0);

    SUB_CASE("UV rectangle and blend mode update the instance") {
        CHECK(manager->setUvRect(id, 0.25f, 0.25f, 0.5f, 0.5f));
        CHECK_EQ(manager->instances().front().uvRect[0], 0.25f);
        CHECK_EQ(manager->instances().front().uvRect[3], 0.5f);
        CHECK(manager->setBlend(id, "add"));
        CHECK_EQ(manager->instances().front().blendMode, 1);
        CHECK(!manager->setBlend(id, "unsupported"));
        CHECK(!manager->setUvRect(id + 1000, 0.f, 0.f, 1.f, 1.f));
    };

    SUB_CASE("projection validates mode sharpness and id before changing the instance") {
        CHECK(manager->setProjection(id, "triplanar", 8.f) == DecalProjectionStatus::Applied);
        CHECK_EQ(manager->instances().front().projectionMode, 1);
        CHECK_EQ(manager->instances().front().blendSharpness, 8.f);
        CHECK(manager->setProjection(id, "unknown", 4.f) == DecalProjectionStatus::InvalidMode);
        CHECK(manager->setProjection(id, "triplanar", 0.f) == DecalProjectionStatus::InvalidSharpness);
        CHECK(manager->setProjection(id + 1000, "triplanar", 4.f) == DecalProjectionStatus::UnknownId);
        CHECK_EQ(manager->instances().front().blendSharpness, 8.f);
    };

    SUB_CASE("parallax and edge fade reject out-of-range values without changing accepted values") {
        CHECK(manager->setParallax(id, 0.06f, 8.f, 32.f) == DecalParallaxStatus::Applied);
        CHECK(manager->setParallax(id, -0.1f, 8.f, 32.f) == DecalParallaxStatus::InvalidScale);
        CHECK(manager->setParallax(id, 0.1f, 32.f, 8.f) == DecalParallaxStatus::InvalidLayers);
        CHECK_EQ(manager->instances().front().parallaxScale, 0.06f);
        CHECK(manager->setEdgeFade(id, 0.12f) == DecalEdgeFadeStatus::Applied);
        CHECK(manager->setEdgeFade(id, 0.5f) == DecalEdgeFadeStatus::InvalidWidth);
        CHECK_EQ(manager->instances().front().edgeFadeWidth, 0.12f);
    };
}

TEST_CASE("decal capability and render control: registration exposes queries and enables the required pass") {
    auto* module = eve::ModuleManager::requireInstance<eve::decal::Decal>("Decal");
    REQUIRE(module != nullptr);
    auto* query = eve::cap::query<eve::IDecalQuery>();
    REQUIRE(query != nullptr);
    query->clearAll();
    const int id = query->project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f);
    REQUIRE_GT(id, 0);
    CHECK_EQ(query->count(), 1);
    CHECK(query->remove(id));
    query->clearAll();

    eve::graphics::RenderControl render;
    CHECK(render.supports("decal"));
    CHECK(!render.isEnabled("decal"));
    render.enable("decal");
    CHECK(render.isEnabled("gbuffer"));
    render.compile();
    CHECK(render.hasPass("decal"));
    CHECK_EQ(render.getPassName(1), "gbuffer");
    CHECK_EQ(render.getPassName(2), "decal");
    render.disable("gbuffer");
    CHECK(!render.isEnabled("decal"));
}
