#pragma once
/** @file DefinitionRegistryCapture.h
 * @brief definitions module-interface: detached native registry content and exact envelope metadata.
 * @remarks Uses the existing definitions.registry / definitions:registry v1 protocol, without a second codec.
 */
#include <memory>
#include "common/Export.h"
#include "common/Snapshot.h"
namespace eve::definitions {
class DefinitionRegistry;
/** @brief Freeze values, tombstones, generations, events, allocator and registry envelope identity together.
 * @ownership Owns immutable CPU facts; no source registry, subscriptions, providers or callbacks retained.
 * @thread Capture/restore on content owner thread; sealing invokes only the explicit digest, without locks.
 */
class EVENGINE_API_FOUNDATION DefinitionRegistryCapture final {
public:
    /** @brief Release detached facts. */
    ~DefinitionRegistryCapture();
    /** @brief Transfer detached facts. */
    DefinitionRegistryCapture(DefinitionRegistryCapture&&) noexcept;
    /** @brief Replace detached facts by move. */
    DefinitionRegistryCapture& operator=(DefinitionRegistryCapture&&) noexcept;
    DefinitionRegistryCapture(const DefinitionRegistryCapture&)            = delete;
    DefinitionRegistryCapture& operator=(const DefinitionRegistryCapture&) = delete;
    /** @brief Seal through the canonical registry envelope, independent of source lifetime and mutations.
     * @param hashProvider Explicit digest borrowed only during this call; not retained.
     * @return Exact existing-format envelope or moved-capture/digest failure, preserving facts for retry.
     * @ownership Copies all facts before digest callbacks, which may retire this capture; no live registry consulted.
     * @cost Copies and hashes registry-sized frozen payload per explicit persistent sealing.
     * @throws std::bad_alloc Source registry and capture remain unchanged.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hashProvider) const;

    /** @brief Admit the existing registry envelope into detached CPU content/history without a live registry.
     * @param snapshot Owning source envelope. @param hash Explicit synchronous verifier, never retained.
     * @return Canonical immutable facts or integrity, version, metadata, schema or generation/history failure.
     * @remarks Uses the same schema migration and state reader as restoreSnapshot/restoreJson. Legacy anonymous
     * identity and zero revision remain supported; registry instance/revision/tick are independent of game epoch.
     * @ownership No source registry, subscriptions, providers or callbacks retained.
     * @thread Content owner CPU admission; digest is invoked without locks, no events are replayed.
     * @cost Parses and validates every retained entry and history row once per explicit persistent load.
     * @throws std::bad_alloc Input and live registries remain unchanged.
     */
    [[nodiscard]] static Result<DefinitionRegistryCapture> parse(SnapshotEnvelope            snapshot,
                                                                 const SnapshotHashProvider& hash);

private:
    friend class DefinitionRegistry;
    struct Data;
    explicit DefinitionRegistryCapture(std::unique_ptr<Data> data);
    std::unique_ptr<Data> data_;
};
}  // namespace eve::definitions
