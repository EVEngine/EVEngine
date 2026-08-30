#pragma once

/**
 * @file Evpack.h
 * @brief Versioned random-access runtime package produced by the asset cooker.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "common/SchemaVersion.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eve::asset {

/** @brief Semantic role of a runtime chunk. */
enum class EvpackChunkKind : std::uint16_t {
    Definition = 1,
    Bulk       = 2,
    Stream     = 3,
    Shader     = 4,
    Dictionary = 5,
    DebugName  = 6,
};

/** @brief Physical outer compression applied to one runtime chunk. */
enum class EvpackCodec : std::uint16_t {
    None = 0,
    Zstd = 1,
};

/** @brief Fixed-size detached publisher signature stored in a v1 runtime pack. */
struct EvpackSignature {
    std::string algorithm;
    std::array<std::uint8_t, 32> keyId{};
    std::array<std::uint8_t, 64> bytes{};
};

/** @brief Tool-side provider that signs the authenticated 32-byte package metadata digest. */
class EvpackSigner {
public:
    virtual ~EvpackSigner() = default;
    /** @brief Produce a publisher signature without retaining the borrowed digest. */
    [[nodiscard]] virtual Result<EvpackSignature> sign(
        std::span<const std::uint8_t, 32> digest) const = 0;
};

/** @brief Runtime trust provider supplied by the application/project policy. */
class EvpackSignatureVerifier {
public:
    virtual ~EvpackSignatureVerifier() = default;
    /** @brief Verify a detached signature against a trusted key identity. */
    [[nodiscard]] virtual Result<void> verify(
        std::span<const std::uint8_t, 32> digest, const EvpackSignature& signature) const = 0;
};

/** @brief Admission policy for unsigned development packages. */
enum class EvpackSignaturePolicy : std::uint8_t {
    AllowUnsignedDevelopment,
    RequireTrustedSignature,
};

/** @brief Trust inputs kept separate from size/resource limits. */
struct EvpackTrust {
    EvpackSignaturePolicy policy = EvpackSignaturePolicy::AllowUnsignedDevelopment;
    std::shared_ptr<const EvpackSignatureVerifier> verifier;
};

/** @brief Capability-addressed runtime representation; list order is fallback order. */
struct EvpackVariant {
    std::string              os;
    std::string              arch;
    std::string              graphics;
    std::vector<std::string> textureFamilies;
    std::string              shaderFormat;
    std::string              quality;
    std::vector<std::string> features;
};

/** @brief Actual runtime device capabilities used for variant selection. */
struct EvpackCapabilities {
    std::string              os;
    std::string              arch;
    std::string              graphics;
    std::vector<std::string> textureFamilies;
    std::vector<std::string> shaderFormats;
    std::vector<std::string> qualities;
    std::vector<std::string> features;
};

/** @brief Cooker-owned uncompressed chunk before deterministic package assembly. */
struct EvpackChunkInput {
    PersistentId                 assetId;
    std::string                  type;
    SchemaVersion               schemaVersion;
    std::uint32_t               variantIndex = 0;
    EvpackChunkKind              kind         = EvpackChunkKind::Definition;
    std::uint32_t               chunkId      = 0;
    EvpackCodec                  codec        = EvpackCodec::None;
    std::uint32_t               alignment    = 1;
    std::vector<PersistentId>    dependencies;
    std::vector<std::uint8_t>    bytes;
};

/** @brief Complete input to deterministic `.evpack` assembly. */
struct EvpackBuild {
    PersistentId                  packageId;
    PersistentId                  buildId;
    std::vector<EvpackVariant>    variants;
    std::vector<EvpackChunkInput> chunks;
    std::shared_ptr<const EvpackSigner> signer;
};

/** @brief Resource limits checked before package allocations and hash work. */
struct EvpackLimits {
    std::uint64_t maximumPackageBytes = 16ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumChunkBytes   = 2ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumDecodedBytes = 32ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumMetadataBytes = 256ULL * 1024 * 1024;
    std::uint32_t maximumChunks       = 1000000;
    std::uint32_t maximumVariants     = 256;
    std::uint32_t maximumDependencies = 4096;
    std::uint32_t maximumStringBytes  = 4096;
};

