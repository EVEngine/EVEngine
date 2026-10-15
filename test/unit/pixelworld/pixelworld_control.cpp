#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorldControl.h"
#include "pixelworld/PixelMaterialCatalogCodec.h"

#include <utility>

using namespace eve::pixelworld;

TEST_CASE("pixelworld.control_registry_tracks_lifetime_and_move") {
    auto& control = pixelWorldControlService();
    std::uint64_t id = 0;
    {
        PixelWorld original(71);
        id = original.worldLink().world;
        REQUIRE(control.world(id).ok());
        PixelWorld moved(std::move(original));
        auto summary = control.world(id).expect("moved world remains registered");
        CHECK_EQ(summary.seed, std::uint64_t(71));
        CHECK_EQ(summary.link.world, id);
    }
    CHECK(!control.world(id).ok());
}

TEST_CASE("pixelworld.control_pause_and_explicit_step_have_one_authority") {
    PixelWorld world(72);
    const std::uint64_t id = world.worldLink().world;
    auto& control = pixelWorldControlService();
    control.setPaused(id, true).expect("pause world");
    const StepStats paused = world.step();
    CHECK_EQ(paused.tick.value(), std::uint64_t(0));
    CHECK_EQ(world.tickValue(), std::uint64_t(0));

    const auto receipt = control.step(id, 3).expect("explicit paused steps");
    CHECK_EQ(receipt.steps, std::uint32_t(3));
    CHECK_EQ(receipt.firstTick, std::uint64_t(1));
    CHECK_EQ(receipt.lastTick, std::uint64_t(3));
    CHECK_EQ(world.tickValue(), std::uint64_t(3));
    CHECK(world.isPaused());
    CHECK(!control.step(id, 0).ok());
    CHECK(!control.step(id, 1025).ok());
    const auto samples = control.performanceSamples(id, 2).expect("bounded samples");
    REQUIRE_EQ(samples.size(), std::size_t(2));
    CHECK_EQ(samples.front().tick, std::uint64_t(2));
    CHECK_EQ(samples.back().tick, std::uint64_t(3));
    CHECK(!control.performanceSamples(id, 0).ok());
}

TEST_CASE("pixelworld.control_edit_snapshot_and_diagnostics_are_transactional") {
    PixelWorld world(73);
    const std::uint64_t id = world.worldLink().world;
    auto& control = pixelWorldControlService();
    PixelEditCommand paint;
    paint.sequence = 1;
    paint.kind = PixelEditKind::PaintCircle;
    paint.radius = 1;
    paint.material = MaterialId::Water;
    control.applyEdit(id, paint).expect("control paint");

    const auto diagnostics = control.chunkDiagnostics(id, {0, 0, 0, 0})
                                 .expect("bounded diagnostics");
    REQUIRE_EQ(diagnostics.size(), std::size_t(1));
    CHECK_EQ(diagnostics.front().nonAirCells, std::uint32_t(5));
    CHECK_EQ(diagnostics.front().mobileCells, std::uint32_t(5));
    CHECK_EQ(diagnostics.front().minimumTemperature, std::int16_t(20));
    CHECK(!control.chunkDiagnostics(id, {0, 0, 256, 256}).ok());

    const auto snapshot = control.captureSnapshot(id).expect("control snapshot");
    world.setMaterial(8, 8, "stone");
    REQUIRE_EQ(world.getMaterial(8, 8), int(MaterialId::Stone));
    control.restoreSnapshot(id, snapshot).expect("control restore");
    CHECK_EQ(world.getMaterial(8, 8), int(MaterialId::Air));

    auto malformed = snapshot;
    malformed.pop_back();
    const std::uint64_t revision = world.revision();
    CHECK(!control.restoreSnapshot(id, malformed).ok());
    CHECK_EQ(world.revision(), revision);
}

TEST_CASE("pixelworld.control_catalog_reload_reuses_validated_authority_path") {
    PixelWorld world(75);
    auto& control = pixelWorldControlService();
    control.setPaused(world.worldLink().world, true).expect("pause authoring world");
    MaterialCatalog builtIn = MaterialCatalog::builtIn();
    std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(),
                                                 builtIn.definitions().end());
    definitions[std::size_t(MaterialId::Stone)].displayRgba = 0x010203FFU;
    auto candidate = MaterialCatalog::create(
                         std::move(definitions),
                         {builtIn.reactions().begin(), builtIn.reactions().end()},
                         {builtIn.phaseRules().begin(), builtIn.phaseRules().end()})
                         .expect("authoring candidate");
    const std::string json = encodeMaterialCatalogJson(candidate).expect("authoring JSON");
    control.reloadMaterialCatalog(world.worldLink().world, json,
                                  world.materialCatalogFingerprint())
        .expect("control Catalog reload");
    CHECK_EQ(world.materialDisplayRgba(MaterialId::Stone), std::uint32_t(0x010203FFU));
    CHECK(!control.reloadMaterialCatalog(world.worldLink().world, "{}",
                                         world.materialCatalogFingerprint())
               .ok());
}
