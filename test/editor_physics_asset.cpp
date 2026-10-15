#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/editor/EditorPhysicsAsset.h"
#include "physics/editor/EditorPhysicsTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_physics_asset.cpp
using namespace eve::editor;

namespace {
AssetRecord collider(const char* id, const char* uri, EditorValue metadata) {
    AssetRecord record;
    record.guid       = AssetGuid(id);
    record.logicalUri = uri;
    record.typeId     = "mesh";
    record.sourceUri  = std::string("source/") + id;
    record.sourceHash = "hash";
    record.importerId = "physics-test";
    record.artifacts  = {std::string("artifact/") + id};
    record.metadata   = std::move(metadata);
    return record;
}
}  // namespace

TEST_CASE("editor.physics.asset_resolver_validates_triangle_geometry_and_uri_lookup") {
    MemoryAssetDatabase database;
    EditorValue::Array  vertices{0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    EditorValue::Array  indices{int64_t{0}, int64_t{1}, int64_t{2}};
    CHECK(database
              .publish(collider(
                  "triangle", "physics/triangle",
                  EditorValue::Object{{"kind", "triangle-mesh"}, {"vertices", vertices}, {"indices", indices}}))
              .ok());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    auto                                 resolved = resolver.resolve("physics/triangle", "triangle-mesh");
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().vertices.size(), 9U);
    CHECK_EQ(resolved.value().indices.size(), 3U);
    CHECK_EQ(static_cast<int>(resolver.resolve("physics/triangle", "convex-hull").code()),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.physics.asset_resolver_rejects_out_of_range_indices_and_builds_height_metadata") {
    MemoryAssetDatabase database;
    CHECK(database
              .publish(collider(
                  "bad", "physics/bad",
                  EditorValue::Object{{"kind", "triangle-mesh"},
                                      {"vertices", EditorValue::Array{0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0}},
                                      {"indices", EditorValue::Array{int64_t{0}, int64_t{1}, int64_t{9}}}}))
              .ok());
    CHECK(database
              .publish(collider("terrain", "physics/terrain",
                                EditorValue::Object{{"kind", "height-field"},
                                                    {"countX", int64_t{2}},
                                                    {"countZ", int64_t{2}},
                                                    {"cellSizeX", 1.0},
                                                    {"cellSizeZ", 2.0},
                                                    {"heights", EditorValue::Array{0.0, 1.0, 2.0, 3.0}}}))
              .ok());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    CHECK_EQ(static_cast<int>(resolver.resolve("bad", "triangle-mesh").code()),
             static_cast<int>(EditorStatus::Rejected));
    auto height = resolver.resolve("terrain", "height-field");
    REQUIRE(height.ok());
    CHECK_EQ(height.value().countX, 2);
    CHECK_EQ(height.value().countZ, 2);
    CHECK_EQ(height.value().minimumHeight, 0.0F);
    CHECK_EQ(height.value().maximumHeight, 3.0F);
}

TEST_CASE("editor.physics.asset_resolver_validates_2d_polygon_and_chain_metadata") {
    MemoryAssetDatabase database;
    CHECK(database
              .publish(collider(
                  "polygon", "physics/polygon",
                  EditorValue::Object{{"kind", "polygon"},
                                      {"vertices", EditorValue::Array{0.0, 0.0, 2.0, 0.0, 2.0, 1.0, 0.0, 1.0}}}))
              .ok());
    CHECK(database
              .publish(collider("chain", "physics/chain",
                                EditorValue::Object{{"kind", "chain"},
                                                    {"loop", true},
                                                    {"vertices", EditorValue::Array{0.0, 0.0, 1.0, 0.0, 1.0, 1.0}}}))
              .ok());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    auto                                 polygon = resolver.resolve("physics/polygon", "polygon");
    REQUIRE(polygon.ok());
    CHECK_EQ(polygon.value().vertices.size(), size_t{8});
    auto chain = resolver.resolve("physics/chain", "chain");
    REQUIRE(chain.ok());
    CHECK(chain.value().loop);

    CHECK(database
              .publish(collider("concave", "physics/concave",
                                EditorValue::Object{{"kind", "polygon"},
                                                    {"vertices", EditorValue::Array{0.0, 0.0, 2.0, 0.0, 1.0, 0.5, 2.0,
                                                                                    1.0, 0.0, 1.0}}}))
              .ok());
    CHECK_EQ(static_cast<int>(resolver.resolve("physics/concave", "polygon").code()),
             static_cast<int>(EditorStatus::Rejected));
}

// editor_physics_publication.cpp
using namespace eve::editor;

namespace {

class PolygonResolver final : public IPhysicsColliderAssetResolver {
public:
    Result<PhysicsColliderAssetGeometry> resolve(const std::string& reference,
                                                 const std::string& expectedKind) const override {
        if (reference != "asset://polygon" || expectedKind != "polygon")
            return eve::editing::failed<PhysicsColliderAssetGeometry>(
                EditorStatus::Conflict, RuleId("test.physics.asset"), "Unexpected 2D collider asset request");
        PhysicsColliderAssetGeometry geometry;
        geometry.kind     = "polygon";
        geometry.vertices = {0.f, 0.f, 80.f, 0.f, 80.f, 40.f, 0.f, 40.f};
        return eve::editing::applied<PhysicsColliderAssetGeometry>(std::move(geometry));
    }
};

SelectionSnapshot selection(const PhysicsColliderPublishingTarget& target) {
    SelectionSnapshot result;
    result.channel = "scene";
    result.items.push_back({SelectionDomain::Scene, TargetId(target.targetId()), StableId("collider"),
                            target.authoringTarget().describe().type});
    result.primary = result.items.front();
    return result;
}

Result<TransactionReceipt> commit(PhysicsColliderPublishingTarget& target, LocalTransactionBackend& transactions,
                                  const DomainOperation& operation, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Publish collider";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.physics.begin"),
                                                        "Could not begin collider transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok())
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.physics.append"),
                                                        "Could not append collider operation");
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.physics.live_shape_publication_swaps_after_build_and_undoes") {
    eve::physics::World3D world(0.f, -9.8f, 0.f, true);
    eve::physics::Body3D* body = world.newBody("static", 0.f, 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Shape3D* initial = body->newBoxShape(1.f, 1.f, 1.f);
    REQUIRE(initial);
    const auto initialHandle = initial->runtimeHandle();

    PhysicsCollider3DRuntimeSink    sink(body, initial);
    PhysicsColliderPublishingTarget target("hero-collider", 3, &sink);
    LocalWorldAuthority             authority(&target);
    LocalTransactionBackend         transactions(&authority);
    auto change = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"), "sphere",
                                                   PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(commit(target, transactions, change.value(), "physics.live.sphere").ok());
    REQUIRE(sink.shape());
    CHECK_EQ(sink.shape()->getKind(), std::string("sphere"));
    CHECK(!initial->isValid());
    CHECK(sink.shape()->runtimeHandle() != initialHandle);
    eve::physics::Shape3D* sphere = sink.shape();

    REQUIRE(transactions.undo().ok());
    REQUIRE(sink.shape());
    CHECK_EQ(sink.shape()->getKind(), std::string("box"));
    CHECK(!sphere->isValid());
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value, EditorValue("box"));
}

