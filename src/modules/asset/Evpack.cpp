#include "asset/Evpack.h"

#include "asset/EvpackCompression.h"

#include "common/Utf8Validation.h"

#include "data/HashFunction.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <tuple>

namespace eve::asset {
namespace {

constexpr std::size_t kHeaderSize = 128;
constexpr std::array<std::uint8_t, 8> kPackMagic = {'E', 'V', 'P', 'A', 'C', 'K', 0, 1};
constexpr std::array<std::uint8_t, 8> kManifestMagic = {'E', 'V', 'M', 'A', 'N', 0, 1, 0};
constexpr std::array<std::uint8_t, 8> kTocMagic = {'E', 'V', 'T', 'O', 'C', 0, 1, 0};
constexpr std::array<std::uint8_t, 8> kSignatureMagic = {'E', 'V', 'S', 'I', 'G', 0, 1, 0};
constexpr std::size_t kSignatureSize = 128;
constexpr std::uint32_t kSignedFlag = 1;

template <class T>
Result<T> packFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.evpack"));
}

void put16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift != 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void putId(std::vector<std::uint8_t>& bytes, const PersistentId& id) {
    const auto value = id.bytes();
    bytes.insert(bytes.end(), value.begin(), value.end());
}
void putString(std::vector<std::uint8_t>& bytes, std::string_view value) {
    put32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}
void putStrings(std::vector<std::uint8_t>& bytes, const std::vector<std::string>& values) {
    put32(bytes, static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) putString(bytes, value);
}

bool checkedAdd(std::uint64_t left, std::uint64_t right, std::uint64_t& result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    result = left + right;
    return true;
}

bool alignUp(std::uint64_t value, std::uint32_t alignment, std::uint64_t& result) {
    const std::uint64_t mask = alignment - 1;
    if (value > std::numeric_limits<std::uint64_t>::max() - mask) return false;
    result = (value + mask) & ~mask;
    return true;
}

bool powerOfTwo(std::uint32_t value) { return value != 0 && (value & (value - 1)) == 0; }

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes) {
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")
        ->hash("sha256", reinterpret_cast<const char*>(bytes.data()), bytes.size(), digest);
    std::array<std::uint8_t, 32> result{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data), result.size(), result.begin());
    return result;
}

bool validText(std::string_view value, const EvpackLimits& limits) {
    if (value.empty() || value.size() > limits.maximumStringBytes ||
        !isValidUtf8(value, Utf8NullPolicy::Reject))
        return false;
    for (unsigned char byte : value)
        if (byte == 0 || byte < 0x20 || byte == 0x7f) return false;
    return true;
}

