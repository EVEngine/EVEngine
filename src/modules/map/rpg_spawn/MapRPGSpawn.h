#pragma once
/** @file MapRPGSpawn.h
 * @brief map_rpg_spawn script face: one owned RPG map session, explicit two-phase assets and frame inputs.
 */
#include "common/Export.h"
#include "common/Module.h"
namespace eve::map_rpg_spawn {
/**
 * @brief Script composition root using real owned domain/content storage and canonical map transactions.
 * @ownership Owns its domain Table and registry; no default-world ownership wrappers. Close before Map unload.
 * @thread Script/simulation/render owner thread; borrows Map/RPG/Graphics only during bound calls.
 * @remarks Borrowed Map mode has no implicit clock, event poll, actor deletion on unload or second compiler.
 * damage(subject, amount) applies finite nonnegative canonical HP damage by current source-owned identity. It does
 * not remove the domain body or advance time. Owns a separate event scope, strips legacy actor pointer borrows before
 * returning, and retains death facts until advance accepts them. Rejected frames may already have committed damage;
 * retry advance rather than resubmitting cached game events. The supplied RPG object's cached events are borrowed
 * synchronously, never polled or cleared. Borrowed advance does not tick combat/AI.
 * captureDiagnostics() returns owning transient tick/sources (id, phase, blocked, alive, total, zero-based wave,
 * remaining_ns, subjects). Subject strings are persistent identities, not actor pointers or liveness authorization.
 * Owned mode is explicit and mutually exclusive with the borrowed Map session: prepareRuntime/commitRuntime
 * accept canonical JSON arrays of units/templates and a map binding. advanceRuntime injects positive contiguous
 * tick/nanoseconds and runs the native domain exactly once. prepareRuntimeActivation owns JSON null/point-array
 * players and string-array events; successful advance consumes the prepared token, failures retain it for retry.
 * captureRuntimeState/damage/draw observe or operate on that exact private owner; close(remove) removes its bodies.
 * freezeRuntime(epoch, revision) owns a detached checkpoint; sealRuntime uses the canonical snapshot hash bridge
 * and returns file text, even after source close. snapshotHashAlgorithm reports the active provider's algorithm.
 * prepareRuntimeRestore(file, epoch) admits CPU facts; commitRuntimeRestore publishes the complete candidate.
 * Failures preserve the old owner and candidate retry responsibility. Successful replacement revokes local input
 * and delivery tokens. No file writes or mutable Map/actor pointers are exposed by these bindings.
 * readRuntimeEvents(epoch, revision) copies a native delivery and seals its canonical simulation-events snapshot;
 * returns snapshot text, hash_algorithm and an opaque positive receipt. A new read replaces only the local receipt,
 * never consumes queue facts. Process-wide non-repeating receipt IDs reject cross-root confirmations; they are
 * transient protocol metadata, excluded from simulation/replay/persistence. acknowledgeRuntimeEvents confirms only
 * that root's latest native prefix, preserving any appended suffix. Duplicate/foreign/old receipts reject.
 * @cost Owned prepare/restore/capture are whole-world operations with explicit prepare/commit or freeze/seal steps;
 * event reads copy/hash the whole pending queue once per explicit observation, acknowledgement erases its prefix.
 * Diagnostic capture copies current sources/members per explicit observation; damage performs logarithmic
 * identity lookup and canonical event preparation. Callers inject time through advance and retain domain ownership.
 */
class EVENGINE_API_WORLD MapRPGSpawn final : public Module {
public:
    Module_REG(MapRPGSpawn);
    /** @brief Allocate local canonical content and domain owners; creates no map layers or actors. */
    MapRPGSpawn();
    /** @brief Release local owners; explicit close is required for normal gameplay unload. */
    ~MapRPGSpawn() override;
    MapRPGSpawn(const MapRPGSpawn&)            = delete;
    MapRPGSpawn& operator=(const MapRPGSpawn&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rpg_spawn
