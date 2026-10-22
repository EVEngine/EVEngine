#pragma once
#include "scene/editing/SceneTarget.h"

namespace eve::scene_editing {
/**
 * @brief Declarative authoring composition: hierarchy plus source and presentation payloads.
 * Owns authored values; meshes, imported hosts and cameras are rebuildable projections.
 * @thread Owner thread only, no callbacks. No native pointers are persisted.
 * @remarks Schema eve.scene.authoring version 4 migrates v1/v2/v3 and rejects unknown fields and versions.
 * Source references describe immutable asset revisions; importing does not overwrite authored TRS.
 */
class EVENGINE_API_BACKENDS SceneAuthoringDocumentTarget final : public SceneTargetBase {
public:
    /** @brief Create an empty authoring document with a stable target identity. */
    explicit SceneAuthoringDocumentTarget(std::string id);
    /** @brief Copy all authored values into an owning versioned snapshot. @cost Linear copy per save/edit inspection.
     */
    EditorValue snapshotValue() const override;
    /** @brief Validate a complete candidate before producing a reversible replacement.
     * @return Operation or diagnostic; no observable state changes.
     * @cost Linear validation/copy per restore, amortized over the imported document.
     */
    [[nodiscard]] Result<DomainOperation> makeRestore(const EditorValue& snapshot) const override;
    /** @brief Apply an operation on this owner thread; failures preserve state. */
    [[nodiscard]] Result<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Clone an isolated transaction candidate, owning all values. @cost Linear copy per transaction. */
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Atomically publish a validated candidate of the same target identity. */
    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;
    /** @brief Plan one source/camera/lighting payload change; missing sources may be defined.
     * @param section sources, camera, lighting or relations; source key is an object identity.
     * @return Reversible operation owning its payload, or rejection without mutation.
     */
    [[nodiscard]] Result<DomainOperation> makePayload(const std::string& section, const std::string& key,
                                                      const EditorValue& payload) const;

    /** @brief Delete a leaf and its relations as one reversible document transaction.
     * @cost Linear document copy per deletion, amortized across one undoable edit.
     */
    [[nodiscard]] Result<DomainOperation> makeDelete(const editing::ObjectId& id) const override;

private:
    EditorValue::Array  relations_;
    EditorValue::Object sources_;
    EditorValue         camera_   = EditorValue::Object{};
    EditorValue         lighting_ = EditorValue::Object{};
};
/** @brief Register authored source creation and camera/lighting payload commands. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> registerAuthoringDocumentCommands(
    editing::IEditingCommandRegistry& registry);
}  // namespace eve::scene_editing
