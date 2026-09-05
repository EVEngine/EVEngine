#pragma once

/**
 * @file ProcgenScriptEditor.h
 * @brief UI-neutral host for a script generator: schema params, undo, PointSet preview.
 */

#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"
#include "procgen_editing/ProcgenScriptTarget.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {
class PointSet;
}

namespace eve::procgen_editor {

/**
 * @brief Workspace controller for one encapsulated procedural script module.
 *
 * Owns the Params document and local undo. Script presenters run `generate`
 * and publish copied PointSets; a stale or failed rebuild keeps the last
 * successful preview on screen.
 *
 * @ownership Editor owns the document and preview copies. PointSet arguments
 *            are borrowed only for the publish call.
 * @threadaffinity Owner thread only.
 * @reentrancy Does not invoke script generate callbacks.
 */
class ProcgenScriptEditor {
public:
    /** @brief Construct an empty generator host. */
    explicit ProcgenScriptEditor(std::string targetId);

    ProcgenScriptEditor(const ProcgenScriptEditor&)            = delete;
    ProcgenScriptEditor& operator=(const ProcgenScriptEditor&) = delete;

    /** @brief Borrow the authoritative generator document. */
    const procgen_editing::ProcgenScriptDocumentTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Modules / Preview / Inspector / Debug Stages panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] procgen_editing::EditorResult<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] procgen_editing::EditorResult<void> loadModule(procgen_editing::ProcgenScriptModuleSpec spec);
    [[nodiscard]] procgen_editing::EditorResult<void> loadModule(std::string uri, std::string id,
                                                                 std::string displayName, std::string kind,
                                                                 const procgen_editing::EditorValue& schema);

    [[nodiscard]] procgen_editing::EditorResult<void> setParam(std::string key,
                                                               procgen_editing::EditorValue value);
    [[nodiscard]] procgen_editing::EditorResult<void> setInt(std::string key, int value);
    [[nodiscard]] procgen_editing::EditorResult<void> setFloat(std::string key, double value);
    [[nodiscard]] procgen_editing::EditorResult<void> setBool(std::string key, bool value);
    [[nodiscard]] procgen_editing::EditorResult<void> setString(std::string key, std::string value);

    /**
     * @brief Copy a successful generation into the preview cache.
     * @param points Borrowed output; null is rejected. Not retained after return.
     * @param stage Debug-stage name also used as the displayed output.
     * @param expectedRevision Document revision observed before generate; mismatch keeps the old preview.
     * @ownership @p points is borrowed for this call only.
     * @lifetime @p points must outlive this call.
     */
    [[nodiscard]] procgen_editing::EditorResult<void> publishPreview(const procgen::PointSet* points,
                                                                     std::string stage, std::uint64_t expectedRevision);
    /**
     * @brief Copy an intermediate debug stage without changing the displayed output unless it is selected.
     * @param points Borrowed stage output; null is rejected. Not retained after return.
     * @ownership @p points is borrowed for this call only.
     * @lifetime @p points must outlive this call.
     */
    [[nodiscard]] procgen_editing::EditorResult<void> publishStage(const procgen::PointSet* points, std::string stage);
    [[nodiscard]] procgen_editing::EditorResult<void> failPreview(std::string message,
                                                                  std::uint64_t expectedRevision);
    [[nodiscard]] procgen_editing::EditorResult<void> selectStage(std::string stage);
    [[nodiscard]] procgen_editing::EditorResult<void> setPointBudget(int budget);
    /**
     * @brief Choose whether the presenter should rebuild on every parameter commit.
     * @param enabled True for continuous rebuild after each committed edit.
     */
    [[nodiscard]] procgen_editing::EditorResult<void> setLive(bool enabled);

    [[nodiscard]] procgen_editing::EditorResult<editor::TransactionReceipt> undo();
    [[nodiscard]] procgen_editing::EditorResult<editor::TransactionReceipt> redo();

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    bool          isDirty() const noexcept { return dirty_; }
    /** @brief True when the presenter should rebuild after every committed parameter edit. */
    bool          continuousRebuild() const noexcept { return continuousRebuild_; }
    std::uint64_t revision() const noexcept { return target_.revision(); }
    std::uint64_t previewRevision() const noexcept { return previewRevision_; }
    int           pointBudget() const noexcept { return pointBudget_; }
    std::string   moduleId() const { return target_.moduleId(); }
    std::string   moduleUri() const { return target_.uri(); }
    std::string   displayName() const { return target_.displayName(); }
    std::string   kind() const { return target_.kind(); }
    std::string   selectedStage() const { return selectedStage_; }
    /** @brief Empty when the last publish succeeded; otherwise the failPreview summary. */
    std::string   previewFailureSummary() const { return previewFailureSummary_; }

    int         paramCount() const { return static_cast<int>(target_.params().size()); }
    std::string paramKey(int index) const;
    std::string paramLabel(int index) const;
    std::string paramKind(int index) const;
    float       paramMinimum(int index) const;
    float       paramMaximum(int index) const;
    float       paramStep(int index) const;
    int         paramChoiceCount(int index) const;
    std::string paramChoice(int paramIndex, int choiceIndex) const;
    int         getInt(const std::string& key) const;
    float       getFloat(const std::string& key) const;
    bool        getBool(const std::string& key) const;
    std::string getString(const std::string& key) const;

    int         stageCount() const { return static_cast<int>(stageOrder_.size()); }
    std::string stageName(int index) const;
    int         pointCount() const;
    float       pointX(int index) const;
    float       pointZ(int index) const;
    std::uint32_t pointSeed(int index) const;

private:
    struct PreviewPoint {
        float         x    = 0;
        float         z    = 0;
        std::uint32_t seed = 1;
    };

    [[nodiscard]] procgen_editing::EditorResult<void> commit(
        procgen_editing::EditorResult<editing::DomainOperation> operation, std::string label);
    editor::SelectionSnapshot selection() const;
    /**
     * @brief Borrow one schema row.
     * @return Schema-owned descriptor, or null for an out-of-range index.
     * @ownership Borrowed from the document.
     * @lifetime Valid until schema mutation or editor destruction.
     * @nullable Yes.
     */
    const procgen::ParamDescriptor* paramAt(int index) const;
    /**
     * @brief Copy preview samples out of a borrowed PointSet.
     * @param points Borrowed generator output; must not be retained.
     * @ownership Argument is borrowed for this call only.
     * @lifetime @p points must outlive this call.
     */
    [[nodiscard]] procgen_editing::EditorResult<std::vector<PreviewPoint>> copyPoints(
        const procgen::PointSet* points) const;
    const std::vector<PreviewPoint>& displayedPoints() const;

    procgen_editing::ProcgenScriptDocumentTarget target_;
    editor::LocalWorldAuthority                  authority_;
    editor::LocalTransactionBackend              transactions_;
    std::unordered_map<std::string, std::vector<PreviewPoint>> stages_;
    std::vector<std::string>                     stageOrder_;
    std::string                                  selectedStage_;
    std::string                                  previewFailureSummary_;
    std::uint64_t                                txSequence_      = 0;
    std::uint64_t                                previewRevision_ = 0;
    int                                          pointBudget_     = 100000;
    bool                                         dirty_           = false;
    bool                                         continuousRebuild_ = false;
};

}  // namespace eve::procgen_editor
