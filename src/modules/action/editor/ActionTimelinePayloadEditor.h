#pragma once
#include "common/Export.h"


/** @file ActionTimelinePayloadEditor.h @brief Validated partial editing for action-block payloads. */

#include "action/ActionNotifyRegistry.h"
#include "action/ActionVfxDuration.h"
#include "action/editor/ActionTimelineEditor.h"
#include "common/BorrowedRef.h"

namespace eve::editor {

/**
 * @brief Applies typed partial payload changes through the canonical timeline transaction authority.
 *
 * The adapter borrows an editor and registry that must outlive it. Calls are owner-thread-only.
 * Existing and unknown payload fields are preserved; the merged payload is validated before publication.
 */
class EVENGINE_API_EDITORS ActionTimelinePayloadEditor {
public:
    /** @brief Construct a transient payload editor over borrowed authoritative services. */
    ActionTimelinePayloadEditor(ActionTimelineEditor& editor, const action::ActionNotifyRegistry& registry)
        : editor_(editor), registry_(registry) {}

    /** @brief Return an owning copy of one editable notify or state payload. */
    [[nodiscard]] EditorResult<Value::Object> payload(const LogicalId& itemId) const;

    /**
     * @brief Merge fields into one payload and commit exactly one validated undo step.
     * @param itemId Stable notify or state identity.
     * @param fields Fields to replace or append; omitted fields remain unchanged.
     * @return Applied or a structured lookup, registry, validation, or transaction failure.
     */
    [[nodiscard]] EditorResult<void> patch(const LogicalId& itemId, Value::Object fields);

    /**
     * @brief Resize one VFX state to its authored clip interval at 1.0x playback.
     * @param itemId Stable VFX notify-state identity.
     * @return Applied, or a structured type, range, validation, or transaction failure.
     */
    [[nodiscard]] EditorResult<void> fitBlockToClip(const LogicalId& itemId);

    /**
     * @brief Extend one VFX state's clip end so its authored interval matches the block duration.
     * @param itemId Stable VFX notify-state identity.
     * @return Applied, or a structured type, range, validation, or transaction failure.
     */
    [[nodiscard]] EditorResult<void> fitClipToBlock(const LogicalId& itemId);

    /**
     * @brief Set one VFX state's clip end to the finite duration reported by its resource provider.
     * @param itemId Stable VFX notify-state identity.
     * @param provider Optional synchronously borrowed provider; absence is reported as Unsupported.
     * @return Applied, or a structured provider, resource, type, validation, or transaction failure.
     */
    [[nodiscard]] EditorResult<void> fitClipToNaturalDuration(
        const LogicalId& itemId, OptionalRef<const action::IActionVfxDurationProvider> provider);

private:
    ActionTimelineEditor&               editor_;
    const action::ActionNotifyRegistry& registry_;
};

}  // namespace eve::editor
