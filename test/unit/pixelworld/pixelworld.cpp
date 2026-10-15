#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorld.h"
#include "pixelworld/PixelMaterialCatalogCodec.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace eve::pixelworld;

namespace {

std::vector<std::byte> withoutThermalRemainders(const std::vector<std::byte>& current) {
    REQUIRE(current.size() >= 50);
    std::uint32_t chunks = 0;
    std::memcpy(&chunks, current.data() + 46, sizeof(chunks));
    std::vector<std::byte> legacy(current.begin(), current.begin() + 50);
    std::size_t cursor = 50;
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

TEST_CASE("pixelworld.sand_falls_and_stops_on_stone") {
    PixelWorld world(7);
    for (int x = -2; x <= 2; ++x) world.setMaterial(x, 4, "stone");
    world.setMaterial(0, 0, "sand");
    for (std::uint64_t tick = 1; tick <= 6; ++tick) {
        auto result = world.advance(eve::SimulationTick(tick));
        REQUIRE(result.ok());
    }
    CHECK_EQ(world.getMaterial(0, 3), int(MaterialId::Sand));
    CHECK_EQ(world.getMaterial(0, 4), int(MaterialId::Stone));
}

TEST_CASE("pixelworld.cross_chunk_motion_is_conservative") {
    PixelWorld world(11);
    for (int x = 61; x <= 65; ++x) world.setMaterial(x, 65, "stone");
    world.setMaterial(63, 63, "sand");
    for (std::uint64_t tick = 1; tick <= 4; ++tick)
        world.advance(eve::SimulationTick(tick)).expect("cross-chunk step");
    CHECK_EQ(world.getMaterial(63, 64), int(MaterialId::Sand));
    CHECK_EQ(world.getMaterial(63, 65), int(MaterialId::Stone));
    CHECK(world.chunkCount() >= 2);
}

TEST_CASE("pixelworld.fire_converts_water_to_steam") {
    PixelWorld world(3);
    world.setMaterial(10, 10, "fire");
    world.setMaterial(11, 10, "water");
    world.setMaterial(10, 11, "stone");
    world.setMaterial(11, 11, "stone");
    world.setMaterial(12, 10, "stone");
    world.setMaterial(12, 11, "stone");
    const StepStats stats = world.advance(eve::SimulationTick(1)).expect("reaction step");
    CHECK(stats.reactions > 0);
    const bool steamPresent = world.getMaterial(11, 10) == int(MaterialId::Steam) ||
                              world.getMaterial(11, 9) == int(MaterialId::Steam);
    CHECK(steamPresent);
}

TEST_CASE("pixelworld.fixed_seed_and_ticks_are_deterministic") {
    PixelWorld a(99), b(99);
    for (int x = -8; x <= 8; ++x) {
        a.setMaterial(x, 20, "stone");
        b.setMaterial(x, 20, "stone");
    }
    a.paintCircle(0, 2, 6, "sand");
    b.paintCircle(0, 2, 6, "sand");
    a.paintCircle(3, 0, 3, "water");
    b.paintCircle(3, 0, 3, "water");
    for (std::uint64_t tick = 1; tick <= 30; ++tick) {
        a.advance(eve::SimulationTick(tick)).expect("determinism a");
        b.advance(eve::SimulationTick(tick)).expect("determinism b");
    }
    auto aBytes = a.saveSnapshot().expect("snapshot a");
    auto bBytes = b.saveSnapshot().expect("snapshot b");
    REQUIRE_EQ(aBytes.size(), bBytes.size());
    CHECK(std::equal(aBytes.begin(), aBytes.end(), bBytes.begin()));
}

TEST_CASE("pixelworld.snapshot_restore_is_transactional") {
    PixelWorld source(123);
    source.paintCircle(65, -2, 4, "oil");
    source.step();
    const auto bytes = source.saveSnapshot().expect("save source");

    PixelWorld restored(1);
    restored.restoreSnapshot(bytes).expect("restore source");
    CHECK_EQ(restored.seed(), source.seed());
    CHECK_EQ(restored.tickValue(), source.tickValue());
    CHECK_EQ(restored.getMaterial(65, -1), source.getMaterial(65, -1));

    std::vector<std::byte> malformed = bytes;
    malformed.pop_back();
    auto failed = restored.restoreSnapshot(malformed);
    CHECK(!failed.ok());
    CHECK_EQ(restored.seed(), source.seed());
    CHECK_EQ(restored.tickValue(), source.tickValue());
}

TEST_CASE("pixelworld.rejects_non_monotonic_tick") {
    PixelWorld world;
    world.advance(eve::SimulationTick(2)).expect("first step");
    auto rejected = world.advance(eve::SimulationTick(2));
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::PreconditionViolation);
}

TEST_CASE("pixelworld.changed_chunk_snapshots_are_owned_canonical_and_incremental") {
    PixelWorld world(17);
    world.setMaterial(70, 2, "sand");
    world.setMaterial(-2, 2, "water");
    const std::uint64_t baseline = world.revision();

    auto initial = world.snapshotChangedChunks(0);
    REQUIRE_EQ(initial.size(), std::size_t(2));
    CHECK(initial[0].x < initial[1].x);
    CHECK_EQ(initial[0].cells.size(), std::size_t(kPixelChunkSize * kPixelChunkSize));
    const PixelCell ownedCell = initial[0].cells[2 * kPixelChunkSize + 62];

    world.setMaterial(70, 3, "stone");
    auto incremental = world.snapshotChangedChunks(baseline);
    REQUIRE_EQ(incremental.size(), std::size_t(1));
    CHECK_EQ(incremental[0].x, 1);
    CHECK_EQ(initial[0].cells[2 * kPixelChunkSize + 62], ownedCell);
}

TEST_CASE("pixelworld.revision_tracks_lifetime_only_changes") {
    PixelWorld world(23);
    world.setMaterial(0, 0, "fire");
    const std::uint64_t before = world.revision();
    const StepStats stats = world.advance(eve::SimulationTick(1)).expect("fire lifetime step");
    CHECK(stats.cellsChanged > 0);
    CHECK(world.revision() > before);
    const auto changed = world.snapshotChangedChunks(before);
    CHECK(!changed.empty());
    CHECK(std::all_of(changed.begin(), changed.end(), [before](const PixelChunkSnapshot& chunk) {
        return chunk.revision > before;
    }));
}

TEST_CASE("pixelworld.material_catalog_validates_names_and_drives_custom_reactions") {
    MaterialCatalog builtIn = MaterialCatalog::builtIn();
    std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(), builtIn.definitions().end());
    const MaterialId brine = MaterialId(definitions.size());
    definitions.push_back(
        {brine, "brine", MaterialState::Liquid, 110, 35, 30, 90, 32767, 32767, 120});
    std::vector<MaterialReactionRule> reactions{
        {"brine.stone.hot-only", brine, MaterialId::Stone, MaterialId::Fire, MaterialId::Lava,
         1000, 0, 200},
        {"brine.stone.erode", brine, MaterialId::Stone, MaterialId::Water, MaterialId::Sand,
         -32768, 5, 100},
    };
    MaterialCatalog catalog =
        MaterialCatalog::create(std::move(definitions), std::move(reactions)).expect("custom catalog");
    PixelWorld world(41, catalog);

