// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "modules": [
    "procgen",
    "scene"
  ],
  "origins": [
    "scene_procgen_rotation.cpp"
  ],
  "module": "scene"
}
*/
// clang-format on

#include <cmath>
#include <limits>
#include <stdexcept>
#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("Scene.procgenSink.deltaSnapshotBuilderPreservesIdentityAndRejectsMalformedOrder") {
    eve::ProcgenInstanceDesc first;
    first.id             = "first";
    first.sourcePointId  = 1;
    auto second          = first;
    second.id            = "second";
    second.sourcePointId = 2;
    std::vector<eve::ProcgenInstanceDesc> current{first, second};
    auto                                  moved = first;
    moved.id                                    = "renamed";
    moved.x                                     = 10.f;
    auto added                                  = first;
    added.id                                    = "added";
    added.sourcePointId                         = 3;
    eve::ProcgenInstanceDelta delta;
    delta.baseRevision     = 7;
    delta.targetRevision   = 8;
    delta.updated          = {moved};
    delta.added            = {added};
    delta.removedPointIds  = {2};
    delta.targetPointOrder = {3, 1};
    auto target            = eve::buildProcgenInstanceDeltaSnapshot(current, 7, delta);
    REQUIRE(target.ok());
    REQUIRE(target.value().size() == 2u);
    REQUIRE(target.value()[0].id == "added");
    REQUIRE(target.value()[1].id == "renamed");
    REQUIRE(target.value()[1].x == 10.f);
    REQUIRE(current[0].id == "first");
    REQUIRE(current[0].x == 0.f);
    delta.targetPointOrder = {1, 1};
    REQUIRE(!eve::buildProcgenInstanceDeltaSnapshot(current, 7, delta).ok());
    delta.targetPointOrder = {3, 1};
    REQUIRE(!eve::buildProcgenInstanceDeltaSnapshot(current, 6, delta).ok());
    current.push_back(first);
    REQUIRE(!eve::buildProcgenInstanceDeltaSnapshot(current, 7, delta).ok());
}

TEST_CASE("Scene.procgenSink.coordinatesReplacementWithoutLeakingCandidateHosts") {
    auto* scene = eve::scene::Scene::create();
    auto* sink  = eve::cap::query<eve::IProcgenSceneSink>();
    REQUIRE(sink != nullptr);
    eve::ProcgenInstanceDesc first;
    first.id            = "existing-facility";
    first.sourcePointId = 1;
    first.x             = 1.f;
    REQUIRE(sink->replaceBatches({{"coordinated-existing", 1, {first}}}).ok());
    auto changed        = first;
    changed.x           = 10.f;
    auto added          = first;
    added.id            = "new-facility";
    added.sourcePointId = 2;
    std::vector<eve::ProcgenBatchSnapshot> target{{"coordinated-existing", 2, {changed}},
                                                  {"coordinated-new", 1, {added}}};
    int                                    commits  = 0;
    auto                                   rejected = sink->replaceBatchesCoordinated(target, [&] {
        ++commits;
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected owner failure"));
    });
    REQUIRE(!rejected.ok());
    REQUIRE(commits == 1);
    REQUIRE(sink->batchRevision("coordinated-existing") == 1);
    REQUIRE(sink->batchRevision("coordinated-new") == 0);
    REQUIRE(!scene->findHost("__pcg/coordinated-new").ok());
    auto originalHost = scene->findHost("__pcg/coordinated-existing");
    REQUIRE(originalHost.ok());
    REQUIRE(originalHost.value()->findById(first.id).value()->x == 1.f);
    auto threw = sink->replaceBatchesCoordinated(
        target, []() -> eve::Result<void> { throw std::runtime_error("injected owner exception"); });
    REQUIRE(!threw.ok());
    REQUIRE(!scene->findHost("__pcg/coordinated-new").ok());
    REQUIRE(sink->batchRevision("coordinated-existing") == 1);
    auto reentry = sink->replaceBatchesCoordinated(target, [&] {
        REQUIRE(!sink->removeBatch("coordinated-existing"));
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "cancel after rejected reentry"));
    });
    REQUIRE(!reentry.ok());
    REQUIRE(sink->batchRevision("coordinated-existing") == 1);
    REQUIRE(!sink->replaceBatchesCoordinated(target, {}).ok());
    REQUIRE(!scene->findHost("__pcg/coordinated-new").ok());
    {
        ecs::ScopedDefer deferred;
        REQUIRE(!sink->replaceBatchesCoordinated(target,
                                                 [&] {
                                                     ++commits;
                                                     return eve::Result<void>::success();
                                                 })
                     .ok());
    }
    REQUIRE(commits == 1);
    int  notifications              = 0;
    auto invalidTarget              = target;
    invalidTarget[0].instances[0].x = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!sink->replaceBatchesCoordinated(invalidTarget,
                                             [&] {
                                                 ++commits;
                                                 return eve::Result<void>::success();
                                             })
                 .ok());
    REQUIRE(commits == 1);
    originalHost.value()->setEventHandler([&](auto*, const auto&, const auto&, const auto&) {
        ++notifications;
        REQUIRE(sink->batchRevision("coordinated-existing") == 2);
        REQUIRE(sink->batchRevision("coordinated-new") == 1);
        REQUIRE(originalHost.value()->findById(first.id).value()->world[3].x == 10.f);
        auto newHost = scene->findHost("__pcg/coordinated-new");
        REQUIRE(newHost.ok());
        REQUIRE(newHost.value()->findById(added.id).value()->world[3].x == added.x);
        throw std::runtime_error("injected post-commit notification exception");
    });
    auto committed = sink->replaceBatchesCoordinated(target, [&] {
        ++commits;
        return eve::Result<void>::success();
    });
    REQUIRE(committed.ok());
    REQUIRE(committed.value() == 2);
    REQUIRE(committed.status().code() == eve::StatusCode::Applied);
    REQUIRE(committed.status().hasDiagnostics());
    REQUIRE(notifications == 1);
    originalHost.value()->setEventHandler({});
    REQUIRE(commits == 2);
    REQUIRE(sink->batchRevision("coordinated-existing") == 2);
    REQUIRE(sink->batchRevision("coordinated-new") == 1);
    REQUIRE(originalHost.value()->findById(first.id).value()->x == 10.f);
    REQUIRE(scene->findHost("__pcg/coordinated-new").ok());
    REQUIRE(!sink->replaceBatchesCoordinated(target,
                                             [&] {
                                                 ++commits;
                                                 return eve::Result<void>::success();
                                             })
                 .ok());
    REQUIRE(commits == 2);
    auto rejectedRemoval = sink->removeBatchesCoordinated(
        {"coordinated-existing", "coordinated-new"},
        []() -> eve::Result<void> { throw std::runtime_error("injected removal owner exception"); });
    REQUIRE(!rejectedRemoval.ok());
    REQUIRE(sink->batchRevision("coordinated-existing") == 2);
    REQUIRE(sink->batchRevision("coordinated-new") == 1);
    REQUIRE(sink->removeBatches({"coordinated-existing", "coordinated-new"}).ok());
}