/** @brief Admitted immutable chunk metadata. */
struct EvpackChunk {
    PersistentId              assetId;
    std::string               type;
    SchemaVersion             schemaVersion;
    std::uint32_t             variantIndex = 0;
    EvpackChunkKind           kind         = EvpackChunkKind::Definition;
    std::uint32_t             chunkId      = 0;
    EvpackCodec               codec        = EvpackCodec::None;
    std::uint32_t             alignment    = 1;
    std::uint64_t             offset       = 0;
    std::uint64_t             storedSize   = 0;
    std::uint64_t             decodedSize  = 0;
    std::array<std::uint8_t, 32> contentHash{};
    std::vector<PersistentId> dependencies;
};

/** @brief Result of ordered capability variant selection. */
struct EvpackVariantSelection {
    std::uint32_t index = 0;
    bool          usedFallback = false;
};

/**
 * @brief Fully admitted owning runtime pack.
 * @remarks Chunk views remain valid until this object is moved, assigned, or destroyed.
 */
class Evpack {
public:
    /** @brief Stable package identity from the verified header. */
    [[nodiscard]] const PersistentId& packageId() const noexcept { return packageId_; }
    /** @brief Deterministic Cook build identity from the verified header. */
    [[nodiscard]] const PersistentId& buildId() const noexcept { return buildId_; }
    /** @brief Ordered capability variants encoded in the binary manifest. */
    [[nodiscard]] const std::vector<EvpackVariant>& variants() const noexcept { return variants_; }
    /** @brief Verified random-access chunk table. */
    [[nodiscard]] const std::vector<EvpackChunk>& chunks() const noexcept { return chunks_; }
    /** @brief Verified publisher signature, absent only for admitted development packages. */
    [[nodiscard]] const std::optional<EvpackSignature>& signature() const noexcept { return signature_; }
    /** @brief Whether publisher trust was cryptographically established during admission. */
    [[nodiscard]] bool hasTrustedSignature() const noexcept { return trustedSignature_; }

    /**
     * @brief Return verified decoded bytes for an uncompressed chunk.
     * @param index Index in `chunks()`.
     * @return A borrowed immutable view or a bounds diagnostic.
     * @remarks The view has the same lifetime as this unmoved Evpack instance. Compressed
     * chunks require `decodeChunk()` because their decoded bytes need owning storage.
     */
    [[nodiscard]] Result<std::span<const std::uint8_t>> chunkBytes(std::size_t index) const;

    /** @brief Decode and authenticate a chunk into owning canonical bytes. */
    [[nodiscard]] Result<std::vector<std::uint8_t>> decodeChunk(
        std::size_t index, std::uint64_t maximumDecodedBytes) const;

private:
    friend Result<Evpack> parseEvpack(std::span<const std::uint8_t>, const EvpackLimits&,
                                      const EvpackTrust&);
    friend Result<Evpack> parseEvpackMetadata(std::span<const std::uint8_t>, std::uint64_t,
                                              const EvpackLimits&, const EvpackTrust&);
    PersistentId                  packageId_;
    PersistentId                  buildId_;
    std::vector<EvpackVariant>    variants_;
    std::vector<EvpackChunk>      chunks_;
    std::vector<std::uint8_t>     bytes_;
    std::optional<EvpackSignature> signature_;
    bool                           trustedSignature_ = false;
};

/** @brief Build deterministic little-endian `.evpack` bytes from validated Cook output. */
[[nodiscard]] Result<std::vector<std::uint8_t>> buildEvpack(
    EvpackBuild build, const EvpackLimits& limits = {});

/** @brief Parse, bound-check and hash-verify a complete untrusted `.evpack`. */
[[nodiscard]] Result<Evpack> parseEvpack(
    std::span<const std::uint8_t> bytes, const EvpackLimits& limits = {},
    const EvpackTrust& trust = {});

/**
 * @brief Admit only the contiguous header/manifest/TOC prefix of a range-readable package.
 * @param metadataPrefix Bytes from offset zero through the end of the TOC.
 * @param packageSize Authoritative total source size used to validate chunk ranges.
 * @return Metadata-only Evpack index; `chunkBytes()` intentionally rejects it.
 * @remarks Chunk hashes are verified later by the range mount before bytes reach a decoder.
 */
[[nodiscard]] Result<Evpack> parseEvpackMetadata(
    std::span<const std::uint8_t> metadataPrefix, std::uint64_t packageSize,
    const EvpackLimits& limits = {}, const EvpackTrust& trust = {});

/**
 * @brief Select the first explicitly ordered variant satisfied by device capabilities.
 * @return Selected index and whether an ordered fallback after index zero was used.
 */
[[nodiscard]] Result<EvpackVariantSelection> selectEvpackVariant(
    const Evpack& pack, const EvpackCapabilities& capabilities);

}  // namespace eve::asset
