// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "pixelworld.cpp",
    "pixelworld_control.cpp",
    "pixelworld_generation.cpp"
  ]
}
*/
// clang-format on

#include "PrivateTestScope.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelworld)
#include "pixelworld/PixelMaterialCatalogCodec.h"
#include "pixelworld/PixelWorld.h"
// pixelworld.cpp
namespace test_pixelworld {
using namespace eve::pixelworld;

namespace {

std::vector<std::byte> withoutThermalRemainders(const std::vector<std::byte>& current) {
    REQUIRE(current.size() >= 50);
    std::uint32_t chunks = 0;
    std::memcpy(&chunks, current.data() + 46, sizeof(chunks));
    std::vector<std::byte> legacy(current.begin(), current.begin() + 50);
    std::size_t            cursor = 50;
    for (std::uint32_t chunk = 0; chunk < chunks; ++chunk) {
        REQUIRE(current.size() - cursor >= 8);
        legacy.insert(legacy.end(), current.begin() + std::ptrdiff_t(cursor),
                      current.begin() + std::ptrdiff_t(cursor + 8));
        cursor += 8;
        for (int cell = 0; cell < kPixelChunkSize * kPixelChunkSize; ++cell) {
            REQUIRE(current.size() - cursor >= 7);
            legacy.insert(legacy.end(), current.begin() + std::ptrdiff_t(cursor),
                          current.begin() + std::ptrdiff_t(cursor + 5));
            cursor += 7;
        }
    }
    REQUIRE_EQ(cursor, current.size());
    return legacy;
}

}  // namespace

TEST_CASE("pixelworld.spec.simulationCellsAndTime") {
    SUB_CASE("sand falls until stone; crossing a chunk boundary preserves one particle") {
        PixelWorld world(11);
        for (int x = 61; x <= 65; ++x) world.setMaterial(x, 65, "stone");
        world.setMaterial(63, 63, "sand");
        for (std::uint64_t tick = 1; tick <= 4; ++tick) REQUIRE(world.advance(eve::SimulationTick(tick)).ok());
        REQUIRE(world.getMaterial(63, 64) == int(MaterialId::Sand));
        REQUIRE(world.getMaterial(63, 65) == int(MaterialId::Stone));
        REQUIRE(world.chunkCount() >= 2);
    };
    SUB_CASE("adjacent fire and water react into steam") {
        PixelWorld world(3);
        world.setMaterial(10, 10, "fire");
        world.setMaterial(11, 10, "water");
        world.setMaterial(10, 11, "stone");
        world.setMaterial(11, 11, "stone");
        world.setMaterial(12, 10, "stone");
        world.setMaterial(12, 11, "stone");
        auto step = world.advance(eve::SimulationTick(1));
        REQUIRE(step.ok());
        REQUIRE(step.value().reactions > 0);
        const bool steam =
            world.getMaterial(11, 10) == int(MaterialId::Steam) || world.getMaterial(11, 9) == int(MaterialId::Steam);
        REQUIRE(steam);
    };
    SUB_CASE("equal seed and edit history produce equal snapshots; repeated tick fails") {
        PixelWorld first(99), second(99);
        for (int x = -8; x <= 8; ++x) {
            first.setMaterial(x, 20, "stone");
            second.setMaterial(x, 20, "stone");
        }
        first.paintCircle(0, 2, 6, "sand");
        second.paintCircle(0, 2, 6, "sand");
        for (std::uint64_t tick = 1; tick <= 12; ++tick) {
            REQUIRE(first.advance(eve::SimulationTick(tick)).ok());
            REQUIRE(second.advance(eve::SimulationTick(tick)).ok());
        }
        auto a = first.saveSnapshot();
        auto b = second.saveSnapshot();
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(a.value() == b.value());
        auto repeated = first.advance(eve::SimulationTick(12));
        REQUIRE(!repeated.ok());
        REQUIRE(repeated.error()->code() == eve::DiagnosticCode::PreconditionViolation);
    };
}

TEST_CASE("pixelworld.spec.snapshotRevisionAndMigration") {
    SUB_CASE("round trip restores seed, tick, and cells; truncated input leaves target unchanged") {
        PixelWorld source(123);
        source.paintCircle(65, -2, 4, "oil");
        source.step();
        auto image = source.saveSnapshot();
        REQUIRE(image.ok());
        PixelWorld target(1);
        REQUIRE(target.restoreSnapshot(image.value()).ok());
        REQUIRE(target.seed() == source.seed());
        REQUIRE(target.tickValue() == source.tickValue());
        REQUIRE(target.getMaterial(65, -1) == source.getMaterial(65, -1));
        auto malformed = image.value();
        malformed.pop_back();
        REQUIRE(!target.restoreSnapshot(malformed).ok());
        REQUIRE(target.seed() == source.seed());
        REQUIRE(target.tickValue() == source.tickValue());
    };
    SUB_CASE("changed chunk snapshots own cells and report only revisions after the cursor") {
        PixelWorld world(17);
        world.setMaterial(70, 2, "sand");
        world.setMaterial(-2, 2, "water");
        const auto cursor  = world.revision();
        auto       initial = world.snapshotChangedChunks(0);
        REQUIRE(initial.size() == 2);
        REQUIRE(initial[0].x < initial[1].x);
        const auto owned = initial[0].cells;
        world.setMaterial(70, 3, "stone");
        auto delta = world.snapshotChangedChunks(cursor);
        REQUIRE(delta.size() == 1);
        REQUIRE(delta[0].x == 1);
        REQUIRE(initial[0].cells == owned);
        const auto beforeLife = world.revision();
        world.setMaterial(0, 0, "fire");
        auto step = world.advance(eve::SimulationTick(1));
        REQUIRE(step.ok());
        REQUIRE(step.value().cellsChanged > 0);
        REQUIRE(world.revision() > beforeLife);
    };
    SUB_CASE("legacy v1, v2, and v3 snapshots migrate with missing state defaulted") {
        PixelWorld source(79);
        source.setCell(2, 3, {MaterialId::Stone, 123, 0, 0});
        auto image = source.saveSnapshot();
        REQUIRE(image.ok());
        auto base = withoutThermalRemainders(image.value());
        auto v3   = base;
        v3[4]     = std::byte{3};
        v3[5]     = std::byte{0};
        PixelWorld old3(1);
        REQUIRE(old3.restoreSnapshot(v3).ok());
        REQUIRE(old3.getCell(2, 3).temperature == 123);
        REQUIRE(old3.getCell(2, 3).thermalRemainder == 0);
        auto v2 = base;
        v2[4]   = std::byte{2};
        v2[5]   = std::byte{0};
        v2.erase(v2.begin() + 38, v2.begin() + 46);
        PixelWorld old2(1);
        REQUIRE(old2.restoreSnapshot(v2).ok());
        REQUIRE(old2.lastEditSequence() == 0);
        auto v1 = v2;
        v1[4]   = std::byte{1};
        v1.erase(v1.begin() + 6, v1.begin() + 14);
        PixelWorld old1(1);
        REQUIRE(old1.restoreSnapshot(v1).ok());
        REQUIRE(old1.seed() == 79);
    };
}

TEST_CASE("pixelworld.spec.materialCatalogContract") {
    SUB_CASE("canonical tags, authored color, and JSON round trip preserve fingerprint") {
        auto builtIn = MaterialCatalog::builtIn();
        REQUIRE(builtIn.definition(MaterialId::Water).displayRgba == 0x1F61F2DBU);
        std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(), builtIn.definitions().end());
        definitions[std::size_t(MaterialId::Sand)].tags = {"mobile", "granular"};
        auto catalog                                    = MaterialCatalog::create(std::move(definitions));
        REQUIRE(catalog.ok());
        REQUIRE(catalog.value().definition(MaterialId::Sand).tags.size() == 2);
        REQUIRE(catalog.value().definition(MaterialId::Sand).tags[0] == "granular");
        auto encoded = encodeMaterialCatalogJson(catalog.value());
        REQUIRE(encoded.ok());
        auto decoded = decodeMaterialCatalogJson(encoded.value());
        REQUIRE(decoded.ok());
        REQUIRE(decoded.value().fingerprint() == catalog.value().fingerprint());
        REQUIRE(decoded.value().reactions().size() == catalog.value().reactions().size());
        auto duplicate =
            std::vector<MaterialDefinition>(catalog.value().definitions().begin(), catalog.value().definitions().end());
        duplicate[std::size_t(MaterialId::Sand)].tags = {"mobile", "mobile"};
        REQUIRE(!MaterialCatalog::create(std::move(duplicate)).ok());
        REQUIRE(!decodeMaterialCatalogJson("{broken").ok());
        std::string unknown = encoded.value();
        unknown.insert(unknown.find('{') + 1, "\"unknown\":1,");
        REQUIRE(!decodeMaterialCatalogJson(unknown).ok());
    };
    SUB_CASE("custom material resolves by name and authored reaction produces declared products") {
        auto                            builtIn = MaterialCatalog::builtIn();
        std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(), builtIn.definitions().end());
        const auto                      brine = MaterialId(definitions.size());
        definitions.push_back({brine, "brine", MaterialState::Liquid, 110, 35, 30, 90, 32767, 32767, 120});
        std::vector<MaterialReactionRule> reactions{
            {"brine.stone.erode", brine, MaterialId::Stone, MaterialId::Water, MaterialId::Sand, -32768, 5, 100}};
        auto made = MaterialCatalog::create(std::move(definitions), std::move(reactions));
        REQUIRE(made.ok());
        PixelWorld world(41, made.value());
        REQUIRE(world.setMaterialChecked(10, 10, "brine").ok());
        world.setMaterial(11, 10, "stone");
        world.setMaterial(9, 10, "stone");
        world.setMaterial(10, 11, "stone");
        world.setMaterial(9, 11, "stone");
        world.setMaterial(11, 11, "stone");
        const auto before  = world.revision();
        auto       unknown = world.setMaterialChecked(0, 0, "typo-material");
        REQUIRE(!unknown.ok());
        REQUIRE(unknown.error()->code() == eve::DiagnosticCode::NotFound);
        REQUIRE(world.revision() == before);
        auto step = world.advance(eve::SimulationTick(1));
        REQUIRE(step.ok());
        REQUIRE(step.value().reactions > 0);
        const bool water =
            world.getMaterial(10, 10) == int(MaterialId::Water) || world.getMaterial(9, 10) == int(MaterialId::Water) ||
            world.getMaterial(11, 10) == int(MaterialId::Water) || world.getMaterial(10, 11) == int(MaterialId::Water);
        const bool sand =
            world.getMaterial(10, 10) == int(MaterialId::Sand) || world.getMaterial(9, 10) == int(MaterialId::Sand) ||
            world.getMaterial(11, 10) == int(MaterialId::Sand) || world.getMaterial(10, 11) == int(MaterialId::Sand);
        REQUIRE(water);
        REQUIRE(sand);
    };
    SUB_CASE("paused compatible reload rebuilds chunks; stale fingerprint and changed identity fail") {
        PixelWorld world(43);
        world.setMaterial(1, 1, "water");
        const auto                      link        = world.worldLink();
        const auto                      fingerprint = world.materialCatalogFingerprint();
        auto                            builtIn     = MaterialCatalog::builtIn();
        std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(), builtIn.definitions().end());
        definitions[std::size_t(MaterialId::Water)].displayRgba = 0x12345678U;
        auto replacement =
            MaterialCatalog::create(std::move(definitions), {builtIn.reactions().begin(), builtIn.reactions().end()},
                                    {builtIn.phaseRules().begin(), builtIn.phaseRules().end()});
        REQUIRE(replacement.ok());
        REQUIRE(!world.reloadMaterialCatalog(replacement.value(), fingerprint).ok());
        world.setPaused(true);
        REQUIRE(!world.reloadMaterialCatalog(replacement.value(), fingerprint + 1).ok());
        auto applied = world.reloadMaterialCatalog(std::move(replacement).takeValue(), fingerprint);
        REQUIRE(applied.ok());
        REQUIRE(applied.value().chunksRebuilt == 1);
        REQUIRE(applied.value().replayHistoryInvalidated);
        REQUIRE(world.worldLink().epoch > link.epoch);
        REQUIRE(world.materialDisplayRgba(MaterialId::Water) == 0x12345678U);
        REQUIRE(world.getMaterial(1, 1) == int(MaterialId::Water));
        auto incompatible = std::vector<MaterialDefinition>(builtIn.definitions().begin(), builtIn.definitions().end());
        incompatible[std::size_t(MaterialId::Water)].name = "renamed-water";
        auto bad                                          = MaterialCatalog::create(std::move(incompatible));
        REQUIRE(bad.ok());
        const auto before = world.revision();
        REQUIRE(!world.reloadMaterialCatalog(std::move(bad).takeValue(), world.materialCatalogFingerprint()).ok());
        REQUIRE(world.revision() == before);
    };
    SUB_CASE("snapshot catalog mismatch rejects without changing custom world") {
        PixelWorld builtIn(5);
        builtIn.setMaterial(0, 0, "sand");
        auto image = builtIn.saveSnapshot();
        REQUIRE(image.ok());
        auto                            catalog = MaterialCatalog::builtIn();
        std::vector<MaterialDefinition> definitions(catalog.definitions().begin(), catalog.definitions().end());
        const auto                      custom = MaterialId(definitions.size());
        definitions.push_back({custom, "custom", MaterialState::Powder, 150, 20, 10, 50});
        auto made = MaterialCatalog::create(std::move(definitions));
        REQUIRE(made.ok());
        PixelWorld world(9, made.value());
        REQUIRE(world.setMaterialChecked(3, 3, "custom").ok());
        const auto revision = world.revision();
        REQUIRE(!world.restoreSnapshot(image.value()).ok());
        REQUIRE(world.revision() == revision);
        REQUIRE(world.getMaterial(3, 3) == int(custom));
    };
}

