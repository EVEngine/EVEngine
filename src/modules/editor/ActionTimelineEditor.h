#pragma once

/**
 * @file ActionTimelineEditor.h
 * @brief UI-neutral transactional action timeline editor and preview model.
 */

#include "action/ActionTimeline.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"

#include <set>
#include <string>
#include <vector>

namespace eve::editor {

/**
 * @brief Authoritative editor target for one action timeline asset.
 *
 * The target owns exactly one canonical action::ActionTimeline. Mutations use
 * a canonical replace operation containing the action module's JSON codec, so
 * document persistence, undo and runtime loading cannot drift into separate
 * field mappings.
 */
class ActionTimelineTarget final : public IDomainOperationTarget, public IDomainOperationTargetStaging {
public:
    /** @brief Construct an owning target from an already validated timeline. */
    ActionTimelineTarget(std::string targetId, action::ActionTimeline timeline);

    /** @brief Stable editor target identity. */
    const std::string& targetId() const override { return targetId_; }
    /** @brief Monotonic content revision. */
    unsigned long long revision() const override { return revision_; }
    /** @brief Timeline edits invalidate the semantic surface, not a pixel region. */
    EditRegion dirtyRegion() const override { return {}; }
    /** @brief No-op for a semantic document target. */
    void clearDirtyRegion() override {}

    /** @brief Borrow the authoritative timeline until the next mutation. */
    const action::ActionTimeline& timeline() const noexcept { return timeline_; }
    /** @brief Apply a validated canonical replace operation. */
    [[nodiscard]] EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clone complete target state for atomic compensation. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Atomically publish a validated candidate. */
    [[nodiscard]] EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

private:
    std::string            targetId_;
    action::ActionTimeline timeline_;
    unsigned long long     revision_ = 0;
};

/**
 * @brief Timeline editing controller shared by developer and in-game editors.
 *
 * It owns selection, clipboard, preview transport and local undo history, but
 * never a second timeline. Every edit creates an invertible DomainOperation
 * and commits through the same authority/transaction path as other editors.
 * All methods are owner-thread-only and invoke no external callbacks.
 */
class ActionTimelineEditor {
public:
    /** @brief Construct a self-contained editor for one canonical timeline. */
    ActionTimelineEditor(std::string targetId, action::ActionTimeline timeline);

    /** @brief Borrow the authoritative target. */
    const ActionTimelineTarget& target() const noexcept { return target_; }
    /** @brief Atomically install the standard asset/preview/timeline/inspector panel composition. */
    [[nodiscard]] EditorResult<void> configureWorkspace(EditorWorkspace& workspace) const;
    /** @brief Add an empty semantic track. */
    [[nodiscard]] EditorResult<void> addTrack(action::ActionTrack track);
    /** @brief Add an instantaneous notify to an unlocked track. */
    [[nodiscard]] EditorResult<void> addNotify(const LogicalId& trackId, action::ActionNotify notify);
    /** @brief Add a notify state interval to an unlocked track. */
    [[nodiscard]] EditorResult<void> addState(const LogicalId& trackId, action::ActionNotifyState state);
    /** @brief Move one notify or state by an exact deterministic delta. */
    [[nodiscard]] EditorResult<void> moveItem(const LogicalId& itemId, Duration delta);
    /** @brief Resize one notify-state interval to exact validated boundaries. */
    [[nodiscard]] EditorResult<void> resizeState(const LogicalId& itemId, Duration start, Duration end);
    /** @brief Replace one item's type and owning payload through one undoable transaction. */
    [[nodiscard]] EditorResult<void> updateItem(const LogicalId& itemId, LogicalId type, Value::Object payload);
    /** @brief Atomically replace one item's timing, type and owning payload. */
    [[nodiscard]] EditorResult<void> editItem(const LogicalId& itemId, Duration start, Duration end, LogicalId type,
                                              Value::Object payload);
    /** @brief Remove one notify or state. */
    [[nodiscard]] EditorResult<void> removeItem(const LogicalId& itemId);
    /** @brief Mute/unmute one track through an undoable operation. */
    [[nodiscard]] EditorResult<void> setTrackMuted(const LogicalId& trackId, bool muted);

    /** @brief Replace selection with items intersecting a time interval. */
    [[nodiscard]] EditorResult<std::size_t> boxSelect(Duration start, Duration end);
    /** @brief Clear selection. */
    void clearSelection() { selection_.clear(); }
    /** @brief Select one existing item, optionally preserving the current selection. */
    [[nodiscard]] EditorResult<void> selectItem(const LogicalId& itemId, bool additive = false);
    /** @brief Number of selected timeline items. */
    std::size_t selectionCount() const noexcept { return selection_.size(); }
    /** @brief Return selected item IDs in lexical order as owning values. */
    [[nodiscard]] std::vector<LogicalId> selectedItemIds() const;
    /** @brief Copy selected items into an owning editor clipboard. */
    [[nodiscard]] EditorResult<std::size_t> copySelection();
    /** @brief Paste copied items at an exact offset with new stable ids. */
    [[nodiscard]] EditorResult<std::size_t> paste(Duration offset);
    /** @brief Delete every selected item as one transaction. */
    [[nodiscard]] EditorResult<void> deleteSelection();

    /** @brief Undo the latest committed edit. */
    [[nodiscard]] EditorResult<TransactionReceipt> undo();
    /** @brief Redo the latest compensated edit. */
    [[nodiscard]] EditorResult<TransactionReceipt> redo();
    /** @brief Whether undo history is non-empty. */
    bool canUndo() const noexcept { return transactions_.canUndo(); }
    /** @brief Whether redo history is non-empty. */
    bool canRedo() const noexcept { return transactions_.canRedo(); }

    /** @brief Begin preview playback from the current cursor. */
    void play() noexcept { playing_ = true; }
    /** @brief Pause preview playback. */
    void pause() noexcept { playing_ = false; }
    /** @brief Seek preview without emitting crossed events. */
    [[nodiscard]] EditorResult<void> seek(Duration time);
    /** @brief Advance preview by injected time and collect crossed events. */
    [[nodiscard]] EditorResult<std::size_t> update(Duration delta);
    /** @brief Current deterministic preview cursor. */
    Duration previewTime() const noexcept { return previewTime_; }
    /** @brief Whether preview transport is playing. */
    bool playing() const noexcept { return playing_; }
    /** @brief Events crossed by the latest preview update. */
    const std::vector<action::ActionTimelineEvent>& previewEvents() const noexcept { return previewEvents_; }

private:
    struct ClipboardItem {
        LogicalId                 trackId;
        bool                      state = false;
        action::ActionNotify      notify;
        action::ActionNotifyState notifyState;
    };

    [[nodiscard]] EditorResult<void> commit(action::ActionTimeline candidate, std::string label, std::string mergeKey);
    [[nodiscard]] static EditorResult<void> rejected(std::string rule, std::string message);
    [[nodiscard]] LogicalId                 copiedId(const LogicalId& source);

    ActionTimelineTarget                     target_;
    LocalWorldAuthority                      authority_;
    LocalTransactionBackend                  transactions_;
    std::set<std::string>                    selection_;
    std::vector<ClipboardItem>               clipboard_;
    std::uint64_t                            transactionSequence_ = 0;
    std::uint64_t                            copySequence_        = 0;
    Duration                                 previewTime_         = Duration::zero();
    bool                                     playing_             = false;
    bool                                     previewStarted_      = false;
    std::vector<action::ActionTimelineEvent> previewEvents_;
};

}  // namespace eve::editor
