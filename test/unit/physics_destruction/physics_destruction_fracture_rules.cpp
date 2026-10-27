// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "physics_destruction_p3.cpp",
    "physics_destruction.cpp"
  ]
}
*/
// clang-format on

#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionSnapshot.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <limits>
#include <string>

using eve::physics::GeometryCollectionAsset;
using eve::physics::GeometryCollectionInstanceSnapshot;

namespace {
eve::Value versionOne(const eve::Value& versionTwo) {
    auto root             = *versionTwo.getIf<eve::Value::Object>();
    root["schemaVersion"] = std::int64_t{1};
    auto bones            = *root["bones"].getIf<eve::Value::Array>();
    for (auto& item : bones) {
        auto fields = *item.getIf<eve::Value::Object>();
        fields.erase("clusterId");
        fields.erase("fractureLevel");
        item = eve::Value(std::move(fields));
    }
    root["bones"] = eve::Value(std::move(bones));
    return eve::Value(std::move(root));
}
}  // namespace

TEST_CASE("Geometry collection asset codec and topology contract") {
    SUB_CASE("geometry collection asset contract: fixture validates and v2 round trip retains fracture metadata") {
        auto created = GeometryCollectionAsset::makeWeldedBoxesFixture(2.5f);
        REQUIRE(created.ok());
        auto asset = std::move(created).takeValue();
        REQUIRE_EQ(asset.bones.size(), 2u);
        REQUIRE_EQ(asset.edges.size(), 1u);
        REQUIRE_EQ(asset.edges.front().strainThreshold, 2.5f);
        asset.bones[0].clusterId     = 3;
        asset.bones[0].fractureLevel = 1;
        asset.bones[1].clusterId     = 7;
        REQUIRE(asset.validate().ok());
        auto encoded = asset.toValue();
        REQUIRE(encoded.ok());
        auto decoded = GeometryCollectionAsset::fromValue(encoded.value());
        REQUIRE(decoded.ok());
        REQUIRE_EQ(decoded.value().bones[0].clusterId, 3);
        REQUIRE_EQ(decoded.value().bones[0].fractureLevel, 1);
        REQUIRE_EQ(decoded.value().bones[1].clusterId, 7);
        REQUIRE_EQ(decoded.value().edges.front().strainThreshold, 2.5f);
    };

    SUB_CASE("geometry collection asset contract: v1 migrates missing cluster data to zero") {
        auto created = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
        REQUIRE(created.ok());
        created.value().bones[0].clusterId     = 9;
        created.value().bones[0].fractureLevel = 2;
        auto encoded                           = created.value().toValue();
        REQUIRE(encoded.ok());
        auto migrated = GeometryCollectionAsset::fromValue(versionOne(encoded.value()));
        REQUIRE(migrated.ok());
        for (const auto& bone : migrated.value().bones) {
            REQUIRE_EQ(bone.clusterId, 0);
            REQUIRE_EQ(bone.fractureLevel, 0);
        }
        REQUIRE_EQ(migrated.value().edges.size(), 1u);
    };

    SUB_CASE("geometry collection asset contract: unknown version and fields are rejected") {
        auto created = GeometryCollectionAsset::makeWeldedBoxesFixture();
        REQUIRE(created.ok());
        auto encoded = created.value().toValue();
        REQUIRE(encoded.ok());
        auto root             = *encoded.value().getIf<eve::Value::Object>();
        root["schemaVersion"] = std::int64_t{99};
        REQUIRE(!GeometryCollectionAsset::fromValue(eve::Value(root)).ok());
        root               = *encoded.value().getIf<eve::Value::Object>();
        root["unexpected"] = true;
        REQUIRE(!GeometryCollectionAsset::fromValue(eve::Value(root)).ok());
        root             = *encoded.value().getIf<eve::Value::Object>();
        auto bones       = *root["bones"].getIf<eve::Value::Array>();
        auto first       = *bones.front().getIf<eve::Value::Object>();
        first["unknown"] = true;
        bones.front()    = eve::Value(std::move(first));
        root["bones"]    = eve::Value(std::move(bones));
        REQUIRE(!GeometryCollectionAsset::fromValue(eve::Value(root)).ok());
    };

    SUB_CASE("geometry collection asset contract: invalid topology and numeric values cannot be serialized") {
        auto created = GeometryCollectionAsset::makeWeldedBoxesFixture();
        REQUIRE(created.ok());
        auto asset           = std::move(created).takeValue();
        asset.edges[0].boneB = 99;
        REQUIRE(!asset.validate().ok());
        REQUIRE(!asset.toValue().ok());
        asset.edges[0].boneB = 1;
        asset.bones[0].mass  = std::numeric_limits<float>::infinity();
        REQUIRE(!asset.validate().ok());
        REQUIRE(!asset.toValue().ok());
    };
}

