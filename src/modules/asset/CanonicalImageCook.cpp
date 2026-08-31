#include "asset/CanonicalImageCook.h"

#include "common/Value.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <zlib.h>

namespace eve::asset {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.cook.image"));
}

std::uint32_t big32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
           (std::uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

std::uint8_t paeth(std::uint8_t left, std::uint8_t above, std::uint8_t upperLeft) {
    const int prediction = int(left) + int(above) - int(upperLeft);
    const int leftDistance = std::abs(prediction - int(left));
    const int aboveDistance = std::abs(prediction - int(above));
    const int cornerDistance = std::abs(prediction - int(upperLeft));
    if (leftDistance <= aboveDistance && leftDistance <= cornerDistance) return left;
    return aboveDistance <= cornerDistance ? above : upperLeft;
}

Result<std::vector<std::uint8_t>> decodePng(std::span<const std::uint8_t> bytes,
                                            std::uint32_t expectedWidth,
                                            std::uint32_t expectedHeight,
                                            std::uint64_t maximumDecodedBytes) {
    static constexpr std::uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() < 33 || !std::equal(std::begin(signature), std::end(signature), bytes.begin()))
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                  "PNG signature or header is invalid");
    std::size_t cursor = 8;
    bool sawHeader = false;
    bool sawEnd = false;
    std::uint32_t width = 0, height = 0;
    std::uint8_t channels = 0;
    std::vector<std::uint8_t> compressed;
    while (cursor < bytes.size()) {
        if (bytes.size() - cursor < 12)
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                      "PNG chunk header is truncated");
        const std::uint32_t size = big32(bytes, cursor);
        if (size > bytes.size() - cursor - 12)
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                      "PNG chunk payload is truncated");
        const auto type = bytes.subspan(cursor + 4, 4);
        const auto payload = bytes.subspan(cursor + 8, size);
        const std::uint32_t storedCrc = big32(bytes, cursor + 8 + size);
        uLong crc = crc32(0, Z_NULL, 0);
        crc = crc32(crc, type.data(), 4);
        if (!payload.empty()) crc = crc32(crc, payload.data(), static_cast<uInt>(payload.size()));
        if (static_cast<std::uint32_t>(crc) != storedCrc)
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::HashMismatch,
                                                      "PNG chunk CRC does not match");
        const std::string_view name(reinterpret_cast<const char*>(type.data()), 4);
        if (!sawHeader) {
            if (name != "IHDR" || size != 13)
                return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                          "PNG IHDR must be first");
            width = big32(payload, 0);
            height = big32(payload, 4);
            const std::uint8_t bitDepth = payload[8];
            const std::uint8_t colorType = payload[9];
            if (width == 0 || height == 0 || width != expectedWidth || height != expectedHeight ||
                bitDepth != 8 || (colorType != 2 && colorType != 6) || payload[10] != 0 ||
                payload[11] != 0 || payload[12] != 0)
                return failure<std::vector<std::uint8_t>>(
                    DiagnosticCode::Unsupported,
                    "PNG Cook supports non-interlaced 8-bit RGB/RGBA matching the image definition");
            channels = colorType == 6 ? 4 : 3;
            sawHeader = true;
        } else if (name == "IHDR") {
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::Conflict,
                                                      "PNG contains duplicate IHDR");
        } else if (name == "IDAT") {
            if (compressed.size() > maximumDecodedBytes ||
                payload.size() > maximumDecodedBytes - compressed.size())
                return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                          "PNG compressed data exceeds Cook budget");
            compressed.insert(compressed.end(), payload.begin(), payload.end());
        } else if (name == "IEND") {
            if (size != 0 || cursor + 12 != bytes.size())
                return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                          "PNG IEND or trailing data is invalid");
            sawEnd = true;
        } else if ((type[0] & 0x20u) == 0) {
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::Unsupported,
                                                      "PNG contains an unsupported critical chunk",
                                                      std::string(name));
        }
        cursor += std::size_t(size) + 12;
        if (sawEnd) break;
    }
    const std::uint64_t rowBytes = std::uint64_t(width) * channels;
    const std::uint64_t filteredSize = std::uint64_t(height) * (rowBytes + 1);
    const std::uint64_t rgbaSize = std::uint64_t(width) * height * 4;
    if (!sawHeader || !sawEnd || compressed.empty() || compressed.size() > (std::numeric_limits<uLong>::max)() ||
        filteredSize > maximumDecodedBytes ||
        rgbaSize > maximumDecodedBytes || rgbaSize + 24 > maximumDecodedBytes ||
        filteredSize > std::numeric_limits<uLongf>::max())
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                  "PNG decoded dimensions exceed Cook budget");
    std::vector<std::uint8_t> filtered(static_cast<std::size_t>(filteredSize));
    uLongf outputSize = static_cast<uLongf>(filtered.size());
    if (uncompress(filtered.data(), &outputSize, compressed.data(), static_cast<uLong>(compressed.size())) != Z_OK ||
        outputSize != filtered.size())
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                  "PNG zlib stream is invalid or has unexpected size");
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(rowBytes) * height);
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::size_t sourceBase = std::size_t(row) * (std::size_t(rowBytes) + 1);
        const std::uint8_t filter = filtered[sourceBase];
        if (filter > 4)
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                      "PNG row uses an invalid filter");
        const std::size_t destinationBase = std::size_t(row) * std::size_t(rowBytes);
        for (std::size_t column = 0; column < rowBytes; ++column) {
            const std::uint8_t encoded = filtered[sourceBase + 1 + column];
            const std::uint8_t left = column >= channels ? raw[destinationBase + column - channels] : 0;
            const std::uint8_t above = row ? raw[destinationBase + column - rowBytes] : 0;
            const std::uint8_t corner = row && column >= channels
                                            ? raw[destinationBase + column - rowBytes - channels]
                                            : 0;
            std::uint8_t predictor = 0;
            if (filter == 1) predictor = left;
            else if (filter == 2) predictor = above;
            else if (filter == 3) predictor = static_cast<std::uint8_t>((int(left) + int(above)) / 2);
            else if (filter == 4) predictor = paeth(left, above, corner);
            raw[destinationBase + column] = static_cast<std::uint8_t>(encoded + predictor);
        }
    }
    std::vector<std::uint8_t> rgba;
    rgba.reserve(static_cast<std::size_t>(rgbaSize));
    for (std::size_t pixel = 0; pixel < std::size_t(width) * height; ++pixel) {
        rgba.insert(rgba.end(), raw.begin() + pixel * channels,
                    raw.begin() + pixel * channels + channels);
        if (channels == 3) rgba.push_back(255);
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(rgba));
}

}  // namespace

