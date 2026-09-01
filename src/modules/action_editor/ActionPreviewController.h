#pragma once

/** @file ActionPreviewController.h @brief Atomic action-preview transport and presentation orchestration. */

#include "action/ActionPreview.h"
#include "action_editor/ActionTimelineEditor.h"
#include "editor/EditorPresentation.h"

#include <optional>

namespace eve::editor {

/**
 * @brief Coordinates deterministic timeline transport with a transactional preview host.
 *
 * The controller borrows services that must outlive it. It invokes no preview
 * host callback while holding a lock and remains owner-thread-only.
 */
class ActionPreviewController {
public:
    /** @brief Construct with a required presentation sink and optional root-motion provider. */
    ActionPreviewController(ActionTimelineEditor& editor, action::IActionPreviewSink& sink,
                            const action::IActionRootMotionSource* rootMotion = nullptr);

    /** @brief Configure root-motion trajectory density in the inclusive range 2..1024. */
    [[nodiscard]] EditorResult<void> setRootMotionSampleCount(std::uint32_t sampleCount);
    /** @brief Re-present the current cursor without changing transport state. */
    [[nodiscard]] EditorResult<void> refresh();
    /** @brief Atomically prepare presentation and seek the authoritative preview transport. */
    [[nodiscard]] EditorResult<void> seek(Duration time);
    /** @brief Atomically prepare presentation and advance by injected deterministic time. */
    [[nodiscard]] EditorResult<std::size_t> update(Duration delta);
    /** @brief Last frame successfully published to the host. */
    [[nodiscard]] const std::optional<action::ActionPreviewFrame>& lastFrame() const noexcept { return lastFrame_; }
    /** @brief Draw the latest root-motion trajectory into a 3D-capable overlay. */
    void drawRootMotion(IEditorOverlay& overlay, OverlayPoint origin = {}, float scale = 1.0f) const;

private:
    [[nodiscard]] Result<action::ActionPreviewFrame> buildFrame(
        action::ActionPreviewReason reason, Duration previous, Duration current,
        const std::vector<action::ActionTimelineEvent>& events) const;
    [[nodiscard]] static action::ActionPreviewCueKind cueKind(const action::ActionTimelineEvent& event) noexcept;

    ActionTimelineEditor&                     editor_;
    action::IActionPreviewSink&               sink_;
    const action::IActionRootMotionSource*    rootMotion_            = nullptr;
    std::uint32_t                             rootMotionSampleCount_ = 64;
    std::optional<action::ActionPreviewFrame> lastFrame_;
};

}  // namespace eve::editor
