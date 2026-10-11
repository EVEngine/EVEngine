#pragma once
#include "common/Export.h"

/**
 * @file AvatarDocumentEditor.h
 * @brief UI-neutral avatar editor: workspace, layer composite preview, undo.
 */

#include "avatar/editing/AvatarTarget.h"
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
class EVENGINE_API_EDITORS AvatarDocumentEditor {
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
    [[nodiscard]] avatar_editing::Result<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] avatar_editing::Result<void> selectLayer(std::string id);
    [[nodiscard]] avatar_editing::Result<void> selectParameter(std::string id);
    [[nodiscard]] avatar_editing::Result<void> selectExpression(std::string id);
    [[nodiscard]] avatar_editing::Result<void> pointerDown(float x, float y);

    [[nodiscard]] avatar_editing::Result<void> setLayerVisible(bool visible);
    [[nodiscard]] avatar_editing::Result<void> setLayerZ(int zIndex);
    [[nodiscard]] avatar_editing::Result<void> setParameterValue(double value);
    [[nodiscard]] avatar_editing::Result<void> createLayer(std::string id, std::string name);
    [[nodiscard]] avatar_editing::Result<void> deleteSelectedLayer();
    [[nodiscard]] avatar_editing::Result<void> createParameter(std::string id, std::string name);
    [[nodiscard]] avatar_editing::Result<void> deleteSelectedParameter();
    [[nodiscard]] avatar_editing::Result<void> createExpression(std::string id, std::string name);
    [[nodiscard]] avatar_editing::Result<void> deleteSelectedExpression();

    [[nodiscard]] avatar_editing::Result<editor::TransactionReceipt> undo();
    [[nodiscard]] avatar_editing::Result<editor::TransactionReceipt> redo();

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    /** @brief Can redo. */
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    /** @brief Revision. */
    std::uint64_t revision() const noexcept { return target_.revision(); }
    /** @brief Preview revision. */
    std::uint64_t previewRevision() const noexcept { return previewRevision_; }
    /** @brief Kind. */
    std::string   kind() const { return target_.kind(); }
    /** @brief Selected id. */
    std::string   selectedId() const { return selectedId_; }
    /** @brief Selected type. */
    std::string   selectedType() const { return selectedType_; }

    /** @brief Layer count. */
    int         layerCount() const { return static_cast<int>(target_.layers().size()); }
    /** @brief Layer id. */
    std::string layerId(int index) const;
    /** @brief Layer name. */
    std::string layerName(int index) const;
    /** @brief Layer visible. */
    bool        layerVisible(int index) const;
    /** @brief Layer z. */
    int         layerZ(int index) const;
    /** @brief True when layer selected. */
    bool        isLayerSelected(int index) const;

    /** @brief Preview layer count. */
    int         previewLayerCount() const { return static_cast<int>(preview_.size()); }
    /** @brief Preview x. */
    float       previewX(int index) const;
    /** @brief Preview y. */
    float       previewY(int index) const;
    /** @brief Preview w. */
    float       previewW(int index) const;
    /** @brief Preview h. */
    float       previewH(int index) const;
    /** @brief Preview r. */
    float       previewR(int index) const;
    /** @brief Preview g. */
    float       previewG(int index) const;
    /** @brief Preview b. */
    float       previewB(int index) const;
    /** @brief Preview a. */
    float       previewA(int index) const;
    /** @brief True when preview selected. */
    bool        isPreviewSelected(int index) const;
    /** @brief Preview name. */
    std::string previewName(int index) const;

    /** @brief Parameter count. */
    int         parameterCount() const { return static_cast<int>(target_.parameters().size()); }
    /** @brief Parameter id. */
    std::string parameterId(int index) const;
    /** @brief Parameter name. */
    std::string parameterName(int index) const;
    /** @brief Parameter value. */
    float       parameterValue(int index) const;
    /** @brief Parameter minimum. */
    float       parameterMinimum(int index) const;
    /** @brief Parameter maximum. */
    float       parameterMaximum(int index) const;
    /** @brief True when parameter selected. */
    bool        isParameterSelected(int index) const;

    /** @brief Expression count. */
    int         expressionCount() const { return static_cast<int>(target_.expressions().size()); }
    /** @brief Expression id. */
    std::string expressionId(int index) const;
    /** @brief Expression name. */
    std::string expressionName(int index) const;
    /** @brief True when expression selected. */
    bool        isExpressionSelected(int index) const;
    /** @brief Expression channel count. */
    int         expressionChannelCount(int index) const;
    /** @brief Expression channel name. */
    std::string expressionChannelName(int expression, int channel) const;

private:
    struct PreviewRect {
        std::string id;
        std::string name;
        float       x = 0, y = 0, w = 0, h = 0;
        float       r = 1, g = 1, b = 1, a = 1;
        bool        selected = false;
    };

    [[nodiscard]] avatar_editing::Result<void> commit(
        avatar_editing::Result<avatar_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] avatar_editing::Result<void> refreshPreview();
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
