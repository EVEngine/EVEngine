#include "asset/graphics/EvpackShader.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include "asset/EvpackResourceReader.h"
#include "asset/ShaderAsset.h"
#include "asset/graphics/ShaderAssetValidation.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"

namespace eve::asset_graphics {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "asset.shader.runtime"));
}
template <std::size_t N>
bool finite(const std::array<float, N>& values) {
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}
}  // namespace
struct EvpackShader::Impl {
    graphics::Graphics& graphics;
    asset::ShaderAsset  definition;
    graphics::Shader*   shader = nullptr;
};
EvpackShader::EvpackShader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EvpackShader::~EvpackShader() {
    auto result = release();
    if (!result) std::fprintf(stderr, "EvpackShader release failed: %s\n", result.error()->message().c_str());
}
const asset::ShaderAsset&             EvpackShader::definition() const noexcept { return impl_->definition; }
Result<std::unique_ptr<EvpackShader>> EvpackShader::load(const asset::EvpackResourceReader& reader,
                                                         const AssetRef&                    asset,
                                                         const asset::EvpackCapabilities&   capabilities,
                                                         graphics::Graphics&                graphics) {
    if (graphics.getBackendName() != "vulkan" || capabilities.graphics != graphics.getBackendName())
        return failure<std::unique_ptr<EvpackShader>>(DiagnosticCode::Unsupported, "Shader backend mismatch");
    auto decoded = asset::loadShaderAsset(reader, asset, capabilities);
    if (!decoded) return Result<std::unique_ptr<EvpackShader>>::failure(decoded.status());
    auto validated = validateShaderAssetGpu(decoded.value());
    if (!validated) return Result<std::unique_ptr<EvpackShader>>::failure(validated.status());
    auto result = std::unique_ptr<EvpackShader>(
        new EvpackShader(std::make_unique<Impl>(Impl{graphics, std::move(decoded).takeValue()})));
    try {
        auto& impl  = *result->impl_;
        impl.shader = impl.definition.interface == asset::ShaderAssetInterface::Mesh3D
                          ? graphics.newMeshShaderFromSpv(impl.definition.vertex, impl.definition.fragment)
                          : graphics.newShaderFromSpv(impl.definition.vertex, impl.definition.fragment);
        if (!impl.shader)
            return failure<std::unique_ptr<EvpackShader>>(DiagnosticCode::Failed,
                                                          "Shader upload returned no allocation");
        for (const auto& parameter : impl.definition.parameters) {
            switch (parameter.defaults.size()) {
                case 1: impl.shader->declareFloat(parameter.name); break;
                case 2: impl.shader->declareVec2(parameter.name); break;
                case 3: impl.shader->declareVec3(parameter.name); break;
                case 4: impl.shader->declareVec4(parameter.name); break;
                default:
                    return failure<std::unique_ptr<EvpackShader>>(DiagnosticCode::InvalidArgument,
                                                                  "Invalid parameter width");
            }
            impl.shader->sendToVar(parameter.name, parameter.defaults.data(),
                                   parameter.defaults.size() * sizeof(float));
        }
        // Initialize/upload the entire ABI block, including unused trailing slots.
        unsigned padding = 0;
        while (impl.shader->usedFloats() < graphics::Shader::kMaxFloats) {
            const auto name = "__eve_padding_" + std::to_string(padding++);
            if (!impl.shader->hasUniform(name)) impl.shader->declareFloat(name);
        }
        return Result<std::unique_ptr<EvpackShader>>::success(std::move(result));
    } catch (const std::exception& error) {
        return failure<std::unique_ptr<EvpackShader>>(DiagnosticCode::Failed, error.what());
    }
}
Result<void> EvpackShader::setParameter(std::string_view name, std::span<const float> values) try {
    if (!impl_->shader) return failure<void>(DiagnosticCode::StaleHandle, "Shader lease is released");
    const auto it = std::find_if(impl_->definition.parameters.begin(), impl_->definition.parameters.end(),
                                 [&](const auto& parameter) { return parameter.name == name; });
    if (it == impl_->definition.parameters.end() || it->defaults.size() != values.size() ||
        !std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); }))
        return failure<void>(DiagnosticCode::InvalidArgument, "Unknown parameter, wrong width or non-finite value");
    impl_->shader->sendToVar(it->name, values.data(), values.size_bytes());
    return Result<void>::success();
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
Result<void> EvpackShader::drawMesh(graphics::Mesh& mesh, const std::array<float, 16>& transform,
                                    OptionalRef<graphics::Texture> texture, const std::array<float, 4>& tint) try {
    if (!impl_->shader) return failure<void>(DiagnosticCode::StaleHandle, "Shader lease is released");
    if (impl_->definition.interface != asset::ShaderAssetInterface::Mesh3D || !finite(transform) || !finite(tint))
        return failure<void>(DiagnosticCode::InvalidArgument, "Invalid mesh draw or shader kind");
    impl_->graphics.drawMeshShader(&mesh, glm::make_mat4(transform.data()), texture ? &texture->get() : nullptr,
                                   graphics::Color(tint[0], tint[1], tint[2], tint[3]), impl_->shader);
    return Result<void>::success();
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
Result<void> EvpackShader::drawSprite(OptionalRef<graphics::Texture> texture, const std::array<float, 4>& rect,
                                      const std::array<float, 4>& tint) try {
    if (!impl_->shader) return failure<void>(DiagnosticCode::StaleHandle, "Shader lease is released");
    if (impl_->definition.interface != asset::ShaderAssetInterface::Sprite2D || !finite(rect) || !finite(tint))
        return failure<void>(DiagnosticCode::InvalidArgument, "Invalid sprite draw or shader kind");
    impl_->graphics.drawTexturedRectShader(texture ? &texture->get() : nullptr, impl_->shader, rect[0], rect[1],
                                           rect[2], rect[3], graphics::Color(tint[0], tint[1], tint[2], tint[3]));
    return Result<void>::success();
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
Result<void> EvpackShader::release() try {
    if (!impl_->shader) return Result<void>::success();
    if (!impl_->graphics.releaseShader(impl_->shader))
        return failure<void>(DiagnosticCode::Failed, "Graphics rejected shader release");
    delete impl_->shader;  // releaseShader transfers the CPU facade back to the lease.
    impl_->shader = nullptr;
    return Result<void>::success();
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
}  // namespace eve::asset_graphics