bool validStrings(const std::vector<std::string>& values, const EvpackLimits& limits) {
    if (values.size() > limits.maximumStringBytes) return false;
    std::set<std::string> unique;
    for (const auto& value : values)
        if (!validText(value, limits) || !unique.emplace(value).second) return false;
    return true;
}

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, std::size_t begin, std::size_t end)
        : bytes_(bytes), cursor_(begin), end_(end) {}

    bool raw(std::size_t count, std::span<const std::uint8_t>& value) {
        if (cursor_ > end_ || count > end_ - cursor_) return false;
        value = bytes_.subspan(cursor_, count);
        cursor_ += count;
        return true;
    }
    bool u16(std::uint16_t& value) {
        std::span<const std::uint8_t> data;
        if (!raw(2, data)) return false;
        value = std::uint16_t(data[0]) | (std::uint16_t(data[1]) << 8);
        return true;
    }
    bool u32(std::uint32_t& value) {
        std::span<const std::uint8_t> data;
        if (!raw(4, data)) return false;
        value = 0;
        for (unsigned index = 0; index != 4; ++index) value |= std::uint32_t(data[index]) << (index * 8);
        return true;
    }
    bool u64(std::uint64_t& value) {
        std::span<const std::uint8_t> data;
        if (!raw(8, data)) return false;
        value = 0;
        for (unsigned index = 0; index != 8; ++index) value |= std::uint64_t(data[index]) << (index * 8);
        return true;
    }
    bool id(PersistentId& value) {
        std::span<const std::uint8_t> data;
        if (!raw(16, data)) return false;
        auto parsed = PersistentId::fromBytes(data);
        if (!parsed || parsed->isNil()) return false;
        value = *parsed;
        return true;
    }
    bool string(std::string& value, const EvpackLimits& limits) {
        std::uint32_t size = 0;
        std::span<const std::uint8_t> data;
        if (!u32(size) || size > limits.maximumStringBytes || !raw(size, data)) return false;
        value.assign(reinterpret_cast<const char*>(data.data()), data.size());
        return validText(value, limits);
    }
    bool strings(std::vector<std::string>& values, const EvpackLimits& limits) {
        std::uint32_t count = 0;
        if (!u32(count) || count > limits.maximumStringBytes) return false;
        values.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            std::string value;
            if (!string(value, limits)) return false;
            values.push_back(std::move(value));
        }
        return validStrings(values, limits);
    }
    [[nodiscard]] std::size_t cursor() const { return cursor_; }
    [[nodiscard]] bool finished() const { return cursor_ == end_; }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_;
    std::size_t end_;
};

std::vector<std::uint8_t> encodeManifest(const std::vector<EvpackVariant>& variants) {
    std::vector<std::uint8_t> bytes(kManifestMagic.begin(), kManifestMagic.end());
    put32(bytes, static_cast<std::uint32_t>(variants.size()));
    put32(bytes, 0);
    for (const auto& variant : variants) {
        putString(bytes, variant.os); putString(bytes, variant.arch); putString(bytes, variant.graphics);
        putStrings(bytes, variant.textureFamilies); putString(bytes, variant.shaderFormat);
        putString(bytes, variant.quality); putStrings(bytes, variant.features);
    }
    return bytes;
}

struct BuildRecord {
    EvpackChunkInput input;
    std::vector<std::uint8_t> stored;
    std::array<std::uint8_t, 32> hash{};
    std::uint64_t offset = 0;
};

