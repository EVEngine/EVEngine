#include "asset/graphics/VegetationAsset.h"
#include <cmath>
#include <glm/geometric.hpp>
#include <new>
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/VegetationMotion.h"
#include "graphics/VegetationPacking.h"
#include "graphics/VegetationRender.h"

namespace eve::asset_graphics {
struct VegetationAsset::Impl {
    std::vector<graphics::VegetationVertex> vertices;
    std::vector<float>                      uv;
    std::vector<uint32_t>                   indices;
};
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, {}, {}, "asset.graphics.vegetation"));
}
}  // namespace
VegetationAsset::VegetationAsset(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
VegetationAsset::~VegetationAsset() = default;
uint32_t VegetationAsset::vertexCount() const noexcept { return uint32_t(impl_->vertices.size()); }
Result<std::unique_ptr<VegetationAsset>> VegetationAsset::fromCanonical(const asset::CanonicalMeshData& mesh) {
    using Output     = std::unique_ptr<VegetationAsset>;
    const auto count = mesh.positions.size() / 3;
    if (!count || count > 4'000'000 || mesh.positions.size() % 3 || mesh.normals.size() != count * 3 ||
        mesh.indices.empty() || mesh.indices.size() > 12'000'000 || mesh.indices.size() % 3)
        return failure<Output>(DiagnosticCode::InvalidArgument, "invalid vegetation mesh geometry");
    auto stream = [&](const char* name) -> const asset::CanonicalMeshAttribute* {
        auto found = mesh.attributes.find(name);
        return found != mesh.attributes.end() && found->second.components == 4 &&
                       found->second.values.size() == count * 4
                   ? &found->second
                   : nullptr;
    };
    const auto* color   = stream("COLOR_0");
    const auto* uv0     = stream("_UNITY_UV0");
    const auto* uv1     = stream("_UNITY_UV1");
    const auto* uv3     = stream("_UNITY_UV3");
    const auto* tangent = stream("TANGENT");
    if (mesh.attributes.contains("TANGENT") && !tangent)
        return failure<Output>(DiagnosticCode::InvalidArgument, "malformed vegetation tangent stream");
    if (!color || !uv0 || !uv1 || !uv3)
        return failure<Output>(DiagnosticCode::Unsupported, "TVE requires float4 color and complete Unity UV0/UV1/UV4");
    const auto renderUv = mesh.texcoords.find(0);
    if (renderUv == mesh.texcoords.end() || renderUv->second.size() != count * 2)
        return failure<Output>(DiagnosticCode::InvalidArgument, "vegetation render UV0 is absent or malformed");
    for (auto f : renderUv->second)
        if (!std::isfinite(f))
            return failure<Output>(DiagnosticCode::InvalidArgument, "nonfinite vegetation render UV");
    for (auto index : mesh.indices)
        if (index >= count) return failure<Output>(DiagnosticCode::InvalidArgument, "vegetation index out of range");
    try {
        std::vector<graphics::TvePackedVertex> packed(count);
        auto four = [](const auto& a, size_t at) { return glm::vec4(a[at], a[at + 1], a[at + 2], a[at + 3]); };
        for (size_t v = 0; v < count; ++v) {
            auto& p           = packed[v];
            p.position        = {mesh.positions[v * 3], mesh.positions[v * 3 + 1], mesh.positions[v * 3 + 2]};
            p.normal          = {mesh.normals[v * 3], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2]};
            const auto length = glm::length(p.normal);
            if (!std::isfinite(length) || length < 1e-6f)
                return failure<Output>(DiagnosticCode::InvalidArgument, "invalid vegetation normal");
            p.color     = four(color->values, v * 4);
            p.texcoord0 = four(uv0->values, v * 4);
            p.texcoord1 = four(uv1->values, v * 4);
            p.texcoord3 = four(uv3->values, v * 4);
        }
        auto decoded = graphics::decodeTveVegetationVertices(packed);
        if (!decoded) return Result<Output>::failure(decoded.status());
        auto impl      = std::make_unique<Impl>();
        impl->vertices = std::move(decoded).takeValue();
        for (size_t v = 0; v < count; ++v) {
            auto& vertex               = impl->vertices[v];
            vertex.pivot.z             = -vertex.pivot.z;
            vertex.texcoord            = {renderUv->second[v * 2], renderUv->second[v * 2 + 1]};
            vertex.secondaryTexcoord.y = 1.f - vertex.secondaryTexcoord.y;
            if (tangent) {
                vertex.tangent = four(tangent->values, v * 4);
                const auto t   = glm::vec3(*vertex.tangent);
                if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z) ||
                    (vertex.tangent->w != 1.f && vertex.tangent->w != -1.f) ||
                    glm::length(glm::cross(vertex.normal, t)) < 1e-6f)
                    return failure<Output>(DiagnosticCode::InvalidArgument, "invalid vegetation tangent frame");
            }
        }
        impl->uv      = renderUv->second;
        impl->indices = mesh.indices;
        return Result<Output>::success(Output(new VegetationAsset(std::move(impl))));
    } catch (const std::bad_alloc&) {
        return failure<Output>(DiagnosticCode::Failed, "vegetation asset allocation failed");
    }
}
Result<std::unique_ptr<VegetationAsset>> VegetationAsset::load(const asset::EvpackResourceReader& reader,
                                                               const AssetRef&                    ref,
                                                               const asset::EvpackCapabilities&   capabilities,
                                                               const asset::CanonicalMeshLimits&  limits) {
    using Output = std::unique_ptr<VegetationAsset>;
    auto payload = reader.read(ref, "eve.mesh/3", capabilities, limits.maximumDecodedBytes);
    if (!payload) return Result<Output>::failure(payload.status());
    const asset::RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Bulk) continue;
        if (bulk) return failure<Output>(DiagnosticCode::ParseError, "vegetation mesh has multiple bulk chunks");
        bulk = &chunk;
    }
    if (!bulk) return failure<Output>(DiagnosticCode::NotFound, "vegetation mesh bulk is missing");
    auto decoded = asset::decodeCanonicalMesh(bulk->bytes, limits);
    if (!decoded) return Result<Output>::failure(decoded.status());
    return fromCanonical(decoded.value());
}
Result<graphics::VegetationGeometry> VegetationAsset::evaluate(const graphics::VegetationField&  field,
                                                               const graphics::VegetationMotion& motion) const {
    return graphics::deformVegetation(field, impl_->vertices, motion);
}
Result<graphics::Mesh*> VegetationAsset::createMesh(graphics::Graphics&               graphics,
                                                    const graphics::VegetationField&  field,
                                                    const graphics::VegetationMotion& motion) const {
    auto geometry = evaluate(field, motion);
    if (!geometry) return Result<graphics::Mesh*>::failure(geometry.status());
    auto* mesh =
        graphics.newMeshFromArrays(geometry.value().positions.data(), geometry.value().normals.data(), impl_->uv.data(),
                                   int(impl_->vertices.size()), impl_->indices.data(), int(impl_->indices.size()));
    if (!mesh) return failure<graphics::Mesh*>(DiagnosticCode::Failed, "vegetation backend mesh creation failed");
    auto attached =
        mesh->adoptTangentFrame(std::move(geometry.value().tangents), std::move(geometry.value().bitangents));
    if (!attached) {
        if (!graphics.releaseMesh(mesh))
            return failure<graphics::Mesh*>(DiagnosticCode::Failed, "vegetation mesh rollback failed");
        return Result<graphics::Mesh*>::failure(attached.status());
    }
    auto highlights = mesh->adoptMotionHighlights(std::move(geometry.value().motionHighlights));
    if (!highlights) {
        if (!graphics.releaseMesh(mesh))
            return failure<graphics::Mesh*>(DiagnosticCode::Failed, "vegetation highlight rollback failed");
        return Result<graphics::Mesh*>::failure(highlights.status());
    }
    auto factors = mesh->adoptVegetationFactors(std::move(geometry.value().vegetationFactors));
    if (!factors) {
        if (!graphics.releaseMesh(mesh))
            return failure<graphics::Mesh*>(DiagnosticCode::Failed, "vegetation factor rollback failed");
        return Result<graphics::Mesh*>::failure(factors.status());
    }
    return Result<graphics::Mesh*>::success(mesh);
}
Result<graphics::Mesh*> VegetationAsset::createGpuFieldMesh(graphics::Graphics& graphics) const {
    try {
        std::vector<float> positions, normals, tangents, bitangents, shading, deformation;
        positions.reserve(impl_->vertices.size() * 3);
        normals.reserve(impl_->vertices.size() * 3);
        shading.reserve(impl_->vertices.size() * 5);
        deformation.reserve(impl_->vertices.size() * 9);
        const bool authored = !impl_->vertices.empty() && impl_->vertices.front().tangent.has_value();
        if (authored) {
            tangents.reserve(impl_->vertices.size() * 3);
            bitangents.reserve(impl_->vertices.size() * 3);
        }
        for (const auto& vertex : impl_->vertices) {
            positions.insert(positions.end(), {vertex.position.x, vertex.position.y, vertex.position.z});
            normals.insert(normals.end(), {vertex.normal.x, vertex.normal.y, vertex.normal.z});
            shading.insert(shading.end(), {vertex.variation, vertex.occlusion, vertex.detail, vertex.detailCoord.x,
                                           vertex.detailCoord.y});
            deformation.insert(deformation.end(),
                               {vertex.pivot.x, vertex.pivot.y, vertex.pivot.z, vertex.bending, vertex.branch,
                                vertex.flutter, vertex.variation, vertex.boundsHeight, vertex.boundsRadius});
            if (authored) {
                const auto normal  = glm::normalize(vertex.normal);
                const auto tangent = glm::normalize(glm::vec3(*vertex.tangent) -
                                                    normal * glm::dot(normal, glm::vec3(*vertex.tangent)));
                const auto bitangent = glm::cross(normal, tangent) * vertex.tangent->w;
                tangents.insert(tangents.end(), {tangent.x, tangent.y, tangent.z});
                bitangents.insert(bitangents.end(), {bitangent.x, bitangent.y, bitangent.z});
            }
        }
        auto* mesh = graphics.newMeshFromArrays(positions.data(), normals.data(), impl_->uv.data(),
                                                int(impl_->vertices.size()), impl_->indices.data(),
                                                int(impl_->indices.size()));
        if (!mesh) return failure<graphics::Mesh*>(DiagnosticCode::Failed, "vegetation rest mesh upload failed");
        auto rollback = [&](Result<void> status) -> Result<graphics::Mesh*> {
            if (!graphics.releaseMesh(mesh))
                return failure<graphics::Mesh*>(DiagnosticCode::Failed, "vegetation rest mesh rollback failed");
            return Result<graphics::Mesh*>::failure(status.status());
        };
        auto attached = mesh->adoptTangentFrame(std::move(tangents), std::move(bitangents));
        if (!attached) return rollback(std::move(attached));
        auto shadingAttached = mesh->adoptVegetationFactors(std::move(shading));
        if (!shadingAttached) return rollback(std::move(shadingAttached));
        auto deformationAttached = mesh->adoptVegetationDeformationFactors(std::move(deformation));
        if (!deformationAttached) return rollback(std::move(deformationAttached));
        return Result<graphics::Mesh*>::success(mesh);
    } catch (const std::bad_alloc&) {
        return failure<graphics::Mesh*>(DiagnosticCode::Failed, "vegetation rest mesh allocation failed");
    }
}
Result<void> VegetationAsset::updateMesh(graphics::Graphics& graphics, graphics::Mesh& mesh,
                                         const graphics::VegetationField&  field,
                                         const graphics::VegetationMotion& motion) const {
    return graphics::updateVegetationMesh(graphics, mesh, field, impl_->vertices, motion, impl_->uv, impl_->indices);
}
}  // namespace eve::asset_graphics
