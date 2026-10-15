#include "graphics/material/editing/MaterialBatchTarget.h"
#include "graphics/material/editor/EditorMaterialPreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_material_batch_target.cpp
using namespace eve::editing;
using namespace eve::material_editing;

namespace {
SelectionSnapshot select(const MaterialBatchTarget& target, std::initializer_list<const char*> ids) {
    SelectionSnapshot result;
    for (const char* id : ids)
        result.items.push_back({SelectionDomain::Asset, target.targetId(), StableId(id), "graphics.material"});
    return result;
}

class BatchSink final : public IMaterialBatchRuntimeSink {
public:
    Result<void> publish(std::span<const MaterialDocumentTarget> candidates) override {
        ++calls;
        observed = candidates.size();
        if (reject)
            return eve::editing::failed<void>(Status::Failed, RuleId("test.material-batch.reject"),
                                              "injected atomic batch rejection");
        return eve::editing::applied<void>();
    }
    bool        reject   = false;
    int         calls    = 0;
    std::size_t observed = 0;
};
}  // namespace

TEST_CASE("editor.material.batch_reports_mixed_and_commits_one_atomic_publication") {
    MaterialDocumentTarget oak("oak");
    MaterialDocumentTarget grass("grass");
    SelectionSnapshot      grassSelection;
    grassSelection.items.push_back({SelectionDomain::Asset, grass.targetId(), StableId("grass"), "graphics.material"});
    auto grassTint = grass.makeSet(grassSelection, PropertyPath("shading.tint"), Value::Array{0.2, 0.4, 0.6, 1.0},
                                   PropertySetMode::Absolute);
    REQUIRE(grassTint.ok());
    REQUIRE(grass.applyDomainOperation(grassTint.value()).ok());

    BatchSink           sink;
    MaterialBatchTarget batch("vegetation-materials", {oak, grass}, &sink);
    const auto          both = select(batch, {"oak", "grass"});
    CHECK_EQ(static_cast<int>(batch.read(both, PropertyPath("shading.tint")).state),
             static_cast<int>(PropertyReadState::Mixed));
    const Value sharedTint = Value::Array{0.3, 0.5, 0.7, 1.0};
    auto        operation  = batch.makeSet(both, PropertyPath("shading.tint"), sharedTint, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    auto candidate = batch.cloneDomainState();
    REQUIRE(candidate->applyDomainOperation(operation.value()).ok());
    CHECK_EQ(sink.calls, 0);
    REQUIRE(batch.commitDomainState(std::move(candidate)).ok());
    CHECK_EQ(sink.calls, 1);
    CHECK_EQ(sink.observed, 2U);
    const auto read = batch.read(both, PropertyPath("shading.tint"));
    REQUIRE_EQ(static_cast<int>(read.state), static_cast<int>(PropertyReadState::Value));
    CHECK(read.value == sharedTint);
}

TEST_CASE("editor.material.batch_sink_failure_preserves_every_document") {
    BatchSink           sink;
    MaterialBatchTarget batch("vegetation-materials", {MaterialDocumentTarget("oak"), MaterialDocumentTarget("grass")},
                              &sink);
    const auto          both     = select(batch, {"oak", "grass"});
    const auto          before   = batch.snapshotValue();
    const auto          revision = batch.revision();
    auto                operation =
        batch.makeSet(both, PropertyPath("shading.tint"), Value::Array{0.6, 0.5, 0.4, 1.0}, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    sink.reject = true;
    CHECK(!batch.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(batch.revision(), revision);
    CHECK(batch.snapshotValue() == before);
}

// editor_material_preview.cpp
using namespace eve::editor;

namespace {

class RecordingPreviewRenderer final : public IMaterialPreviewRenderer {
public:
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override {
        ++calls;
        last = request;
        return {status, artifact, {}};
    }

    int                          calls    = 0;
    EditorStatus                 status   = EditorStatus::Applied;
    std::string                  artifact = "artifact://material-preview/one";
    MaterialPreviewRenderRequest last;
};

SelectionSnapshot selection(const MaterialDocumentTarget& material) {
    SelectionSnapshot result;
    result.items.push_back(
        {SelectionDomain::Asset, TargetId(material.targetId()), StableId("material"), "graphics.material"});
    return result;
}

}  // namespace

TEST_CASE("editor.material.preview_uses_immutable_isolated_revisioned_request") {
    MaterialDocumentTarget   material("preview-material");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService   previews;
    MaterialPreviewSettings  settings;
    settings.geometry               = "custom";
    settings.customMeshAsset        = "asset://meshes/helmet.evm";
    const Revision renderedRevision = material.revision();
    auto           task             = previews.render(DocumentId("material-doc"), material, settings, renderer);
    REQUIRE(task.ok());
    CHECK_EQ(renderer.calls, 1);
    CHECK(!renderer.last.sceneId.empty());
    CHECK_EQ(renderer.last.documentRevision, renderedRevision);
    CHECK_EQ(renderer.last.settings.customMeshAsset, "asset://meshes/helmet.evm");
    REQUIRE(renderer.last.material.getIf<EditorValue::Object>());
    REQUIRE(previews.publish(DocumentId("material-doc"), material.revision(), task.value()).ok());
    CHECK_EQ(previews.publishedArtifact(DocumentId("material-doc")), renderer.artifact);
}

TEST_CASE("editor.material.preview_rejects_stale_or_failed_publication") {
    MaterialDocumentTarget   material("preview-stale");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService   previews;
    auto                     task = previews.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(task.ok());
    auto roughness =
        material.makeSet(selection(material), PropertyPath("shading.roughness"), 0.8, PropertySetMode::Absolute);
    REQUIRE(roughness.ok());
    REQUIRE(material.applyDomainOperation(roughness.value()).ok());
    CHECK_EQ(static_cast<int>(previews.publish(DocumentId("doc"), material.revision(), task.value()).code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(previews.publishedArtifact(DocumentId("doc")).empty());

    renderer.status = EditorStatus::Failed;
    renderer.artifact.clear();
    auto failed = previews.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(failed.ok());
    CHECK_EQ(static_cast<int>(previews.publish(DocumentId("doc"), material.revision(), failed.value()).code()),
             static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.material.preview_validates_scene_settings_before_renderer_call") {
    MaterialDocumentTarget   material("preview-invalid");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService   previews;
    MaterialPreviewSettings  settings;
    settings.geometry  = "custom";
    settings.width     = 4;
    const auto invalid = previews.render(DocumentId("doc"), material, settings, renderer);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(renderer.calls, 0);
    CHECK(invalid.diagnostics().size() >= size_t{2});
}
