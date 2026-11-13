#pragma once
/** @file SpawnSavedState.h
 * @brief spawn module-interface: durable source/membership/lifecycle values, never live authority.
 * @remarks No incarnation, provider, lease, callback, ECS object or registry is retained. These values require
 * participant admission and joint relation validation before a running source can be issued.
 */
#include <cstdint>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Value.h"
namespace eve::spawn {
struct SpawnerHandle;
struct LifecycleInput;
/** @brief Lifecycle reasons; only Death permits population replacement. Stable values also describe saved inputs. */
enum class RemovalReason { Death, Administrative, Unloaded, Detached };
/** @brief Durable source address, deliberately distinct from a process-local SpawnerHandle. */
struct SavedSpawnerKey {
    PersistentId  owner;
    std::uint64_t epoch      = 0;
    std::uint32_t slot       = 0;
    std::uint32_t generation = 0;
    /** @brief Compare durable fields, without implying a live source exists. */
    friend bool operator==(const SavedSpawnerKey&, const SavedSpawnerKey&) = default;
};
/** @brief Encode one durable source address with the canonical lifecycle source shape.
 * @return Owning four-field metadata or invalid nil/epoch/generation failure; no live authority is issued.
 * @thread CPU boundary, no callbacks; input borrowed synchronously. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> encodeSavedSpawnerKey(const SavedSpawnerKey& key);
/** @brief Admit one strict durable source address; unknown fields and noncanonical counters reject.
 * @return Durable fields only; the caller must join them against admitted source bindings before execution.
 * @thread CPU boundary, no callbacks or publication. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<SavedSpawnerKey> prepareSavedSpawnerKey(const Value& value);
/** @brief Admitted core membership projection, ordered by persistent subject ID. */
struct SavedSpawnMember {
    SubjectRef      subject;
    SavedSpawnerKey source;
};
/** @brief Owning unconsumed domain input using only durable source data. */
struct SavedLifecycleInput {
    SubjectRef      subject;
    std::uint64_t   sequence = 0;
    RemovalReason   reason   = RemovalReason::Death;
    SavedSpawnerKey source;
};
/** @brief Encode durable lifecycle inputs in original order, including repeated observations.
 * @return Owning canonical array, or invalid identity/reason/source/sequence; maximum 1M rows.
 * @remarks Same row format as RPG bundle v1; no incarnation or live authority is encoded.
 * @ownership Borrows input only during call. @thread CPU save, no callbacks or services.
 * @cost Copies all input rows once per explicit participant save.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> encodeSavedLifecycleInputs(
    std::span<const SavedLifecycleInput> inputs);
/** @brief Admit strict durable lifecycle values without issuing source handles or removing duplicates.
 * @return Owning ordered rows or unknown fields, malformed identity/counter/reason and size failure.
 * @ownership Borrows value only during call. @thread CPU admission, no callbacks or services.
 * @cost Validates and copies all rows per explicit participant load.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::vector<SavedLifecycleInput>> prepareSavedLifecycleInputs(
    const Value& value);
/** @brief Project observed source fields into a durable value; does not validate or issue live authority.
 * @ownership Synchronous borrow only, pointer-free output. No callbacks, allocation or source changes.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION SavedSpawnerKey savedSpawnerKey(const SpawnerHandle& source) noexcept;
/** @brief Copy a lifecycle fact without retaining its process incarnation; no validation or source changes.
 * @ownership Synchronous borrow only; the coordinator must validate against admitted core membership.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION SavedLifecycleInput savedLifecycleInput(const LifecycleInput& input) noexcept;
}  // namespace eve::spawn
