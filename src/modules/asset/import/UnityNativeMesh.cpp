#include <bit>
#include <charconv>
#include <cmath>
#include <regex>
#include <stdexcept>
#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

namespace eve::asset_import {
namespace {
std::string capture(const std::string& text, const std::string& pattern) {
    std::smatch m;
    if (!std::regex_search(text, m, std::regex(pattern))) throw std::runtime_error("missing Mesh field");
    return m[1].str();
}
std::uint64_t number(const std::string& text, const std::string& key) {
    const auto    value  = capture(text, "(?:^|\\n) *" + key + ": *([0-9]+)(?:\\r?\\n|$)");
    std::uint64_t out    = 0;
    auto          parsed = std::from_chars(value.data(), value.data() + value.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("Mesh integer overflow");
    return out;
}
std::vector<std::uint8_t> hex(const std::string& text, std::uint64_t limit) {
    if (text.size() % 2 || text.size() / 2 > limit) throw std::runtime_error("Mesh byte budget or hex length invalid");
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned value  = 0;
        auto     parsed = std::from_chars(text.data() + i, text.data() + i + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + i + 2) throw std::runtime_error("invalid Mesh hex");
        out.push_back(std::uint8_t(value));
    }
    return out;
}
std::uint32_t read(const std::vector<std::uint8_t>& data, std::size_t at, unsigned size) {
    if (at > data.size() || size > data.size() - at) throw std::runtime_error("Mesh buffer truncated");
    std::uint32_t out = 0;
    for (unsigned i = 0; i < size; ++i) out |= std::uint32_t(data[at + i]) << (8 * i);
    return out;
}
void put(std::vector<std::uint8_t>& data, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.push_back(std::uint8_t(value >> (8 * i)));
}
}  // namespace
Result<PreparedAssetImport> prepareUnityNativeMesh(const UnityProjectImportRequest& request,
                                                   const UnitySourceAsset&          source) {
    const auto&       bytes = request.files.at(source.path);
    const std::string text(bytes.begin(), bytes.end());
    if (text.find("\nMesh:") == std::string::npos && text.find("\nMesh:\r") == std::string::npos)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported, "native asset is not a text Mesh",
                                                    source.path);
    try {
        const auto id = capture(text, R"(--- !u!43 &(-?[0-9]+))");
        if (number(text, "serializedVersion") != 9 || number(text, "m_MeshCompression") != 0 ||
            text.find("m_BindPose: []") == std::string::npos)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                        "requires uncompressed static Mesh version 9", source.path);
        const auto shapes = text.find("m_Shapes:");
        if (shapes != std::string::npos) {
            const auto end     = text.find("m_BindPose:", shapes);
            const auto section = text.substr(shapes, end - shapes);
            if (section.find("vertices: []") == std::string::npos || section.find("shapes: []") == std::string::npos)
                return detail::failure<PreparedAssetImport>(
                    DiagnosticCode::Unsupported, "Mesh blend shapes require animation conversion", source.path);
        }
        const auto count = number(text, "m_VertexCount");
        if (!count || count > request.limits.maximumVerticesPerPrimitive)
            throw std::runtime_error("Mesh vertex budget exceeded");
        struct Channel {
            std::uint64_t stream, offset, format, dimension;
        };
        std::vector<Channel> channels;
        const std::regex     channel(
            R"(- stream: ([0-9]+)\r?\n      offset: ([0-9]+)\r?\n      format: ([0-9]+)\r?\n      dimension: ([0-9]+))");
        std::uint64_t stride = 0;
        for (std::sregex_iterator it(text.begin(), text.end(), channel), end; it != end; ++it) {
            Channel c{std::stoull((*it)[1]), std::stoull((*it)[2]), std::stoull((*it)[3]), std::stoull((*it)[4])};
            if (c.dimension && (c.stream != 0 || c.format != 0 || c.dimension > 4 || c.offset > 256))
                return detail::failure<PreparedAssetImport>(
                    DiagnosticCode::Unsupported, "Mesh requires interleaved float32 vertex channels", source.path);
            if (c.dimension) stride = std::max(stride, c.offset + c.dimension * 4);
            channels.push_back(c);
        }
        if (channels.size() != 14 || channels[0].dimension != 3 || channels[1].dimension != 3 ||
            channels[4].dimension != 2 || !stride)
            throw std::runtime_error("Mesh position, normal or UV0 channel missing");
        auto vertices = hex(capture(text, R"(_typelessdata: ([0-9a-fA-F]*))"), request.limits.maximumDecodedBytes);
        if (vertices.size() != number(text, "m_DataSize") || vertices.size() != stride * count)
            throw std::runtime_error("Mesh vertex buffer size mismatch");
        auto       indices = hex(capture(text, R"(m_IndexBuffer: ([0-9a-fA-F]*))"), request.limits.maximumDecodedBytes);
        const auto indexFormat = number(text, "m_IndexFormat");
        if (indexFormat > 1) throw std::runtime_error("invalid Mesh index format");
        const unsigned indexSize = indexFormat == 0 ? 2 : 4;
        if (indices.size() % indexSize || indices.size() / indexSize > request.limits.maximumIndicesPerPrimitive)
            throw std::runtime_error("Mesh index budget exceeded");
        if (number(text, "size") != 0)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported, "external Mesh stream unsupported",
                                                        source.path);
        std::vector<std::uint8_t> buffer;
        Value::Array              views, accessors, primitives;
        auto                      view = [&](std::size_t start, std::uint64_t n, const char* type, int component) {
            const auto index = std::int64_t(views.size());
            views.emplace_back(Value::Object{{"buffer", Value(std::int64_t(0))},
                                                                  {"byteOffset", Value(std::int64_t(start))},
                                                                  {"byteLength", Value(std::int64_t(buffer.size() - start))}});
            accessors.emplace_back(Value::Object{{"bufferView", Value(index)},
                                                                      {"componentType", Value(std::int64_t(component))},
                                                                      {"count", Value(std::int64_t(n))},
                                                                      {"type", Value(type)}});
            return index;
        };
        for (unsigned c : {0u, 1u, 4u}) {
            const auto start = buffer.size();
            for (std::uint64_t i = 0; i < count; ++i)
                for (unsigned j = 0; j < channels[c].dimension; ++j) {
                    float value =
                        std::bit_cast<float>(read(vertices, std::size_t(i * stride + channels[c].offset + j * 4), 4));
                    if (!std::isfinite(value)) throw std::runtime_error("nonfinite Mesh vertex");
                    if (c != 4 && j == 2) value = -value;
                    if (c == 4 && j == 1) value = 1.f - value;
                    put(buffer, std::bit_cast<std::uint32_t>(value));
                }
            view(start, count, c == 4 ? "VEC2" : "VEC3", 5126);
        }
        const std::regex sub(
            R"(    firstByte: ([0-9]+)\r?\n    indexCount: ([0-9]+)\r?\n    topology: ([0-9]+)\r?\n    baseVertex: ([0-9]+))");
        std::uint64_t            consumed = 0;
        std::vector<std::size_t> slots;
        std::size_t              slot = 0;
        for (std::sregex_iterator it(text.begin(), text.end(), sub), end; it != end; ++it) {
            const auto first = std::stoull((*it)[1]), n = std::stoull((*it)[2]), topology = std::stoull((*it)[3]),
                       base = std::stoull((*it)[4]);
            if (topology != 0)
                return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported, "non-triangle Mesh topology",
                                                            source.path);
            if (n % 3 || first != consumed || first > indices.size() || n > (indices.size() - first) / indexSize ||
                primitives.size() >= request.limits.maximumAssets)
                throw std::runtime_error("invalid Mesh submesh range");
            const auto currentSlot = slot++;
            if (n == 0) continue;
            slots.push_back(currentSlot);
            const auto start = buffer.size();
            for (std::uint64_t i = 0; i < n; i += 3)
                for (unsigned j : {0u, 2u, 1u}) {
                    const auto value =
                        std::uint64_t(read(indices, std::size_t(first + (i + j) * indexSize), indexSize)) + base;
                    if (value >= count) throw std::runtime_error("Mesh index out of bounds");
                    put(buffer, std::uint32_t(value));
                }
            const auto accessor = view(start, n, "SCALAR", 5125);
            primitives.emplace_back(
                Value::Object{{"attributes", Value(Value::Object{{"POSITION", Value(std::int64_t(0))},
                                                                 {"NORMAL", Value(std::int64_t(1))},
                                                                 {"TEXCOORD_0", Value(std::int64_t(2))}})},
                              {"indices", Value(accessor)}});
            consumed = first + n * indexSize;
        }
        if (primitives.empty() || consumed != indices.size() || buffer.size() > request.limits.maximumDecodedBytes)
            throw std::runtime_error("Mesh submesh coverage or output budget invalid");
        if (count * 32 * slots.size() + indices.size() * 2 > request.limits.maximumDecodedBytes)
            throw std::runtime_error("aggregate canonical submesh byte budget exceeded");
        Value document(Value::Object{
            {"asset", Value(Value::Object{{"version", Value("2.0")}})},
            {"buffers", Value(Value::Array{Value(Value::Object{{"uri", Value("mesh.bin")},
                                                               {"byteLength", Value(std::int64_t(buffer.size()))}})})},
            {"bufferViews", Value(std::move(views))},
            {"accessors", Value(std::move(accessors))},
            {"meshes", Value(Value::Array{Value(Value::Object{{"primitives", Value(std::move(primitives))}})})}});
        auto  json = document.toJson();
        if (!json) return Result<PreparedAssetImport>::failure(json.status());
        auto identity      = request.package;
        identity.packageId = identity.packageId.child("unity:" + source.guid + ":" + id);
        auto result        = prepareGltfImport({identity,
                                                source.path,
                                                {json.value().begin(), json.value().end()},
                                                {{"mesh.bin", std::move(buffer)}},
                                                request.limits});
        if (!result) return result;
        // Canonical identity follows the Unity slot, including empty slots, never the compacted glTF ordinal.
        for (std::size_t i = 0; i < result.value().sourceMappings.size(); ++i) {
            const auto sourceObject = "meshes[0].primitives[" + std::to_string(i) + "]";
            auto mappingIt = std::find_if(result.value().sourceMappings.begin(), result.value().sourceMappings.end(),
                                          [&](const auto& value) { return value.sourceObject == sourceObject; });
            if (mappingIt == result.value().sourceMappings.end()) throw std::runtime_error("missing primitive mapping");
            auto&      mapping = *mappingIt;
            const auto oldRef  = mapping.asset;
            auto       newRef  = detail::assetRef(request.package.packageId.child("unity:" + source.guid + ":" + id +
                                                                                  ":submesh:" + std::to_string(slots[i])));
            if (!newRef) return Result<PreparedAssetImport>::failure(newRef.status());
            const auto oldBase = "assets/" + oldRef.id().format() + "/";
            const auto newBase = "assets/" + newRef.value().id().format() + "/";
            auto&      asset   = result.value().manifest.assets.at(i);
            for (auto& entry : result.value().entries) {
                if (!entry.path.starts_with(oldBase)) continue;
                if (entry.path == asset.definition) {
                    auto definition = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
                    if (!definition) return Result<PreparedAssetImport>::failure(definition.status());
                    definition.value().getIf<Value::Object>()->at("blob") = Value(newBase + "mesh.bin");
                    auto encoded                                          = definition.value().toJson();
                    if (!encoded) return Result<PreparedAssetImport>::failure(encoded.status());
                    entry.bytes.assign(encoded.value().begin(), encoded.value().end());
                    asset.contentHash = detail::sha256(entry.bytes);
                }
                entry.path = newBase + entry.path.substr(oldBase.size());
            }
            asset.asset      = newRef.value();
            asset.definition = newBase + "asset.json";
            for (auto& [name, ref] : result.value().manifest.entrypoints)
                if (ref == oldRef) ref = newRef.value();
            mapping.asset        = newRef.value();
            mapping.sourceObject = id + "/submesh/" + std::to_string(slots[i]);
        }
        std::erase_if(result.value().entries, [](const auto& entry) { return entry.path == "reports/import.json"; });
        result.value().findings.push_back(
            {source.path, "Mesh.extraChannels", ImportDisposition::Unsupported,
             "tangent, vertex color and secondary UV source channels are preserved but not applied"});
        auto report = detail::finalizeImportReport(result.value(), identity, "unity", "mesh-v9");
        if (!report) return Result<PreparedAssetImport>::failure(report.status());
        return result;
    } catch (const std::exception& e) {
        return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, e.what(), source.path);
    }
}
}  // namespace eve::asset_import
