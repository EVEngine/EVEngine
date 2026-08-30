#include "asset/EvpackCompression.h"

#include <zstd.h>

#include <limits>

namespace eve::asset {
namespace {

template <class T>
Result<T> codecFailure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), {}, {}, "asset.evpack.codec"));
}

}  // namespace

Result<std::vector<std::uint8_t>> compressEvpackChunk(
    EvpackCodec codec, std::span<const std::uint8_t> decoded) {
    if (codec == EvpackCodec::None)
        return Result<std::vector<std::uint8_t>>::success(
            std::vector<std::uint8_t>(decoded.begin(), decoded.end()));
    if (codec != EvpackCodec::Zstd)
        return codecFailure<std::vector<std::uint8_t>>(DiagnosticCode::Unsupported,
                                                       "unknown runtime chunk codec");
    if (decoded.size() > ZSTD_MAX_INPUT_SIZE)
        return codecFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                       "chunk exceeds the Zstd input limit");
    const std::size_t bound = ZSTD_compressBound(decoded.size());
    std::vector<std::uint8_t> stored(bound);
    const std::size_t size = ZSTD_compress(stored.data(), stored.size(), decoded.data(), decoded.size(), 3);
    if (ZSTD_isError(size))
        return codecFailure<std::vector<std::uint8_t>>(
            DiagnosticCode::Failed, std::string("Zstd compression failed: ") + ZSTD_getErrorName(size));
    stored.resize(size);
    return Result<std::vector<std::uint8_t>>::success(std::move(stored));
}

Result<std::vector<std::uint8_t>> decompressEvpackChunk(
    EvpackCodec codec, std::span<const std::uint8_t> stored, std::uint64_t decodedSize,
    std::uint64_t maximumDecodedBytes) {
    if (decodedSize > maximumDecodedBytes || decodedSize > std::numeric_limits<std::size_t>::max())
        return codecFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                       "decoded chunk exceeds its resource budget");
    if (codec == EvpackCodec::None) {
        if (stored.size() != decodedSize)
            return codecFailure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                           "uncompressed chunk size does not match the TOC");
        return Result<std::vector<std::uint8_t>>::success(
            std::vector<std::uint8_t>(stored.begin(), stored.end()));
    }
    if (codec != EvpackCodec::Zstd)
        return codecFailure<std::vector<std::uint8_t>>(DiagnosticCode::Unsupported,
                                                       "unknown runtime chunk codec");
    std::vector<std::uint8_t> decoded(static_cast<std::size_t>(decodedSize));
    const std::size_t size = ZSTD_decompress(decoded.data(), decoded.size(), stored.data(), stored.size());
    if (ZSTD_isError(size))
        return codecFailure<std::vector<std::uint8_t>>(
            DiagnosticCode::ParseError, std::string("Zstd decompression failed: ") + ZSTD_getErrorName(size));
    if (size != decoded.size())
        return codecFailure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                       "decoded chunk size does not match the TOC");
    return Result<std::vector<std::uint8_t>>::success(std::move(decoded));
}

std::string evpackCodecBuildIdentity(EvpackCodec codec) {
    if (codec == EvpackCodec::None) return "none/1";
    if (codec == EvpackCodec::Zstd)
        return "zstd/" + std::to_string(ZSTD_VERSION_MAJOR) + "." +
               std::to_string(ZSTD_VERSION_MINOR) + "." + std::to_string(ZSTD_VERSION_RELEASE) + "/level-3";
    return "unknown";
}

}  // namespace eve::asset