Result<CookedCanonicalImage> cookCanonicalImageRgba8(
    std::span<const std::uint8_t> definition,
    std::span<const std::uint8_t> encodedSource,
    std::uint64_t maximumDecodedBytes) {
    auto parsed = Value::fromJson(std::string_view(reinterpret_cast<const char*>(definition.data()),
                                                   definition.size()));
    if (!parsed) return Result<CookedCanonicalImage>::failure(parsed.status());
    auto* object = parsed.value().getIf<Value::Object>();
    const Value* schema = object ? field(*object, "schema") : nullptr;
    const Value* version = object ? field(*object, "schemaVersion") : nullptr;
    const Value* widthValue = object ? field(*object, "width") : nullptr;
    const Value* heightValue = object ? field(*object, "height") : nullptr;
    const Value* encoding = object ? field(*object, "encoding") : nullptr;
    const Value* color = object ? field(*object, "color") : nullptr;
    const auto* colorObject = color ? color->getIf<Value::Object>() : nullptr;
    const Value* transfer = colorObject ? field(*colorObject, "transfer") : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.image" || !version ||
        !version->isInt64() || version->asInt() != 2 || !widthValue || !widthValue->isInt64() ||
        widthValue->asInt() <= 0 || !heightValue || !heightValue->isInt64() ||
        heightValue->asInt() <= 0 || !encoding || !encoding->isString() || !transfer ||
        !transfer->isString() || (transfer->asString() != "srgb" && transfer->asString() != "linear"))
        return failure<CookedCanonicalImage>(DiagnosticCode::ParseError,
                                             "eve.image/2 runtime Cook definition is malformed");
    if (encoding->asString() != "png")
        return failure<CookedCanonicalImage>(DiagnosticCode::Unsupported,
                                             "runtime RGBA8 Cook currently supports PNG source encoding");
    const auto width = static_cast<std::uint32_t>(widthValue->asInt());
    const auto height = static_cast<std::uint32_t>(heightValue->asInt());
    auto rgba = decodePng(encodedSource, width, height, maximumDecodedBytes);
    if (!rgba) return Result<CookedCanonicalImage>::failure(rgba.status());
    const bool sourceSrgb = transfer->asString() == "srgb";
    if (sourceSrgb) {
        for (std::size_t index = 0; index < rgba.value().size(); index += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double encoded = rgba.value()[index + channel] / 255.0;
                const double linear = encoded <= 0.04045
                                          ? encoded / 12.92
                                          : std::pow((encoded + 0.055) / 1.055, 2.4);
                rgba.value()[index + channel] = static_cast<std::uint8_t>(
                    std::clamp(std::lround(linear * 255.0), 0l, 255l));
            }
        }
    }
    std::vector<std::uint8_t> bulk = {'E', 'V', 'I', 'M', 'G', 0, 1, 0};
    put32(bulk, width);
    put32(bulk, height);
    put32(bulk, 0);
    put32(bulk, 0);
    bulk.insert(bulk.end(), rgba.value().begin(), rgba.value().end());
    (*object)["encoding"] = Value("rgba8");
    (*object)["sourceEncoding"] = Value("png");
    (*object)["blob"] = Value("chunk:1");
    auto runtimeColor = *colorObject;
    runtimeColor["sourceTransfer"] = Value(transfer->asString());
    runtimeColor["transfer"] = Value("linear");
    (*object)["color"] = Value(std::move(runtimeColor));
    auto runtimeDefinition = parsed.value().toJson();
    if (!runtimeDefinition) return Result<CookedCanonicalImage>::failure(runtimeDefinition.status());
    return Result<CookedCanonicalImage>::success(
        {{runtimeDefinition.value().begin(), runtimeDefinition.value().end()}, std::move(bulk)});
}

}  // namespace eve::asset
