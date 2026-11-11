#pragma once
/** @file TiledSpawnImport.h
 * @brief spawn_editor module-interface: owning, reversible Tiled object import through the canonical form.
 * @remarks No runtime entities, file writes, event/script/tool endpoints or persistent format are introduced.
 */
#include <memory>
#include <span>
#include <string>
#include "common/Identity.h"
#include "common/Revision.h"
#include "common/Value.h"
namespace eve::editor {
class IEditorOverlay;
}
namespace eve::map_spawn {
struct PixelTransform;
}
namespace eve::spawn {
class CompiledMap;
class ITemplateResolver;
class IUnitDefinitionResolver;
}  // namespace eve::spawn
namespace eve::spawn_editor {
class SpawnForm;
struct MapViewportFrame;
/** @brief Immutable import proposal bound to one form incarnation and committed source revision.
 * @ownership Owns the candidate and stable IDs; observes the form weakly, retaining no live map or resolver.
 * @thread Authoring owner thread; no locks. Resolver callbacks must not mutate the active form or reenter prepare.
 * @remarks Imports objects whose type/class is eve.spawn. New IDs encode source asset UUID and Tiled object ID;
 * references preserve existing sidecar IDs. Reimport upserts those IDs; eve.spawn.instance references existing sidecar
 * IDs. Absent objects are never deleted.
 */
class EVENGINE_API_EDITORS PreparedTiledSpawnImport final {
public:
    /** @brief Release unpublished facts only. */
    ~PreparedTiledSpawnImport();
    /** @brief Transfer the proposal; moved-from operations reject. */
    PreparedTiledSpawnImport(PreparedTiledSpawnImport&&) noexcept;
    /** @brief Replace local unpublished facts. */
    PreparedTiledSpawnImport& operator=(PreparedTiledSpawnImport&&) noexcept;
    PreparedTiledSpawnImport(const PreparedTiledSpawnImport&)            = delete;
    PreparedTiledSpawnImport& operator=(const PreparedTiledSpawnImport&) = delete;
    /** @brief Translate marked finite orthogonal Tiled JSON objects, then admit the whole canonical sidecar.
     * @param form Actual map form; pending transactions reject. Unapplied UI drafts are not imported.
     * @param tiled Owned JSON map, copied before any resolver callback by argument ownership.
     * @param sourceAsset Stable nonnil identity of the source Tiled asset, never derived from a path or object name.
     * @param transform Mandatory explicit pixel transform; group/layer offsets and clockwise rotation are honored.
     * @param instanceDefaults Owned canonical fields for new instances, excluding id/region; null permits existing
     * targets only.
     * @param templates Actual shared template resolver used by the host; borrowed only during preparation.
     * @param units Actual shared unit resolver used by the host; borrowed only during preparation.
     * @return Owning plan or shape/content/version failure without changing source/history.
     * @remarks Supports points, circles, rectangles and polygons. Ellipses that are not circles, tile/text/polyline/
     * capsule/template objects and nonorthogonal/infinite maps reject. Object IDs are verified globally; unrelated
     * geometry and custom properties are ignored. Selected object/layer unknown fields and reserved properties reject.
     * Limits: 32 nesting levels, 4096 layers, 65536 source objects/geometry points, 1024 selected objects/vertices per
     * polygon. Recognizes JSON 1.0-1.12 and legacy numeric 1.0-1.5 only. General rotation uses host libm rounding;
     * finalized world coordinates, rather than a rotation recipe, enter the exported runtime asset.
     * @cost Full input/source copy, bounded geometry translation and canonical compilation per import proposal.
     * @throws std::bad_alloc No source publication; original form remains owned.
     */
    [[nodiscard]] static Result<PreparedTiledSpawnImport> prepare(
        std::shared_ptr<SpawnForm> form, Value tiled, AssetGuid sourceAsset, const map_spawn::PixelTransform& transform,
        Value instanceDefaults, std::shared_ptr<const spawn::ITemplateResolver> templates,
        std::shared_ptr<const spawn::IUnitDefinitionResolver> units);
    /** @brief Borrow the immutable candidate until move/destruction. */
    [[nodiscard]] const Value& source() const noexcept;
    /** @brief Borrow selected stable source IDs, sorted by Tiled object ID, until move/destruction. */
    [[nodiscard]] std::span<const std::string> importedIds() const noexcept;
    /** @brief Captured form source revision; zero after move. */
    [[nodiscard]] Revision sourceRevision() const noexcept;
    /** @brief Draw only the immutable imported regions in amber, without changing source/history.
     * @param frame Explicit viewport projection; invalid frames reject before invoking the sink.
     * @param overlay Borrowed nonnull sink for this call only; callbacks may destroy/move the outer plan.
     * @return Projection/callback error or success; moved, consumed and reentrant plans reject.
     * @remarks Authoring owner thread. Pins candidate facts during callbacks; commit reentry rejects.
     * No resolver callbacks or source parsing; an expired form does not invalidate this detached preview.
     * @cost Linear in selected geometry vertices per explicit preview draw.
     */
    [[nodiscard]] Result<void> drawPreview(const MapViewportFrame& frame, editor::IEditorOverlay& overlay) const;
    /** @brief Publish one canonical source write through the actual form's transaction/history backend.
     * @return Applied/NoOp or expired/stale/consumed/pending/content failure. Success consumes this plan.
     * @remarks Rechecks source preimage and revision; commit revalidates using the form's authoritative resolvers.
     * Failed backend commits remain with the form's existing retry/discard machinery; no second history is kept.
     * Observer callbacks may move/destroy the outer plan; same-plan commit reentry rejects without locks.
     * @remarks Allocation/observer exceptions may follow complete form publication; form.sourceRevision() and its
     * actual history remain authoritative, and stale plans cannot replay a committed write.
     * @cost Full source compilation and canonical reversible transaction per explicit commit attempt.
     */
    [[nodiscard]] Result<void> commit();

private:
    struct Impl;
    explicit PreparedTiledSpawnImport(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