    world.setMaterialChecked(10, 10, "brine").expect("place brine");
    world.setMaterial(11, 10, "stone");
    world.setMaterial(9, 10, "stone");
    world.setMaterial(10, 11, "stone");
    world.setMaterial(9, 11, "stone");
    world.setMaterial(11, 11, "stone");
    const std::uint64_t beforeUnknown = world.revision();
    auto unknown = world.setMaterialChecked(0, 0, "typo-material");
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.error()->code(), eve::DiagnosticCode::NotFound);
    CHECK_EQ(world.revision(), beforeUnknown);

    const StepStats stats = world.advance(eve::SimulationTick(1)).expect("custom reaction step");
    CHECK(stats.reactions > 0);
    const int center = world.getMaterial(10, 10);
    const bool adjacentSand = world.getMaterial(9, 10) == int(MaterialId::Sand) ||
                              world.getMaterial(11, 10) == int(MaterialId::Sand) ||
                              world.getMaterial(10, 11) == int(MaterialId::Sand);
    const bool adjacentWater = world.getMaterial(9, 10) == int(MaterialId::Water) ||
                               world.getMaterial(11, 10) == int(MaterialId::Water) ||
                               world.getMaterial(10, 11) == int(MaterialId::Water);
    const bool productsPresent = (center == int(MaterialId::Water) && adjacentSand) ||
                                 (center == int(MaterialId::Sand) && adjacentWater);
    CHECK(productsPresent);
}

