#pragma once

#include "editor/EditorMaterialPreview.h"
#include "editor/EditorTransactionService.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Observable state for a host-rendered real-time material studio. */
struct MaterialStudioState {
    bool                          interactionActive = false;
    bool                          previewDirty      = true;
    PropertyPath                  activeProperty;
    Revision                      documentRevision = 0;
    Revision                      previewRevision  = 0;
    std::string                   previewArtifact;
    std::vector<EditorDiagnostic> diagnostics;
};

/**
 * @brief UI-neutral controller for coalesced material edits and revision-safe live previews.
 *
 * A host begins an interaction when a slider, color control, or asset picker becomes active,
 * sends every intermediate value to updateInteraction(), and calls commitInteraction() once.
 * Intermediate values are applied only to an owned draft and may be previewed at a bounded rate;
 * the live material changes atomically once, so one gesture produces one undo record.
 *
 * @ownership The target, transaction backend, preview service, and renderer are borrowed and must
 * outlive this controller. The draft and all presentation state are owned by this controller.
 * @threadaffinity Owner thread only; preview rendering and transaction callbacks are synchronous.
 * @reentrancy Do not call this controller again from renderer or transaction callbacks.
 */
class MaterialStudioController {
public:
    MaterialStudioController(DocumentId document, MaterialPublishingTarget& target,
                             IEditorTransactionBackend& transactions, MaterialPreviewService& previews,
                             IMaterialPreviewRenderer& renderer);

    /** @brief Replace preview geometry, camera, resolution, and environment settings. */
    [[nodiscard]] EditorResult<void> setPreviewSettings(MaterialPreviewSettings settings);
    /** @brief Start one coalesced property gesture from the current live document revision. */
    [[nodiscard]] EditorResult<void> beginInteraction(PropertyPath path);
    /** @brief Update the owned draft and mark its isolated preview dirty. */
    [[nodiscard]] EditorResult<void> updateInteraction(EditorValue value);
    /** @brief Atomically publish the final draft value as one undoable transaction. */
    [[nodiscard]] EditorResult<TransactionReceipt> commitInteraction();
    /** @brief Abandon the draft without changing the live material. */
    [[nodiscard]] EditorResult<void> cancelInteraction();
    /**
     * @brief Render a dirty preview when the injected monotonic time reaches the rate limit.
     * @param monotonicMilliseconds Host-provided monotonic time; wall-clock time is not read.
     * @return Applied when rendered, NoOp when clean or rate-limited, otherwise a diagnostic.
     */
    [[nodiscard]] EditorResult<void> tick(std::uint64_t monotonicMilliseconds);
    /** @brief Force an isolated preview of the current draft or live document. */
    [[nodiscard]] EditorResult<void> refreshPreview();
    /** @brief Set the maximum preview rate; valid range is 1 to 240 Hz. */
    [[nodiscard]] EditorResult<void> setPreviewRate(double framesPerSecond);
    /** @brief Return an owning snapshot suitable for any developer or in-game UI host. */
    [[nodiscard]] MaterialStudioState state() const;

private:
    [[nodiscard]] SelectionSnapshot  selectionFor(const MaterialDocumentTarget& material) const;
    [[nodiscard]] EditorResult<void> renderPreview(const MaterialDocumentTarget& material);
    void                             clearInteraction();

    DocumentId                              document_;
    MaterialPublishingTarget&               target_;
    IEditorTransactionBackend&              transactions_;
    MaterialPreviewService&                 previews_;
    IMaterialPreviewRenderer&               renderer_;
    MaterialPreviewSettings                 settings_;
    std::unique_ptr<MaterialDocumentTarget> draft_;
    std::optional<PropertyPath>             activeProperty_;
    std::optional<EditorValue>              finalValue_;
    std::uint64_t                           transactionSequence_         = 0;
    std::uint64_t                           lastPreviewMilliseconds_     = 0;
    std::uint64_t                           previewIntervalMilliseconds_ = 33;
    bool                                    hasPreviewTimestamp_         = false;
    bool                                    previewDirty_                = true;
    Revision                                previewRevision_             = 0;
    std::string                             previewArtifact_;
    std::vector<EditorDiagnostic>           diagnostics_;
};

}  // namespace eve::editor
