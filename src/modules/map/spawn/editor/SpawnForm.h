#pragma once
/** @file SpawnForm.h
 * @brief spawn_editor module-interface: owning authoring composition, property view and canonical undo history.
 * @remarks Owns one spawn_editing document, existing edit authority/backend/model. No ECS, runtime providers,
 * simulation, script or tool protocol. Configuration validation/persistence stay with canonical document codecs.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/Value.h"
#include "editing/EditingProtocol.h"
namespace eve::spawn {
class CompiledMap;
}
namespace eve::property_access {
class IPropertyAccess;
}
namespace eve::spawn_editing {
class SpawnDocumentTarget;
class SpawnTemplateDocumentTarget;
}  // namespace eve::spawn_editing
namespace eve::spawn_editor {
/** @brief Cheap facts used to enable form history actions; no separate history ownership. */
struct FormHistory {
    bool canUndo = false;
    bool canRedo = false;
    bool pending = false;
};
/** @brief Owning admitted source at one document revision; contains no cached UI or runtime state. */
struct FormSourceSnapshot {
    eve::Revision revision;
    Value         source;
};
/** @brief Actual committed map projection at one document revision, owning the immutable document state. */
struct FormMapSnapshot {
    eve::Revision                             revision;
    std::shared_ptr<const spawn::CompiledMap> map;
};
/** @brief Own a complete generic property-form binding and its target/authority/history lifetimes.
 * @thread Authoring owner thread; callbacks run without locks. Do not destroy the form during a view callback.
 * @ownership Nonmovable identity owner; all borrowed view references expire at destruction. No runtime publication.
 */
class EVENGINE_API_EDITORS SpawnForm final {
public:
    /** @brief Destroy model/history before authority and document; no gameplay cleanup. */
    ~SpawnForm();
    SpawnForm(const SpawnForm&)            = delete;
    SpawnForm& operator=(const SpawnForm&) = delete;
    /** @brief Transfer an admitted sidecar into an independent property form and bind its initial snapshot.
     * @return Owning form or missing target/binding failure; the input owner is consumed.
     * @cost Full source property snapshot per form creation, amortized for subsequent reads.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnForm>> create(
        std::unique_ptr<spawn_editing::SpawnDocumentTarget> document);
    /** @brief Transfer an admitted template into an independent property form and bind its initial snapshot.
     * @return Owning form or missing target/binding failure; the input owner is consumed.
     * @cost Full source property snapshot per form creation, amortized for subsequent reads.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnForm>> create(
        std::unique_ptr<spawn_editing::SpawnTemplateDocumentTarget> document);
    /** @brief Borrow the actual generic UI property model until form destruction.
     * @remarks Reads use cached owning values; writes route through canonical authority and retained undo history.
     * Failed commits remain retryable; do not destroy this form from its change callbacks.
     */
    [[nodiscard]] property_access::IPropertyAccess& view() noexcept;
    /** @brief Return the provider revision currently captured by the form. */
    [[nodiscard]] eve::Revision targetRevision() const noexcept;
    /** @brief Read the authoritative committed document revision, independent of the UI property cache. */
    [[nodiscard]] eve::Revision sourceRevision() const noexcept;
    /** @brief Freeze authoritative committed source and revision without resolver callbacks or UI cache reads.
     * @return Owning snapshot or unresolved transaction error, preserving source/history.
     * @cost Full source tree copy per explicit save/export preparation.
     */
    [[nodiscard]] Result<FormSourceSnapshot> captureSource() const;
    /** @brief Acquire the authoritative immutable map projection, independent of cached UI source or pending drafts.
     * @return Owning committed map/revision, or Unsupported for a template form; no parsing, callbacks or publication.
     * @remarks Pending transactions/drafts are not previewed; committed source remains observable while they exist.
     * @ownership Shares the real immutable document state, which may outlive this form and all content resolvers.
     * @cost Constant owner acquisition; final snapshot retirement may release asset-sized state.
     */
    [[nodiscard]] Result<FormMapSnapshot> captureMap() const;
    /** @brief Read action availability directly from the actual transaction backend. */
    [[nodiscard]] FormHistory history() const noexcept;
    /** @brief Compensate the latest form transaction and refresh its property snapshot.
     * @return Canonical receipt or failure without silently dropping history.
     * @cost Full document transaction and property refresh per undo action.
     */
    [[nodiscard]] Result<editing::TransactionReceipt> undo();
    /** @brief Reapply the latest compensated form transaction and refresh its property snapshot.
     * @return Canonical receipt or failure preserving retry/diagnostic responsibility.
     * @cost Full document transaction and property refresh per redo action.
     */
    [[nodiscard]] Result<editing::TransactionReceipt> redo();
    /** @brief Retry the actual backend's retained failed commit, then refresh the property model.
     * @return Canonical receipt or retry failure; no parallel transaction store.
     * @cost Full document transaction and property refresh per retry attempt.
     */
    [[nodiscard]] Result<editing::TransactionReceipt> retry();
    /** @brief Discard pending unpublished work through the existing model/backend.
     * @return Applied or structured rollback failure; committed source stays owned by its document.
     */
    [[nodiscard]] Result<void> discard();

private:
    struct Impl;
    explicit SpawnForm(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
