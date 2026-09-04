#pragma once

/**
 * @file AvatarDocumentEditor.h
 * @brief UI-neutral avatar editor: workspace, layer composite preview, undo.
 */

#include "avatar_editing/AvatarTarget.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::avatar_editor {

/**
 * @brief Timeline-style controller for one authored avatar document.
 *
 * Owns the document, local undo and a CPU composite of authored layer rects.
 * Live `AvatarInstance` publication stays candidate-first and is skipped until a
 * texture resolver is attached; failed publish keeps the last successful layout.
 *
 * @ownership Editor owns the document. Preview getters copy values.
 * @threadaffinity Owner thread only.
 * @reentrancy No unknown callbacks.
 */
class AvatarDocumentEditor {
public:
    /** @brief Construct a seeded two-layer face with a referenced parameter. */
    explicit AvatarDocumentEditor(std::string targetId);

    AvatarDocumentEditor(const AvatarDocumentEditor&)            = delete;
    AvatarDocumentEditor& operator=(const AvatarDocumentEditor&) = delete;

    /** @brief Borrow the authoritative avatar document. */
    const avatar_editing::AvatarDocumentTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Layers / Preview / Inspector / Parameters / Expressions panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] avatar_editing::EditorResult<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] avatar_editing::EditorResult<void> selectLayer(std::string id);
    [[nodiscard]] avatar_editing::EditorResult<void> selectParameter(std::string id);
    [[nodiscard]] avatar_editing::EditorResult<void> selectExpression(std::string id);
    [[nodiscard]] avatar_editing::EditorResult<void> pointerDown(float x, float y);

    [[nodiscard]] avatar_editing::EditorResult<void> setLayerVisible(bool visible);
    [[nodiscard]] avatar_editing::EditorResult<void> setLayerZ(int zIndex);
    [[nodiscard]] avatar_editing::EditorResult<void> setParameterValue(double value);
    [[nodiscard]] avatar_editing::EditorResult<void> createLayer(std::string id, std::string name);
    [[nodiscard]] avatar_editing::EditorResult<void> deleteSelectedLayer();
    [[nodiscard]] avatar_editing::EditorResult<void> createParameter(std::string id, std::string name);
    [[nodiscard]] avatar_editing::EditorResult<void> deleteSelectedParameter();
    [[nodiscard]] avatar_editing::EditorResult<void> createExpression(std::string id, std::string name);
    [[nodiscard]] avatar_editing::EditorResult<void> deleteSelectedExpression();

    [[nodiscard]] avatar_editing::EditorResult<editor::TransactionReceipt> undo();
    [[nodiscard]] avatar_editing::EditorResult<editor::TransactionReceipt> redo();

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    std::uint64_t revision() const noexcept { return target_.revision(); }
    std::uint64_t previewRevision() const noexcept { return previewRevision_; }
    std::string   kind() const { return target_.kind(); }
    std::string   selectedId() const { return selectedId_; }
    std::string   selectedType() const { return selectedType_; }

    int         layerCount() const { return static_cast<int>(target_.layers().size()); }
    std::string layerId(int index) const;
    std::string layerName(int index) const;
    bool        layerVisible(int index) const;
    int         layerZ(int index) const;
    bool        isLayerSelected(int index) const;

    int         previewLayerCount() const { return static_cast<int>(preview_.size()); }
    float       previewX(int index) const;
    float       previewY(int index) const;
    float       previewW(int index) const;
    float       previewH(int index) const;
    float       previewR(int index) const;
    float       previewG(int index) const;
    float       previewB(int index) const;
    float       previewA(int index) const;
    bool        isPreviewSelected(int index) const;
    std::string previewName(int index) const;

    int         parameterCount() const { return static_cast<int>(target_.parameters().size()); }
    std::string parameterId(int index) const;
    std::string parameterName(int index) const;
    float       parameterValue(int index) const;
    float       parameterMinimum(int index) const;
    float       parameterMaximum(int index) const;
    bool        isParameterSelected(int index) const;

    int         expressionCount() const { return static_cast<int>(target_.expressions().size()); }
    std::string expressionId(int index) const;
    std::string expressionName(int index) const;
    bool        isExpressionSelected(int index) const;
    int         expressionChannelCount(int index) const;
    std::string expressionChannelName(int expression, int channel) const;

private:
    struct PreviewRect {
        std::string id;
        std::string name;
        float       x = 0, y = 0, w = 0, h = 0;
        float       r = 1, g = 1, b = 1, a = 1;
        bool        selected = false;
    };

    [[nodiscard]] avatar_editing::EditorResult<void> commit(
        avatar_editing::EditorResult<avatar_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] avatar_editing::EditorResult<void> refreshPreview();
    void                                             seedPreviewDocument();
    editor::SelectionSnapshot                        selection() const;
    /** @ownership Borrowed preview rect owned by this editor. @lifetime Valid until the next preview refresh or destruction; null when index is out of range. */
    const PreviewRect*                               previewAt(int index) const;
    /** @ownership Borrowed layer owned by the document target. @lifetime Valid until the next document mutation or destruction; null when index is out of range. */
    const avatar_editing::AvatarLayerValue*          layerAt(int index) const;
    /** @ownership Borrowed parameter owned by the document target. @lifetime Valid until the next document mutation or destruction; null when index is out of range. */
    const avatar_editing::AvatarParameterValue*      parameterAt(int index) const;
    /** @ownership Borrowed expression owned by the document target. @lifetime Valid until the next document mutation or destruction; null when index is out of range. */
    const avatar_editing::AvatarExpressionValue*     expressionAt(int index) const;
    float                                            smileAmount() const;

    avatar_editing::AvatarDocumentTarget target_;
    editor::LocalWorldAuthority          authority_;
    editor::LocalTransactionBackend      transactions_;
    std::vector<PreviewRect>             preview_;
    std::string                          selectedId_   = "eyes";
    std::string                          selectedType_ = "avatar.layer";
    std::uint64_t                        txSequence_   = 0;
    std::uint64_t                        previewRevision_ = 0;
};

}  // namespace eve::avatar_editor
