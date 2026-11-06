// eve-hot-path
#include "graphics/ShaderImageUpdate.h"
#include <exception>
#include "common/config.h"
#include "graphics/Graphics.h"
#include "graphics/ShaderResources.h"
#ifndef EVENGINE_WEBGPU
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/ShaderResourceReload.h"
#endif

namespace eve::graphics {
struct ShaderImageUpdate::Impl {
    ShaderImageInput               shape;
    std::vector<std::byte>         bytes;
    std::vector<ShaderImageRegion> regions;
};
ShaderImageUpdate::ShaderImageUpdate(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ShaderImageUpdate::~ShaderImageUpdate()                                              = default;
ShaderImageUpdate::ShaderImageUpdate(ShaderImageUpdate&&) noexcept                   = default;
ShaderImageUpdate&        ShaderImageUpdate::operator=(ShaderImageUpdate&&) noexcept = default;
Result<ShaderImageUpdate> ShaderImageUpdate::prepare(const ShaderImageInput& image) {
    if (image.contentOwner)
        return Result<ShaderImageUpdate>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Image updates cannot use immutable deduplication identities", "shader.imageUpdate"));
    try {
        auto regions = shaderImageRegions(image);
        if (!regions.ok()) return Result<ShaderImageUpdate>::failure(regions.status());
        auto state   = std::make_unique<Impl>();
        state->shape = image;
        state->bytes.assign(image.bytes.begin(), image.bytes.end());
        state->shape.bytes = state->bytes;
        state->regions     = std::move(regions).takeValue();
        return Result<ShaderImageUpdate>::success(ShaderImageUpdate(std::move(state)));
    } catch (const std::exception& error) {
        return Result<ShaderImageUpdate>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "shader.imageUpdate"));
    }
}
Result<void> ShaderImageUpdate::apply(Graphics& graphics, Shader& shader) const {
    if (!impl_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Moved image update", "shader.imageUpdate"));
#ifndef EVENGINE_WEBGPU
    if (auto* backend = dynamic_cast<vulkan::Graphics*>(&graphics))
        return vulkan::updateMeshResourceImage(*backend, shader, impl_->shape, impl_->regions);
#else
    (void)graphics;
    (void)shader;
#endif
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported, "Provider does not support mesh resource image updates", "shader.imageUpdate"));
}

Result<ResidentShaderImageUpdate> ResidentShaderImageUpdate::prepare(std::uint32_t binding, std::uint32_t width,
                                                                     std::uint32_t height) {
    if (binding >= 32 || width == 0 || height == 0 || width > 16384 || height > 16384 ||
        std::uint64_t(width) * height * 16 > std::uint64_t(1) << 31)
        return Result<ResidentShaderImageUpdate>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid resident RGBA32F image shape", "shader.residentImageUpdate"));
    return Result<ResidentShaderImageUpdate>::success(ResidentShaderImageUpdate(binding, width, height));
}

Result<void> ResidentShaderImageUpdate::apply(Graphics& graphics, Shader& shader,
                                               const GpuResidentBufferView& source) const {
#ifndef EVENGINE_WEBGPU
    if (auto* backend = dynamic_cast<vulkan::Graphics*>(&graphics))
        return vulkan::updateMeshResourceImageFromResident(*backend, shader, binding_, width_, height_, source);
#else
    (void)graphics;
    (void)shader;
    (void)source;
#endif
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                    "Provider does not support resident mesh image updates",
                                                    "shader.residentImageUpdate"));
}
}  // namespace eve::graphics