TEST_CASE("pixelworld.material_catalog_owns_color_and_canonical_tags") {
    MaterialCatalog builtIn = MaterialCatalog::builtIn();
    CHECK_EQ(builtIn.definition(MaterialId::Water).displayRgba, std::uint32_t(0x1F61F2DBU));
    PixelWorld world(42, builtIn);
    CHECK_EQ(world.materialDisplayRgba(MaterialId::Acid), std::uint32_t(0x4DEB3DE0U));

    std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(),
                                                 builtIn.definitions().end());
    definitions[std::size_t(MaterialId::Sand)].tags = {"mobile", "granular"};
    MaterialCatalog canonical = MaterialCatalog::create(std::move(definitions)).expect("sorted tags");
    const auto& tags = canonical.definition(MaterialId::Sand).tags;
    REQUIRE_EQ(tags.size(), std::size_t(2));
    CHECK_EQ(tags[0], std::string("granular"));
    CHECK_EQ(tags[1], std::string("mobile"));

    std::vector<MaterialDefinition> invalid(canonical.definitions().begin(),
                                             canonical.definitions().end());
    invalid[std::size_t(MaterialId::Sand)].tags = {"mobile", "mobile"};
    CHECK(!MaterialCatalog::create(std::move(invalid)).ok());
}

TEST_CASE("pixelworld.material_catalog_json_roundtrips_and_rejects_unknown_schema") {
    const MaterialCatalog builtIn = MaterialCatalog::builtIn();
    const std::string json = encodeMaterialCatalogJson(builtIn).expect("encode Catalog JSON");
    const MaterialCatalog decoded = decodeMaterialCatalogJson(json).expect("decode Catalog JSON");
    CHECK_EQ(decoded.fingerprint(), builtIn.fingerprint());
    CHECK_EQ(decoded.definition(MaterialId::Fire).displayRgba,
             builtIn.definition(MaterialId::Fire).displayRgba);
    CHECK_EQ(decoded.reactions().size(), builtIn.reactions().size());
    CHECK_EQ(decoded.phaseRules().size(), builtIn.phaseRules().size());

    std::string unknownField = json;
    unknownField.insert(unknownField.find('{') + 1, "\"unknown\":1,");
    CHECK(!decodeMaterialCatalogJson(unknownField).ok());
    std::string unknownVersion = json;
    const auto version = unknownVersion.find("\"version\": 1");
    REQUIRE(version != std::string::npos);
    unknownVersion.replace(version, std::string("\"version\": 1").size(), "\"version\": 2");
    CHECK(!decodeMaterialCatalogJson(unknownVersion).ok());
    CHECK(!decodeMaterialCatalogJson("{broken").ok());
    CHECK(!decodeMaterialCatalogJson(
               R"({"schema":"eve.pixelworld.material-catalog","version":1,"materials":{},"reactions":[],"phaseRules":[]})")
               .ok());
    CHECK(!decodeMaterialCatalogJson(
               R"({"schema":"eve.pixelworld.material-catalog","version":1,"materials":[7],"reactions":[],"phaseRules":[]})")
               .ok());
    std::string overflowingId = json;
    const auto firstId = overflowingId.find("\"id\": 0");
    REQUIRE(firstId != std::string::npos);
    overflowingId.replace(firstId, std::string("\"id\": 0").size(), "\"id\": 70000");
    CHECK(!decodeMaterialCatalogJson(overflowingId).ok());
}