TEST_CASE("pixelworld.spec.heatPhaseAndEdits") {
    SUB_CASE("heat moves from hot to cold and changes revision") {
        PixelWorld world(88);
        world.setCell(0, 0, {MaterialId::Stone, 1000, 0});
        world.setCell(1, 0, {MaterialId::Stone, 0, 0});
        const auto before = world.revision();
        auto       step   = world.advance(eve::SimulationTick(1));
        REQUIRE(step.ok());
        REQUIRE(step.value().temperatureTransfers > 0);
        REQUIRE(world.getCell(0, 0).temperature < 1000);
        REQUIRE(world.getCell(1, 0).temperature > 0);
        REQUIRE(world.revision() > before);
    };
    SUB_CASE("thermal remainders conserve weighted energy and survive snapshot restore") {
        auto                            builtIn = MaterialCatalog::builtIn();
        std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(), builtIn.definitions().end());
        definitions[std::size_t(MaterialId::Stone)].heatCapacity        = 7;
        definitions[std::size_t(MaterialId::Stone)].thermalConductivity = 13;
        definitions[std::size_t(MaterialId::Wood)].heatCapacity         = 11;
        definitions[std::size_t(MaterialId::Wood)].thermalConductivity  = 13;
        auto made = MaterialCatalog::create(std::move(definitions));
        REQUIRE(made.ok());
        PixelWorld world(8801, made.value());
        world.setCell(0, 0, {MaterialId::Stone, 1000, 0, 3});
        world.setCell(1, 0, {MaterialId::Wood, -200, 0, 5});
        const auto energy = [](const PixelWorld& value) {
            auto stone = value.getCell(0, 0);
            auto wood  = value.getCell(1, 0);
            return std::int64_t(stone.temperature) * 7 + stone.thermalRemainder + std::int64_t(wood.temperature) * 11 +
                   wood.thermalRemainder;
        };
        const auto before = energy(world);
        for (std::uint64_t tick = 1; tick <= 20; ++tick) {
            auto step = world.advance(eve::SimulationTick(tick));
            REQUIRE(step.ok());
            REQUIRE(step.value().thermalEnergyClamped == 0);
            REQUIRE(energy(world) == before);
        }
        auto image = world.saveSnapshot();
        REQUIRE(image.ok());
        PixelWorld restored(1, made.value());
        REQUIRE(restored.restoreSnapshot(image.value()).ok());
        REQUIRE(restored.getCell(0, 0) == world.getCell(0, 0));
        REQUIRE(restored.getCell(1, 0) == world.getCell(1, 0));
    };
    SUB_CASE("phase rules freeze, boil, and melt three independent cells") {
        PixelWorld world(89);
        world.setCell(10, 10, {MaterialId::Water, -20, 0});
        world.setCell(20, 10, {MaterialId::Water, 130, 0});
        world.setCell(30, 10, {MaterialId::Ice, 20, 0});
        for (int x : {10, 20}) {
            world.setMaterial(x - 1, 10, "stone");
            world.setMaterial(x + 1, 10, "stone");
            world.setMaterial(x - 1, 11, "stone");
            world.setMaterial(x, 11, "stone");
            world.setMaterial(x + 1, 11, "stone");
        }
        auto step = world.advance(eve::SimulationTick(1));
        REQUIRE(step.ok());
        REQUIRE(step.value().phaseChanges >= 3);
        REQUIRE(world.getMaterial(10, 10) == int(MaterialId::Ice));
        REQUIRE(world.getMaterial(20, 10) == int(MaterialId::Steam));
        REQUIRE(world.getMaterial(30, 10) == int(MaterialId::Water));
    };
    SUB_CASE("explosion applies resistance and heat; edit sequence gap is atomic") {
        PixelWorld world(90);
        world.setMaterial(0, 0, "wood");
        world.setMaterial(1, 0, "stone");
        world.setMaterial(-1, 0, "gunpowder");
        PixelEditCommand edit;
        edit.sequence         = 1;
        edit.kind             = PixelEditKind::Explosion;
        edit.radius           = 4;
        edit.strength         = 120;
        edit.temperatureDelta = 400;
        auto receipt          = world.applyEdit(edit);
        REQUIRE(receipt.ok());
        REQUIRE(receipt.value().cellsRemoved >= 2);
        REQUIRE(world.getMaterial(0, 0) == int(MaterialId::Air));
        REQUIRE(world.getMaterial(1, 0) == int(MaterialId::Stone));
        REQUIRE(world.getCell(1, 0).temperature > 20);
        REQUIRE(world.lastEditSequence() == 1);
        auto before = world.saveSnapshot();
        REQUIRE(before.ok());
        edit.sequence = 3;
        edit.kind     = PixelEditKind::HeatCircle;
        REQUIRE(!world.applyEdit(edit).ok());
        auto after = world.saveSnapshot();
        REQUIRE(after.ok());
        REQUIRE(after.value() == before.value());
    };
}

