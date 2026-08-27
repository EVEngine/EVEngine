#pragma once

#include "common/Result.h"
#include "common/Value.h"
#include "schema/SchemaTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::schema {

/** @brief One pure transformation from one registered schema version to the next. */
using MigrationFunction = std::function<eve::Result<eve::Value>(const eve::Value& input)>;

/**
 * @brief The exact compatibility path selected by SchemaRegistry.
 *
 * `versions` contains both endpoints. An exact match contains one version;
 * a migrated match contains every explicit edge endpoint. No implicit
 * nearest-version or downgrade behavior is represented by this type.
 */
struct EVENGINE_API SchemaCompatibility {
    std::string       schemaId;
    int               fromVersion = 0;
    int               toVersion = 0;
    std::vector<int>  versions;

    /** @brief Whether source and target are the same exact schema version. */
    [[nodiscard]] bool exact() const noexcept { return versions.size() == 1; }
};

/** @brief Process-wide registry for runtime gameplay schemas. */
class EVENGINE_API SchemaRegistry {
public:
    /** @brief Registers or replaces the exact `(id, version)` entry.
     * @param definition Schema to copy into the registry.
     * @return Registered or Replaced, or a structured validation failure.
     */
    [[nodiscard]] static eve::Result<SchemaRegistrationStatus> registerSchema(
        const SchemaDefinition& definition);

    /** @brief Parses and registers or replaces one schema definition from JSON.
     * @param json JSON schema definition.
     * @return Registered or Replaced, or a structured parse/validation failure.
     */
    [[nodiscard]] static eve::Result<SchemaRegistrationStatus> registerFromJson(const std::string& json);

    /** @brief Registers a new exact `(id, version)` entry without replacing it.
     * @param definition Schema to copy into the registry.
     * @return Registered, or a structured conflict/validation failure.
     */
    [[nodiscard]] static eve::Result<SchemaRegistrationStatus> registerVersioned(
        const SchemaDefinition& definition);

    /** @brief Parses and registers a new exact `(id, schemaVersion)` entry.
     * @param json JSON schema definition. `schemaVersion` is canonical;
     * `version` is accepted for compatibility. Supplying both with different
     * values is rejected as a version conflict.
     * @return Registered, or a structured parse/conflict/validation failure.
     */
    [[nodiscard]] static eve::Result<SchemaRegistrationStatus> registerFromJsonVersioned(const std::string& json);

    /**
     * @brief Finds the highest registered version for an id, or nullptr.
     *
     * This is the legacy id-only resolution rule. Use resolve() when the
     * caller has a persisted schema version and must not silently select a
     * newer definition.
     * @return Borrowed nullable schema definition owned by the registry.
     * @ownership SchemaRegistry owns the entry; callers must not delete or mutate it.
     * @lifetime Valid until any registry mutation or destruction; copy data before registering another version.
     * @thread Call on the schema registry thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across re-entrant mutation.
     */
    [[nodiscard]] static const SchemaDefinition* find(const std::string& id);

    /**
     * @brief Resolves one exact `(schemaId, schemaVersion)` pair, or nullptr.
     * @param schemaId Stable schema id.
     * @param schemaVersion Exact positive schema version.
     * @return Borrowed nullable registry entry.
     * @ownership SchemaRegistry owns the entry; callers must not delete or mutate it.
     * @lifetime Valid until any registry mutation or destruction.
     * @thread Call on the schema registry thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across re-entrant mutation.
     */
    [[nodiscard]] static const SchemaDefinition* resolve(const std::string& schemaId, int schemaVersion);

    /** @brief Returns all registered versions for an id in ascending order.
     * @param schemaId Stable schema id.
     * @return Empty when the id is absent.
     */
    [[nodiscard]] static std::vector<int> versions(const std::string& schemaId);

    /** @brief Removes all registered versions for a stable id.
     * @param id Stable schema id.
     * @return Success, or NotFound when no version is registered.
     */
    [[nodiscard]] static eve::Result<void> remove(const std::string& id);

