#pragma once
/** @file SpawnAssetDocument.h
 * @brief spawn_editor persistence composition face: canonical source form and existing CAS document service.
 */
#include <memory>
#include <string>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"
namespace eve::editor {
class IAtomicDocumentStore;
struct DocumentKey;
}  // namespace eve::editor
namespace eve::spawn {
class ITemplateResolver;
class IUnitDefinitionResolver;
}  // namespace eve::spawn
namespace eve::spawn_editor {
class SpawnForm;
/** @brief Own an asset-backed form and revision-safe persistence coordinator.
 * @ownership Shares the actual store and form; store outlives its owned DocumentService. No runtime world is retained.
 * @thread Authoring owner thread. Store callbacks may edit the form, but opening/saving must not reenter or destroy
 * this owner. No locks are held across callbacks. Store failure never discards source or transaction history.
 * @remarks The store controls its envelope/file format; this owner saves canonical configuration values, not runtime
 * state. DiskAtomicDocumentStore envelopes require the host's asset import/export boundary for runtime sidecar files.
 */
class EVENGINE_API_EDITORS SpawnAssetDocument final {
public:
    /** @brief Release the coordinator before the real store owner; shared form observers may outlive this object. */
    ~SpawnAssetDocument();
    SpawnAssetDocument(const SpawnAssetDocument&)            = delete;
    SpawnAssetDocument& operator=(const SpawnAssetDocument&) = delete;
    /** @brief Read persisted content (or initial source when absent), canonically admit it and create a sidecar form.
     * @return Owning document or store/configuration failure without writing the store.
     * @ownership Copies source and retains real store/resolver owners before invoking external callbacks.
     * @cost Store read plus full canonical compilation and form binding per asset open.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnAssetDocument>> openSidecar(
        std::shared_ptr<editor::IAtomicDocumentStore> store, editor::DocumentKey key, std::string uri,
        const Value& initialSource, std::shared_ptr<const spawn::ITemplateResolver> templates,
        std::shared_ptr<const spawn::IUnitDefinitionResolver> units);
    /** @brief Read and canonically admit a template using the same owned document/save protocol.
     * @return Owning document or failure without publication or store writes.
     * @ownership Copies source and retains store/unit resolver owners before callbacks.
     * @cost Store read plus canonical template compilation and form binding per asset open.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnAssetDocument>> openTemplate(
        std::shared_ptr<editor::IAtomicDocumentStore> store, editor::DocumentKey key, std::string uri,
        const Value& initialSource, std::shared_ptr<const spawn::IUnitDefinitionResolver> units);
    /** @brief Share the actual authoritative form, including its existing undo/redo history. */
    [[nodiscard]] std::shared_ptr<SpawnForm> form() const noexcept;
    /** @brief True when no source revision has been saved, or the actual form changed since the last successful save.
     */
    [[nodiscard]] bool dirty() const noexcept;
    /** @brief Capture committed source, request an exact save ticket and execute the existing store CAS.
     * @return Success or pending-transaction/reentry/store conflict/failure; dirty state advances only on success.
     * @remarks Edits during store callbacks remain dirty; retry captures current committed source. Unapplied UI drafts
     * remain outside this operation. External conflicts require explicit host reconciliation, never blind overwrite.
     * @cost Full source snapshot, document copy and store write per explicit save.
     */
    [[nodiscard]] Result<void> save();

private:
    struct Impl;
    explicit SpawnAssetDocument(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