TEST_CASE("Geometry collection runtime snapshot contract") {
    SUB_CASE("geometry collection snapshot contract: valid runtime values round trip without live links") {
        GeometryCollectionInstanceSnapshot snapshot;
        snapshot.originX            = 2.f;
        snapshot.lastTick           = 40;
        snapshot.sleepBatchRevision = 7;
        GeometryCollectionInstanceSnapshot::BoneSnapshot bone;
        bone.state     = 1;
        bone.clusterId = 3;
        snapshot.bones.push_back(bone);
        REQUIRE(snapshot.validate().ok());
        auto encoded = snapshot.toValue();
        REQUIRE(encoded.ok());
        auto restored = GeometryCollectionInstanceSnapshot::fromValue(encoded.value());
        REQUIRE(restored.ok());
        REQUIRE_EQ(restored.value().originX, 2.f);
        REQUIRE_EQ(restored.value().lastTick, 40);
        REQUIRE_EQ(restored.value().sleepBatchRevision, 7u);
        REQUIRE_EQ(restored.value().bones.front().state, 1u);
        REQUIRE_EQ(restored.value().bones.front().clusterId, 3);
    };

    SUB_CASE("geometry collection snapshot contract: unknown fields and invalid bone state are rejected") {
        GeometryCollectionInstanceSnapshot               snapshot;
        GeometryCollectionInstanceSnapshot::BoneSnapshot bone;
        snapshot.bones.push_back(bone);
        auto encoded = snapshot.toValue();
        REQUIRE(encoded.ok());
        auto root     = *encoded.value().getIf<eve::Value::Object>();
        root["extra"] = true;
        REQUIRE(!GeometryCollectionInstanceSnapshot::fromValue(eve::Value(root)).ok());
        root           = *encoded.value().getIf<eve::Value::Object>();
        auto bones     = *root["bones"].getIf<eve::Value::Array>();
        auto first     = *bones.front().getIf<eve::Value::Object>();
        first["state"] = std::int64_t{3};
        bones.front()  = eve::Value(std::move(first));
        root["bones"]  = eve::Value(std::move(bones));
        REQUIRE(!GeometryCollectionInstanceSnapshot::fromValue(eve::Value(root)).ok());
    };
}

TEST_CASE("Geometry collection schema registration contract") {
    SUB_CASE("geometry collection schema contract: v2 asset and v1 snapshot register idempotently") {
        REQUIRE(GeometryCollectionAsset::ensureSchemaRegistered().ok());
        REQUIRE(GeometryCollectionAsset::ensureSchemaRegistered().ok());
        REQUIRE(eve::schema::SchemaRegistry::resolve(std::string(GeometryCollectionAsset::SchemaId),
                                                     static_cast<int>(GeometryCollectionAsset::SchemaVersion)) !=
                nullptr);
        REQUIRE(GeometryCollectionInstanceSnapshot::ensureSchemaRegistered().ok());
        REQUIRE(eve::schema::SchemaRegistry::resolve(
                    std::string(GeometryCollectionInstanceSnapshot::SchemaId),
                    static_cast<int>(GeometryCollectionInstanceSnapshot::SchemaVersion)) != nullptr);
    };
}
