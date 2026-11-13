#pragma once
/** @file SpawnDocument.h
 * @brief spawn_editing module-interface: UI-neutral sidecar document, reversible operations and staged publication.
 * @remarks Explicit owning content resolvers in; editing target/domain operations out. No ECS, map runtime,
 * renderer, scheduler, tool or script bindings. Persistent data uses the canonical eve.map-spawns/v1 codec.
 */
#include <memory>
#include "common/Value.h"
#include "editing/EditableTarget.h"
#include "editing/EditingProperty.h"
#include "editing/EditingTargetOperations.h"
namespace eve::spawn {
class CompiledMap;
class ITemplateResolver;
class IUnitDefinitionResolver;
}  // namespace eve::spawn
namespace eve::spawn_editing {
/** @brief Typed form/viewport capability for source-only spawn authoring. */
class ISpawnDocumentEditTarget {
public:
    /** @brief Release the capability interface. */
    virtual ~ISpawnDocumentEditTarget() = default;
    /** @brief Stable editing capability identity; no runtime capability lookup. */
    [[nodiscard]] static editing::CapabilityId editingCapabilityId() {
        return editing::CapabilityId("eve.editing.target.map-spawns");
    }
    /** @brief Copy the canonical sidecar tree; editing it does not mutate the document.
     * @cost Full owning source copy per form snapshot, amortized per document change. */
    [[nodiscard]] virtual Value source() const = 0;
    /** @brief Validate a replacement and plan an owning, reversible source operation without publication.
     * @return Operation or canonical configuration/content failure; source and revision remain unchanged.
     * @ownership Copies input before resolver callbacks; returned operation retains no document/resolver pointer.
     * @cost Full source compilation and before/after encoding per planned edit.
     */
    [[nodiscard]] virtual Result<editing::DomainOperation> makeReplace(const Value& source) const = 0;
};
/** @brief Authoritative sidecar source and immutable compiled projection, independent of runtime SpawnWorld.
 * @ownership Owns state; shares actual resolver owners and immutable revisions. Clones share no mutable document data.
 * @thread Authoring owner thread; no locks. Resolver callbacks may inspect; they must not mutate, reenter or destroy
 * the active target. No game callbacks, entities, timers or RNG are executed. Allocations may throw without partial
 * publication.
 */
class EVENGINE_API_WORLD SpawnDocumentTarget final : public editing::EditableTargetState,
                                                     public editing::IDomainOperationTarget,
                                                     public editing::IDomainOperationTargetStaging,
                                                     public ISpawnDocumentEditTarget,
                                                     public editing::IPropertyProvider {
public:
    /** @brief Release local source/revision/content owners only. */
    ~SpawnDocumentTarget();
    SpawnDocumentTarget(const SpawnDocumentTarget&)            = delete;
    SpawnDocumentTarget& operator=(const SpawnDocumentTarget&) = delete;
    /** @brief Admit a complete sidecar through the canonical compiler before creating an editable target.
     * @return Owning document or invalid id/missing resolver/configuration error; no runtime publication.
     * @ownership Retains real shared resolver owners, never fake default ownership; copies source before callbacks.
     * @remarks Unknown fields/versions and unsupported branches follow compileMap; no independent document schema.
     * @cost Full source compilation and immutable state allocation per document import.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnDocumentTarget>> create(
        editing::TargetId id, const Value& source, std::shared_ptr<const spawn::ITemplateResolver> templates,
        std::shared_ptr<const spawn::IUnitDefinitionResolver> units);
    /** @brief Copy stable document identity. */
    [[nodiscard]] editing::TargetId targetId() const override;
    /** @brief Copy editing discovery metadata; no content compilation or callbacks. */
    [[nodiscard]] editing::TargetDescriptor describe() const override;
    /** @brief Query the form capability through the existing editing ABI.
     * @return Borrowed capability or null when unsupported; valid until document destruction.
     * @lifetime Synchronous editing ABI borrow; this query never changes state.
     */
    [[nodiscard]] void* queryCapability(const editing::CapabilityId& capability) override;
    /** @brief Copy canonical editable source; no remaining cooldown, live units or scheduler data.
     * @cost Full source tree copy per form snapshot, amortized per document change. */
    [[nodiscard]] Value source() const override;
    /** @brief Borrow pinned compiled content until document mutation/destruction; no callbacks or allocation. */
    [[nodiscard]] const spawn::CompiledMap& compiled() const noexcept;
    /** @brief Share the actual immutable compiled state without copying, parsing or invoking resolvers.
     * @ownership Aliases the document's real immutable state owner; survives replacement, undo and document
     * destruction.
     * @thread Acquire on the authoring owner thread; returned data is read-only and retains no live target/service.
     * @cost Constant shared ownership acquisition; releasing the last state owner retires its asset-sized data.
     */
    [[nodiscard]] std::shared_ptr<const spawn::CompiledMap> shareCompiled() const noexcept;
    /** @brief Plan the existing source-replacement protocol with exact before/after JSON and inverse.
     * @return Owning operation or failure preserving target; rejects resolver callback reentry.
     * @cost Full canonical map compilation and source encoding per user edit.
     */
    [[nodiscard]] Result<editing::DomainOperation> makeReplace(const Value& source) const override;
    /** @brief Validate target/type/shape/preimage and canonical content, then atomically replace source/projection.
     * @return Applied/NoOp or conflict/validation failure; failed edits keep revision/dirty state/content unchanged.
     * @remarks Operations are public untrusted data; inverse must exactly reverse the source payload. Revision-level
     * authorization and undo history remain with existing editing authority/transaction services.
     * @cost Full candidate parsing/compilation per applied source edit; no-op compares canonical text only.
     */
    [[nodiscard]] Result<void> applyDomainOperation(const editing::DomainOperation& operation) override;
    /** @brief Clone state for existing atomic editing transactions; shares only immutable state/content owners.
     * @ownership Owning detached target, bound privately to this document family and current base revision.
     * @cost Small target metadata copy and shared immutable-state ownership per staged transaction.
     */
    [[nodiscard]] std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override;
    /** @brief Publish only a matching family/base-revision candidate with no intervening source change.
     * @return Applied or conflict preserving authoritative state; does not rerun resolver callbacks at publication.
     * @ownership Consumes the staging owner; preserves this target's own parent staging relationship.
     * @cost Shared-state transfer per publication; retiring the last old owner traverses its source/content data.
     */
    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<editing::IDomainOperationTarget> candidate) override;

