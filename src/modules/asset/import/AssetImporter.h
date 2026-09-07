#pragma once

/**
 * @file AssetImporter.h
 * @brief Tool-side image and glTF importers producing canonical `.eva` candidates.
 */

#include "asset/EvaArchive.h"

#include <map>

namespace eve::asset_import {

/** @brief Parser and allocation budgets applied to untrusted importer inputs. */
struct AssetImportLimits {
    std::uint64_t maximumSourceBytes = 2ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumDecodedBytes = 4ULL * 1024 * 1024 * 1024;
    std::uint32_t maximumAssets = 100000;
    std::uint32_t maximumVerticesPerPrimitive = 100000000;
    std::uint32_t maximumIndicesPerPrimitive = 300000000;
    std::uint32_t maximumStringBytes = 4096;
};

/** @brief Explicit source color semantics; importers never infer platform defaults. */
enum class ImageColorSpace : std::uint8_t {
    Srgb,
    Linear,
};

/** @brief Common stable package metadata supplied by an import transaction. */
struct ImportPackageIdentity {
    PersistentId  packageId;
    std::string   packageName;
    std::string   packageVersion;
    Value::Object provenance;
};

/** @brief Untrusted encoded image input and its explicit semantic settings. */
struct ImageImportRequest {
    ImportPackageIdentity        package;
    std::string                  sourceName;
    std::vector<std::uint8_t>    encodedBytes;
    ImageColorSpace              colorSpace = ImageColorSpace::Srgb;
    std::string                  usage = "color";
    AssetImportLimits            limits;
};

/** @brief Untrusted `.gltf`/`.glb` input plus explicitly supplied external URI bytes. */
struct GltfImportRequest {
    ImportPackageIdentity                         package;
    std::string                                   sourceName;
    std::vector<std::uint8_t>                     documentBytes;
    std::map<std::string, std::vector<std::uint8_t>> externalResources;
    AssetImportLimits                             limits;
};

/** @brief Import disposition required for every encountered source feature. */
enum class ImportDisposition : std::uint8_t {
    Translated,
    Baked,
    PreservedSource,
    Unsupported,
};

/** @brief One auditable source-feature outcome emitted by an importer. */
struct ImportFinding {
    std::string       sourcePath;
    std::string       feature;
    ImportDisposition disposition = ImportDisposition::Translated;
    std::string       message;
};

/** @brief Stable source object to canonical asset identity mapping for reimport diagnostics. */
struct ImportSourceMapping {
    std::string sourceObject;
    AssetRef    asset;
};

/** @brief Owning, validated source archive candidate not yet published to disk/database. */
struct PreparedAssetImport {
    asset::EvaManifest                  manifest;
    std::vector<asset::EvaArchiveEntry> entries;
    std::vector<ImportFinding>           findings;
    std::vector<ImportSourceMapping>      sourceMappings;
};

/**
 * @brief Decode image metadata and prepare one canonical `eve.image` source asset.
 * @return Owning candidate or structured format/size diagnostics.
 * @thread Worker-safe.
 * @reentrancy Does not execute callbacks or perform filesystem/network access.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareImageImport(const ImageImportRequest& request);

/**
 * @brief Decode triangle primitives from glTF 2.0 into canonical typed mesh blobs.
 * @return Owning candidate with one `eve.mesh` per primitive.
 * @thread Worker-safe.
 * @reentrancy Does not execute extensions, scripts, callbacks, filesystem or network access.
 * @remarks v1 accepts float POSITION/NORMAL/TEXCOORD_0 and unsigned scalar indices;
 * unsupported primitive modes or sparse/compressed accessors fail explicitly.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareGltfImport(const GltfImportRequest& request);

}  // namespace eve::asset_import
