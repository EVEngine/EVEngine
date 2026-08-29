#include "editor/EditorMaterialPreview.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {

class RecordingPreviewRenderer final : public IMaterialPreviewRenderer {
public:
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override {
        ++calls;
        last = request;
        return {status, artifact, {}};
    }

    int calls = 0;
    EditorStatus status = EditorStatus::Applied;
    std::string artifact = "artifact://material-preview/one";
    MaterialPreviewRenderRequest last;
};

SelectionSnapshot selection(const MaterialDocumentTarget& material) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Asset, TargetId(material.targetId()), StableId("material"),
                            "graphics.material"});
    return result;
}

}  // namespace

TEST_CASE("editor.material.preview_uses_immutable_isolated_revisioned_request") {
    MaterialDocumentTarget material("preview-material");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService previews;
    MaterialPreviewSettings settings;
    settings.geometry = "custom";
    settings.customMeshAsset = "asset://meshes/helmet.evm";
    const Revision renderedRevision = material.revision();
    auto task = previews.render(DocumentId("material-doc"), material, settings, renderer);
    REQUIRE(task.value);
    CHECK_EQ(renderer.calls, 1);
    CHECK(!renderer.last.sceneId.empty());
    CHECK_EQ(renderer.last.documentRevision, renderedRevision);
    CHECK_EQ(renderer.last.settings.customMeshAsset, "asset://meshes/helmet.evm");
    REQUIRE(renderer.last.material.getIf<EditorValue::Object>());
    REQUIRE(previews.publish(DocumentId("material-doc"), material.revision(), *task.value).accepted());
    CHECK_EQ(previews.publishedArtifact(DocumentId("material-doc")), renderer.artifact);
}

TEST_CASE("editor.material.preview_rejects_stale_or_failed_publication") {
    MaterialDocumentTarget material("preview-stale");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService previews;
    auto task = previews.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(task.value);
    auto roughness = material.makeSet(selection(material), PropertyPath("shading.roughness"), 0.8,
                                      PropertySetMode::Absolute);
    REQUIRE(roughness.value);
    REQUIRE(material.applyDomainOperation(*roughness.value).accepted());
    CHECK_EQ(static_cast<int>(previews.publish(DocumentId("doc"), material.revision(), *task.value).status),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(previews.publishedArtifact(DocumentId("doc")).empty());

    renderer.status = EditorStatus::Failed;
    renderer.artifact.clear();
    auto failed = previews.render(DocumentId("doc"), material, {}, renderer);
    REQUIRE(failed.value);
    CHECK_EQ(static_cast<int>(previews.publish(DocumentId("doc"), material.revision(), *failed.value).status),
             static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.material.preview_validates_scene_settings_before_renderer_call") {
    MaterialDocumentTarget material("preview-invalid");
    RecordingPreviewRenderer renderer;
    MaterialPreviewService previews;
    MaterialPreviewSettings settings;
    settings.geometry = "custom";
    settings.width = 4;
    const auto invalid = previews.render(DocumentId("doc"), material, settings, renderer);
    CHECK_EQ(static_cast<int>(invalid.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(renderer.calls, 0);
    CHECK(invalid.diagnostics.size() >= size_t{2});
}