TEST_CASE("editor.physics.live_shape_publication_rejection_preserves_old_shape_and_document") {
    eve::physics::World3D world(0.f, 0.f, 0.f, true);
    eve::physics::Body3D* body = world.newBody("static", 0.f, 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Shape3D* initial = body->newBoxShape(2.f, 3.f, 4.f);
    REQUIRE(initial);
    const auto initialHandle = initial->runtimeHandle();

    PhysicsCollider3DRuntimeSink    sink(body, initial);
    PhysicsColliderPublishingTarget target("mesh-collider", 3, &sink);
    LocalWorldAuthority             authority(&target);
    LocalTransactionBackend         transactions(&authority);
    auto invalid = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"), "triangle-mesh",
                                                    PropertySetMode::Absolute);
    REQUIRE(invalid.ok());
    const Revision before = target.revision();
    auto           failed = commit(target, transactions, invalid.value(), "physics.live.invalid-mesh");
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
    CHECK_EQ(sink.shape(), initial);
    CHECK(initial->isValid());
    CHECK_EQ(initial->runtimeHandle(), initialHandle);
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value, EditorValue("box"));
}

TEST_CASE("editor.physics.live_2d_polygon_publication_swaps_after_build_and_undoes") {
    eve::physics::World world(0.f, 980.f, true, 100.f);
    eve::physics::Body* body = world.newBody("static", 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Fixture* initial = body->newRectangleFixture(50.f, 50.f);
    REQUIRE(initial);
    PolygonResolver                 resolver;
    PhysicsCollider2DRuntimeSink    sink(body, initial, &resolver);
    PhysicsColliderPublishingTarget target("hero-fixture", 2, &sink);
    LocalWorldAuthority             authority(&target);
    LocalTransactionBackend         transactions(&authority);

    auto asset = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.asset"), "asset://polygon",
                                                  PropertySetMode::Absolute);
    REQUIRE(asset.ok());
    REQUIRE(commit(target, transactions, asset.value(), "physics.live.2d-asset").ok());
    eve::physics::Fixture* boxWithAsset = sink.fixture();
    REQUIRE(boxWithAsset);
    CHECK(!initial->raw());

    auto polygon = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"), "polygon",
                                                    PropertySetMode::Absolute);
    REQUIRE(polygon.ok());
    REQUIRE(commit(target, transactions, polygon.value(), "physics.live.2d-polygon").ok());
    eve::physics::Fixture* livePolygon = sink.fixture();
    REQUIRE(livePolygon);
    CHECK(livePolygon->raw());
    CHECK(!boxWithAsset->raw());
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value,
             EditorValue("polygon"));

    REQUIRE(transactions.undo().ok());
    REQUIRE(sink.fixture());
    CHECK(sink.fixture()->raw());
    CHECK(!livePolygon->raw());
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value, EditorValue("box"));
}
