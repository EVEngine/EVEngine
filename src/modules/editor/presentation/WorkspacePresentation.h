#pragma once

#include "editor/EditorProperty.h"

#include <optional>
#include <string>
#include <string_view>

namespace eve::editor {

/** @brief Owning state projection; the bound domain remains the only document/history owner. */
struct WorkspacePresentationState {
    std::uint64_t revision = 0;
    std::string   context;
    bool          canUndo = false;
    bool          canRedo = false;
    bool          playing = false;
};

/**
 * @brief Synchronous adapter for an open domain editor, without UI or domain includes.
 * @ownership Caller owns the source and keeps it alive for each controller call.
 * @thread Owner thread only. No source reference is retained by the controller.
 * @reentrancy Implementations must not re-enter the controller during a call.
 */
class IWorkspacePresentationSource {
public:
    virtual ~IWorkspacePresentationSource() = default;
    /** @brief Snapshot of document revision, selection/playhead context and history availability. */
    virtual WorkspacePresentationState state() const = 0;
    /** @brief Owning schema; field identity is its canonical property path. */
    virtual PropertySchema schema() const = 0;
    /** @brief Read a field for the current selection without mutation. */
    virtual PropertyReadResult read(const PropertyPath& path) const = 0;
    /** @brief Commit one validated field through the domain's existing transaction path. */
    [[nodiscard]] virtual EditorResult<void> write(const PropertyPath& path, const EditorValue& value) = 0;
    /** @brief Execute a domain action, including undo/redo on its sole history backend. */
    [[nodiscard]] virtual EditorResult<void> action(std::string_view id) = 0;
};

/**
 * @brief Shared validation and draft/commit controller used by domain editor UI adapters.
 * @ownership Owns only a temporary draft. Never owns a document, selection or undo stack.
 * @lifetime One controller belongs to one domain instance; cancel before switching sources.
 * @thread Owner thread only. No callbacks or retained source pointers.
 * @reentrancy Non-reentrant; host dispatches sequentially and closes at a safe point.
 */
class WorkspacePresentation {
public:
    /** @brief Stage an owning draft; repeated updates create no document/history mutation. */
    [[nodiscard]] EditorResult<void> stage(IWorkspacePresentationSource& source, PropertyPath path, EditorValue value);
    /** @brief Commit the draft once; rejects revision or selection changes and consumes the draft. */
    [[nodiscard]] EditorResult<void> commit(IWorkspacePresentationSource& source);
    /** @brief Cancel the draft without changing the domain or history. */
    void cancel() noexcept;
    /** @brief Validate and commit one discrete keyboard/checkbox edit. */
    [[nodiscard]] EditorResult<void> edit(IWorkspacePresentationSource& source, PropertyPath path, EditorValue value);
    /** @brief Route an action through the domain backend; pending drafts are cancelled first. */
    [[nodiscard]] EditorResult<void> action(IWorkspacePresentationSource& source, std::string_view id);
    /** @brief Whether an uncommitted draft exists. */
    bool hasDraft() const noexcept { return draft_.has_value(); }
    /** @brief Immutable current schema/value projection, including any local draft. */
    [[nodiscard]] Value snapshot(const IWorkspacePresentationSource& source) const;

private:
    struct Draft {
        PropertyPath               path;
        EditorValue                value;
        EditorValue                original;
        WorkspacePresentationState start;
    };
    std::optional<Draft> draft_;
};

}  // namespace eve::editor
