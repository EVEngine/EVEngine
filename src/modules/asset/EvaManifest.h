#pragma once

/**
 * @file EvaManifest.h
 * @brief Canonical manifest model for EVEngine Asset Archive (`.eva`) version 1.
 */

#include "common/ResourceRef.h"
#include "common/SchemaVersion.h"
#include "common/Value.h"

#include <map>
#include <string>
#include <vector>

namespace eve::asset {

/** @brief Dependency role recorded by a source asset archive. */
enum class EvaDependencyKind : std::uint8_t {
    RuntimeRequired,
    RuntimeOptional,
    Build,
    Editor,
    Source,
    Platform,
};

/** @brief Return the stable manifest spelling for a dependency role. */
[[nodiscard]] std::string_view evaDependencyKindName(EvaDependencyKind kind) noexcept;

/** @brief One typed asset definition in an `.eva` archive. */
struct EvaAssetEntry {
    AssetRef                 asset;
    std::string              type;
    SchemaVersion            schemaVersion;
    std::string              definition;
    std::string              contentHash;
    std::vector<std::string> tags;
};

/** @brief One directed dependency between stable asset identities. */
struct EvaDependency {
    AssetRef           from;
    AssetRef           to;
    EvaDependencyKind  kind = EvaDependencyKind::RuntimeRequired;
    std::string        path;
    Value::Object      predicate;
    std::string        expectedType;
    /** @brief Required observable behavior when a runtime-optional target is unavailable. */
    Value::Object      fallback;
};

/**
 * @brief Validated, owning `.eva` manifest.
 *
 * Known fields are held in typed members. Unknown top-level fields are retained
 * in `extensions` so a supported reader can rewrite the source archive without
 * erasing newer metadata. Runtime semantics never branch on those fields.
 */
struct EvaManifest {
    static constexpr std::string_view kSchema = "eve.asset-archive";
    static constexpr std::uint64_t    kVersion = 1;

    PersistentId                   packageId;
    std::string                    packageName;
    std::string                    packageVersion;
    std::vector<EvaAssetEntry>     assets;
    std::vector<EvaDependency>     dependencies;
    std::map<std::string, AssetRef> entrypoints;
    Value::Object                  provenance;
    Value::Object                  extensions;
};

/**
 * @brief Parse and validate canonical `.eva` manifest JSON.
 * @param json Untrusted UTF-8 JSON bytes from `manifest.json`.
 * @return An owning validated manifest or a structured diagnostic.
 * @thread Worker-safe when each call owns its input and result.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<EvaManifest> parseEvaManifest(std::string_view json);

/**
 * @brief Serialize a validated manifest as deterministic compact JSON.
 * @return Canonical JSON or a serialization diagnostic.
 * @thread Worker-safe when the manifest is not concurrently mutated.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<std::string> serializeEvaManifest(const EvaManifest& manifest);

}  // namespace eve::asset