TEST_CASE("pixelworld.material_catalog_hot_reload_is_paused_compatible_and_transactional") {
    PixelWorld world(43);
    world.setMaterial(1, 1, "water");
    const auto originalLink = world.worldLink();
    const std::uint64_t originalFingerprint = world.materialCatalogFingerprint();
    MaterialCatalog builtIn = MaterialCatalog::builtIn();
    std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(),
                                                 builtIn.definitions().end());
    definitions[std::size_t(MaterialId::Water)].displayRgba = 0x12345678U;
    definitions[std::size_t(MaterialId::Water)].viscosity = 31;
    definitions[std::size_t(MaterialId::Water)].density = 60;
    MaterialCatalog replacement = MaterialCatalog::create(
        std::move(definitions),
        {builtIn.reactions().begin(), builtIn.reactions().end()},
        {builtIn.phaseRules().begin(), builtIn.phaseRules().end()})
                                      .expect("compatible Catalog");
    const std::string replacementJson =
        encodeMaterialCatalogJson(replacement).expect("encode replacement Catalog");

    CHECK(!world.reloadMaterialCatalog(replacement, originalFingerprint).ok());
    CHECK_EQ(world.materialCatalogFingerprint(), originalFingerprint);
    world.setPaused(true);
    CHECK(!world.reloadMaterialCatalog(replacement, originalFingerprint + 1).ok());
    CHECK_EQ(world.materialCatalogFingerprint(), originalFingerprint);
    const auto receipt = world.reloadMaterialCatalog(std::move(replacement), originalFingerprint)
                             .expect("paused Catalog reload");
    CHECK_EQ(receipt.fingerprintBefore, originalFingerprint);
    CHECK(receipt.fingerprintAfter != originalFingerprint);
    CHECK_EQ(receipt.chunksRebuilt, std::uint32_t(1));
    CHECK(receipt.replayHistoryInvalidated);
    CHECK(world.worldLink().epoch > originalLink.epoch);
    CHECK_EQ(world.materialDisplayRgba(MaterialId::Water), std::uint32_t(0x12345678U));
    CHECK_EQ(world.getMaterial(1, 1), int(MaterialId::Water));

    world.setMaterial(1, 2, "oil");
    world.setMaterial(0, 1, "stone");
    world.setMaterial(2, 1, "stone");
    world.setMaterial(0, 2, "stone");
    world.setMaterial(2, 2, "stone");
    world.setPaused(false);
    world.advance(eve::SimulationTick(1)).expect("advance with rebuilt displacement table");
    CHECK_EQ(world.getMaterial(1, 1), int(MaterialId::Water));
    CHECK_EQ(world.getMaterial(1, 2), int(MaterialId::Oil));
    world.setPaused(true);

    const std::uint64_t revisionBeforeNoOp = world.revision();
    const auto noOp = world.reloadMaterialCatalog(
                               decodeMaterialCatalogJson(replacementJson)
                                   .expect("decode replacement Catalog"),
                               world.materialCatalogFingerprint())
                          .expect("same Catalog is a no-op");
    CHECK_EQ(noOp.revisionBefore, revisionBeforeNoOp);
    CHECK_EQ(noOp.revisionAfter, revisionBeforeNoOp);
    CHECK_EQ(noOp.worldEpoch, world.worldLink().epoch);
    CHECK(!noOp.replayHistoryInvalidated);

    const MaterialCatalog incompatibleBase = MaterialCatalog::builtIn();
    std::vector<MaterialDefinition> incompatible(incompatibleBase.definitions().begin(),
                                                 incompatibleBase.definitions().end());
    incompatible[std::size_t(MaterialId::Water)].name = "renamed-water";
    auto bad = MaterialCatalog::create(std::move(incompatible)).expect("valid but incompatible");
    const std::uint64_t revision = world.revision();
    CHECK(!world.reloadMaterialCatalog(std::move(bad), world.materialCatalogFingerprint()).ok());
    CHECK_EQ(world.revision(), revision);
}

TEST_CASE("pixelworld.snapshot_rejects_material_catalog_mismatch_transactionally") {
    PixelWorld builtInWorld(5);
    builtInWorld.setMaterial(0, 0, "sand");
    const auto bytes = builtInWorld.saveSnapshot().expect("built-in snapshot");

    MaterialCatalog builtIn = MaterialCatalog::builtIn();
    std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(), builtIn.definitions().end());
    const MaterialId custom = MaterialId(definitions.size());
    definitions.push_back({custom, "custom", MaterialState::Powder, 150, 20, 10, 50});
    PixelWorld customWorld(
        9, MaterialCatalog::create(std::move(definitions)).expect("catalog with custom material"));
    customWorld.setMaterialChecked(3, 3, "custom").expect("place custom material");
    const std::uint64_t revisionBefore = customWorld.revision();

    auto rejected = customWorld.restoreSnapshot(bytes);
    CHECK(!rejected.ok());
    CHECK_EQ(customWorld.revision(), revisionBefore);
    CHECK_EQ(customWorld.getMaterial(3, 3), int(custom));
}

