#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"

namespace eve {
class Value;
}
namespace eve::animation {
class AnimSkeleton;
class AnimPose;
class AnimClip;
struct RigDefinition;
struct TransformTRS;
}  // namespace eve::animation
namespace eve::animation_editor {
/**
 * @brief UI-independent rig authoring controller with existing editing transactions.
 * @ownership Owns document, skeleton snapshot and previews. Call arguments borrowed synchronously.
 * @thread Owner thread only. @reentrancy Does not invoke scripts or unknown callbacks.
 */
class EVENGINE_API_EDITORS ControlRigEditor {
public:
    /** @brief Create an empty editor with stable identity. */
    explicit ControlRigEditor(std::string targetId);
    /** @brief Release all owned state. */
    ~ControlRigEditor();
    ControlRigEditor(const ControlRigEditor&)            = delete;
    ControlRigEditor& operator=(const ControlRigEditor&) = delete;
    /** @brief Copy skeleton, seed FK controls and clear history after validation.
     * @cost Skeleton-sized preparation, amortized over an editing session.
     */
    [[nodiscard]] Result<void> initialize(const animation::AnimSkeleton& skeleton);
    /** @brief Validate and transactionally replace rig definition, preserving history.
     * @cost Compile and preview once per user edit; no per-frame compilation.
     */
    [[nodiscard]] Result<void> replace(const animation::RigDefinition& definition);
    /** @brief Register the initialized live document and existing history with the active editor host.
     * @return Structured unsupported status when the host is absent; duplicate target IDs are rejected.
     * @ownership Borrows the host with a teardown sentinel; destruction unregisters this target.
     * @thread Owner thread only. No scripts are invoked.
     */
    [[nodiscard]] Result<void> enableAutomation();
    /** @brief Remove Agent access without discarding the UI document or history. */
    [[nodiscard]] Result<void> disableAutomation();
    /** @brief Copy authoritative document for inspection/serialization. */
    [[nodiscard]] Result<Value> snapshot() const;
    /** @brief Load schema-validated document through same reversible replacement path. */
    [[nodiscard]] Result<void> restore(const Value& value);
    /** @brief Seek an explicit time; updates preview atomically. */
    [[nodiscard]] Result<void> seek(double seconds);
    /** @brief Edit a control's local TRS, optionally creating/replacing a key at playhead. */
    [[nodiscard]] Result<void> setControl(std::string_view id, const animation::TransformTRS& transform, bool key);
    /** @brief Move a control in model space, converting through its sampled parent transform. */
    [[nodiscard]] Result<void> moveControl(int index, float x, float y, float z, bool key);
    /** @brief Preview a model-space drag without history; seek cancels, moveControl commits. */
    [[nodiscard]] Result<void> previewMoveControl(int index, float x, float y, float z);
    /** @brief Remove a control key exactly at playhead; returns a diagnostic when absent. */
    [[nodiscard]] Result<void> deleteKey(std::string_view id);
    /** @brief Reparent unanimated controls preserving model-space pose. */
    [[nodiscard]] Result<void> reparent(std::string_view id, std::string_view parent);
    /** @brief Undo latest committed edit and rebuild derived preview. */
    [[nodiscard]] Result<void> undo();
    /** @brief Redo latest compensated edit and rebuild derived preview. */
    [[nodiscard]] Result<void> redo();
    /** @brief Bake into caller clip only after complete successful preparation. */
    [[nodiscard]] Result<void> bakeInto(animation::AnimClip& clip, double sampleRate) const;
    /** @brief Copy current solved pose into caller output. */
    [[nodiscard]] Result<void> copyPose(animation::AnimPose& output) const;
    /** @brief Match an existing pose to unique full-weight FK controls, recording one undoable key batch.
     * @cost Recompiles the authored snapshot once per pose import, not per preview frame.
     */
    [[nodiscard]] Result<void> keyFromPose(const animation::AnimPose& pose);
    /** @brief Copy current control model transform by index for viewport drawing/picking. */
    [[nodiscard]] Result<animation::TransformTRS> controlWorld(int index) const;
    /** @brief Copy sampled parent-local control for inspector/drag edits. */
    [[nodiscard]] Result<animation::TransformTRS> controlLocal(int index) const;
    /** @brief Number of authored controls. */
    int controlCount() const;
    /** @brief Control ID at index, empty if invalid. */
    std::string controlId(int index) const;
    /** @brief Monotonic document revision for UI invalidation after external edits. */
    std::uint64_t revision() const;
    /** @brief Current playhead in seconds. */
    double time() const;
    /** @brief Document duration. */
    double duration() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::animation_editor
