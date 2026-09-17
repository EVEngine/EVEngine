#include "asset/graphics/EvpackGraphicsLoader.h"
#include <cmath>
#include <limits>
#include <new>
#include "asset/CanonicalMesh.h"
namespace eve::asset_graphics {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "asset.graphics"));
}
}
Result<LoadedGraphicsMesh> EvpackGraphicsLoader::loadMesh(const AssetRef&                  asset,
                                                          const asset::EvpackCapabilities& capabilities,
                                                          const GraphicsAssetLoadLimits&   limits,
                                                          std::uint32_t texcoordSet, bool preserveAllTexcoords) const {
    auto payload = reader_.read(asset, "eve.mesh/3", capabilities, limits.maximumDecodedBytes);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader_.read(asset, "eve.mesh/2", capabilities, limits.maximumDecodedBytes);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader_.read(asset, "eve.mesh/1", capabilities, limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedGraphicsMesh>::failure(payload.status());
    const asset::RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Bulk) continue;
        if (bulk)
            return failure<LoadedGraphicsMesh>(DiagnosticCode::ParseError,
                                               "eve.mesh must contain exactly one bulk chunk");
        bulk = &chunk;
    }
    if (!bulk) return failure<LoadedGraphicsMesh>(DiagnosticCode::NotFound, "eve.mesh bulk chunk is missing");
    auto staging = asset::decodeCanonicalMesh(
        bulk->bytes, {limits.maximumVertices, limits.maximumIndices, limits.maximumDecodedBytes});
    if (!staging) return Result<LoadedGraphicsMesh>::failure(staging.status());
    if (staging.value().positions.size() / 3 > size_t(std::numeric_limits<int>::max()) ||
        staging.value().indices.size() > size_t(std::numeric_limits<int>::max()))
        return failure<LoadedGraphicsMesh>(DiagnosticCode::InvalidArgument, "mesh exceeds backend count range");
    const auto uv = staging.value().texcoords.find(texcoordSet);
    if (uv == staging.value().texcoords.end() && (texcoordSet != 0 || !staging.value().texcoords.empty()))
        return failure<LoadedGraphicsMesh>(DiagnosticCode::NotFound,
                                           "requested UV set is absent; no remapping performed");
    auto uploaded = staging.value().colors.empty()
        ? factory_.uploadMesh(staging.value().positions.data(),
                              staging.value().normals.empty() ? nullptr : staging.value().normals.data(),
                              uv == staging.value().texcoords.end() ? nullptr : uv->second.data(),
                              static_cast<int>(staging.value().positions.size() / 3), staging.value().indices.data(),
                              static_cast<int>(staging.value().indices.size()))
        : factory_.uploadMeshColored(staging.value().positions.data(),
                                     staging.value().normals.empty() ? nullptr : staging.value().normals.data(),
                                     uv == staging.value().texcoords.end() ? nullptr : uv->second.data(),
                                     staging.value().colors.data(),
                                     static_cast<int>(staging.value().positions.size() / 3), staging.value().indices.data(),
                                     static_cast<int>(staging.value().indices.size()));
    if (!uploaded) return Result<LoadedGraphicsMesh>::failure(uploaded.status());
    if (const auto authored = staging.value().attributes.find("TANGENT");
        authored != staging.value().attributes.end()) {
        const auto  count  = staging.value().positions.size() / 3;
        const auto& a      = authored->second;
        auto        attach = [&]() -> Result<void> {
            try {
                if (a.components != 4 || a.values.size() != count * 4 || staging.value().normals.size() != count * 3)
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid canonical tangent frame"));
                std::vector<float> tangents(count * 3), bitangents(count * 3);
                for (size_t i = 0; i < count; ++i) {
                    const auto& normals = staging.value().normals;
                    const float w       = a.values[i * 4 + 3];
                    if (w != 1.f && w != -1.f)
                        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                              "tangent handedness must be signed unit"));
                    for (unsigned c = 0; c < 3; ++c) {
                        tangents[i * 3 + c] = a.values[i * 4 + c];
                        const unsigned j = (c + 1) % 3, k = (c + 2) % 3;
                        bitangents[i * 3 + c] =
                            (normals[i * 3 + j] * a.values[i * 4 + k] - normals[i * 3 + k] * a.values[i * 4 + j]) * w;
                        if (!std::isfinite(bitangents[i * 3 + c]))
                            return Result<void>::failure(
                                Diagnostic::error(DiagnosticCode::InvalidArgument, "nonfinite canonical bitangent"));
                    }
                }
                return factory_.setMeshTangentFrame(uploaded.value(), tangents, bitangents);
            } catch (const std::bad_alloc&) {
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "canonical tangent allocation failed"));
            }
        };
        auto attached = attach();
        if (!attached) {
            auto released = factory_.releaseMesh(uploaded.value());
            if (!released) return Result<LoadedGraphicsMesh>::failure(released.status());
            return Result<LoadedGraphicsMesh>::failure(attached.status());
        }
    }
    if (preserveAllTexcoords)
        for (const auto& [set, values] : staging.value().texcoords) {
            if (set == texcoordSet) continue;
            auto attached = factory_.setMeshTexcoords(uploaded.value(), set, values);
            if (!attached) {
                auto released = factory_.releaseMesh(uploaded.value());
                if (!released) return Result<LoadedGraphicsMesh>::failure(released.status());
                return Result<LoadedGraphicsMesh>::failure(attached.status());
            }
        }
    std::vector<uint32_t> available;
    for (const auto& [set, values] : staging.value().texcoords) available.push_back(set);
    return Result<LoadedGraphicsMesh>::success({asset, uploaded.value(), std::move(payload).takeValue().variant,
                                                texcoordSet, std::move(available),
                                                std::move(staging.value().attributes)});
}

}  // namespace eve::asset_graphics
