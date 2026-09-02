#include "editor/EditorAuthority.h"
#include "material_editor/EditorMaterialStudio.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {

class RecordingMaterialSink final : public IMaterialRuntimeSink {
public:
    EditorResult<void> publish(const MaterialDocumentTarget& candidate) override {
        ++publications;
        last = candidate.snapshotValue();
        return eve::editing::applied<void>();
    }

    int         publications = 0;
    EditorValue last;
};

class RecordingStudioRenderer final : public IMaterialPreviewRenderer {
public:
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override {
        ++renders;
        revisions.push_back(request.documentRevision);
        snapshots.push_back(request.material);
        return {EditorStatus::Applied, "artifact://material-studio/" + std::to_string(renders), {}};
    }

    int                      renders = 0;
    std::vector<Revision>    revisions;
    std::vector<EditorValue> snapshots;
};

SelectionSnapshot materialSelection(const MaterialDocumentTarget& material) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item{SelectionDomain::Asset, TargetId(material.targetId()), StableId(material.targetId().value()),
                       "graphics.material"};
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.material.studio_coalesces_live_gesture_into_one_transaction") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-metal", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-metal"), target, transactions, previews, renderer);

    REQUIRE(studio.beginInteraction(PropertyPath("shading.roughness")).ok());
    REQUIRE(studio.updateInteraction(0.2).ok());
    REQUIRE(studio.tick(100).ok());
    REQUIRE(studio.updateInteraction(0.35).ok());
    REQUIRE(studio.tick(133).ok());
    REQUIRE(studio.updateInteraction(0.7).ok());
    REQUIRE(studio.tick(166).ok());

    CHECK_EQ(renderer.renders, 3);
    CHECK_EQ(sink.publications, 0);
    CHECK(studio.state().interactionActive);
    const Revision before    = target.revision();
    auto           committed = studio.commitInteraction();
    REQUIRE(committed.ok());
    CHECK_EQ(target.revision(), before + 1);
    CHECK_EQ(sink.publications, 1);
    CHECK(!studio.state().interactionActive);
    CHECK(transactions.canUndo());
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("shading.roughness"))
              .value == EditorValue(0.7));

    REQUIRE(transactions.undo().ok());
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("shading.roughness"))
              .value == EditorValue(0.45));
}

TEST_CASE("editor.material.studio_cancel_preserves_live_material_and_refreshes_preview") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-cancel", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-cancel"), target, transactions, previews, renderer);

    const Revision original = target.revision();
    REQUIRE(studio.beginInteraction(PropertyPath("shading.metallic")).ok());
    REQUIRE(studio.updateInteraction(0.9).ok());
    REQUIRE(studio.refreshPreview().ok());
    REQUIRE(studio.cancelInteraction().ok());
    REQUIRE(studio.refreshPreview().ok());
    CHECK_EQ(target.revision(), original);
    CHECK_EQ(sink.publications, 0);
    CHECK_EQ(renderer.renders, 2);
    CHECK_EQ(studio.state().previewRevision, original);
}

TEST_CASE("editor.material.studio_rate_limits_with_injected_monotonic_time") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-rate", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-rate"), target, transactions, previews, renderer);

    REQUIRE(studio.setPreviewRate(20.0).ok());
    REQUIRE(studio.beginInteraction(PropertyPath("shading.roughness")).ok());
    REQUIRE(studio.updateInteraction(0.6).ok());
    REQUIRE(studio.tick(1000).ok());
    REQUIRE(studio.updateInteraction(0.61).ok());
    CHECK_EQ(static_cast<int>(studio.tick(1020).code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(studio.tick(1050).ok());
    CHECK_EQ(renderer.renders, 2);
    REQUIRE(studio.updateInteraction(0.62).ok());
    CHECK_EQ(static_cast<int>(studio.tick(1049).code()), static_cast<int>(EditorStatus::Rejected));
}