TEST_CASE("Scene.procgenSink.preservesDegreeRotationAcrossSnapshotsAndDeltas") {
    auto* scene = eve::scene::Scene::create();
    auto* sink  = eve::cap::query<eve::IProcgenSceneSink>();
    REQUIRE(sink != nullptr);
    eve::ProcgenInstanceDesc item;
    item.id            = "sloped-fence";
    item.sourcePointId = 1;
    item.yaw           = 90.f;
    item.pitch         = 30.f;
    item.roll          = -45.f;
    auto asset         = eve::AssetRef::parse("asset://018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
    auto build         = eve::PersistentId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd192");
    REQUIRE(asset.ok());
    REQUIRE(build.has_value());
    item.asset        = asset.value().format();
    item.assetBinding = eve::ProcgenInstanceAssetBinding{asset.value(), *build};
    REQUIRE(sink->replaceBatch("road-rotation-test", 1, {item}).ok());
    auto host = scene->findHost("__pcg/road-rotation-test");
    REQUIRE(host.ok());
    auto node = host.value()->findById(item.id);
    REQUIRE(node.ok());
    REQUIRE(host.value()->hasTag(node.value(), "pcg.asset-build:" + build->format()));
    constexpr float radians = 0.017453292519943295f;
    REQUIRE(std::fabs(node.value()->yaw - item.yaw * radians) < 1e-5f);
    REQUIRE(std::fabs(node.value()->pitch - item.pitch * radians) < 1e-5f);
    REQUIRE(std::fabs(node.value()->roll - item.roll * radians) < 1e-5f);
    eve::ProcgenInstanceDelta delta;
    delta.baseRevision     = 1;
    delta.targetRevision   = 2;
    item.pitch             = -20.f;
    item.roll              = 15.f;
    delta.updated          = {item};
    delta.targetPointOrder = {1};
    REQUIRE(sink->applyDelta("road-rotation-test", delta).ok());
    node = host.value()->findById(item.id);
    REQUIRE(node.ok());
    REQUIRE(std::fabs(node.value()->pitch - item.pitch * radians) < 1e-5f);
    REQUIRE(std::fabs(node.value()->roll - item.roll * radians) < 1e-5f);
    delta.updated[0].pitch = 99.f;
    const auto persisted   = scene->serializeHostAt("__pcg/road-rotation-test");
    REQUIRE(persisted.find("pcg.asset-build:" + build->format()) != std::string::npos);
    REQUIRE(scene->deserializeHostAt("road-rotation-restored", persisted));
    auto restoredHost = scene->findHost("road-rotation-restored");
    REQUIRE(restoredHost.ok());
    auto restoredNode = restoredHost.value()->findById(item.id);
    REQUIRE(restoredNode.ok());
    REQUIRE(restoredHost.value()->hasTag(restoredNode.value(), "pcg.asset:" + asset.value().format()));
    REQUIRE(restoredHost.value()->hasTag(restoredNode.value(), "pcg.asset-build:" + build->format()));
    REQUIRE(std::fabs(restoredNode.value()->pitch - item.pitch * radians) < 1e-5f);
    REQUIRE(std::fabs(restoredNode.value()->roll - item.roll * radians) < 1e-5f);
    REQUIRE(!sink->applyDelta("road-rotation-test", delta).ok());
    REQUIRE(std::fabs(host.value()->findById(item.id).value()->pitch - item.pitch * radians) < 1e-5f);
    auto invalid = item;
    invalid.roll = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!sink->replaceBatch("road-rotation-test", 3, {invalid}).ok());
    REQUIRE(sink->batchRevision("road-rotation-test") == 2);
    auto mismatched  = item;
    mismatched.asset = "another-asset";
    REQUIRE(!sink->replaceBatch("road-rotation-test", 3, {mismatched}).ok());
    REQUIRE(sink->batchRevision("road-rotation-test") == 2);
    REQUIRE(std::fabs(host.value()->findById(item.id).value()->roll - item.roll * radians) < 1e-5f);
    REQUIRE(sink->removeBatch("road-rotation-test"));
}
