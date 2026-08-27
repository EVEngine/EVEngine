#pragma once

/**
 * @file Snapshot.h
 * @brief Versioned, integrity-checked persistence envelopes.
 *
 * SnapshotEnvelope owns the stable header and delegates domain state to an
 * owning eve::Value payload.  It deliberately has no wall-clock field:
 * deterministic hash input is assembled only from the stable header and
 * payload.  The hash algorithm is injected by the caller because ContentId
 * must not silently imply a cryptographic or weak substitute algorithm.
 */

#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/SchemaVersion.h"
#include "common/Time.h"
#include "common/Value.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace eve {

/**
 * @brief Injected content-digest implementation used by snapshots.
 * @param canonicalInput Canonical JSON containing stable header fields and payload.
 * @return A ContentId, or a structured failure when the provider is unavailable.
 * @remarks No default hash or security claim is made.  Providers must document
 *          their digest algorithm and collision/security properties.
 */
using SnapshotHashProvider = std::function<Result<ContentId>(std::string_view canonicalInput)>;

/**
 * @brief Stable outer format shared by persistence and cross-process snapshots.
 *
 * `payload` is owned by the envelope.  `schema` identifies the payload
 * contract, while `schemaVersion` identifies the version of that contract.
 * Unknown payload fields remain the domain consumer's policy; unknown
 * envelope fields are rejected by the strict parser.
 */
struct EVENGINE_API SnapshotEnvelope {
    std::string    type;
    LogicalId      schema;
    SchemaVersion  schemaVersion;
    PersistentId   instanceId;
    Revision       revision;
    SimulationTick tick;
    ContentId      contentHash;
    Value          payload;
};

/**
 * @brief Construct and seal a snapshot envelope.
 * @param type Stable runtime/domain snapshot type.
 * @param schema Stable `namespace:name` payload schema identifier.
 * @param schemaVersion Version of the payload schema.
 * @param instanceId Stable identity of the state owner; nil is allowed for legacy anonymous stores.
 * @param revision State revision at capture time.
 * @param tick Simulation tick at capture time; wall-clock time is not accepted.
 * @param payload Owning domain payload.
 * @param hashProvider Injected digest provider; must not be empty.
 * @return A sealed envelope, or a structured validation/hash failure.
 */
[[nodiscard]] EVENGINE_API Result<SnapshotEnvelope> makeSnapshotEnvelope(std::string type, LogicalId schema,
                                                                         SchemaVersion schemaVersion,
                                                                         PersistentId instanceId, Revision revision,
                                                                         SimulationTick tick, Value payload,
                                                                         const SnapshotHashProvider& hashProvider);

/**
 * @brief Produce the canonical JSON input used for a snapshot content hash.
 * @param snapshot Envelope whose content hash is excluded from the input.
 * @return Deterministic JSON containing stable header fields and payload.
 * @remarks There is intentionally no `createdAt` or wall-clock field.
 */
[[nodiscard]] EVENGINE_API Result<std::string> snapshotHashInput(const SnapshotEnvelope& snapshot);

/**
 * @brief Verify an envelope's content hash without modifying it.
 * @param snapshot Envelope to verify.
 * @param hashProvider Injected digest provider used when the envelope was sealed.
 * @return Success when the computed digest equals contentHash.
 */
[[nodiscard]] EVENGINE_API Result<void> verifySnapshotEnvelope(const SnapshotEnvelope&     snapshot,
                                                               const SnapshotHashProvider& hashProvider);

/**
 * @brief Validate optional payload copies of envelope revision and tick.
 * @param payload Domain payload after any schema migration.
 * @param revision Authoritative envelope revision.
 * @param tick Authoritative envelope simulation tick.
 * @return Success when omitted or exactly equal; Conflict/ParseError on a
 *         duplicate field that disagrees or is not a decimal string.
 * @remarks Consumers may omit duplicate fields from new payloads. This
 *          helper gives legacy payloads one uniform compatibility rule before
 *          any consumer state is mutated.
 */
[[nodiscard]] EVENGINE_API Result<void> validateSnapshotPayloadMetadata(const Value& payload, Revision revision,
                                                                        SimulationTick tick);

/**
 * @brief Convert an envelope to its strict canonical value representation.
 * @param snapshot Envelope to encode.
 * @return An owning object with exactly the public envelope fields.
 */
[[nodiscard]] EVENGINE_API Result<Value> snapshotEnvelopeValue(const SnapshotEnvelope& snapshot);

/**
 * @brief Parse and verify an envelope from an owning Value.
 * @param value Object containing exactly the envelope fields.
 * @param hashProvider Provider used to verify contentHash.
 * @return A verified envelope, or a parse/version/hash failure.
 */
[[nodiscard]] EVENGINE_API Result<SnapshotEnvelope> parseSnapshotEnvelopeValue(
    const Value& value, const SnapshotHashProvider& hashProvider);

/**
 * @brief Serialize an envelope as deterministic compact JSON.
 * @param snapshot Envelope to serialize.
 * @return Canonical JSON, or a serialization failure.
 * @remarks Serialization does not silently recalculate or repair contentHash;
 *          call verifySnapshotEnvelope when accepting an external envelope.
 */
[[nodiscard]] EVENGINE_API Result<std::string> serializeSnapshotEnvelope(const SnapshotEnvelope& snapshot);

/**
 * @brief Parse and verify an envelope from canonical or compatible JSON text.
 * @param json UTF-8 JSON object containing the strict envelope shape.
 * @param hashProvider Provider used to verify contentHash.
 * @return A verified envelope, or a parse/version/hash failure.
 */
[[nodiscard]] EVENGINE_API Result<SnapshotEnvelope> parseSnapshotEnvelope(std::string_view            json,
                                                                          const SnapshotHashProvider& hashProvider);

/**
 * @brief One directed payload migration step.
 *
 * Steps must move forward to a larger version.  The chain never guesses a
 * missing step and never accepts an envelope newer than the requested target.
 */
class EVENGINE_API SnapshotMigrationChain {
public:
    using Migration = std::function<Result<Value>(const Value& payload)>;

    /**
     * @brief Register one schema-local migration edge.
     * @param schema Schema whose payload is transformed.
     * @param from Version accepted by migration.
     * @param to Version produced by migration; must be greater than from.
     * @param migration Pure function that returns a new owning payload.
     * @return Success, or Conflict/InvalidArgument when the edge is invalid or duplicated.
     */
    [[nodiscard]] Result<void> add(LogicalId schema, SchemaVersion from, SchemaVersion to, Migration migration);

    /**
     * @brief Migrate and reseal an envelope to an exact target version.
     * @param snapshot Verified source envelope; it is consumed on success.
     * @param targetVersion Required current version.
     * @param hashProvider Provider used to verify source and seal migrated output.
     * @return Migrated envelope, or UnknownVersion/Unsupported/failure.
     * @remarks If source is newer than target, it is rejected as an unknown new version.
     */
    [[nodiscard]] Result<SnapshotEnvelope> migrate(SnapshotEnvelope snapshot, SchemaVersion targetVersion,
                                                   const SnapshotHashProvider& hashProvider) const;

private:
    struct Key {
        std::string   schema;
        std::uint64_t from = 0;

        friend bool operator<(const Key& left, const Key& right) noexcept {
            return left.schema < right.schema || (left.schema == right.schema && left.from < right.from);
        }
    };

    struct Step {
        SchemaVersion to;
        Migration     migration;
    };

    std::map<Key, Step> steps_;
};

}  // namespace eve