TEST_CASE("pixelworld.spec.fragmentAndAuthoritativeChunks") {
    SUB_CASE("unsupported component extracts canonically and rasterize rejects conflict atomically") {
        PixelWorld world(92);
        for (int x = -2; x <= 2; ++x) world.setMaterial(x, 10, "stone");
        world.setMaterial(-1, 2, "wood");
        world.setMaterial(0, 2, "wood");
        world.setMaterial(0, 3, "wood");
        auto fragments = world.extractUnsupportedFragments({-4, 0, 6, 10}, 10, 2);
        REQUIRE(fragments.ok());
        REQUIRE(fragments.value().size() == 1);
        const auto fragment = fragments.value().front();
        REQUIRE(fragment.originX == -1);
        REQUIRE(fragment.originY == 2);
        REQUIRE(fragment.solidCellCount == 3);
        REQUIRE(world.getMaterial(-1, 2) == int(MaterialId::Air));
        world.setMaterial(20, 20, "stone");
        auto before = world.saveSnapshot();
        REQUIRE(before.ok());
        REQUIRE(!world.rasterizeFragment(fragment, 20, 20).ok());
        REQUIRE(world.saveSnapshot().value() == before.value());
        auto placed = world.rasterizeFragment(fragment, 30, 30);
        REQUIRE(placed.ok());
        REQUIRE(placed.value().cellsPlaced == 3);
        REQUIRE(world.getMaterial(30, 30) == int(MaterialId::Wood));
        auto checkpoint = world.saveSnapshot();
        REQUIRE(checkpoint.ok());
        REQUIRE(world.restoreSnapshot(checkpoint.value()).ok());
        REQUIRE(!world.rasterizeFragment(fragment, 40, 40).ok());
    };
    SUB_CASE("empty chunk reclaims after hysteresis and emits a removal tombstone") {
        PixelWorld world(94);
        world.setMaterial(3, 4, "stone");
        const auto populated = world.revision();
        world.setMaterial(3, 4, "air");
        for (std::uint64_t tick = 1; tick < kPixelSleepHysteresisTicks; ++tick) {
            auto step = world.advance(eve::SimulationTick(tick));
            REQUIRE(step.ok());
            REQUIRE(step.value().chunksReclaimed == 0);
            REQUIRE(world.chunkCount() == 1);
        }
        auto reclaimed = world.advance(eve::SimulationTick(kPixelSleepHysteresisTicks));
        REQUIRE(reclaimed.ok());
        REQUIRE(reclaimed.value().chunksReclaimed == 1);
        REQUIRE(world.chunkCount() == 0);
        auto delta = world.snapshotChangedChunks(populated);
        REQUIRE(delta.size() == 1);
        REQUIRE(delta.front().removed);
        REQUIRE(delta.front().cells.empty());
    };
    SUB_CASE("authoritative batch applies a populated chunk and rejects stale or malformed corrections") {
        PixelWorld authority(95);
        authority.setMaterial(2, 3, "wood");
        PixelChunkBatch batch;
        batch.catalogFingerprint     = authority.materialCatalogFingerprint();
        batch.sourceSeed             = authority.seed();
        batch.sourceRevision         = authority.revision();
        batch.sourceTick             = eve::SimulationTick(authority.tickValue());
        batch.sourceLastEditSequence = authority.lastEditSequence();
        batch.chunks                 = authority.snapshotChangedChunks(0);
        PixelWorld replica(95);
        auto       applied = replica.applyChunkBatch(batch, 0);
        REQUIRE(applied.ok());
        REQUIRE(applied.value().chunksReplaced == 1);
        REQUIRE(replica.getMaterial(2, 3) == int(MaterialId::Wood));
        auto before = replica.saveSnapshot();
        REQUIRE(before.ok());
        REQUIRE(!replica.applyChunkBatch(batch, replica.revision() - 1).ok());
        PixelChunkSnapshot malformed;
        malformed.revision = replica.revision();
        malformed.cells.resize(1);
        batch.chunks = {malformed};
        REQUIRE(!replica.applyChunkBatch(batch, replica.revision()).ok());
        REQUIRE(replica.saveSnapshot().value() == before.value());
    };
}
}  // namespace test_pixelworld
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelworld_control)
#include "pixelworld/PixelMaterialCatalogCodec.h"
#include "pixelworld/PixelWorldControl.h"
// pixelworld_control.cpp
namespace test_pixelworld_control {
using namespace eve::pixelworld;

TEST_CASE("pixelworld.control.spec.lifetimePauseAndStep") {
    SUB_CASE("control registry follows a moved world and removes a destroyed world") {
        auto&         control = pixelWorldControlService();
        std::uint64_t id      = 0;
        {
            PixelWorld original(71);
            id = original.worldLink().world;
            REQUIRE(control.world(id).ok());
            PixelWorld moved(std::move(original));
            auto       state = control.world(id);
            REQUIRE(state.ok());
            REQUIRE(state.value().seed == 71);
            REQUIRE(state.value().link.world == id);
        }
        REQUIRE(!control.world(id).ok());
    };
    SUB_CASE("paused implicit step is inert; explicit step advances a bounded count") {
        PixelWorld world(72);
        const auto id      = world.worldLink().world;
        auto&      control = pixelWorldControlService();
        REQUIRE(control.setPaused(id, true).ok());
        REQUIRE(world.step().tick.value() == 0);
        REQUIRE(world.tickValue() == 0);
        auto receipt = control.step(id, 3);
        REQUIRE(receipt.ok());
        REQUIRE(receipt.value().steps == 3);
        REQUIRE(receipt.value().firstTick == 1);
        REQUIRE(receipt.value().lastTick == 3);
        REQUIRE(world.tickValue() == 3);
        REQUIRE(world.isPaused());
        REQUIRE(!control.step(id, 0).ok());
        REQUIRE(!control.step(id, 1025).ok());
        auto samples = control.performanceSamples(id, 2);
        REQUIRE(samples.ok());
        REQUIRE(samples.value().size() == 2);
        REQUIRE(samples.value().front().tick == 2);
        REQUIRE(samples.value().back().tick == 3);
        REQUIRE(!control.performanceSamples(id, 0).ok());
    };
}

TEST_CASE("pixelworld.control.spec.editSnapshotAndCatalog") {
    SUB_CASE("control edit yields diagnostics; failed restore preserves revision") {
        PixelWorld       world(73);
        const auto       id      = world.worldLink().world;
        auto&            control = pixelWorldControlService();
        PixelEditCommand paint;
        paint.sequence = 1;
        paint.kind     = PixelEditKind::PaintCircle;
        paint.radius   = 1;
        paint.material = MaterialId::Water;
        REQUIRE(control.applyEdit(id, paint).ok());
        auto diagnostics = control.chunkDiagnostics(id, {0, 0, 0, 0});
        REQUIRE(diagnostics.ok());
        REQUIRE(diagnostics.value().size() == 1);
        // The radius-one disk paints five cells. Center (0,0) lies on two
        // chunk seams: three cells are in chunk (0,0), while the negative X
        // and negative Y neighbours each belong to a separate chunk.
        REQUIRE(diagnostics.value().front().nonAirCells == 3);
        REQUIRE(diagnostics.value().front().mobileCells == 3);
        auto acrossSeams = control.chunkDiagnostics(id, {-1, -1, 0, 0});
        REQUIRE(acrossSeams.ok());
        REQUIRE(acrossSeams.value().size() == 3);
        std::uint32_t painted = 0;
        std::uint32_t mobile  = 0;
        for (const auto& chunk : acrossSeams.value()) {
            painted += chunk.nonAirCells;
            mobile += chunk.mobileCells;
        }
        REQUIRE(painted == 5);
        REQUIRE(mobile == 5);
        REQUIRE(!control.chunkDiagnostics(id, {0, 0, 256, 256}).ok());
        auto snapshot = control.captureSnapshot(id);
        REQUIRE(snapshot.ok());
        world.setMaterial(8, 8, "stone");
        REQUIRE(control.restoreSnapshot(id, snapshot.value()).ok());
        REQUIRE(world.getMaterial(8, 8) == int(MaterialId::Air));
        auto malformed = snapshot.value();
        malformed.pop_back();
        const auto revision = world.revision();
        REQUIRE(!control.restoreSnapshot(id, malformed).ok());
        REQUIRE(world.revision() == revision);
    };
    SUB_CASE("catalog reload travels through the same paused authority contract") {
        PixelWorld world(75);
        auto&      control = pixelWorldControlService();
        const auto id      = world.worldLink().world;
        REQUIRE(control.setPaused(id, true).ok());
        auto                            builtIn = MaterialCatalog::builtIn();
        std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(), builtIn.definitions().end());
        definitions[std::size_t(MaterialId::Stone)].displayRgba = 0x010203FFU;
        auto candidate =
            MaterialCatalog::create(std::move(definitions), {builtIn.reactions().begin(), builtIn.reactions().end()},
                                    {builtIn.phaseRules().begin(), builtIn.phaseRules().end()});
        REQUIRE(candidate.ok());
        auto json = encodeMaterialCatalogJson(candidate.value());
        REQUIRE(json.ok());
        REQUIRE(control.reloadMaterialCatalog(id, json.value(), world.materialCatalogFingerprint()).ok());
        REQUIRE(world.materialDisplayRgba(MaterialId::Stone) == 0x010203FFU);
        REQUIRE(!control.reloadMaterialCatalog(id, "{}", world.materialCatalogFingerprint()).ok());
    };
}
}  // namespace test_pixelworld_control
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelworld_generation)
#include "pixelworld/PixelWorldGeneration.h"
#include "pixelworld/PixelWorldGenerationCodec.h"
// pixelworld_generation.cpp
namespace test_pixelworld_generation {
using namespace eve::pixelworld;

TEST_CASE("pixelworld.generation.spec.determinismSeamsAndStamping") {
    const auto  catalog     = MaterialCatalog::builtIn();
    const auto* catalogView = &catalog;
    SUB_CASE("same seed and request are bit exact; isolated chunk agrees with regional generation") {
        PixelWorldGenerationRequest request;
        request.seed             = 0xC0FFEE;
        request.region           = {-3, -1, 3, 2};
        request.surfaceY         = 4;
        request.terrainAmplitude = 20;
        request.waterLevel       = 48;
        request.caveThreshold    = 4200;
        auto first               = generatePixelWorld(request, *catalogView, 1);
        auto repeated            = generatePixelWorld(request, *catalogView, 1);
        REQUIRE(first.ok());
        REQUIRE(repeated.ok());
        REQUIRE(first.value().contentHash == repeated.value().contentHash);
        REQUIRE(first.value().batch == repeated.value().batch);
        REQUIRE(std::any_of(first.value().chunks.begin(), first.value().chunks.end(),
                            [](const auto& chunk) { return chunk.caveCells > 0; }));
        PixelWorldGenerationRequest single = request;
        single.region                      = {0, 0, 0, 0};
        auto isolated                      = generatePixelWorld(single, *catalogView, 1);
        REQUIRE(isolated.ok());
        auto shared = std::find_if(first.value().batch.chunks.begin(), first.value().batch.chunks.end(),
                                   [](const auto& chunk) { return chunk.x == 0 && chunk.y == 0; });
        REQUIRE(shared != first.value().batch.chunks.end());
        REQUIRE(isolated.value().batch.chunks.size() == 1);
        REQUIRE(shared->cells == isolated.value().batch.chunks.front().cells);
        request.seed += 1;
        auto different = generatePixelWorld(request, *catalogView, 1);
        REQUIRE(different.ok());
        REQUIRE(different.value().contentHash != first.value().contentHash);
    };
    SUB_CASE("stamp spanning two chunks is split, applied, and invalid schema is rejected") {
        PixelWorldGenerationRequest request;
        request.seed          = 77;
        request.region        = {0, 0, 1, 0};
        request.surfaceY      = 0;
        request.caveThreshold = 0;
        PixelMaterialStamp stamp;
        stamp.originX = 62;
        stamp.originY = 10;
        stamp.width   = 4;
        stamp.height  = 2;
        stamp.cells.assign(8, {MaterialId::Wood, 20, 0, 0});
        request.stamps.push_back(stamp);
        auto generated = generatePixelWorld(request, *catalogView, 1);
        REQUIRE(generated.ok());
        REQUIRE(generated.value().batch.chunks.size() == 2);
        REQUIRE(generated.value().chunks[0].stampedCells == 4);
        REQUIRE(generated.value().chunks[1].stampedCells == 4);
        PixelWorld world(request.seed, *catalogView);
        auto       applied = world.applyChunkBatch(generated.value().batch, 0);
        REQUIRE(applied.ok());
        REQUIRE(applied.value().chunksReplaced == 2);
        for (int y = 10; y < 12; ++y)
            for (int x = 62; x < 66; ++x) REQUIRE(world.getMaterial(x, y) == int(MaterialId::Wood));
        auto before = world.saveSnapshot();
        REQUIRE(before.ok());
        request.schemaVersion = 99;
        REQUIRE(!generatePixelWorld(request, *catalogView, 2).ok());
        auto after = world.saveSnapshot();
        REQUIRE(after.ok());
        REQUIRE(after.value() == before.value());
    };
}

TEST_CASE("pixelworld.generation.spec.versionedRequestCodec") {
    PixelWorldGenerationRequest request;
    request.seed             = std::numeric_limits<std::uint64_t>::max();
    request.region           = {-2, 3, 4, 5};
    request.surfaceY         = -17;
    request.terrainAmplitude = 91;
    request.waterLevel       = 73;
    request.caveThreshold    = 1234;
    PixelMaterialStamp stamp;
    stamp.originX = -65;
    stamp.originY = 7;
    stamp.width   = 1;
    stamp.height  = 1;
    stamp.cells.push_back({MaterialId::Lava, 777, 9, 4});
    request.stamps.push_back(stamp);
    auto encoded = encodePixelWorldGenerationRequestJson(request);
    REQUIRE(encoded.ok());
    auto decoded = decodePixelWorldGenerationRequestJson(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().seed == request.seed);
    REQUIRE(decoded.value().region.minX == -2);
    REQUIRE(decoded.value().region.maxY == 5);
    REQUIRE(decoded.value().surfaceY == -17);
    REQUIRE(decoded.value().stamps.front().cells == stamp.cells);
    auto again = encodePixelWorldGenerationRequestJson(decoded.value());
    REQUIRE(again.ok());
    REQUIRE(again.value() == encoded.value());
    std::string future  = encoded.value();
    const auto  version = future.find("\"version\": 1");
    REQUIRE(version != std::string::npos);
    future.replace(version, std::string("\"version\": 1").size(), "\"version\": 2");
    REQUIRE(!decodePixelWorldGenerationRequestJson(future).ok());
    std::string unknown = encoded.value();
    const auto  close   = unknown.rfind('}');
    REQUIRE(close != std::string::npos);
    unknown.insert(close, ",\n  \"future\": true\n");
    REQUIRE(!decodePixelWorldGenerationRequestJson(unknown).ok());
    REQUIRE(!decodePixelWorldGenerationRequestJson("{broken").ok());
}
}  // namespace test_pixelworld_generation
#endif