TEST_CASE("pixelworld.snapshot_v1_migrates_only_with_builtin_catalog") {
    PixelWorld source(77);
    source.setMaterial(4, 5, "water");
    std::vector<std::byte> legacy =
        withoutThermalRemainders(source.saveSnapshot().expect("v4 source"));
    REQUIRE(legacy.size() > 14);
    legacy[4] = std::byte{1};
    legacy[5] = std::byte{0};
    legacy.erase(legacy.begin() + 38, legacy.begin() + 46);
    legacy.erase(legacy.begin() + 6, legacy.begin() + 14);

    PixelWorld restored(1);
    restored.restoreSnapshot(legacy).expect("v1 migration");
    CHECK_EQ(restored.seed(), std::uint64_t(77));
    CHECK_EQ(restored.getMaterial(4, 5), int(MaterialId::Water));
}

TEST_CASE("pixelworld.snapshot_v2_migrates_with_zero_edit_sequence") {
    PixelWorld source(78);
    source.setMaterial(2, 3, "sand");
    std::vector<std::byte> version2 =
        withoutThermalRemainders(source.saveSnapshot().expect("v4 source"));
    REQUIRE(version2.size() > 46);
    version2[4] = std::byte{2};
    version2[5] = std::byte{0};
    version2.erase(version2.begin() + 38, version2.begin() + 46);

    PixelWorld restored(1);
    restored.restoreSnapshot(version2).expect("v2 migration");
    CHECK_EQ(restored.getMaterial(2, 3), int(MaterialId::Sand));
    CHECK_EQ(restored.lastEditSequence(), std::uint64_t(0));
}

TEST_CASE("pixelworld.snapshot_v3_migrates_with_zero_thermal_remainder") {
    PixelWorld source(79);
    source.setCell(2, 3, {MaterialId::Stone, 123, 0, 0});
    std::vector<std::byte> version3 =
        withoutThermalRemainders(source.saveSnapshot().expect("v4 source"));
    version3[4] = std::byte{3};
    version3[5] = std::byte{0};

    PixelWorld restored(1);
    restored.restoreSnapshot(version3).expect("v3 migration");
    CHECK_EQ(restored.getCell(2, 3).temperature, std::int16_t(123));
    CHECK_EQ(restored.getCell(2, 3).thermalRemainder, std::uint16_t(0));
}

TEST_CASE("pixelworld.fixed_point_heat_diffusion_is_directional_and_revisioned") {
    PixelWorld world(88);
    world.setCell(0, 0, {MaterialId::Stone, 1000, 0});
    world.setCell(1, 0, {MaterialId::Stone, 0, 0});
    const std::uint64_t before = world.revision();
    const StepStats stats = world.advance(eve::SimulationTick(1)).expect("thermal step");
    CHECK(stats.temperatureTransfers > 0);
    CHECK(world.getCell(0, 0).temperature < 1000);
    CHECK(world.getCell(1, 0).temperature > 0);
    CHECK(world.revision() > before);
}

