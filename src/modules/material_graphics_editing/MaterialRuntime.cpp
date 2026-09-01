#include "material_editing/MaterialTarget.h"

// Optional graphics adapter for the renderer-neutral material authoring contract.

#include "graphics/RenderSystem3D.h"

#include "ECS.hpp"

namespace eve::material_editing {
namespace {

template <class T>
EditorResult<T> runtimeError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue::Object* properties(const MaterialDocumentTarget& target,
                                      EditorValue& snapshot) {
    snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    if (!root) return nullptr;
    const auto found = root->find("properties");
    return found == root->end() ? nullptr : found->second.getIf<EditorValue::Object>();
}

template <class T>
const T* value(const EditorValue::Object& properties, const char* path) {
    const auto found = properties.find(path);
    return found == properties.end() ? nullptr : found->second.getIf<T>();
}

template <class T, class Resolver>
EditorResult<T*> resolveAsset(const std::string& asset, Resolver&& resolver) {
    if (asset.empty()) return EditorResult<T*>::applied(nullptr);
    return resolver(asset);
}

}  // namespace

struct Renderable3DMaterialRuntimeSink::Impl {
    ecs::EntityHandle handle;
    const IMaterialRuntimeAssetResolver* assets = nullptr;
};

Renderable3DMaterialRuntimeSink::Renderable3DMaterialRuntimeSink(
    graphics::Renderable3D* renderable, const IMaterialRuntimeAssetResolver* assets)
    : impl_(std::make_unique<Impl>()) {
    impl_->handle = ecs::handle_of(renderable);
    impl_->assets = assets;
}

double number(const EditorValue& value) {
    if (const auto* real = value.getIf<double>()) return *real;
    if (const auto* integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
    return 0.0;
}

Renderable3DMaterialRuntimeSink::~Renderable3DMaterialRuntimeSink() = default;

EditorResult<void> Renderable3DMaterialRuntimeSink::publish(
    const MaterialDocumentTarget& candidate) {
    if (!impl_->assets)
        return runtimeError<void>(EditorStatus::Rejected, "editor.material.runtime-input",
                                  "Material asset resolver is required");
    auto* renderable = dynamic_cast<graphics::Renderable3D*>(ecs::try_get(impl_->handle));
    if (!renderable)
        return runtimeError<void>(EditorStatus::Conflict, "editor.material.runtime-stale",
                                  "Renderable3D handle is missing or stale");
    const auto diagnostics = candidate.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            EditorResult<void> result;
            result.status = EditorStatus::Rejected;
            result.diagnostics = diagnostics;
            return result;
        }
    }
    EditorValue snapshot;
    const auto* values = properties(candidate, snapshot);
    if (!values)
        return runtimeError<void>(EditorStatus::Failed, "editor.material.runtime-properties",
                                  "Material properties are unavailable");
    if (*value<std::string>(*values, "shading.model") != "pbr" ||
        *value<std::string>(*values, "surface.mode") != "opaque" ||
        *value<std::string>(*values, "surface.blend") != "alpha" ||
        *value<bool>(*values, "surface.double-sided"))
        return runtimeError<void>(EditorStatus::Unsupported,
                                  "editor.material.runtime-legacy-surface",
                                  "Legacy Renderable3D supports PBR opaque single-sided materials only");

    auto albedo = resolveAsset<graphics::Texture>(
        *value<std::string>(*values, "textures.albedo"),
        [&](const std::string& asset) { return impl_->assets->resolveTexture(asset); });
    auto normal = resolveAsset<graphics::Texture>(
        *value<std::string>(*values, "textures.normal"),
        [&](const std::string& asset) { return impl_->assets->resolveTexture(asset); });
    auto height = resolveAsset<graphics::Texture>(
        *value<std::string>(*values, "textures.height"),
        [&](const std::string& asset) { return impl_->assets->resolveTexture(asset); });
    auto shader = resolveAsset<graphics::Shader>(
        *value<std::string>(*values, "textures.shader"),
        [&](const std::string& asset) { return impl_->assets->resolveShader(asset); });
    for (const auto* result : {static_cast<const EditorResult<graphics::Texture*>*>(&albedo),
                               static_cast<const EditorResult<graphics::Texture*>*>(&normal),
                               static_cast<const EditorResult<graphics::Texture*>*>(&height)}) {
        if (!result->isAccepted() || !result->value) {
            EditorResult<void> failed;
            failed.status = result->status;
            failed.diagnostics = result->diagnostics;
            return failed;
        }
    }
    if (!shader.isAccepted() || !shader.value) {
        EditorResult<void> failed;
        failed.status = shader.status;
        failed.diagnostics = std::move(shader.diagnostics);
        return failed;
    }

    const auto& tint = *value<EditorValue::Array>(*values, "shading.tint");
    renderable->setTexture(*albedo.value);
    renderable->setNormalTexture(*normal.value);
    renderable->setHeightTexture(*height.value);
    renderable->setShader(*shader.value);
    renderable->setTint(static_cast<float>(number(tint[0])),
                        static_cast<float>(number(tint[1])),
                        static_cast<float>(number(tint[2])),
                        static_cast<float>(number(tint[3])));
    renderable->setMetallic(static_cast<float>(*value<double>(*values, "shading.metallic")));
    renderable->setRoughness(static_cast<float>(*value<double>(*values, "shading.roughness")));
    renderable->setParallax(static_cast<float>(*value<double>(*values, "parallax.scale")));
    renderable->setReceiveLight(*value<bool>(*values, "lighting.receive"));
    renderable->setCastShadow(*value<bool>(*values, "shadow.cast"));
    renderable->setReceiveShadow(*value<bool>(*values, "shadow.receive"));
    return EditorResult<void>::applied();
}

}  // namespace eve::material_editing