    /** @brief Validate one root Asset selection and return the authoritative document revision.
     * @return Revision or selection mismatch; selection item identity is the target id and type is describe().type.
     */
    [[nodiscard]] Result<eve::Revision> currentRevision(const editing::SelectionSnapshot& selection) const override;
    /** @brief Describe current top-level source fields for generic structured form presenters.
     * @remarks Schema/version fields are read-only; $source permits atomic cross-field branch edits. Arrays and
     * structures remain owning nested values. Compiler is the sole configuration validator; descriptors do not define a
     * second persistent schema.
     * @cost Full source snapshot and field descriptor construction per form rebuild, amortized per revision.
     */
    [[nodiscard]] editing::PropertySchema schema(const editing::SelectionSnapshot& selection) const override;
    /** @brief Copy one root field for a matching asset selection; invalid selections/paths return Missing.
     * @cost Full source snapshot and selected nested field conversion per form refresh.
     */
    [[nodiscard]] editing::PropertyReadResult read(const editing::SelectionSnapshot& selection,
                                                   const editing::PropertyPath&      path) const override;
    /** @brief Plan an absolute structured field replacement through the canonical source compiler.
     * @return Owning reversible operation or selection/path/mode/content failure; does not mutate the document.
     * @ownership Consumes selection/path and copies values before callbacks; retains no presentation or input
     * references.
     * @cost Full source copy and compilation per form edit, amortized once per explicit user edit.
     */
    [[nodiscard]] Result<editing::DomainOperation> makeSet(const editing::SelectionSnapshot& selection,
                                                           const editing::PropertyPath&      path,
                                                           const editing::Value&             value,
                                                           editing::PropertySetMode          mode) const override;
    /** @brief Reject reset explicitly: configuration codecs do not declare universal field defaults.
     * @return Unsupported without mutation; callers must supply an explicit valid replacement.
     */
    [[nodiscard]] Result<editing::DomainOperation> makeReset(const editing::SelectionSnapshot& selection,
                                                             const editing::PropertyPath&      path) const override;

private:
    struct Impl;
    explicit SpawnDocumentTarget(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editing