TEST_CASE("pixelworld.thermal_remainders_conserve_energy_and_roundtrip_authority") {
    const MaterialCatalog builtIn = MaterialCatalog::builtIn();
    std::vector<MaterialDefinition> definitions(builtIn.definitions().begin(),
                                                 builtIn.definitions().end());
    definitions[std::size_t(MaterialId::Stone)].heatCapacity = 7;
    definitions[std::size_t(MaterialId::Stone)].thermalConductivity = 13;
    definitions[std::size_t(MaterialId::Wood)].heatCapacity = 11;
    definitions[std::size_t(MaterialId::Wood)].thermalConductivity = 13;
    const MaterialCatalog catalog =
        MaterialCatalog::create(std::move(definitions)).expect("thermal test Catalog");
    PixelWorld world(8801, catalog);
    world.setCell(0, 0, {MaterialId::Stone, 1000, 0, 3});
    world.setCell(1, 0, {MaterialId::Wood, -200, 0, 5});
    const auto energy = [&](const PixelWorld& value) {
        const PixelCell stone = value.getCell(0, 0);
        const PixelCell wood = value.getCell(1, 0);
        return std::int64_t(stone.temperature) * 7 + stone.thermalRemainder +
               std::int64_t(wood.temperature) * 11 + wood.thermalRemainder;
    };
    const std::int64_t initialEnergy = energy(world);
    std::uint64_t transferred = 0;
    for (std::uint64_t tick = 1; tick <= 200; ++tick) {
        const auto stats = world.advance(eve::SimulationTick(tick)).expect("conservative heat step");
        transferred += stats.thermalEnergyTransferred;
        CHECK_EQ(stats.thermalEnergyClamped, std::uint64_t(0));
        CHECK_EQ(energy(world), initialEnergy);
    }
    CHECK(transferred > 0);
    CHECK(world.getCell(0, 0).thermalRemainder < 7);
    CHECK(world.getCell(1, 0).thermalRemainder < 11);

    const auto snapshot = world.saveSnapshot().expect("thermal v4 snapshot");
    PixelWorld restored(2, catalog);
    restored.restoreSnapshot(snapshot).expect("restore thermal v4 snapshot");
    CHECK_EQ(restored.getCell(0, 0), world.getCell(0, 0));
    CHECK_EQ(restored.getCell(1, 0), world.getCell(1, 0));

    auto malformed = snapshot;
    REQUIRE(malformed.size() >= 67);
    const std::uint16_t invalidRemainder = std::numeric_limits<std::uint16_t>::max();
    std::memcpy(malformed.data() + 63, &invalidRemainder, sizeof(invalidRemainder));
    const auto beforeRejectedRestore = restored.saveSnapshot().expect("before bad remainder");
    CHECK(!restored.restoreSnapshot(malformed).ok());
    const auto afterRejectedRestore = restored.saveSnapshot().expect("after bad remainder");
    CHECK(std::equal(beforeRejectedRestore.begin(), beforeRejectedRestore.end(),
                     afterRejectedRestore.begin()));
}

TEST_CASE("pixelworld.phase_rules_freeze_boil_and_melt_without_hardcoded_branches") {
    PixelWorld world(89);
    world.setCell(10, 10, {MaterialId::Water, -20, 0});
    world.setCell(20, 10, {MaterialId::Water, 130, 0});
    world.setCell(30, 10, {MaterialId::Ice, 20, 0});
    for (const int center : {10, 20}) {
        world.setMaterial(center - 1, 10, "stone");
        world.setMaterial(center + 1, 10, "stone");
        world.setMaterial(center - 1, 11, "stone");
        world.setMaterial(center, 11, "stone");
        world.setMaterial(center + 1, 11, "stone");
    }
    const StepStats stats = world.advance(eve::SimulationTick(1)).expect("phase step");
    CHECK(stats.phaseChanges >= 3);
    CHECK_EQ(world.getMaterial(10, 10), int(MaterialId::Ice));
    CHECK_EQ(world.getMaterial(20, 10), int(MaterialId::Steam));
    CHECK_EQ(world.getMaterial(30, 10), int(MaterialId::Water));
}

TEST_CASE("pixelworld.explosion_uses_resistance_heat_and_atomic_sequence_receipt") {
    PixelWorld world(90);
    world.setMaterial(0, 0, "wood");
    world.setMaterial(1, 0, "stone");
    world.setMaterial(-1, 0, "gunpowder");
    PixelEditCommand command;
    command.sequence = 1;
    command.kind = PixelEditKind::Explosion;
    command.radius = 4;
    command.strength = 120;
    command.temperatureDelta = 400;
    const PixelEditReceipt receipt = world.applyEdit(command).expect("explosion command");

    CHECK_EQ(receipt.sequence, std::uint64_t(1));
    CHECK(receipt.cellsRemoved >= 2);
    CHECK_EQ(world.getMaterial(0, 0), int(MaterialId::Air));
    CHECK_EQ(world.getMaterial(-1, 0), int(MaterialId::Air));
    CHECK_EQ(world.getMaterial(1, 0), int(MaterialId::Stone));
    CHECK(world.getCell(1, 0).temperature > 20);
    CHECK_EQ(world.lastEditSequence(), std::uint64_t(1));
    CHECK(receipt.revisionAfter > receipt.revisionBefore);
}

