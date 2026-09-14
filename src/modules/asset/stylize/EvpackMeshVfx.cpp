#include "asset/stylize/EvpackMeshVfx.h"
#include <cmath>
#include <map>
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/ShaderAsset.h"
#include "asset/graphics/EvpackShader.h"
#include "asset/graphics/ShaderAssetValidation.h"
#include "stylize/MeshVfxAsset.h"

namespace eve::asset_stylize {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "asset.mesh-vfx"));
}
}  // namespace
struct EvpackMeshVfx::Impl {
    graphics::Graphics&                                        graphics;
    std::unique_ptr<stylize::MeshVfxAssetInstance>             playback;
    std::vector<std::unique_ptr<asset_graphics::EvpackShader>> shaders;
};
EvpackMeshVfx::EvpackMeshVfx(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EvpackMeshVfx::~EvpackMeshVfx() = default;
Result<std::unique_ptr<EvpackMeshVfx>> EvpackMeshVfx::load(const asset::EvpackResourceReader& reader,
                                                           const AssetRef&                    asset,
                                                           const asset::EvpackCapabilities&   capabilities,
                                                           graphics::Graphics&                graphics) {
    auto payload = reader.read(asset, "eve.stylize.mesh-vfx/1", capabilities, 1024 * 1024);
    if (!payload) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(payload.status());
    if (payload.value().chunks.size() != 1 || payload.value().chunks[0].kind != asset::EvpackChunkKind::Definition)
        return failure<std::unique_ptr<EvpackMeshVfx>>(DiagnosticCode::InvalidArgument,
                                                       "Effect requires one definition");
    auto value = asset::decodeRuntimeDefinition(payload.value().chunks[0].bytes);
    if (!value) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(value.status());
    auto json = value.value().toJson();
    if (!json) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(json.status());
    auto definition = stylize::MeshVfxAsset::fromJson(json.value());
    if (!definition) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(definition.status());
    if (definition.value().layers.size() > 32 || definition.value().trail || definition.value().trailBinding ||
        !definition.value().animationTriggers.play.empty() || !definition.value().animationTriggers.stop.empty() ||
        !definition.value().animationTriggers.trailBreak.empty())
        return failure<std::unique_ptr<EvpackMeshVfx>>(
            DiagnosticCode::Unsupported,
            "Package renderer supports up to 32 mesh layers; trail/animation attachments require a gameplay adapter");
    std::map<std::string, std::map<std::string, float>> defaults;
    std::vector<AssetRef>                               references;
    for (const auto& layer : definition.value().layers) {
        const float cycle = layer.playback.fadeIn + layer.playback.duration + layer.playback.fadeOut;
        if (!std::isfinite(cycle) || (layer.playback.loop && cycle <= 0))
            return failure<std::unique_ptr<EvpackMeshVfx>>(DiagnosticCode::InvalidArgument, "Invalid playback cycle");
        auto reference = AssetRef::parse(layer.style);
        if (!reference) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(reference.status());
        auto shader = asset::loadShaderAsset(reader, reference.value(), capabilities);
        if (!shader) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(shader.status());
        if (shader.value().interface != asset::ShaderAssetInterface::Mesh3D)
            return failure<std::unique_ptr<EvpackMeshVfx>>(DiagnosticCode::InvalidArgument,
                                                           "Mesh effect requires mesh shader");
        auto layout = asset_graphics::validateShaderAssetGpu(shader.value());
        if (!layout) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(layout.status());
        auto& parameters = defaults[layer.style];
        for (const auto& parameter : shader.value().parameters) {
            if (parameter.defaults.size() != 1)
                return failure<std::unique_ptr<EvpackMeshVfx>>(DiagnosticCode::Unsupported,
                                                               "Mesh VFX curves require scalar shader parameters");
            parameters[parameter.name] = parameter.defaults.front();
        }
        references.push_back(std::move(reference).takeValue());
    }
    auto playback = stylize::MeshVfxAssetInstance::create(definition.value(), defaults);
    if (!playback) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(playback.status());
    auto impl = std::make_unique<Impl>(Impl{graphics, std::move(playback).takeValue(), {}});
    for (const auto& reference : references) {
        auto shader = asset_graphics::EvpackShader::load(reader, reference, capabilities, graphics);
        if (!shader) return Result<std::unique_ptr<EvpackMeshVfx>>::failure(shader.status());
        impl->shaders.push_back(std::move(shader).takeValue());
    }
    auto result = std::unique_ptr<EvpackMeshVfx>(new EvpackMeshVfx(std::move(impl)));
    result->play();
    return Result<std::unique_ptr<EvpackMeshVfx>>::success(std::move(result));
}
void         EvpackMeshVfx::play() noexcept { impl_->playback->play(); }
Result<void> EvpackMeshVfx::advance(float dt) {
    if (!std::isfinite(dt) || dt < 0) return failure<void>(DiagnosticCode::InvalidArgument, "Invalid dt");
    // Bound accumulated time before changing any layer.
    for (std::size_t i = 0; i < impl_->playback->layerCount(); ++i)
        if (!std::isfinite(impl_->playback->layer(i).elapsed() + dt))
            return failure<void>(DiagnosticCode::InvalidArgument, "Playback time overflow");
    impl_->playback->update(dt);
    return Result<void>::success();
}
Result<void> EvpackMeshVfx::stop(float fadeOutSeconds) {
    if (!std::isfinite(fadeOutSeconds) || fadeOutSeconds < 0)
        return failure<void>(DiagnosticCode::InvalidArgument, "Invalid fade");
    impl_->playback->stop(fadeOutSeconds);
    return Result<void>::success();
}
Result<void> EvpackMeshVfx::setFloat(std::size_t layer, std::string_view name, float value) {
    if (layer >= impl_->playback->layerCount() || !std::isfinite(value))
        return failure<void>(DiagnosticCode::InvalidArgument, "Invalid layer/value");
    auto& style = impl_->playback->layer(layer).style();
    if (!style.hasParam(std::string(name))) return failure<void>(DiagnosticCode::NotFound, "Parameter not found");
    style.setFloat(std::string(name), value);
    return Result<void>::success();
}
Result<void> EvpackMeshVfx::draw(graphics::Mesh& mesh, const std::array<float, 16>& transform,
                                 OptionalRef<graphics::Texture> texture, const std::array<float, 4>& tint) {
    for (std::size_t i = 0; i < impl_->shaders.size(); ++i) {
        auto& layer = impl_->playback->layer(i);
        if (layer.intensity() <= 0) continue;
        auto& shader = *impl_->shaders[i];
        for (const auto& parameter : shader.definition().parameters) {
            const float value   = layer.style().getFloat(parameter.name);
            auto        applied = shader.setParameter(parameter.name, std::span<const float>(&value, 1));
            if (!applied) return applied;
        }
        auto color = tint;
        color[3] *= layer.intensity();
        auto drawn = shader.drawMesh(mesh, transform, texture, color);
        if (!drawn) return drawn;
    }
    return Result<void>::success();
}
Result<void> EvpackMeshVfx::reload(const asset::EvpackResourceReader& reader, const AssetRef& asset,
                                   const asset::EvpackCapabilities& capabilities) {
    auto candidate = load(reader, asset, capabilities, impl_->graphics);
    if (!candidate) return Result<void>::failure(candidate.status());
    impl_.swap(candidate.value()->impl_);
    return Result<void>::success();
}
std::vector<std::string> EvpackMeshVfx::drainEvents() { return impl_->playback->drainEvents(); }
}  // namespace eve::asset_stylize
