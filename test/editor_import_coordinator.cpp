#include "editor/EditorAssetDatabase.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
ImportProduct product(const ImportTicket& ticket) {
    ImportProduct result;
    result.record.guid = ticket.asset;
    result.record.logicalUri = "asset://animations/walk";
    result.record.typeId = "animation-clip";
    result.record.sourceUri = "source://walk.fbx";
    result.record.sourceHash = ticket.sourceHash;
    result.record.importerId = ticket.importerId;
    result.record.importerVersion = ticket.importerVersion;
    result.record.artifacts = {"cache://walk.eva"};
    return result;
}
}

TEST_CASE("editor.import.generation_tickets_reject_stale_or_incomplete_worker_products") {
    MemoryAssetDatabase database;
    ImportCoordinator coordinator(&database);
    auto first = coordinator.begin(AssetGuid("walk"), "hash-a", "fbx-animation", 2);
    auto latest = coordinator.begin(AssetGuid("walk"), "hash-b", "fbx-animation", 2);
    REQUIRE(first.ok()); REQUIRE(latest.ok());
    CHECK_EQ(static_cast<int>(coordinator.publish(first.value(), product(first.value())).code()),
             static_cast<int>(EditorStatus::Conflict));
    ImportProduct incomplete = product(latest.value());
    incomplete.record.artifacts.clear();
    CHECK_EQ(static_cast<int>(coordinator.publish(latest.value(), std::move(incomplete)).code()),
             static_cast<int>(EditorStatus::Rejected));
    auto published = coordinator.publish(latest.value(), product(latest.value()));
    REQUIRE(published.ok()); CHECK_EQ(published.value().sourceHash, "hash-b");
    REQUIRE(database.find(AssetGuid("walk")).ok());
}
