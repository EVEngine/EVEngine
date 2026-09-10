#include "asset/graphics/EvpackGraphicsLoader.h"
#include <limits>
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
    auto payload = reader_.read(asset, "eve.mesh/2", capabilities, limits.maximumDecodedBytes);
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
    auto uploaded = factory_.uploadMesh(
        staging.value().positions.data(), staging.value().normals.empty() ? nullptr : staging.value().normals.data(),
        uv == staging.value().texcoords.end() ? nullptr : uv->second.data(),
        static_cast<int>(staging.value().positions.size() / 3), staging.value().indices.data(),
        static_cast<int>(staging.value().indices.size()));
    if (!uploaded) return Result<LoadedGraphicsMesh>::failure(uploaded.status());
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
    return Result<LoadedGraphicsMesh>::success(
        {asset, uploaded.value(), std::move(payload).takeValue().variant, texcoordSet, std::move(available)});
}

}  // namespace eve::asset_graphics