std::vector<std::uint8_t> encodeToc(const std::vector<BuildRecord>& records) {
    std::vector<std::uint8_t> bytes(kTocMagic.begin(), kTocMagic.end());
    put32(bytes, static_cast<std::uint32_t>(records.size())); put32(bytes, 0);
    for (const auto& record : records) {
        const auto& input = record.input;
        putId(bytes, input.assetId); put64(bytes, input.schemaVersion.value());
        put32(bytes, input.variantIndex); put16(bytes, static_cast<std::uint16_t>(input.kind));
        put16(bytes, static_cast<std::uint16_t>(input.codec)); put32(bytes, input.chunkId);
        put32(bytes, input.alignment);
        put64(bytes, record.offset); put64(bytes, record.stored.size()); put64(bytes, input.bytes.size());
        bytes.insert(bytes.end(), record.hash.begin(), record.hash.end());
        put32(bytes, static_cast<std::uint32_t>(input.dependencies.size()));
        putString(bytes, input.type);
        for (const auto& dependency : input.dependencies) putId(bytes, dependency);
    }
    return bytes;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool variantMatches(const EvpackVariant& variant, const EvpackCapabilities& capabilities) {
    if (variant.os != capabilities.os || variant.arch != capabilities.arch ||
        variant.graphics != capabilities.graphics || !contains(capabilities.shaderFormats, variant.shaderFormat) ||
        !contains(capabilities.qualities, variant.quality)) return false;
    if (!variant.textureFamilies.empty() &&
        std::none_of(variant.textureFamilies.begin(), variant.textureFamilies.end(),
                     [&](const auto& family) { return contains(capabilities.textureFamilies, family); })) return false;
    return std::all_of(variant.features.begin(), variant.features.end(),
                       [&](const auto& feature) { return contains(capabilities.features, feature); });
}

Result<std::vector<std::uint8_t>> encodeSignature(const EvpackSignature& signature) {
    if (signature.algorithm.empty() || signature.algorithm.size() > 15 ||
        !validText(signature.algorithm, EvpackLimits{}))
        return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                      "signature algorithm identifier is invalid");
    std::vector<std::uint8_t> bytes(kSignatureSize, 0);
    std::copy(kSignatureMagic.begin(), kSignatureMagic.end(), bytes.begin());
    std::copy(signature.algorithm.begin(), signature.algorithm.end(), bytes.begin() + 8);
    std::copy(signature.keyId.begin(), signature.keyId.end(), bytes.begin() + 24);
    std::copy(signature.bytes.begin(), signature.bytes.end(), bytes.begin() + 56);
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<EvpackSignature> decodeSignature(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kSignatureSize ||
        !std::equal(kSignatureMagic.begin(), kSignatureMagic.end(), bytes.begin()))
        return packFailure<EvpackSignature>(DiagnosticCode::ParseError,
                                            "runtime package signature block is invalid");
    const auto terminator = std::find(bytes.begin() + 8, bytes.begin() + 24, std::uint8_t(0));
    if (terminator == bytes.begin() + 8)
        return packFailure<EvpackSignature>(DiagnosticCode::ParseError,
                                            "runtime package signature algorithm is empty");
    if (std::any_of(terminator, bytes.begin() + 24, [](std::uint8_t value) { return value != 0; }) ||
        std::any_of(bytes.begin() + 120, bytes.end(), [](std::uint8_t value) { return value != 0; }))
        return packFailure<EvpackSignature>(DiagnosticCode::ParseError,
                                            "runtime package signature padding is not canonical");
    EvpackSignature signature;
    signature.algorithm.assign(reinterpret_cast<const char*>(&bytes[8]),
                               static_cast<std::size_t>(terminator - (bytes.begin() + 8)));
    if (!validText(signature.algorithm, EvpackLimits{}))
        return packFailure<EvpackSignature>(DiagnosticCode::ParseError,
                                            "runtime package signature algorithm is invalid");
    std::copy_n(bytes.begin() + 24, 32, signature.keyId.begin());
    std::copy_n(bytes.begin() + 56, 64, signature.bytes.begin());
    return Result<EvpackSignature>::success(std::move(signature));
}

}  // namespace