TEST_CASE("pixelworld.edit_sequence_gap_rejects_without_partial_mutation_and_roundtrips") {
    PixelWorld world(91);
    world.setMaterial(5, 5, "wood");
    world.explode(5, 5, 2, 100, 100);
    const auto before = world.saveSnapshot().expect("before rejected edit");

    PixelEditCommand gap;
    gap.sequence = 3;
    gap.kind = PixelEditKind::HeatCircle;
    gap.centerX = 5;
    gap.centerY = 5;
    gap.radius = 2;
    gap.temperatureDelta = 500;
    auto rejected = world.applyEdit(gap);
    CHECK(!rejected.ok());
    const auto after = world.saveSnapshot().expect("after rejected edit");
    REQUIRE_EQ(before.size(), after.size());
    CHECK(std::equal(before.begin(), before.end(), after.begin()));

    PixelWorld restored(1);
    restored.restoreSnapshot(before).expect("restore edit sequence");
    CHECK_EQ(restored.lastEditSequence(), std::uint64_t(1));
}

TEST_CASE("pixelworld.unsupported_solids_detach_canonically_and_rasterize_atomically") {
    PixelWorld world(92);
    for (int x = -2; x <= 2; ++x) world.setMaterial(x, 10, "stone");
    world.setMaterial(-1, 2, "wood");
    world.setMaterial(0, 2, "wood");
    world.setMaterial(0, 3, "wood");
    world.setMaterial(4, 9, "stone");
    world.setMaterial(4, 10, "stone");

    auto fragments = world.extractUnsupportedFragments({-4, 0, 6, 10}, 10, 2)
                         .expect("extract unsupported component");
    REQUIRE_EQ(fragments.size(), std::size_t(1));
    const PixelFragment& fragment = fragments.front();
    CHECK_EQ(fragment.originX, -1);
    CHECK_EQ(fragment.originY, 2);
    CHECK_EQ(fragment.width, 2);
    CHECK_EQ(fragment.height, 2);
    CHECK_EQ(fragment.solidCellCount, std::uint32_t(3));
    CHECK_EQ(world.getMaterial(-1, 2), int(MaterialId::Air));
    CHECK_EQ(world.getMaterial(0, 3), int(MaterialId::Air));
    CHECK_EQ(world.getMaterial(4, 9), int(MaterialId::Stone));

    world.setMaterial(20, 20, "stone");
    const auto beforeConflict = world.saveSnapshot().expect("before fragment conflict");
    auto conflict = world.rasterizeFragment(fragment, 20, 20);
    CHECK(!conflict.ok());
    const auto afterConflict = world.saveSnapshot().expect("after fragment conflict");
    CHECK(std::equal(beforeConflict.begin(), beforeConflict.end(), afterConflict.begin()));

    const auto receipt = world.rasterizeFragment(fragment, 30, 30).expect("rasterize fragment");
    CHECK_EQ(receipt.cellsPlaced, std::uint32_t(3));
    CHECK_EQ(world.getMaterial(30, 30), int(MaterialId::Wood));
    CHECK_EQ(world.getMaterial(31, 31), int(MaterialId::Wood));
}

TEST_CASE("pixelworld.fragment_link_is_stale_after_restore_or_clear") {
    PixelWorld world(93);
    world.setMaterial(0, 0, "wood");
    const auto checkpoint = world.saveSnapshot().expect("fragment checkpoint");
    auto fragment = world.extractUnsupportedFragments({-1, -1, 1, 1}, 10)
                        .expect("extract before restore").front();
    world.restoreSnapshot(checkpoint).expect("restore invalidates links");
    CHECK(!world.rasterizeFragment(fragment, 5, 5).ok());

    auto second = world.extractUnsupportedFragments({-1, -1, 1, 1}, 10)
                      .expect("extract before clear").front();
    world.clear();
    CHECK(!world.rasterizeFragment(second, 5, 5).ok());
}

