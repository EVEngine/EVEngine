#include "editor/EditorCrowdDocument.h"

#include "crowd/Crowd.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
CrowdPathRecord patrolPath() {
    return {StableId("patrol"), "Patrol", true,
            {{StableId("a"), 0.0, 0.0, 0.5, 0.0},
             {StableId("b"), 5.0, 0.0, 0.5, 1.0}}};
}

CrowdAgentRecord guard() {
    CrowdAgentRecord result;
    result.id = StableId("guard");
    result.archetype = "guard";
    result.x = 1.0;
    result.y = 2.0;
    result.behavior = "path";
    result.path = StableId("patrol");
    return result;
}
}  // namespace

TEST_CASE("editor.crowd.agent_zone_path_operations_are_reversible") {
    CrowdDocumentTarget document("level-ai");
    auto path = document.makeSetPath(patrolPath());
    REQUIRE(path.value);
    CHECK(document.applyDomainOperation(*path.value).accepted());

    auto agent = document.makeSetAgent(guard());
    REQUIRE(agent.value);
    CHECK(document.applyDomainOperation(*agent.value).accepted());
    CHECK_EQ(document.agents().size(), 1U);
    CHECK_EQ(document.paths().size(), 1U);
    CHECK_EQ(static_cast<int>(document.makeDeletePath(StableId("patrol")).status),
             static_cast<int>(EditorStatus::Conflict));

    DomainOperation undoAgent = *agent.value;
    undoAgent.type = agent.value->inverseType;
    undoAgent.payload = agent.value->inverse;
    CHECK(document.applyDomainOperation(undoAgent).accepted());
    CHECK(document.agents().empty());
    CHECK(document.makeDeletePath(StableId("patrol")).accepted());

    CrowdZoneRecord zone{StableId("gate"), "Gate", "sense",
                         {{{0.0, 0.0}}, {{3.0, 0.0}}, {{3.0, 2.0}}, {{0.0, 2.0}}}, 2.0, true};
    auto setZone = document.makeSetZone(zone);
    REQUIRE(setZone.value);
    CHECK(document.applyDomainOperation(*setZone.value).accepted());
    CHECK_EQ(document.zones().front().points.size(), 4U);
}

TEST_CASE("editor.crowd.snapshot_and_overlay_are_revision_and_budget_safe") {
    CrowdDocumentTarget source("source");
    REQUIRE(source.makeSetPath(patrolPath()).value);
    CHECK(source.applyDomainOperation(*source.makeSetPath(patrolPath()).value).accepted());
    CHECK(source.applyDomainOperation(*source.makeSetAgent(guard()).value).accepted());

    CrowdDocumentTarget loaded("loaded");
    CHECK(loaded.loadSnapshot(source.snapshotValue()).accepted());
    CHECK(loaded.validate().empty());
    auto overlay = loaded.overlay();
    CHECK_EQ(static_cast<int>(overlay.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(overlay.revision, loaded.revision());
    CHECK_EQ(overlay.primitives.size(), 2U);
    CHECK_EQ(static_cast<int>(loaded.overlay(1).status), static_cast<int>(EditorStatus::Rejected));

    EditorValue invalid = EditorValue::Object{
        {"schemaVersion", int64_t{1}}, {"paths", EditorValue::Array{}}, {"zones", EditorValue::Array{}},
        {"agents", EditorValue::Array{EditorValue::Object{
            {"id", "lost"}, {"archetype", "guard"}, {"x", 0.0}, {"y", 0.0},
            {"heading", 0.0}, {"radius", 0.5}, {"maximumSpeed", 1.0},
            {"behavior", "path"}, {"path", "missing"}}}}};
    CHECK_EQ(static_cast<int>(loaded.loadSnapshot(invalid).status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(loaded.agents().size(), 1U);
}

TEST_CASE("editor.crowd.document_applies_to_named_runtime_agents") {
    CrowdDocumentTarget document("runtime-source");
    CHECK(document.applyDomainOperation(*document.makeSetPath(patrolPath()).value).accepted());
    CHECK(document.applyDomainOperation(*document.makeSetAgent(guard()).value).accepted());
    eve::crowd::Crowd runtime;
    CrowdRuntimeApplier applier;
    CHECK(applier.apply(document, &runtime).accepted());
    CHECK_EQ(runtime.getAgentCount(), 1);
    CHECK(runtime.hasNamedAgent("guard"));
    const int index = runtime.getNamedAgentIndex("guard");
    CHECK_EQ(runtime.getAgentAction(index), std::string("seek"));
}
