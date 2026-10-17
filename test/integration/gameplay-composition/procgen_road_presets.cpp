#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen_editing;
using namespace eve::procgen::road;
TEST_CASE("procgen.road.authoring.presetV1RollDefaultsMigrateWithoutChangingSavedBehavior") {
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(source.ok());
    draft.source   = std::move(source).takeValue();
    draft.types    = {{"root", "", {}}};
    draft.bindings = {{1, "root", {}}};
    auto created   = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto document = std::move(created).takeValue();
    REQUIRE(document->network().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);
    auto capture = document->capture();
    REQUIRE(capture.ok());
    REQUIRE(*capture.value().getIf<eve::Value::Object>()->at("schemaVersion").getIf<std::int64_t>() == 3);
    capture.value().getIf<eve::Value::Object>()->at("schemaVersion") = 1;
    auto prepared                                                    = document->prepareRestore(capture.value());
    REQUIRE(prepared.ok());
    REQUIRE(document->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(document->network().edges()[0].style.rollInterpolation == RoadRollInterpolation::Linear);
    REQUIRE(document->beginEdit().types[0].overrides.rollInterpolation == RoadRollInterpolation::Linear);
    auto edit                                 = document->beginEdit();
    edit.types[0].overrides.rollInterpolation = RoadRollInterpolation::SmoothStep;
    auto update                               = document->prepare(std::move(edit));
    REQUIRE(update.ok());
    REQUIRE(document->commit(std::move(update).takeValue()).ok());
    REQUIRE(document->network().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);
    REQUIRE(document->undo().ok());
    REQUIRE(document->network().edges()[0].style.rollInterpolation == RoadRollInterpolation::Linear);
    REQUIRE(document->redo().ok());
    REQUIRE(document->network().edges()[0].style.rollInterpolation == RoadRollInterpolation::SmoothStep);
}
namespace {
RoadPresetDraft presetDraft() {
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeStraight(36.f, 2);
    if (!source.ok()) throw std::runtime_error(source.status().describe());
    draft.source = std::move(source).takeValue();
    auto c       = draft.source.addNode(36.f, 0.f, 0.f);
    if (!c.ok()) throw std::runtime_error(c.status().describe());
    auto edge = draft.source.addEdge(2, c.value(), {{18, 0, 0}, {36, 0, 0}}, 2);
    if (!edge.ok()) throw std::runtime_error(edge.status().describe());
    RoadTypePreset project{"project.urban", "", {}};
    project.overrides.laneWidth      = 3.f;
    project.overrides.curbWidth      = 0.f;
    project.overrides.curbHeight     = 0.f;
    project.overrides.sidewalkWidth  = 0.f;
    project.overrides.sidewalkHeight = 0.f;
    RoadTypePreset scene{"scene.urban", "project.urban", {}};
    scene.overrides.speedLimitMps         = 10.f;
    draft.types                           = {project, scene};
    draft.bindings                        = {{1, "scene.urban", {}}, {edge.value(), "scene.urban", {}}};
    draft.bindings[1].overrides.laneWidth = 4.f;
    return draft;
}
}  // namespace
TEST_CASE("procgen.road.authoring.sharedPresetsPropagateWithExplicitOverridesAndBake") {
    auto created = RoadPresetDocument::create(presetDraft());
    REQUIRE(created.ok());
    auto document = std::move(created).takeValue();
    REQUIRE(document->network().edges()[0].style.laneWidth == 3.f);
    REQUIRE(document->network().edges()[1].style.laneWidth == 4.f);
    REQUIRE(document->network().edges()[0].style.speedLimitMps == 10.f);
    auto before = bakeRoadNetwork(document->network());
    REQUIRE(before.ok());
    auto draft = document->beginEdit();
    for (auto& type : draft.types)
        if (type.id == "project.urban") type.overrides.laneWidth = 5.f;
    auto prepared = document->prepare(std::move(draft));
    REQUIRE(prepared.ok());
    REQUIRE(document->network().edges()[0].style.laneWidth == 3.f);
    REQUIRE(document->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(document->network().edges()[0].style.laneWidth == 5.f);
    REQUIRE(document->network().edges()[1].style.laneWidth == 4.f);
    auto after = bakeRoadNetwork(document->network());
    REQUIRE(after.ok());
    REQUIRE(after.value().mesh.getVertexCount() > 0);
    REQUIRE(after.value().mesh.getPositionZ(0) != before.value().mesh.getPositionZ(0));
    REQUIRE(document->undo().ok());
    REQUIRE(document->network().edges()[0].style.laneWidth == 3.f);
    auto saved = document->capture();
    REQUIRE(saved.ok());
    auto clone = RoadPresetDocument::create(presetDraft());
    REQUIRE(clone.ok());
    auto restore = clone.value()->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(clone.value()->commit(std::move(restore).takeValue()).ok());
    REQUIRE(clone.value()->redo().ok());
    REQUIRE(clone.value()->network().edges()[0].style.laneWidth == 5.f);
    REQUIRE(clone.value()->undo().ok());
    auto unchanged = clone.value()->capture();
    REQUIRE(unchanged.ok());
    REQUIRE(unchanged.value() == saved.value());
}
TEST_CASE("procgen.road.authoring.presetCandidatesRejectCyclesStaleOwnersAndLateFailuresAtomically") {
    auto document = RoadPresetDocument::create(presetDraft());
    auto other    = RoadPresetDocument::create(presetDraft());
    REQUIRE(document.ok());
    REQUIRE(other.ok());
    const auto saved = document.value()->capture();
    REQUIRE(saved.ok());
    auto                          oversized = document.value()->beginEdit();
    std::vector<RoadControlPoint> manyPoints;
    for (int i = 0; i <= 4096; ++i) manyPoints.push_back({-18.f + 36.f * static_cast<float>(i) / 4096.f, 0, 0});
    REQUIRE(oversized.source.setEdgeControlPoints(1, std::move(manyPoints)).ok());
    REQUIRE(!document.value()->prepare(std::move(oversized)).ok());
    auto draft              = document.value()->beginEdit();
    draft.types[0].parentId = draft.types[1].id;
    REQUIRE(!document.value()->prepare(draft).ok());
    draft                        = document.value()->beginEdit();
    draft.bindings.back().edgeId = 9999;
    REQUIRE(!document.value()->prepare(draft).ok());
    draft                                     = document.value()->beginEdit();
    draft.bindings.back().overrides.laneWidth = -1.f;
    REQUIRE(!document.value()->prepare(draft).ok());
    auto first = document.value()->prepare(document.value()->beginEdit());
    auto stale = document.value()->prepare(document.value()->beginEdit());
    REQUIRE(first.ok());
    REQUIRE(stale.ok());
    auto foreign = other.value()->commit(std::move(first).takeValue());
    REQUIRE(!foreign.ok());
    REQUIRE(foreign.status().code() == eve::StatusCode::Rejected);
    auto live = document.value()->prepare(document.value()->beginEdit());
    REQUIRE(live.ok());
    REQUIRE(document.value()->commit(std::move(live).takeValue()).ok());
    auto conflict = document.value()->commit(std::move(stale).takeValue());
    REQUIRE(!conflict.ok());
    REQUIRE(conflict.status().code() == eve::StatusCode::Conflict);
    REQUIRE(document.value()->undo().ok());
    auto corrupt                                             = saved.value();
    corrupt.getIf<eve::Value::Object>()->at("schemaVersion") = std::int64_t{4};
    REQUIRE(!document.value()->prepareRestore(corrupt).ok());
    auto unchanged = document.value()->capture();
    REQUIRE(unchanged.ok());
    // A failed prepare or restore must not truncate the retained redo branch.
    REQUIRE(document.value()->redo().ok());
    auto extra = saved.value();
    extra.getIf<eve::Value::Object>()->emplace("futureMetadata", "ignored");
    auto restore = document.value()->prepareRestore(extra);
    REQUIRE(restore.ok());
    REQUIRE(document.value()->commit(std::move(restore).takeValue()).ok());
    auto final = document.value()->capture();
    REQUIRE(final.ok());
    REQUIRE(final.value() == saved.value());
}

TEST_CASE("procgen.road.authoring.presetExplicitFalseAndEmptyArraysRoundTripAndUnbindingRestoresSource") {
    auto draft            = presetDraft();
    auto points           = draft.source.edges()[0].controlPoints;
    points[1].widthScale  = 1.5f;
    points[1].rollDegrees = 10.f;
    REQUIRE(draft.source.setEdgeControlPoints(1, points).ok());
    draft.types[0].overrides.sideObjectsLeft = true;
    draft.types[0].overrides.structures = std::vector<RoadStructureInterval>{{.2f, .8f, RoadStructureKind::Bridge}};
    draft.bindings[0].overrides.sideObjectsLeft = false;
    draft.bindings[0].overrides.structures      = std::vector<RoadStructureInterval>{};
    auto document                               = RoadPresetDocument::create(draft);
    REQUIRE(document.ok());
    REQUIRE(!document.value()->network().edges()[0].style.sideObjectsLeft);
    REQUIRE(document.value()->network().edges()[0].style.structures.empty());
    REQUIRE(document.value()->network().edges()[1].style.structures.size() == 1);
    REQUIRE(document.value()->network().edges()[0].controlPoints[1].widthScale == 1.5f);
    REQUIRE(document.value()->network().edges()[0].controlPoints[1].rollDegrees == 10.f);
    auto saved = document.value()->capture();
    REQUIRE(saved.ok());
    auto json = saved.value().toJson();
    REQUIRE(json.ok());
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    auto restore = document.value()->prepareRestore(parsed.value());
    REQUIRE(restore.ok());
    REQUIRE(document.value()->commit(std::move(restore).takeValue()).ok());
    auto restored = document.value()->beginEdit();
    REQUIRE(restored.bindings[0].overrides.sideObjectsLeft.has_value());
    REQUIRE(!restored.bindings[0].overrides.sideObjectsLeft.value());
    REQUIRE(restored.bindings[0].overrides.structures.has_value());
    REQUIRE(restored.bindings[0].overrides.structures->empty());
    restored.bindings[0].overrides.sideObjectsLeft.reset();
    restored.bindings[0].overrides.structures.reset();
    auto inherited = document.value()->prepare(std::move(restored));
    REQUIRE(inherited.ok());
    REQUIRE(document.value()->commit(std::move(inherited).takeValue()).ok());
    REQUIRE(document.value()->network().edges()[0].style.sideObjectsLeft);
    REQUIRE(document.value()->network().edges()[0].style.structures.size() == 1);
    auto unbound = document.value()->beginEdit();
    unbound.bindings.clear();
    auto candidate = document.value()->prepare(std::move(unbound));
    REQUIRE(candidate.ok());
    REQUIRE(document.value()->commit(std::move(candidate).takeValue()).ok());
    REQUIRE(document.value()->network().edges()[0].style.laneWidth == draft.source.edges()[0].style.laneWidth);
}

TEST_CASE("procgen.road.authoring.presetMarkerFacilityOverridesAndAuthorLocksPersist") {
    auto draft                               = presetDraft();
    draft.types[0].pointDefaults.widthScale  = .8f;
    draft.types[0].pointDefaults.rollDegrees = 12.f;
    RoadContinuousFacility facility;
    facility.id                                = 41;
    facility.profile                           = {{-.1f, 0, RoadMaterial::Curb},
                                                  {.1f, 0, RoadMaterial::Curb},
                                                  {.1f, 1, RoadMaterial::Curb},
                                                  {-.1f, 1, RoadMaterial::Curb}};
    draft.types[0].overrides.facilities        = std::vector<RoadContinuousFacility>{facility};
    draft.bindings[0].pointDefaults.widthScale = 1.1f;
    RoadMarkerOverride marker;
    marker.markerIndex           = 1;
    marker.overrides.widthScale  = 2.f;
    marker.overrides.rollDegrees = 0.f;
    draft.bindings[0].markers.push_back(marker);
    RoadFacilityOverrides instance;
    instance.id             = 41;
    instance.verticalOffset = 2.f;
    instance.capStart       = false;
    draft.bindings[0].facilities.push_back(instance);
    draft.metadata = {{1, "Main Street", true}};
    auto created   = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto document = std::move(created).takeValue();
    REQUIRE(document->network().edges()[0].controlPoints[0].widthScale == 1.1f);
    REQUIRE(document->network().edges()[0].controlPoints[1].widthScale == 2.f);
    REQUIRE(document->network().edges()[0].controlPoints[1].rollDegrees == 0.f);
    REQUIRE(document->network().edges()[1].controlPoints[0].rollDegrees == 12.f);
    REQUIRE(document->network().edges()[0].style.facilities[0].verticalOffset == 2.f);
    REQUIRE(!document->network().edges()[0].style.facilities[0].capStart);
    auto endpoint = document->beginEdit();
    REQUIRE(endpoint.source.setNodeJunctionRadius(1, 7.f).ok());
    REQUIRE(!document->prepare(endpoint).ok());
    auto direct = document->beginEdit();
    auto points = direct.source.edges()[0].controlPoints;
    points[1].y = 1.f;
    REQUIRE(direct.source.setEdgeControlPoints(1, points).ok());
    REQUIRE(!document->prepare(direct).ok());
    direct                                             = document->beginEdit();
    direct.bindings[0].markers[0].overrides.widthScale = 3.f;
    REQUIRE(!document->prepare(direct).ok());
    auto inherited                               = document->beginEdit();
    inherited.types[0].pointDefaults.rollDegrees = 8.f;
    auto prepared                                = document->prepare(std::move(inherited));
    REQUIRE(prepared.ok());
    REQUIRE(document->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(document->network().edges()[0].controlPoints[0].rollDegrees == 8.f);
    REQUIRE(document->network().edges()[0].controlPoints[1].rollDegrees == 0.f);
    auto saved = document->capture();
    REQUIRE(saved.ok());
    auto restore = document->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(document->commit(std::move(restore).takeValue()).ok());
    REQUIRE(document->beginEdit().metadata[0].name == "Main Street");
    REQUIRE(document->beginEdit().metadata[0].locked);
    REQUIRE(document->beginEdit().bindings[0].markers[0].overrides.rollDegrees.has_value());
    REQUIRE(!document->beginEdit().bindings[0].facilities[0].capStart.value());
    direct.metadata[0].locked = false;
    auto unlocked             = document->prepare(std::move(direct));
    REQUIRE(unlocked.ok());
    REQUIRE(document->commit(std::move(unlocked).takeValue()).ok());
    REQUIRE(document->network().edges()[0].controlPoints[1].widthScale == 3.f);
    REQUIRE(document->undo().ok());
    REQUIRE(document->beginEdit().metadata[0].locked);
    REQUIRE(document->network().edges()[0].controlPoints[1].widthScale == 2.f);
    auto invalid                         = document->beginEdit();
    invalid.metadata[0].locked           = false;
    invalid.bindings[0].facilities[0].id = 999;
    REQUIRE(!document->prepare(invalid).ok());
    invalid                                    = document->beginEdit();
    invalid.metadata[0].locked                 = false;
    invalid.bindings[0].markers[0].markerIndex = 999;
    REQUIRE(!document->prepare(invalid).ok());
    REQUIRE(document->redo().ok());
}

TEST_CASE("procgen.road.authoring.presetRestoreRejectsLateHistoryFailureAndMigratesAbsentOverrides") {
    auto document = RoadPresetDocument::create(presetDraft());
    REQUIRE(document.ok());
    auto edit                         = document.value()->beginEdit();
    edit.types[0].overrides.laneWidth = 5.f;
    auto candidate                    = document.value()->prepare(std::move(edit));
    REQUIRE(candidate.ok());
    REQUIRE(document.value()->commit(std::move(candidate).takeValue()).ok());
    REQUIRE(document.value()->undo().ok());
    auto saved = document.value()->capture();
    REQUIRE(saved.ok());
    auto  late     = saved.value();
    auto& history  = *late.getIf<eve::Value::Object>()->at("history").getIf<eve::Value::Array>();
    auto& bindings = *history.back().getIf<eve::Value::Object>()->at("bindings").getIf<eve::Value::Array>();
    bindings.back().getIf<eve::Value::Object>()->at("edgeId") = std::int64_t{9999};
    REQUIRE(!document.value()->prepareRestore(late).ok());
    auto unchanged = document.value()->capture();
    REQUIRE(unchanged.ok());
    REQUIRE(unchanged.value() == saved.value());
    REQUIRE(document.value()->redo().ok());
    auto legacy = saved.value();
    for (auto& frame : *legacy.getIf<eve::Value::Object>()->at("history").getIf<eve::Value::Array>()) {
        auto& object = *frame.getIf<eve::Value::Object>();
        object.erase("metadata");
        for (auto& type : *object.at("types").getIf<eve::Value::Array>())
            type.getIf<eve::Value::Object>()->erase("pointDefaults");
        for (auto& binding : *object.at("bindings").getIf<eve::Value::Array>()) {
            for (const auto* key : {"pointDefaults", "markers", "facilities"})
                binding.getIf<eve::Value::Object>()->erase(key);
        }
    }
    auto restore = document.value()->prepareRestore(legacy);
    REQUIRE(restore.ok());
    REQUIRE(document.value()->commit(std::move(restore).takeValue()).ok());
    auto migrated = document.value()->capture();
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value() == saved.value());
}

TEST_CASE("procgen.road.authoring.presetCombinedProfilesValidateFinalCustomPortsAndPreserveTraffic") {
    RoadPresetDraft draft;
    auto            a   = draft.source.addNode(-20, 0, 0);
    auto            hub = draft.source.addNode(0, 0, 0);
    auto            b   = draft.source.addNode(20, 0, 0);
    REQUIRE(a.ok());
    REQUIRE(hub.ok());
    REQUIRE(b.ok());
    RoadStyle original;
    original.laneWidth           = 3.f;
    original.automaticStructures = false;
    original.crossSection        = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    auto left  = draft.source.addEdge(a.value(), hub.value(), {{-20, 0, 0}, {-10, 0, 0}, {0, 0, 0}}, 1, 1, original);
    auto right = draft.source.addEdge(hub.value(), b.value(), {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}}, 1, 1, original);
    REQUIRE(left.ok());
    REQUIRE(right.ok());
    RoadConnection connection;
    connection.assetId   = "preset.square";
    connection.vertices  = {{-3, 0, -3}, {-3, 0, 3}, {3, 0, 3}, {3, 0, -3}};
    connection.triangles = {{0, 1, 2, RoadMaterial::Asphalt}, {0, 2, 3, RoadMaterial::Asphalt}};
    RoadConnectionPort first;
    first.id         = 1;
    first.edgeId     = left.value();
    first.x          = -3;
    first.forwardX   = -1;
    first.section    = original.crossSection;
    auto second      = first;
    second.id        = 2;
    second.edgeId    = right.value();
    second.x         = 3;
    second.forwardX  = 1;
    connection.ports = {first, second};
    REQUIRE(draft.source.setNodeConnection(hub.value(), connection).ok());
    REQUIRE(draft.source.connectAllTurns(hub.value()).ok());
    REQUIRE(!draft.source.laneLinks().empty());
    const auto blocked = draft.source.laneLinks().front();
    REQUIRE(draft.source.blockLaneLink(blocked).ok());
    RoadTypePreset type{"wide-with-half-profile", "", {}};
    type.overrides.laneWidth = 6.f;
    type.overrides.crossSection =
        std::vector<RoadProfilePoint>{{-6, 0, RoadMaterial::Asphalt}, {6, 0, RoadMaterial::Asphalt}};
    type.overrides.automaticStructures = false;
    type.pointDefaults.widthScale      = .5f;
    draft.types                        = {type};
    draft.bindings                     = {{left.value(), type.id, {}}, {right.value(), type.id, {}}};
    auto created                       = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    REQUIRE(created.value()->network().laneLinks().size() == draft.source.laneLinks().size());
    REQUIRE(created.value()->network().blockedLaneLinks().size() == 1);
    REQUIRE(created.value()->network().blockedLaneLinks()[0].inEdge == blocked.inEdge);
    REQUIRE(created.value()->network().blockedLaneLinks()[0].outEdge == blocked.outEdge);
    auto baked = bakeRoadNetwork(created.value()->network());
    REQUIRE(baked.ok());
    REQUIRE(baked.value().mesh.getVertexCount() > 0);
}
