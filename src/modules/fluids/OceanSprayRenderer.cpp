#include "fluids/OceanSprayRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include "fluids/shaders/OceanSprayWgsl.h"
#include "fluids/shaders/ocean_spray_frag_spv.inc"
#include "fluids/shaders/ocean_spray_vert_spv.inc"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"

namespace eve::fluids {
struct OceanSprayRenderer::Impl {
    graphics::Graphics*            graphics = nullptr;
    std::weak_ptr<const void>      lifetime;
    std::array<graphics::Mesh*, 2> mesh{};
    graphics::Shader*              shader    = nullptr;
    unsigned                       front     = 0;
    bool                           published = false, empty = true;
    ~Impl() {
        if (!graphics || lifetime.expired()) return;
        for (auto* m : mesh)
            if (m && graphics->releaseMesh(m)) delete m;
        if (shader && graphics->releaseShader(shader)) delete shader;
    }
};
namespace {
Diagnostic failure(DiagnosticCode code, const char* text) {
    return Diagnostic::error(code, text, "fluids.ocean.spray.render");
}
}  // namespace
OceanSprayRenderer::OceanSprayRenderer() : impl_(std::make_unique<Impl>()) {}
OceanSprayRenderer::~OceanSprayRenderer() = default;
Result<std::unique_ptr<OceanSprayRenderer>> OceanSprayRendererBuilder::build() const {
    auto lifetime = graphics_.resourceLifetime();
    if (lifetime.expired())
        return Result<std::unique_ptr<OceanSprayRenderer>>::failure(
            failure(DiagnosticCode::StaleHandle, "Graphics retired"));
    const auto backend = graphics_.getBackendName();
    if (backend != "vulkan" && backend != "webgpu")
        return Result<std::unique_ptr<OceanSprayRenderer>>::failure(
            failure(DiagnosticCode::Unsupported, "Spray requires Vulkan or WebGPU"));
    std::unique_ptr<OceanSprayRenderer> result(new OceanSprayRenderer);
    auto&                               state = *result->impl_;
    state.graphics                            = &graphics_;
    state.lifetime                            = lifetime;
    try {
        if (backend == "webgpu")
            state.shader = graphics_.newMeshShaderFromWgsl(shaders::sprayVertex, shaders::sprayFragment);
        else
            state.shader = graphics_.newMeshShaderFromSpv(
                std::vector<std::uint32_t>(ocean_spray_vert_spv, ocean_spray_vert_spv + ocean_spray_vert_spv_count),
                std::vector<std::uint32_t>(ocean_spray_frag_spv, ocean_spray_frag_spv + ocean_spray_frag_spv_count));
        if (!state.shader)
            return Result<std::unique_ptr<OceanSprayRenderer>>::failure(
                failure(DiagnosticCode::Unsupported, "Spray shader unavailable"));
        auto configured = graphics_.configureMeshShaderSurface(*state.shader, graphics::BlendMode::Alpha, false, true);
        if (!configured) return Result<std::unique_ptr<OceanSprayRenderer>>::failure(configured.status());
        const float         positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0}, attributes[9] = {}, uv[6] = {};
        const std::uint32_t indices[3] = {0, 1, 2};
        for (auto& mesh : state.mesh) {
            mesh = graphics_.newMeshFromArrays(positions, attributes, uv, 3, indices, 3);
            if (!mesh)
                return Result<std::unique_ptr<OceanSprayRenderer>>::failure(
                    failure(DiagnosticCode::Unsupported, "Spray mesh unavailable"));
        }
    } catch (const std::exception& error) {
        return Result<std::unique_ptr<OceanSprayRenderer>>::failure(failure(DiagnosticCode::Unsupported, error.what()));
    }
    return Result<std::unique_ptr<OceanSprayRenderer>>::success(std::move(result));
}
Result<void> OceanSprayRenderer::publish(const OceanSprayMeshFrame& frame) {
    auto& s = *impl_;
    if (s.lifetime.expired()) return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics retired"));
    const auto vertices = frame.positions.size() / 3;
    auto       finite   = [](float v) { return std::isfinite(v); };
    if (frame.positions.size() % 12 || vertices > 16384 * 4 || frame.attributes.size() != frame.positions.size() ||
        frame.uv.size() != vertices * 2 || frame.indices.size() != vertices / 4 * 6 || !std::isfinite(frame.time) ||
        !std::all_of(frame.positions.begin(), frame.positions.end(), finite) ||
        !std::all_of(frame.attributes.begin(), frame.attributes.end(), finite) ||
        !std::all_of(frame.uv.begin(), frame.uv.end(), finite))
        return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid spray batch"));
    for (std::size_t i = 0; i < vertices; ++i)
        if (frame.attributes[i * 3] < 0 || frame.attributes[i * 3] > 1 || frame.attributes[i * 3 + 1] != 0 ||
            frame.attributes[i * 3 + 2] != 0)
            return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid spray coverage"));
    for (auto index : frame.indices)
        if (index >= vertices)
            return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid spray index"));
    if (vertices == 0) {
        s.empty     = true;
        s.published = true;
        return Result<void>::success();
    }
    const auto next = 1 - s.front;
    if (!s.graphics->updateMeshVertices(s.mesh[next], frame.positions.data(), frame.attributes.data(), frame.uv.data(),
                                        static_cast<int>(vertices), frame.indices.data(),
                                        static_cast<int>(frame.indices.size())))
        return Result<void>::failure(failure(DiagnosticCode::Unsupported, "Spray upload rejected"));
    s.front     = next;
    s.empty     = false;
    s.published = true;
    return Result<void>::success();
}
Result<void> OceanSprayRenderer::draw() {
    auto& s = *impl_;
    if (s.lifetime.expired()) return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics retired"));
    if (!s.published) return Result<void>::failure(failure(DiagnosticCode::Conflict, "Spray batch unpublished"));
    if (!s.empty)
        s.graphics->drawMeshShader(s.mesh[s.front], glm::mat4(1.0F), nullptr, graphics::Color(1.0F), s.shader);
    return Result<void>::success();
}
}  // namespace eve::fluids