TEST_CASE("pixelworld.empty_chunks_sleep_with_hysteresis_and_emit_removal_tombstones") {
    PixelWorld world(94);
    world.setMaterial(3, 4, "stone");
    const auto populatedRevision = world.revision();
    world.setMaterial(3, 4, "air");
    CHECK_EQ(world.chunkCount(), 1);

    for (std::uint64_t tick = 1; tick < kPixelSleepHysteresisTicks; ++tick) {
        const auto stats = world.advance(eve::SimulationTick(tick)).expect("idle hysteresis tick");
        CHECK_EQ(stats.chunksReclaimed, std::uint32_t(0));
        CHECK_EQ(world.chunkCount(), 1);
    }
    const auto reclaimed = world.advance(eve::SimulationTick(kPixelSleepHysteresisTicks))
                               .expect("chunk reclaim tick");
    CHECK_EQ(reclaimed.chunksReclaimed, std::uint32_t(1));
    CHECK_EQ(world.chunkCount(), 0);

    const auto changed = world.snapshotChangedChunks(populatedRevision);
    REQUIRE_EQ(changed.size(), std::size_t(1));
    CHECK(changed.front().removed);
    CHECK(changed.front().cells.empty());
    CHECK(changed.front().revision > populatedRevision);
}

TEST_CASE("pixelworld.authoritative_chunk_batch_replaces_and_removes_transactionally") {
    PixelWorld authority(95);
    authority.setMaterial(2, 3, "wood");
    const std::uint64_t populatedRevision = authority.revision();
    PixelChunkBatch populated;
    populated.catalogFingerprint = authority.materialCatalogFingerprint();
    populated.sourceSeed = authority.seed();
    populated.sourceRevision = populatedRevision;
    populated.sourceTick = eve::SimulationTick(authority.tickValue());
    populated.sourceLastEditSequence = authority.lastEditSequence();
    populated.chunks = authority.snapshotChangedChunks(0);

    PixelWorld replica(95);
    const auto applied = replica.applyChunkBatch(populated, 0).expect("apply populated Chunk");
    CHECK_EQ(applied.chunksReplaced, std::uint32_t(1));
    CHECK_EQ(replica.getMaterial(2, 3), int(MaterialId::Wood));
    CHECK_EQ(replica.revision(), authority.revision());

    auto fragment = replica.extractUnsupportedFragments({0, 0, 5, 5}, 20)
                        .expect("extract replica fragment")
                        .front();
    authority.setMaterial(2, 3, "air");
    for (std::uint64_t tick = 1; tick <= kPixelSleepHysteresisTicks; ++tick)
        authority.advance(eve::SimulationTick(tick)).expect("reclaim authority Chunk");
    PixelChunkBatch removed;
    removed.catalogFingerprint = authority.materialCatalogFingerprint();
    removed.sourceSeed = authority.seed();
    removed.sourceRevision = authority.revision();
    removed.sourceTick = eve::SimulationTick(authority.tickValue());
    removed.sourceLastEditSequence = authority.lastEditSequence();
    removed.chunks = authority.snapshotChangedChunks(populatedRevision);
    const auto removedReceipt = replica.applyChunkBatch(removed, replica.revision()).expect("apply tombstone");
    CHECK_EQ(removedReceipt.chunksRemoved, std::uint32_t(1));
    CHECK_EQ(replica.chunkCount(), 0);
    CHECK(!replica.rasterizeFragment(fragment, 10, 10).ok());
}

TEST_CASE("pixelworld.authoritative_chunk_batch_rejects_stale_or_malformed_without_mutation") {
    PixelWorld world(96);
    world.setMaterial(0, 0, "stone");
    const auto before = world.saveSnapshot().expect("before bad correction");

    PixelChunkBatch stale;
    stale.catalogFingerprint = world.materialCatalogFingerprint();
    stale.sourceSeed = world.seed();
    stale.sourceRevision = world.revision();
    stale.sourceTick = eve::SimulationTick(world.tickValue());
    CHECK(!world.applyChunkBatch(stale, world.revision() - 1).ok());

    PixelChunkSnapshot malformed;
    malformed.revision = world.revision();
    malformed.cells.resize(1);
    stale.chunks.push_back(std::move(malformed));
    CHECK(!world.applyChunkBatch(stale, world.revision()).ok());
    const auto after = world.saveSnapshot().expect("after bad correction");
    REQUIRE_EQ(before.size(), after.size());
    CHECK(std::equal(before.begin(), before.end(), after.begin()));
}