Result<std::vector<std::uint8_t>> buildEvpack(EvpackBuild build, const EvpackLimits& limits) {
    if (build.packageId.isNil() || build.buildId.isNil())
        return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                      "packageId and buildId must be non-nil");
    if (build.variants.empty() || build.variants.size() > limits.maximumVariants ||
        build.chunks.size() > limits.maximumChunks)
        return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                      "variant or chunk count is outside limits");
    for (const auto& variant : build.variants) {
        if (!validText(variant.os, limits) || !validText(variant.arch, limits) ||
            !validText(variant.graphics, limits) || !validText(variant.shaderFormat, limits) ||
            !validText(variant.quality, limits) || !validStrings(variant.textureFamilies, limits) ||
            !validStrings(variant.features, limits))
            return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                          "variant contains invalid capability text");
    }

    std::vector<BuildRecord> records;
    std::uint64_t decodedTotal = 0;
    for (auto& input : build.chunks) {
        if (input.assetId.isNil() || !validText(input.type, limits) || input.schemaVersion.value() == 0 ||
            input.variantIndex >= build.variants.size() || !powerOfTwo(input.alignment) ||
            input.alignment > 16 * 1024 * 1024 || input.dependencies.size() > limits.maximumDependencies ||
            (input.codec != EvpackCodec::None && input.codec != EvpackCodec::Zstd) ||
            input.bytes.size() > limits.maximumChunkBytes ||
            input.bytes.size() > limits.maximumDecodedBytes ||
            decodedTotal > limits.maximumDecodedBytes - input.bytes.size())
            return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                          "chunk metadata or resource budget is invalid", input.type);
        for (const auto& dependency : input.dependencies)
            if (dependency.isNil())
                return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                              "chunk dependency must be non-nil", input.type);
        std::sort(input.dependencies.begin(), input.dependencies.end());
        if (std::adjacent_find(input.dependencies.begin(), input.dependencies.end()) != input.dependencies.end())
            return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::Conflict,
                                                          "chunk dependencies contain duplicates", input.type);
        decodedTotal += input.bytes.size();
        records.push_back({std::move(input), {}, {}, 0});
        records.back().hash = sha256(records.back().input.bytes);
        auto compressed = compressEvpackChunk(records.back().input.codec, records.back().input.bytes);
        if (!compressed) return Result<std::vector<std::uint8_t>>::failure(compressed.status());
        records.back().stored = std::move(compressed).takeValue();
        if (records.back().stored.size() > limits.maximumChunkBytes)
            return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                          "stored chunk exceeds size limits",
                                                          records.back().input.type);
    }
    std::sort(records.begin(), records.end(), [](const BuildRecord& left, const BuildRecord& right) {
        return std::tie(left.input.assetId, left.input.type, left.input.schemaVersion, left.input.variantIndex,
                        left.input.kind, left.input.chunkId) <
               std::tie(right.input.assetId, right.input.type, right.input.schemaVersion, right.input.variantIndex,
                        right.input.kind, right.input.chunkId);
    });
    for (std::size_t index = 1; index < records.size(); ++index) {
        const auto key = [](const BuildRecord& value) {
            return std::tie(value.input.assetId, value.input.type, value.input.schemaVersion,
                            value.input.variantIndex, value.input.kind, value.input.chunkId);
        };
        if (key(records[index - 1]) == key(records[index]))
            return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::Conflict,
                                                          "duplicate runtime chunk key", records[index].input.type);
    }

    auto manifest = encodeManifest(build.variants);
    auto preliminaryToc = encodeToc(records);
    if (manifest.size() > limits.maximumMetadataBytes || preliminaryToc.size() > limits.maximumMetadataBytes ||
        manifest.size() > limits.maximumMetadataBytes - preliminaryToc.size())
        return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                      "runtime package metadata exceeds size limits");
    const std::uint64_t signatureSize = build.signer ? kSignatureSize : 0;
    std::uint64_t cursor = kHeaderSize + manifest.size() + preliminaryToc.size() + signatureSize;
    for (auto& record : records) {
        if (!alignUp(cursor, record.input.alignment, record.offset) ||
            !checkedAdd(record.offset, record.stored.size(), cursor) || cursor > limits.maximumPackageBytes)
            return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                          "encoded package exceeds size limits");
    }
    auto toc = encodeToc(records);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(cursor), 0);
    std::copy(manifest.begin(), manifest.end(), bytes.begin() + kHeaderSize);
    const std::uint64_t tocOffset = kHeaderSize + manifest.size();
    std::copy(toc.begin(), toc.end(), bytes.begin() + static_cast<std::size_t>(tocOffset));
    for (const auto& record : records)
        std::copy(record.stored.begin(), record.stored.end(),
                  bytes.begin() + static_cast<std::size_t>(record.offset));

    std::vector<std::uint8_t> header(kPackMagic.begin(), kPackMagic.end());
    put32(header, kHeaderSize); put32(header, build.signer ? kSignedFlag : 0);
    putId(header, build.packageId); putId(header, build.buildId);
    put64(header, tocOffset); put64(header, toc.size()); put64(header, kHeaderSize); put64(header, manifest.size());
    put64(header, build.signer ? tocOffset + toc.size() : 0); put64(header, signatureSize);
    header.resize(kHeaderSize, 0);
    std::vector<std::uint8_t> metadataHashInput = header;
    metadataHashInput.insert(metadataHashInput.end(), manifest.begin(), manifest.end());
    metadataHashInput.insert(metadataHashInput.end(), toc.begin(), toc.end());
    const auto headerHash = sha256(metadataHashInput);
    std::copy(headerHash.begin(), headerHash.end(), header.begin() + 96);
    std::copy(header.begin(), header.end(), bytes.begin());
    if (build.signer) {
        auto signature = build.signer->sign(headerHash);
        if (!signature) return Result<std::vector<std::uint8_t>>::failure(signature.status());
        auto encoded = encodeSignature(signature.value());
        if (!encoded) return encoded;
        std::copy(encoded.value().begin(), encoded.value().end(),
                  bytes.begin() + static_cast<std::size_t>(tocOffset + toc.size()));
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<Evpack> parseEvpackMetadata(std::span<const std::uint8_t> bytes, std::uint64_t packageSize,
                                   const EvpackLimits& limits, const EvpackTrust& trust) {
    if (packageSize < kHeaderSize || packageSize > limits.maximumPackageBytes ||
        bytes.size() < kHeaderSize || bytes.size() > packageSize)
        return packFailure<Evpack>(DiagnosticCode::ParseError, "package size is outside limits");
    const bool completePackage = bytes.size() == packageSize;
    Reader header(bytes, 0, kHeaderSize);
    std::span<const std::uint8_t> magic;
    std::uint32_t headerSize = 0, flags = 0;
    PersistentId packageId, buildId;
    std::uint64_t tocOffset = 0, tocSize = 0, manifestOffset = 0, manifestSize = 0, signatureOffset = 0,
                  signatureSize = 0;
    std::span<const std::uint8_t> storedHeaderHash;
    if (!header.raw(8, magic) || !std::equal(magic.begin(), magic.end(), kPackMagic.begin()) ||
        !header.u32(headerSize) || headerSize != kHeaderSize || !header.u32(flags) ||
        (flags & ~kSignedFlag) != 0 ||
        !header.id(packageId) || !header.id(buildId) || !header.u64(tocOffset) || !header.u64(tocSize) ||
        !header.u64(manifestOffset) || !header.u64(manifestSize) || !header.u64(signatureOffset) ||
        !header.u64(signatureSize) ||
        !header.raw(32, storedHeaderHash) || !header.finished())
        return packFailure<Evpack>(DiagnosticCode::ParseError, "runtime package header is invalid");
    const auto validRange = [&](std::uint64_t offset, std::uint64_t size) {
        return offset >= kHeaderSize && offset <= packageSize && size <= packageSize - offset;
    };
    if (!validRange(manifestOffset, manifestSize) || !validRange(tocOffset, tocSize) ||
        manifestOffset != kHeaderSize || manifestOffset + manifestSize != tocOffset ||
        manifestSize > limits.maximumMetadataBytes || tocSize > limits.maximumMetadataBytes ||
        manifestSize > limits.maximumMetadataBytes - tocSize)
        return packFailure<Evpack>(DiagnosticCode::ParseError, "manifest or TOC range is invalid");
    const bool signedPackage = (flags & kSignedFlag) != 0;
    if (signedPackage != (signatureSize != 0) ||
        (signedPackage && (signatureSize != kSignatureSize || signatureOffset != tocOffset + tocSize ||
                           !validRange(signatureOffset, signatureSize))) ||
        (!signedPackage && (signatureOffset != 0 || signatureSize != 0)))
        return packFailure<Evpack>(DiagnosticCode::ParseError, "signature range is invalid");
    if (tocOffset + tocSize > bytes.size())
        return packFailure<Evpack>(DiagnosticCode::ParseError, "metadata prefix does not contain the complete TOC");
    std::vector<std::uint8_t> metadataHashInput(bytes.begin(), bytes.begin() + kHeaderSize);
    std::fill(metadataHashInput.begin() + 96, metadataHashInput.end(), 0);
    metadataHashInput.insert(metadataHashInput.end(), bytes.begin() + static_cast<std::size_t>(manifestOffset),
                             bytes.begin() + static_cast<std::size_t>(manifestOffset + manifestSize));
    metadataHashInput.insert(metadataHashInput.end(), bytes.begin() + static_cast<std::size_t>(tocOffset),
                             bytes.begin() + static_cast<std::size_t>(tocOffset + tocSize));
    const auto computedMetadataHash = sha256(metadataHashInput);
    if (!std::equal(storedHeaderHash.begin(), storedHeaderHash.end(), computedMetadataHash.begin()))
        return packFailure<Evpack>(DiagnosticCode::HashMismatch,
                                   "runtime package header and metadata hash does not match");

    Evpack pack;
    pack.packageId_ = packageId;
    pack.buildId_ = buildId;
    if (signedPackage) {
        if (signatureOffset + signatureSize > bytes.size())
            return packFailure<Evpack>(DiagnosticCode::ParseError,
                                       "metadata prefix does not contain the signature block");
        auto signature = decodeSignature(bytes.subspan(static_cast<std::size_t>(signatureOffset),
                                                       static_cast<std::size_t>(signatureSize)));
        if (!signature) return Result<Evpack>::failure(signature.status());
        if (!trust.verifier)
            return packFailure<Evpack>(DiagnosticCode::PreconditionViolation,
                                       "signed package requires a configured trust verifier");
        auto verified = trust.verifier->verify(computedMetadataHash, signature.value());
        if (!verified) return Result<Evpack>::failure(verified.status());
        pack.signature_ = signature.value();
        pack.trustedSignature_ = true;
    } else if (trust.policy == EvpackSignaturePolicy::RequireTrustedSignature) {
        return packFailure<Evpack>(DiagnosticCode::PreconditionViolation,
                                   "project policy requires a trusted package signature");
    }

    Reader manifestReader(bytes, static_cast<std::size_t>(manifestOffset),
                          static_cast<std::size_t>(manifestOffset + manifestSize));
    std::uint32_t variantCount = 0, reserved = 0;
    if (!manifestReader.raw(8, magic) || !std::equal(magic.begin(), magic.end(), kManifestMagic.begin()) ||
        !manifestReader.u32(variantCount) || variantCount == 0 || variantCount > limits.maximumVariants ||
        !manifestReader.u32(reserved) || reserved != 0)
        return packFailure<Evpack>(DiagnosticCode::ParseError, "binary Cook manifest is invalid");
    pack.variants_.reserve(variantCount);
    for (std::uint32_t index = 0; index < variantCount; ++index) {
        EvpackVariant variant;
        if (!manifestReader.string(variant.os, limits) || !manifestReader.string(variant.arch, limits) ||
            !manifestReader.string(variant.graphics, limits) ||
            !manifestReader.strings(variant.textureFamilies, limits) ||
            !manifestReader.string(variant.shaderFormat, limits) || !manifestReader.string(variant.quality, limits) ||
            !manifestReader.strings(variant.features, limits))
            return packFailure<Evpack>(DiagnosticCode::ParseError, "variant record is invalid");
        pack.variants_.push_back(std::move(variant));
    }
    if (!manifestReader.finished())
        return packFailure<Evpack>(DiagnosticCode::ParseError, "binary Cook manifest has trailing bytes");

    Reader tocReader(bytes, static_cast<std::size_t>(tocOffset), static_cast<std::size_t>(tocOffset + tocSize));
    std::uint32_t chunkCount = 0;
    if (!tocReader.raw(8, magic) || !std::equal(magic.begin(), magic.end(), kTocMagic.begin()) ||
        !tocReader.u32(chunkCount) || chunkCount > limits.maximumChunks ||
        !tocReader.u32(reserved) || reserved != 0)
        return packFailure<Evpack>(DiagnosticCode::ParseError, "runtime package TOC is invalid");
    std::uint64_t decodedTotal = 0;
    std::uint64_t previousEnd = signedPackage ? signatureOffset + signatureSize : tocOffset + tocSize;
    using ChunkKey = std::tuple<PersistentId, std::string, std::uint64_t, std::uint32_t,
                                std::uint16_t, std::uint32_t>;
    std::optional<ChunkKey> previousKey;
    for (std::uint32_t index = 0; index < chunkCount; ++index) {
        EvpackChunk chunk;
        std::uint16_t kind = 0, codec = 0;
        std::uint64_t schemaVersion = 0;
        std::span<const std::uint8_t> hash;
        std::uint32_t dependencyCount = 0;
        if (!tocReader.id(chunk.assetId) || !tocReader.u64(schemaVersion) || schemaVersion == 0 ||
            !tocReader.u32(chunk.variantIndex) ||
            chunk.variantIndex >= variantCount || !tocReader.u16(kind) || !tocReader.u16(codec) ||
            !tocReader.u32(chunk.chunkId) || !tocReader.u32(chunk.alignment) || !powerOfTwo(chunk.alignment) ||
            chunk.alignment > 16 * 1024 * 1024 || !tocReader.u64(chunk.offset) ||
            !tocReader.u64(chunk.storedSize) || !tocReader.u64(chunk.decodedSize) ||
            !tocReader.raw(32, hash) || !tocReader.u32(dependencyCount) ||
            dependencyCount > limits.maximumDependencies || !tocReader.string(chunk.type, limits))
            return packFailure<Evpack>(DiagnosticCode::ParseError, "runtime chunk record is invalid");
        chunk.schemaVersion = SchemaVersion(schemaVersion);
        chunk.kind = static_cast<EvpackChunkKind>(kind);
        chunk.codec = static_cast<EvpackCodec>(codec);
        if (chunk.kind < EvpackChunkKind::Definition || chunk.kind > EvpackChunkKind::DebugName ||
            (chunk.codec != EvpackCodec::None && chunk.codec != EvpackCodec::Zstd) ||
            (chunk.codec == EvpackCodec::None && chunk.storedSize != chunk.decodedSize) ||
            chunk.storedSize > limits.maximumChunkBytes || chunk.decodedSize > limits.maximumDecodedBytes ||
            decodedTotal > limits.maximumDecodedBytes - chunk.decodedSize || chunk.offset % chunk.alignment != 0 ||
            chunk.offset < previousEnd || chunk.offset > packageSize || chunk.storedSize > packageSize - chunk.offset)
            return packFailure<Evpack>(DiagnosticCode::Unsupported, "unsupported or unsafe runtime chunk", chunk.type);
        ChunkKey key{chunk.assetId, chunk.type, chunk.schemaVersion.value(), chunk.variantIndex, kind, chunk.chunkId};
        if (previousKey && !(*previousKey < key))
            return packFailure<Evpack>(DiagnosticCode::Conflict, "runtime chunk TOC is not canonical", chunk.type);
        previousKey = std::move(key);
        std::copy(hash.begin(), hash.end(), chunk.contentHash.begin());
        chunk.dependencies.reserve(dependencyCount);
        for (std::uint32_t dependency = 0; dependency < dependencyCount; ++dependency) {
            PersistentId id;
            if (!tocReader.id(id))
                return packFailure<Evpack>(DiagnosticCode::ParseError, "chunk dependency is invalid", chunk.type);
            chunk.dependencies.push_back(id);
        }
        if (!std::is_sorted(chunk.dependencies.begin(), chunk.dependencies.end()) ||
            std::adjacent_find(chunk.dependencies.begin(), chunk.dependencies.end()) != chunk.dependencies.end())
            return packFailure<Evpack>(DiagnosticCode::Conflict, "chunk dependencies are not canonical", chunk.type);
        if (completePackage) {
            const auto payload = bytes.subspan(static_cast<std::size_t>(chunk.offset),
                                               static_cast<std::size_t>(chunk.storedSize));
            auto decoded = decompressEvpackChunk(chunk.codec, payload, chunk.decodedSize,
                                                 limits.maximumDecodedBytes);
            if (!decoded) return Result<Evpack>::failure(decoded.status());
            if (sha256(decoded.value()) != chunk.contentHash)
                return packFailure<Evpack>(DiagnosticCode::HashMismatch, "runtime chunk hash does not match", chunk.type);
        }
        decodedTotal += chunk.decodedSize;
        previousEnd = chunk.offset + chunk.storedSize;
        pack.chunks_.push_back(std::move(chunk));
    }
    if (!tocReader.finished())
        return packFailure<Evpack>(DiagnosticCode::ParseError, "runtime package TOC has trailing bytes");
    if (completePackage) pack.bytes_.assign(bytes.begin(), bytes.end());
    return Result<Evpack>::success(std::move(pack));
}

Result<Evpack> parseEvpack(std::span<const std::uint8_t> bytes, const EvpackLimits& limits,
                           const EvpackTrust& trust) {
    return parseEvpackMetadata(bytes, bytes.size(), limits, trust);
}

Result<std::span<const std::uint8_t>> Evpack::chunkBytes(std::size_t index) const {
    if (index >= chunks_.size())
        return packFailure<std::span<const std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                          "chunk index is outside the TOC");
    if (bytes_.empty())
        return packFailure<std::span<const std::uint8_t>>(DiagnosticCode::PreconditionViolation,
                                                          "metadata-only package requires a range source");
    const auto& chunk = chunks_[index];
    if (chunk.codec != EvpackCodec::None)
        return packFailure<std::span<const std::uint8_t>>(DiagnosticCode::PreconditionViolation,
                                                          "compressed chunk requires decodeChunk()", chunk.type);
    return Result<std::span<const std::uint8_t>>::success(
        std::span<const std::uint8_t>(bytes_).subspan(static_cast<std::size_t>(chunk.offset),
                                                      static_cast<std::size_t>(chunk.storedSize)));
}

