#include "editor/EditorAuthority.h"
#include "physics_editor/EditorPhysicsTarget.h"
#include "physics_editor/EditorPhysicsAsset.h"
#include "editor/EditorTransactionService.h"

#include "physics/Body3D.h"
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"

#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {

class PolygonResolver final : public IPhysicsColliderAssetResolver {
public:
    EditorResult<PhysicsColliderAssetGeometry> resolve(const std::string& reference,
                                                        const std::string& expectedKind) const override {
        if (reference != "asset://polygon" || expectedKind != "polygon")
            return eve::editing::failed<PhysicsColliderAssetGeometry>(
                EditorStatus::Conflict, RuleId("test.physics.asset"),
                "Unexpected 2D collider asset request");
        PhysicsColliderAssetGeometry geometry;
        geometry.kind = "polygon";
        geometry.vertices = {0.f, 0.f, 80.f, 0.f, 80.f, 40.f, 0.f, 40.f};
        return eve::editing::applied<PhysicsColliderAssetGeometry>(std::move(geometry));
    }
};

SelectionSnapshot selection(const PhysicsColliderPublishingTarget& target) {
    SelectionSnapshot result;
    result.channel = "scene";
    result.items.push_back({SelectionDomain::Scene, TargetId(target.targetId()),
                            StableId("collider"), target.authoringTarget().describe().type});
    result.primary = result.items.front();
    return result;
}

EditorResult<TransactionReceipt> commit(PhysicsColliderPublishingTarget& target,
                                        LocalTransactionBackend& transactions,
                                        const DomainOperation& operation,
                                        const char* id) {
    TransactionSpec specification;
    specification.id = TransactionId(id);
    specification.label = "Publish collider";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun = transactions.begin(std::move(specification));
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

    PhysicsCollider3DRuntimeSink sink(body, initial);
    PhysicsColliderPublishingTarget target("hero-collider", 3, &sink);
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    auto change = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"),
                                                   "sphere", PropertySetMode::Absolute);
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
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value,
             EditorValue("box"));
}

TEST_CASE("editor.physics.live_shape_publication_rejection_preserves_old_shape_and_document") {
    eve::physics::World3D world(0.f, 0.f, 0.f, true);
    eve::physics::Body3D* body = world.newBody("static", 0.f, 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Shape3D* initial = body->newBoxShape(2.f, 3.f, 4.f);
    REQUIRE(initial);
    const auto initialHandle = initial->runtimeHandle();

    PhysicsCollider3DRuntimeSink sink(body, initial);
    PhysicsColliderPublishingTarget target("mesh-collider", 3, &sink);
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    auto invalid = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"),
                                                    "triangle-mesh", PropertySetMode::Absolute);
    REQUIRE(invalid.ok());
    const Revision before = target.revision();
    auto failed = commit(target, transactions, invalid.value(), "physics.live.invalid-mesh");
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
    CHECK_EQ(sink.shape(), initial);
    CHECK(initial->isValid());
    CHECK_EQ(initial->runtimeHandle(), initialHandle);
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value,
             EditorValue("box"));
}

TEST_CASE("editor.physics.live_2d_polygon_publication_swaps_after_build_and_undoes") {
    eve::physics::World world(0.f, 980.f, true, 100.f);
    eve::physics::Body* body = world.newBody("static", 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Fixture* initial = body->newRectangleFixture(50.f, 50.f);
    REQUIRE(initial);
    PolygonResolver resolver;
    PhysicsCollider2DRuntimeSink sink(body, initial, &resolver);
    PhysicsColliderPublishingTarget target("hero-fixture", 2, &sink);
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);

    auto asset = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.asset"),
                                                  "asset://polygon", PropertySetMode::Absolute);
    REQUIRE(asset.ok());
    REQUIRE(commit(target, transactions, asset.value(), "physics.live.2d-asset").ok());
    eve::physics::Fixture* boxWithAsset = sink.fixture();
    REQUIRE(boxWithAsset);
    CHECK(!initial->raw());

    auto polygon = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"),
                                                    "polygon", PropertySetMode::Absolute);
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
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value,
             EditorValue("box"));
}
