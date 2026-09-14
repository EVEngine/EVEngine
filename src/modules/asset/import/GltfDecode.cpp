#include "asset/import/GltfDecode.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include "asset/import/ImportCommon.h"

namespace eve::asset_import::gltf {
const Value* member(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::uint64_t> unsignedValue(const Value* value, std::string path, bool required, std::uint64_t fallback) {
    if (!value && !required) return Result<std::uint64_t>::success(fallback);
    if (!value || !value->isInt64() || value->asInt() < 0)
        return detail::failure<std::uint64_t>(DiagnosticCode::ParseError, "glTF field must be a non-negative integer",
                                              std::move(path));
    return Result<std::uint64_t>::success(static_cast<std::uint64_t>(value->asInt()));
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void putFloat(std::vector<std::uint8_t>& bytes, float value) { put32(bytes, std::bit_cast<std::uint32_t>(value)); }


Result<Document> parseDocument(const GltfImportRequest& request) {
    if (request.documentBytes.empty() || request.documentBytes.size() > request.limits.maximumSourceBytes)
        return detail::failure<Document>(DiagnosticCode::InvalidArgument, "glTF document size is outside limits");
    std::string               json;
    std::vector<std::uint8_t> binary;
    const auto                bytes = std::span<const std::uint8_t>(request.documentBytes);
    if (bytes.size() >= 12 && little32(bytes, 0) == 0x46546c67) {
        if (little32(bytes, 4) != 2 || little32(bytes, 8) != bytes.size() || bytes.size() < 20)
            return detail::failure<Document>(DiagnosticCode::ParseError, "GLB header is invalid");
        std::size_t         cursor   = 12;
        const std::uint32_t jsonSize = little32(bytes, cursor);
        const std::uint32_t jsonType = little32(bytes, cursor + 4);
        cursor += 8;
        if (jsonType != 0x4e4f534a || jsonSize > bytes.size() - cursor)
            return detail::failure<Document>(DiagnosticCode::ParseError, "GLB JSON chunk is invalid");
        json.assign(reinterpret_cast<const char*>(bytes.data() + cursor), jsonSize);
        while (!json.empty() && (json.back() == ' ' || json.back() == '\0')) json.pop_back();
        cursor += jsonSize;
        if (cursor < bytes.size()) {
            if (bytes.size() - cursor < 8)
                return detail::failure<Document>(DiagnosticCode::ParseError, "GLB trailing chunk is truncated");
            const std::uint32_t binarySize = little32(bytes, cursor);
            const std::uint32_t binaryType = little32(bytes, cursor + 4);
            cursor += 8;
            if (binaryType != 0x004e4942 || binarySize > bytes.size() - cursor || cursor + binarySize != bytes.size())
                return detail::failure<Document>(DiagnosticCode::ParseError, "GLB BIN chunk is invalid");
            binary.assign(bytes.begin() + cursor, bytes.end());
        }
    } else {
        json.assign(request.documentBytes.begin(), request.documentBytes.end());
    }
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<Document>::failure(parsed.status());
    return Result<Document>::success({std::move(parsed).takeValue(), std::move(binary)});
}

bool safeUri(std::string_view uri, const AssetImportLimits& limits) {
    if (uri.empty() || uri.size() > limits.maximumStringBytes || uri.front() == '/' ||
        uri.find('\\') != std::string_view::npos || uri.starts_with("data:") || uri.find(':') != std::string_view::npos)
        return false;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= uri.size(); ++index) {
        if (index != uri.size() && uri[index] != '/') continue;
        const auto segment = uri.substr(start, index - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        start = index + 1;
    }
    return true;
}

Result<std::vector<std::span<const std::uint8_t>>> resolveBuffers(const Value::Object& root, const Document& document,
                                                                  const GltfImportRequest& request) {
    const Value* buffersValue = member(root, "buffers");
    const auto*  buffers      = buffersValue ? buffersValue->getIf<Value::Array>() : nullptr;
    if (!buffers || buffers->empty())
        return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::ParseError,
                                                                           "glTF buffers are required", "$.buffers");
    std::vector<std::span<const std::uint8_t>> result;
    result.reserve(buffers->size());
    std::uint64_t totalBytes = 0;
    for (std::size_t index = 0; index < buffers->size(); ++index) {
        const auto* object = (*buffers)[index].getIf<Value::Object>();
        if (!object)
            return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::ParseError,
                                                                               "glTF buffer must be an object");
        auto declared =
            unsignedValue(member(*object, "byteLength"), "$.buffers[" + std::to_string(index) + "].byteLength");
        if (!declared) return Result<std::vector<std::span<const std::uint8_t>>>::failure(declared.status());
        std::span<const std::uint8_t> data;
        const Value*                  uriValue = member(*object, "uri");
        if (uriValue) {
            if (!uriValue->isString() || !safeUri(uriValue->asString(), request.limits))
                return detail::failure<std::vector<std::span<const std::uint8_t>>>(
                    DiagnosticCode::Unsupported, "external glTF buffer URI is unsafe or unsupported");
            const auto found = request.externalResources.find(uriValue->asString());
            if (found == request.externalResources.end())
                return detail::failure<std::vector<std::span<const std::uint8_t>>>(
                    DiagnosticCode::NotFound, "external glTF buffer was not supplied", uriValue->asString());
            data = found->second;
        } else {
            if (index != 0 || document.binaryChunk.empty())
                return detail::failure<std::vector<std::span<const std::uint8_t>>>(
                    DiagnosticCode::ParseError, "buffer without URI requires the first GLB BIN chunk");
            data = document.binaryChunk;
        }
        if (declared.value() > data.size())
            return detail::failure<std::vector<std::span<const std::uint8_t>>>(
                DiagnosticCode::ParseError, "glTF buffer is shorter than byteLength");
        if (declared.value() > request.limits.maximumSourceBytes ||
            totalBytes > request.limits.maximumSourceBytes - declared.value())
            return detail::failure<std::vector<std::span<const std::uint8_t>>>(DiagnosticCode::InvalidArgument,
                                                                               "glTF buffer budget is exceeded");
        totalBytes += declared.value();
        result.push_back(data.first(static_cast<std::size_t>(declared.value())));
    }
    return Result<std::vector<std::span<const std::uint8_t>>>::success(std::move(result));
}


std::uint32_t componentSize(std::uint32_t type) {
    if (type == 5120 || type == 5121) return 1;
    if (type == 5122 || type == 5123) return 2;
    if (type == 5125 || type == 5126) return 4;
    return 0;
}

Result<Accessor> accessorAt(const Value::Object& root, const std::vector<std::span<const std::uint8_t>>& buffers,
                            std::uint64_t accessorIndex, const AssetImportLimits& limits) {
    const auto* accessors = member(root, "accessors") ? member(root, "accessors")->getIf<Value::Array>() : nullptr;
    const auto* views     = member(root, "bufferViews") ? member(root, "bufferViews")->getIf<Value::Array>() : nullptr;
    if (!accessors || !views || accessorIndex >= accessors->size())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor index is invalid");
    const auto* object = (*accessors)[accessorIndex].getIf<Value::Object>();
    if (!object || member(*object, "sparse"))
        return detail::failure<Accessor>(DiagnosticCode::Unsupported,
                                         "sparse or malformed glTF accessor is unsupported");
    auto viewIndex      = unsignedValue(member(*object, "bufferView"), "accessor.bufferView");
    auto count          = unsignedValue(member(*object, "count"), "accessor.count");
    auto component      = unsignedValue(member(*object, "componentType"), "accessor.componentType");
    auto accessorOffset = unsignedValue(member(*object, "byteOffset"), "accessor.byteOffset", false, 0);
    if (!viewIndex || !count || !component || !accessorOffset)
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor fields are invalid");
    if (viewIndex.value() >= views->size() || count.value() == 0 ||
        count.value() > std::max(limits.maximumVerticesPerPrimitive, limits.maximumIndicesPerPrimitive))
        return detail::failure<Accessor>(DiagnosticCode::InvalidArgument, "glTF accessor exceeds limits");
    const Value* typeValue = member(*object, "type");
    if (!typeValue || !typeValue->isString())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor type is missing");
    std::uint32_t components = 0;
    if (typeValue->asString() == "SCALAR")
        components = 1;
    else if (typeValue->asString() == "VEC2")
        components = 2;
    else if (typeValue->asString() == "VEC3")
        components = 3;
    else if (typeValue->asString() == "VEC4")
        components = 4;
    else if (typeValue->asString() == "MAT4")
        components = 16;
    else
        return detail::failure<Accessor>(DiagnosticCode::Unsupported, "glTF accessor shape is unsupported");
    const auto* view = (*views)[viewIndex.value()].getIf<Value::Object>();
    if (!view) return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF bufferView is malformed");
    auto bufferIndex = unsignedValue(member(*view, "buffer"), "bufferView.buffer");
    auto viewOffset  = unsignedValue(member(*view, "byteOffset"), "bufferView.byteOffset", false, 0);
    auto viewLength  = unsignedValue(member(*view, "byteLength"), "bufferView.byteLength");
    auto byteStride  = unsignedValue(member(*view, "byteStride"), "bufferView.byteStride", false, 0);
    if (!bufferIndex || !viewOffset || !viewLength || !byteStride || bufferIndex.value() >= buffers.size())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF bufferView fields are invalid");
    const std::uint32_t elementSize = componentSize(static_cast<std::uint32_t>(component.value())) * components;
    const std::uint64_t stride      = byteStride.value() == 0 ? elementSize : byteStride.value();
    if (elementSize == 0 || stride < elementSize || stride > 252 ||
        viewOffset.value() > buffers[bufferIndex.value()].size() ||
        viewLength.value() > buffers[bufferIndex.value()].size() - viewOffset.value() ||
        accessorOffset.value() > viewLength.value())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor layout is invalid");
    const std::uint64_t required = (count.value() - 1) * stride + elementSize;
    if (required > viewLength.value() - accessorOffset.value())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF accessor range is truncated");
    const auto* normalized = member(*object, "normalized");
    if (normalized && !normalized->isBool())
        return detail::failure<Accessor>(DiagnosticCode::ParseError, "glTF normalized flag must be boolean");
    return Result<Accessor>::success(
        {buffers[bufferIndex.value()], static_cast<std::size_t>(viewOffset.value() + accessorOffset.value()),
         static_cast<std::size_t>(stride), static_cast<std::uint32_t>(count.value()),
         static_cast<std::uint32_t>(component.value()), components, normalized && normalized->asBool()});
}

Result<float> readFloat(const Accessor& accessor, std::uint32_t element, std::uint32_t component) {
    if (accessor.componentType != 5126 || accessor.normalized || element >= accessor.count ||
        component >= accessor.components)
        return detail::failure<float>(DiagnosticCode::Unsupported, "mesh attribute must use FLOAT components");
    const std::size_t offset = accessor.offset + std::size_t(element) * accessor.stride + component * 4;
    const float       value  = std::bit_cast<float>(little32(accessor.buffer, offset));
    if (!std::isfinite(value))
        return detail::failure<float>(DiagnosticCode::ParseError, "mesh attribute contains non-finite values");
    return Result<float>::success(value);
}

Result<std::uint32_t> readIndex(const Accessor& accessor, std::uint32_t element) {
    if (accessor.components != 1)
        return detail::failure<std::uint32_t>(DiagnosticCode::ParseError, "index accessor must be SCALAR");
    const std::size_t offset = accessor.offset + std::size_t(element) * accessor.stride;
    if (accessor.componentType == 5121) return Result<std::uint32_t>::success(accessor.buffer[offset]);
    if (accessor.componentType == 5123)
        return Result<std::uint32_t>::success(std::uint32_t(accessor.buffer[offset]) |
                                              (std::uint32_t(accessor.buffer[offset + 1]) << 8));
    if (accessor.componentType == 5125) return Result<std::uint32_t>::success(little32(accessor.buffer, offset));
    return detail::failure<std::uint32_t>(DiagnosticCode::Unsupported,
                                          "indices must use UNSIGNED_BYTE, UNSIGNED_SHORT or UNSIGNED_INT");
}

}  // namespace eve::asset_import::gltf