Result<std::vector<std::uint8_t>> Evpack::decodeChunk(
    std::size_t index, std::uint64_t maximumDecodedBytes) const {
    if (index >= chunks_.size())
        return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                       "chunk index is outside the TOC");
    if (bytes_.empty())
        return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::PreconditionViolation,
                                                       "metadata-only package requires a range source");
    const auto& chunk = chunks_[index];
    const auto stored = std::span<const std::uint8_t>(bytes_).subspan(
        static_cast<std::size_t>(chunk.offset), static_cast<std::size_t>(chunk.storedSize));
    auto decoded = decompressEvpackChunk(chunk.codec, stored, chunk.decodedSize, maximumDecodedBytes);
    if (!decoded) return decoded;
    if (sha256(decoded.value()) != chunk.contentHash)
        return packFailure<std::vector<std::uint8_t>>(DiagnosticCode::HashMismatch,
                                                       "runtime chunk hash does not match", chunk.type);
    return decoded;
}

Result<EvpackVariantSelection> selectEvpackVariant(const Evpack& pack,
                                                    const EvpackCapabilities& capabilities) {
    for (std::size_t index = 0; index < pack.variants().size(); ++index) {
        if (variantMatches(pack.variants()[index], capabilities))
            return Result<EvpackVariantSelection>::success(
                {static_cast<std::uint32_t>(index), index != 0});
    }
    return packFailure<EvpackVariantSelection>(DiagnosticCode::Unsupported,
                                                "no runtime package variant satisfies device capabilities");
}

}  // namespace eve::asset
