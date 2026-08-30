#include "asset/EvpackRange.h"

#include "asset/EvpackCompression.h"

#include "data/HashFunction.h"

#include <algorithm>
#include <fstream>
#include <limits>

namespace eve::asset {
namespace {

template <class T>
Result<T> rangeFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.evpack.range"));
}

std::uint64_t little64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index != 8; ++index) value |= std::uint64_t(bytes[offset + index]) << (index * 8);
    return value;
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes) {
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")
        ->hash("sha256", reinterpret_cast<const char*>(bytes.data()), bytes.size(), digest);
    std::array<std::uint8_t, 32> result{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data), result.size(), result.begin());
    return result;
}

}  // namespace

Result<std::uint64_t> FileEvpackRangeSource::size() const {
    std::error_code ec;
    const auto value = std::filesystem::file_size(path_, ec);
    if (ec) return rangeFailure<std::uint64_t>(DiagnosticCode::NotFound,
                                               "cannot read runtime package size: " + ec.message(), path_.string());
    return Result<std::uint64_t>::success(value);
}

Result<std::vector<std::uint8_t>> FileEvpackRangeSource::read(std::uint64_t offset,
                                                              std::uint64_t size) const {
    if (size > std::numeric_limits<std::size_t>::max() ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                       "file range cannot be represented", path_.string());
    std::ifstream input(path_, std::ios::binary);
    if (!input) return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::NotFound,
                                                               "cannot open runtime package", path_.string());
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input) return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::Failed,
                                                               "cannot seek runtime package", path_.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || static_cast<std::uint64_t>(input.gcount()) != size)
        return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::Failed,
                                                       "runtime package range is truncated", path_.string());
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<EvpackRangeMount> prepareEvpackRangeMount(std::shared_ptr<const EvpackRangeSource> source,
                                                  const EvpackLimits& limits,
                                                  const EvpackTrust& trust) {
    if (!source) return rangeFailure<EvpackRangeMount>(DiagnosticCode::InvalidArgument,
                                                       "range source is required");
    auto sourceSize = source->size();
    if (!sourceSize) return Result<EvpackRangeMount>::failure(sourceSize.status());
    if (sourceSize.value() < 128 || sourceSize.value() > limits.maximumPackageBytes)
        return rangeFailure<EvpackRangeMount>(DiagnosticCode::InvalidArgument,
                                              "range source size is outside package limits");
    auto header = source->read(0, 128);
    if (!header) return Result<EvpackRangeMount>::failure(header.status());
    if (header.value().size() != 128)
        return rangeFailure<EvpackRangeMount>(DiagnosticCode::Failed, "range source returned a short header");
    // Header offsets are still untrusted here; only use them to request the bounded metadata prefix.
    const std::uint64_t tocOffset = little64(header.value(), 48);
    const std::uint64_t tocSize = little64(header.value(), 56);
    const std::uint64_t signatureOffset = little64(header.value(), 80);
    const std::uint64_t signatureSize = little64(header.value(), 88);
    if (tocOffset > sourceSize.value() || tocSize > sourceSize.value() - tocOffset ||
        tocOffset + tocSize > limits.maximumMetadataBytes + 128)
        return rangeFailure<EvpackRangeMount>(DiagnosticCode::ParseError,
                                              "runtime package metadata extent is invalid");
    std::uint64_t metadataSize = tocOffset + tocSize;
    if (signatureSize != 0) {
        if (signatureOffset > sourceSize.value() || signatureSize > sourceSize.value() - signatureOffset)
            return rangeFailure<EvpackRangeMount>(DiagnosticCode::ParseError,
                                                  "runtime package signature extent is invalid");
        metadataSize = std::max(metadataSize, signatureOffset + signatureSize);
    }
    auto metadata = source->read(0, metadataSize);
    if (!metadata) return Result<EvpackRangeMount>::failure(metadata.status());
    if (metadata.value().size() != metadataSize)
        return rangeFailure<EvpackRangeMount>(DiagnosticCode::Failed,
                                              "range source returned a short metadata prefix");
    auto index = parseEvpackMetadata(metadata.value(), sourceSize.value(), limits, trust);
    if (!index) return Result<EvpackRangeMount>::failure(index.status());
    return Result<EvpackRangeMount>::success(
        EvpackRangeMount(std::move(source), std::move(index).takeValue(), limits));
}

Result<std::vector<std::uint8_t>> EvpackRangeMount::readChunk(std::size_t index) const {
    if (index >= index_.chunks().size())
        return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                       "chunk index is outside the TOC");
    const auto& chunk = index_.chunks()[index];
    if (chunk.storedSize > limits_.maximumChunkBytes)
        return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                       "range chunk exceeds configured budget", chunk.type);
    auto bytes = source_->read(chunk.offset, chunk.storedSize);
    if (!bytes) return Result<std::vector<std::uint8_t>>::failure(bytes.status());
    if (bytes.value().size() != chunk.storedSize)
        return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::Failed,
                                                       "range source returned a short chunk", chunk.type);
    auto decoded = decompressEvpackChunk(chunk.codec, bytes.value(), chunk.decodedSize,
                                         limits_.maximumDecodedBytes);
    if (!decoded) return Result<std::vector<std::uint8_t>>::failure(decoded.status());
    if (sha256(decoded.value()) != chunk.contentHash)
        return rangeFailure<std::vector<std::uint8_t>>(DiagnosticCode::HashMismatch,
                                                       "range chunk hash does not match", chunk.type);
    return decoded;
}

}  // namespace eve::asset
