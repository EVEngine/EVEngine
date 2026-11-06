// eve-hot-path
#include "graphics/multiscale/FarFlux.h"
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <vector>
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/ShaderImageUpdate.h"
#include "graphics/ShaderResources.h"
#include "graphics/multiscale/shaders/far_flux_frag_spv.inc"
#include "graphics/multiscale/shaders/far_flux_vert_spv.inc"

namespace eve::graphics::multiscale {
namespace {
Diagnostic failure(DiagnosticCode code, const char* message) {
    return Diagnostic::error(code, message, "graphics.multiscale.farFluxView");
}
}

struct FarFluxView::Impl {
    Graphics*                 graphics = nullptr;
    std::weak_ptr<const void> lifetime;
    Mesh*                     mesh = nullptr;
    Shader*                   shader = nullptr;
    std::size_t               width = 0, height = 0;
    float                     exposure = 1;
    ~Impl() {
        if (!graphics || lifetime.expired()) return;
        if (mesh && graphics->releaseMesh(mesh)) delete mesh;
        if (shader && graphics->releaseShader(shader)) delete shader;
    }
};

FarFluxView::FarFluxView() : impl_(std::make_unique<Impl>()) {}
FarFluxView::~FarFluxView() = default;

Result<std::unique_ptr<FarFluxView>> FarFluxView::prepare(Graphics& graphics, const FarFluxFrame& frame,
                                                         float exposure) {
    if (!std::isfinite(exposure) || exposure <= 0)
        return Result<std::unique_ptr<FarFluxView>>::failure(
            failure(DiagnosticCode::InvalidArgument, "Invalid flux exposure"));
    if (graphics.resourceLifetime().expired())
        return Result<std::unique_ptr<FarFluxView>>::failure(
            failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (graphics.getBackendName() != "vulkan")
        return Result<std::unique_ptr<FarFluxView>>::failure(
            failure(DiagnosticCode::Unsupported, "Flux view currently requires Vulkan"));
    if (frame.width() == 0 || frame.height() == 0 || frame.width() != static_cast<std::size_t>(graphics.getPixelWidth()) ||
        frame.height() != static_cast<std::size_t>(graphics.getPixelHeight()))
        return Result<std::unique_ptr<FarFluxView>>::failure(
            failure(DiagnosticCode::InvalidArgument, "Flux dimensions differ from the graphics surface"));
    try {
        std::vector<float> rgba(frame.pixels().size() * 4);
        for (std::size_t i = 0; i < frame.pixels().size(); ++i)
            for (unsigned channel = 0; channel < 3; ++channel) {
                const double source = frame.pixels()[i][channel];
                if (!std::isfinite(source) || source < 0 || source > std::numeric_limits<float>::max())
                    return Result<std::unique_ptr<FarFluxView>>::failure(
                        failure(DiagnosticCode::Unsupported, "Flux pixel exceeds GPU float range"));
                const float value = static_cast<float>(source);
                if (source > 0 && value == 0)
                    return Result<std::unique_ptr<FarFluxView>>::failure(
                        failure(DiagnosticCode::Unsupported, "Flux pixel underflows GPU float"));
                rgba[i * 4 + channel] = value;
            }
        std::unique_ptr<FarFluxView> output(new FarFluxView);
        auto&                        state = *output->impl_;
        state.graphics = &graphics;
        state.lifetime = graphics.resourceLifetime();
        state.width = frame.width();
        state.height = frame.height();
        state.exposure = exposure;
        const std::vector<std::uint32_t> vertex(far_flux_vert_spv, far_flux_vert_spv + far_flux_vert_spv_count);
        const std::vector<std::uint32_t> fragment(far_flux_frag_spv, far_flux_frag_spv + far_flux_frag_spv_count);
        // The initial mesh pipeline has only the legacy descriptor layout. The
        // resource-aware replacement installs the image layout with the shader.
        state.shader = graphics.newMeshShaderFromSpv({}, {});
        if (!state.shader)
            return Result<std::unique_ptr<FarFluxView>>::failure(
                failure(DiagnosticCode::Unsupported, "Flux shader unavailable"));
        auto surface = graphics.configureMeshShaderSurface(*state.shader, BlendMode::Additive, false, true);
        if (!surface.ok()) return Result<std::unique_ptr<FarFluxView>>::failure(surface.status());
        auto raster = graphics.configureMeshShaderRaster(*state.shader, {});
        if (!raster.ok()) return Result<std::unique_ptr<FarFluxView>>::failure(raster.status());
        const float         positions[]{-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
        const float         normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
        const float         uv[]{0, 0, 1, 0, 1, 1, 0, 1};
        const std::uint32_t indices[]{0, 1, 2, 0, 2, 3};
        state.mesh = graphics.newMeshFromArrays(positions, normals, uv, 4, indices, 6);
        if (!state.mesh)
            return Result<std::unique_ptr<FarFluxView>>::failure(
                failure(DiagnosticCode::Failed, "Flux quad upload failed"));
        ShaderImageInput image;
        image.format = ShaderImageFormat::RGBA32Float;
        image.width = static_cast<std::uint32_t>(state.width);
        image.height = static_cast<std::uint32_t>(state.height);
        image.sampler = TextureSampler::nearest();
        image.bytes = std::as_bytes(std::span(rgba));
        std::array<glm::mat4, 1> transforms{glm::mat4(1.f)};
        ShaderResourceInputs inputs;
        inputs.images = std::span(&image, 1);
        inputs.instanceMatrices = std::as_bytes(std::span(transforms));
        auto prepared = graphics.replaceMeshShaderResources(*state.shader, vertex, fragment, inputs);
        if (!prepared.ok()) return Result<std::unique_ptr<FarFluxView>>::failure(prepared.status());
        return Result<std::unique_ptr<FarFluxView>>::success(std::move(output));
    } catch (const std::exception& error) {
        return Result<std::unique_ptr<FarFluxView>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.farFluxView"));
    }
}

Result<void> FarFluxView::setExposure(float exposure) {
    if (!std::isfinite(exposure) || exposure <= 0)
        return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Invalid flux exposure"));
    if (impl_->lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    impl_->exposure = exposure;
    return Result<void>::success();
}

Result<void> FarFluxView::update(const FarFluxFrame& frame) {
    auto& state = *impl_;
    if (state.lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (frame.width() != state.width || frame.height() != state.height)
        return Result<void>::failure(failure(DiagnosticCode::InvalidArgument, "Flux image dimensions changed"));
    try {
        std::vector<float> rgba(frame.pixels().size() * 4);
        for (std::size_t i = 0; i < frame.pixels().size(); ++i)
            for (unsigned channel = 0; channel < 3; ++channel) {
                const double source = frame.pixels()[i][channel];
                if (!std::isfinite(source) || source < 0 || source > std::numeric_limits<float>::max())
                    return Result<void>::failure(
                        failure(DiagnosticCode::Unsupported, "Flux pixel exceeds GPU float range"));
                const float value = static_cast<float>(source);
                if (source > 0 && value == 0)
                    return Result<void>::failure(failure(DiagnosticCode::Unsupported, "Flux pixel underflows GPU float"));
                rgba[i * 4 + channel] = value;
            }
        ShaderImageInput image;
        image.format = ShaderImageFormat::RGBA32Float;
        image.width = static_cast<std::uint32_t>(state.width);
        image.height = static_cast<std::uint32_t>(state.height);
        image.sampler = TextureSampler::nearest();
        image.bytes = std::as_bytes(std::span(rgba));
        auto prepared = ShaderImageUpdate::prepare(image);
        if (!prepared.ok()) return Result<void>::failure(prepared.status());
        return prepared.value().apply(*state.graphics, *state.shader);
    } catch (const std::exception& error) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.farFluxView"));
    }
}

Result<void> FarFluxView::draw() {
    auto& state = *impl_;
    if (state.lifetime.expired())
        return Result<void>::failure(failure(DiagnosticCode::StaleHandle, "Graphics provider retired"));
    if (state.width != static_cast<std::size_t>(state.graphics->getPixelWidth()) ||
        state.height != static_cast<std::size_t>(state.graphics->getPixelHeight()))
        return Result<void>::failure(failure(DiagnosticCode::Unsupported, "Flux view must be rebuilt after resize"));
    if (state.graphics->isHeadless())
        return Result<void>::failure(failure(DiagnosticCode::Unsupported, "Flux draw requires a presentation surface"));
    try {
        std::array<float, 32> constants{};
        constants[0] = state.exposure;
        constants[28] = 1.f;
        state.shader->setPushConstantBlock(constants);
        state.graphics->setMesh3DViewProj(glm::mat4(1.f));
        state.graphics->setMesh3DView(glm::mat4(1.f));
        return state.graphics->drawMeshShaderInstances(*state.mesh, *state.shader, glm::mat4(1.f), Color(1.f), 0, 1);
    } catch (const std::exception& error) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "graphics.multiscale.farFluxView"));
    }
}
}  // namespace eve::graphics::multiscale
