#pragma once

/** @file UnitySource.h @brief Bounded Unity package ingestion and project identity index. */

#include "asset/import/AssetImporter.h"

namespace eve::asset_import {

/** @brief Owning project-relative source bytes, including the original .meta files. */
using UnitySourceFiles = std::map<std::string, std::vector<std::uint8_t>>;

/** @brief Source category; classification does not imply runtime conversion support. */
enum class UnitySourceKind : std::uint8_t {
    Folder,
    Prefab,
    Scene,
    Model,
    Material,
    Image,
    Animation,
    Audio,
    Font,
    Shader,
    Script,
    Data,
    Other,
};

/** @brief External serialized object identity. fileId is signed, as in Unity serialization. */
struct UnitySourceReference {
    std::string  guid;
    std::int64_t fileId = 0;
};

/** @brief Immutable-by-convention owning source index entry; no borrowed source addresses. */
struct UnitySourceAsset {
    std::string                       path;
    std::string                       guid;
    UnitySourceKind                   kind = UnitySourceKind::Other;
    std::string                       importer;
    std::vector<UnitySourceReference> references;
};

/** @brief Owning deterministic index sorted by path, plus missing-reference diagnostics. */
struct UnitySourceIndex {
    std::vector<UnitySourceAsset> assets;
    std::vector<ImportFinding>    findings;
};

/**
 * @brief Decode a gzip/tar .unitypackage into project-relative source bytes without extraction.
 * @param bytes Borrowed compressed bytes, required only during this call.
 * @param limits Bounds for compressed/decompressed bytes, asset count, and path length.
 * @return Owning files or a diagnostic; malformed, duplicate, linked and unsafe entries fail.
 * @thread Worker-safe with immutable input; no global state.
 * @reentrancy No callbacks, filesystem access, Unity execution, or partial publication.
 */
[[nodiscard]] Result<UnitySourceFiles> readUnityPackage(std::span<const std::uint8_t> bytes,
                                                        const AssetImportLimits&      limits = {});

/**
 * @brief Validate project paths and .meta identities and index serialized GUID/fileID references.
 * @param files Borrowed immutable project files, required only during this call.
 * @param limits Bounds checked before parsing source metadata.
 * @return Owning index; ambiguous GUIDs/paths fail, unresolved references produce findings.
 * @remarks Unknown importer fields remain in the caller's source bytes. Binary payloads are
 * classified without interpreting them as YAML. Built-in Unity GUIDs are not missing files.
 * @thread Worker-safe with immutable input.
 * @reentrancy No callbacks, filesystem access, execution, or mutation of input.
 */
[[nodiscard]] Result<UnitySourceIndex> indexUnitySources(const UnitySourceFiles&  files,
                                                         const AssetImportLimits& limits = {});

}  // namespace eve::asset_import
