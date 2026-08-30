#include "editor/EvaAssetDatabaseProjection.h"

#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.asset.evaProjectionPublishesOneGenerationAndRejectsAtomically") {
    MemoryAssetDatabase    database;
    eve::asset::EvaManifest manifest;
    manifest.packageId      = *eve::PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    manifest.packageName    = "terrain-kit";
    manifest.packageVersion = "1.0.0";
    auto terrain = eve::AssetRef::parse("asset://018f6f22-2490-7ad2-bf58-4f1dbca31041").takeValue();
    auto material = eve::AssetRef::parse("asset://018f6f22-2490-7ad2-bf58-4f1dbca31042").takeValue();
    manifest.assets = {
        {terrain, "eve.terrain", eve::SchemaVersion(1), "assets/terrain.json",
         "sha256:" + std::string(64, '1'), {}},
        {material, "eve.terrain-material", eve::SchemaVersion(1), "assets/material.json",
         "sha256:" + std::string(64, '2'), {"ground"}},
    };
    manifest.dependencies.push_back(
        {terrain, material, eve::asset::EvaDependencyKind::RuntimeRequired, "material", {},
         "eve.terrain-material", {}});

    auto published = publishEvaAssetProjection(database, manifest, "project://Assets/terrain.eva", "unity/1");
    REQUIRE(published.isAccepted());
    CHECK_EQ(published.value->size(), std::size_t(2));
    CHECK_EQ(database.generation(), std::uint64_t(1));
    CHECK_EQ(database.dependencies(AssetGuid(terrain.id().format())).size(), std::size_t(1));

    auto conflicting = manifest;
    conflicting.assets[1].asset = terrain;
    auto rejected = publishEvaAssetProjection(database, conflicting, "project://Assets/bad.eva", "unity/1");
    CHECK(!rejected.isAccepted());
    CHECK_EQ(database.generation(), std::uint64_t(1));
    CHECK_EQ(database.find(AssetGuid(material.id().format())).value->sourceUri,
             std::string("project://Assets/terrain.eva#assets/material.json"));
}
