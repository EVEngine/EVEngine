#include "fluids/OceanRendererInternal.h"

#include "fluids/shaders/OceanWgsl.h"
#include "fluids/shaders/ocean_frag_spv.inc"
#include "fluids/shaders/ocean_gpu_vert_spv.inc"
#include "fluids/shaders/ocean_vert_spv.inc"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace eve::fluids {
OceanRenderer::Impl::~Impl() {
    if (!graphics || lifetime.expired()) return;
    if (mesh && graphics->releaseMesh(mesh)) delete mesh;
    if (shader && graphics->releaseShader(shader)) delete shader;
    for (auto* texture : atlas)
        if (texture && graphics->releaseTexture(texture)) delete texture;
    for (auto* texture : hullMask)
        if (texture && graphics->releaseTexture(texture)) delete texture;
    for (auto* texture : wake)
        if (texture && graphics->releaseTexture(texture)) delete texture;
}
namespace {
Diagnostic failure(DiagnosticCode code, const char* message) {
    return Diagnostic::error(code, message, "fluids.ocean.render");
}
}  // namespace
OceanRenderer::OceanRenderer() : impl_(std::make_unique<Impl>()) {}
OceanRenderer::~OceanRenderer() = default;
OceanMeshFrameBuilder OceanRenderer::frameBuilder(const OceanWaterView& water) const {
    return OceanMeshFrameBuilder(impl_->topology, water);
}
Result<std::unique_ptr<OceanRenderer>> OceanRendererBuilder::build() const {
    const auto lifetime = graphics_.resourceLifetime();
    if (lifetime.expired())
        return Result<std::unique_ptr<OceanRenderer>>::failure(
            failure(DiagnosticCode::StaleHandle, "Graphics provider has retired"));
    if (graphics_.getBackendName() != "vulkan" && graphics_.getBackendName() != "webgpu")
        return Result<std::unique_ptr<OceanRenderer>>::failure(
            failure(DiagnosticCode::Unsupported, "Ocean renderer requires Vulkan or WebGPU"));
    if (backend != OceanRenderBackend::CpuReference && backend != OceanRenderBackend::GpuResident)
        return Result<std::unique_ptr<OceanRenderer>>::failure(
            failure(DiagnosticCode::InvalidArgument, "Invalid ocean render backend"));
    if (backend == OceanRenderBackend::GpuResident && graphics_.getBackendName() != "vulkan")
        return Result<std::unique_ptr<OceanRenderer>>::failure(
            failure(DiagnosticCode::Unsupported, "Resident ocean rendering requires Vulkan"));
    OceanMeshBuilder builder;
    builder.settings = mesh;
    auto topology    = builder.build();
    if (!topology) return Result<std::unique_ptr<OceanRenderer>>::failure(topology.status());
    std::unique_ptr<OceanRenderer> renderer(new OceanRenderer);
    auto&                          state = *renderer->impl_;
    state.graphics                       = &graphics_;
    state.lifetime                       = lifetime;
    state.topology                       = std::move(topology.value());
    state.backend                        = backend;
    try {
        if (graphics_.getBackendName() == "webgpu")
            state.shader = graphics_.newMeshShaderFromWgsl(shaders::oceanVertex, shaders::oceanFragment);
        else
            state.shader = graphics_.newMeshShaderFromSpv(
                backend == OceanRenderBackend::GpuResident
                    ? std::vector<uint32_t>(ocean_gpu_vert_spv, ocean_gpu_vert_spv + ocean_gpu_vert_spv_count)
                    : std::vector<uint32_t>(ocean_vert_spv, ocean_vert_spv + ocean_vert_spv_count),
                std::vector<uint32_t>(ocean_frag_spv, ocean_frag_spv + ocean_frag_spv_count));
        if (!state.shader)
            return Result<std::unique_ptr<OceanRenderer>>::failure(
                failure(DiagnosticCode::Unsupported, "Ocean mesh shader unavailable"));
        auto configured = graphics_.configureMeshShaderSurface(*state.shader, graphics::BlendMode::Opaque, true, true);
        if (!configured) return Result<std::unique_ptr<OceanRenderer>>::failure(configured.status());
        if (backend == OceanRenderBackend::GpuResident) {
            state.shader->declareMatrix("oceanBands");
            state.shader->declareMatrix("oceanFrame");
        }
        const auto         count = state.topology.parameterOffsets.size();
        std::vector<float> positions(count * 3, 0.0F), normals(count * 3, 0.0F), uv(count * 2, 0.0F);
        for (std::size_t i = 0; i < count; ++i) {
            positions[i * 3]     = static_cast<float>(state.topology.parameterOffsets[i].x);
            positions[i * 3 + 2] = static_cast<float>(state.topology.parameterOffsets[i].y);
            normals[i * 3 + 1]   = 1.0F;
        }
        state.mesh =
            graphics_.newMeshFromArrays(positions.data(), normals.data(), uv.data(), static_cast<int>(count),
                                        state.topology.indices.data(), static_cast<int>(state.topology.indices.size()));
        if (!state.mesh)
            return Result<std::unique_ptr<OceanRenderer>>::failure(
                failure(DiagnosticCode::Unsupported, "Ocean mesh allocation unavailable"));
    } catch (const std::exception& error) {
        return Result<std::unique_ptr<OceanRenderer>>::failure(failure(DiagnosticCode::Unsupported, error.what()));
    }
    return Result<std::unique_ptr<OceanRenderer>>::success(std::move(renderer));
}
Result<void> OceanRenderer::publish(const OceanMeshFrame& frame) {
    auto& state = *impl_;
    if (state.backend != OceanRenderBackend::CpuReference)
        return Result<void>::failure(failure(DiagnosticCode::Conflict, "Renderer expects a resident GPU projection"));
    if (state.lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics retired"));
    const auto count  = state.topology.parameterOffsets.size();
    auto       finite = [](float value) { return std::isfinite(value); };
    if (frame.positions.size() != count * 3 || frame.normals.size() != count * 3 || frame.surface.size() != count * 2 ||
        !std::isfinite(frame.time) || !std::isfinite(frame.origin.x) || !std::isfinite(frame.origin.y) ||
        !std::isfinite(frame.origin.z) || !std::all_of(frame.positions.begin(), frame.positions.end(), finite) ||
        !std::all_of(frame.normals.begin(), frame.normals.end(), finite) ||
        !std::all_of(frame.surface.begin(), frame.surface.end(), finite))
        return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid ocean vertex frame"));
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 normal(frame.normals[i * 3], frame.normals[i * 3 + 1], frame.normals[i * 3 + 2]);
        const float     length = glm::length(normal);
        if (length < 0.99F || length > 1.01F || frame.surface[i * 2] < 0.0F || frame.surface[i * 2] > 1.0F)
            return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Ocean normals or foam invalid"));
    }
    if (!state.graphics->updateMeshVertices(state.mesh, frame.positions.data(), frame.normals.data(),
                                            frame.surface.data(), static_cast<int>(count), nullptr, 0))
        return Result<void>::failure(failure(DiagnosticCode::Unsupported, "Ocean mesh upload rejected"));
    state.origin    = frame.origin;
    state.published = true;
    return Result<void>::success();
}
Result<void> OceanRenderer::draw() {
    auto& state = *impl_;
    if (state.lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics retired"));
    if (!state.published) return Result<void>::failure(failure(DiagnosticCode::Conflict, "Ocean frame not published"));
    if (state.hullPublished && state.origin != state.hullOrigin)
        return Result<void>::failure(failure(DiagnosticCode::Conflict, "Hull mask and ocean render origins differ"));
    state.graphics->setMesh3DNormalTexture(state.hullPublished ? state.hullMask[state.hullFront] : nullptr);
    auto* atlas = state.backend == OceanRenderBackend::GpuResident ? state.atlas[state.front] : nullptr;
    if (state.backend == OceanRenderBackend::GpuResident)
        state.graphics->setMesh3DHeightTexture(state.wake[state.front]);
    state.graphics->drawMeshShader(state.mesh, glm::mat4(1.0F), atlas, glm::vec4(1.0F), state.shader);
    if (state.backend == OceanRenderBackend::GpuResident) state.graphics->setMesh3DHeightTexture(nullptr);
    state.graphics->setMesh3DNormalTexture(nullptr);
    return Result<void>::success();
}
}  // namespace eve::fluids
