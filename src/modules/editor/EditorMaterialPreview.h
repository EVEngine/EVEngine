#pragma once

#include "editor/EditorMaterialTarget.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::editor {

/** @brief Camera, mesh and environment settings for an isolated material preview. */
struct MaterialPreviewSettings {
    std::string geometry = "sphere";
    std::string customMeshAsset;
    std::string environmentAsset;
    int width = 512;
    int height = 512;
    double yawDegrees = 25.0;
    double pitchDegrees = -15.0;
    double distance = 3.0;
};

/** @brief Immutable request passed to a renderer-owned isolated preview scene. */
struct MaterialPreviewRenderRequest {
    StableId sceneId;
    DocumentId document;
    Revision documentRevision = 0;
    EditorValue material;
    MaterialPreviewSettings settings;
};

/** @brief Renderer output referenced by a stable artifact rather than GPU pointers. */
struct MaterialPreviewRenderResult {
    EditorStatus status = EditorStatus::Failed;
    std::string artifact;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Host boundary that owns creation and teardown of an isolated preview scene. */
class IMaterialPreviewRenderer {
public:
    virtual ~IMaterialPreviewRenderer() = default;
    /** @brief Render one immutable request without reading or mutating the live game scene. */
    virtual MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) = 0;
};

/** @brief Revision-safe material preview request and publication service. */
class MaterialPreviewService {
public:
    /** @brief Validate and render a material snapshot in a unique isolated scene. */
    EditorResult<TaskId> render(const DocumentId& document, const MaterialDocumentTarget& material,
                                MaterialPreviewSettings settings, IMaterialPreviewRenderer& renderer);
    /** @brief Return one immutable render result. */
    EditorResult<MaterialPreviewRenderResult> result(const TaskId& task) const;
    /** @brief Publish only a successful result matching the current document revision. */
    EditorResult<void> publish(const DocumentId& document, Revision currentRevision, const TaskId& task);
    /** @brief Return the most recently published artifact for a document. */
    std::string publishedArtifact(const DocumentId& document) const;

private:
    struct Task {
        DocumentId document;
        Revision revision = 0;
        MaterialPreviewRenderResult result;
    };
    std::unordered_map<TaskId, Task, StrongEditorIdHash<TaskId>> tasks_;
    std::unordered_map<DocumentId, std::string, StrongEditorIdHash<DocumentId>> published_;
    std::uint64_t sequence_ = 0;
};

}  // namespace eve::editor