    /** @brief Removes one exact schema version.
     * @param id Stable schema id.
     * @param schemaVersion Exact version to remove.
     * @return Success, or NotFound when that version is not registered.
     */
    [[nodiscard]] static eve::Result<void> remove(const std::string& id, int schemaVersion);

    /** @brief Clears all registered schemas. */
    static void clear();

    /** @brief Returns the number of registered schema ids.
     * @return Unique id count, independent of how many versions each id has.
     */
    static int count();

    /** @brief Returns the number of registered `(id, version)` entries.
     * @return Total number of schema versions across all ids.
     */
    static int versionCount();

    /** @brief Returns registered schema ids, unique and in deterministic lexical order. */
    static std::vector<std::string> ids();

    /** @brief Validates JSON text against the highest registered schema version.
     * @param schemaId Stable id of the schema to use.
     * @param json JSON-compatible value to validate.
     * @return All discovered failures; empty means valid.
     */
    [[nodiscard]] static std::vector<ValidationError> validate(const std::string& schemaId, const std::string& json);

    /** @brief Validates JSON against one exact schema version. */
    [[nodiscard]] static std::vector<ValidationError> validate(const std::string& schemaId, int schemaVersion,
                                                                 const std::string& json);

    /**
     * @brief Registers one explicit forward migration edge.
     * @param schemaId Stable schema id whose payload is transformed.
     * @param fromVersion Exact source version.
     * @param toVersion Exact target version; it must be greater than source.
     * @param migration Pure function returning a new owning payload.
     * @return Success, or a structured invalid/conflict failure. The registry
     *         is unchanged on every failure.
     * @remarks One outgoing edge per source version is allowed. This makes a
     *          migration chain unambiguous and lets missing links be reported.
     */
    [[nodiscard]] static eve::Result<void> registerMigration(const std::string& schemaId, int fromVersion,
                                                               int toVersion, MigrationFunction migration);

    /**
     * @brief Resolves an exact compatibility path without executing migrations.
     * @param schemaId Stable schema id.
     * @param fromVersion Persisted source version.
     * @param toVersion Supported target version.
     * @return Exact or explicitly chained compatibility path.
     * @remarks Missing versions/links, cycles and downgrade requests are
     *          failures; the registry is never mutated by this query.
     */
    [[nodiscard]] static eve::Result<SchemaCompatibility> queryCompatibility(const std::string& schemaId,
                                                                               int fromVersion, int toVersion);

    /**
     * @brief Applies an explicit migration chain to an owning payload.
     * @param schemaId Stable schema id.
     * @param fromVersion Exact version of `input`.
     * @param toVersion Exact target version.
     * @param input Source payload; never modified by this operation.
     * @return Migrated owning payload, validated against the target schema.
     * @remarks Migration callbacks receive const input and must return a new
     *          value. Callback or validation failure leaves both input and
     *          the registry unchanged.
     */
    [[nodiscard]] static eve::Result<eve::Value> migrate(const std::string& schemaId, int fromVersion,
                                                         int toVersion, const eve::Value& input);

    /**
     * @brief Parses, migrates and serializes a JSON payload through the chain.
     * @param schemaId Stable schema id.
     * @param fromVersion Exact source version.
     * @param toVersion Exact target version.
     * @param json Source JSON payload.
     * @return Canonical migrated JSON, or a structured failure.
     */
    [[nodiscard]] static eve::Result<std::string> migrateJson(const std::string& schemaId, int fromVersion,
                                                               int toVersion, const std::string& json);

    /**
     * @brief Produce stable Markdown for one exact registered schema.
     * @param schemaId Stable schema id.
     * @param schemaVersion Exact schema version.
     * @return Minimal generated schema documentation.
     */
    [[nodiscard]] static eve::Result<std::string> generateDocumentation(const std::string& schemaId,
                                                                         int schemaVersion);

    /**
     * @brief Produce a stable JSON binding contract for one exact schema.
     * @param schemaId Stable schema id.
     * @param schemaVersion Exact schema version.
     * @return Canonical contract consumed by tooling/binding generators.
     */
    [[nodiscard]] static eve::Result<std::string> generateBindingContract(const std::string& schemaId,
                                                                            int schemaVersion);
};

}  // namespace eve::schema
