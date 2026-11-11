#pragma once
/** @file PreparedSpawnExport.h
 * @brief spawn_editor export face: immutable committed configuration bytes for the canonical atomic filesystem writer.
 */
#include <memory>
#include <string>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Revision.h"
namespace eve::filesystem {
class AtomicTextWriteLease;
}
namespace eve::spawn_editor {
class SpawnForm;
/** @brief Owning runtime JSON export snapshot, independent of later form edits and document envelopes.
 * @ownership Owns bytes and captured source revision; no form, store, filesystem or runtime pointers are retained.
 * @thread Prepare on the authoring owner thread; write on the filesystem owner thread with serialized target writes.
 * @remarks Uses the admitted committed source, not unapplied UI drafts. Export never marks an editor asset saved.
 */
class EVENGINE_API_EDITORS PreparedSpawnExport final {
public:
    /** @brief Freeze the actual admitted sidecar/template source without recompiling or changing history.
     * @return Owning export or missing form/pending transaction/source/encoding failure.
     * @ownership Shares form synchronously only; output outlives the form and all resolver owners.
     * @cost Full source snapshot and JSON encoding per explicit export preparation.
     */
    [[nodiscard]] static Result<PreparedSpawnExport> prepare(std::shared_ptr<SpawnForm> form);
    /** @brief Borrow the complete runtime configuration JSON until this snapshot is moved/destroyed. */
    [[nodiscard]] const std::string& json() const noexcept;
    /** @brief Read the exact form source revision represented by these bytes. */
    [[nodiscard]] eve::Revision sourceRevision() const noexcept;
    /** @brief Atomically replace a relative file under the explicit filesystem's configured save directory.
     * @return Canonical filesystem result; missing parent/path traversal/I/O errors preserve retryable snapshot bytes.
     * @ownership Checks a provider-specific weak writer lease and borrows path synchronously. No editor mutation;
     * repeat writes are explicit.
     * @remarks Parent directories must already exist. Uses Filesystem::writeTextAtomic; WebGPU returns Unsupported.
     * Concurrent writers to the same target are unsupported; this export is replacement, not document-store CAS.
     * @cost Native temporary-file write/flush/replace linear in JSON bytes per explicit write.
     */
    [[nodiscard]] Result<void> write(const filesystem::AtomicTextWriteLease& filesystem,
                                     std::string_view                        relativePath) const;

private:
    PreparedSpawnExport(std::string json, eve::Revision revision);
    std::string   json_;
    eve::Revision revision_;
};
}  // namespace eve::spawn_editor
