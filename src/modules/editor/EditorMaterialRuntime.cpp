#include "editor/EditorMaterialTarget.h"

#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

namespace eve::editor {
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

EditorResult<void> Renderable3DMaterialRuntimeSink::publish(
    const MaterialDocumentTarget& candidate) {
    if (!renderable_ || !assets_)
        return runtimeError<void>(EditorStatus::Rejected, "editor.material.runtime-input",
                                  "Live Renderable3D and material asset resolver are required");
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
        [&](const std::string& asset) { return assets_->resolveTexture(asset); });
    auto normal = resolveAsset<graphics::Texture>(
        *value<std::string>(*values, "textures.normal"),
        [&](const std::string& asset) { return assets_->resolveTexture(asset); });
    auto height = resolveAsset<graphics::Texture>(
        *value<std::string>(*values, "textures.height"),
        [&](const std::string& asset) { return assets_->resolveTexture(asset); });
    auto shader = resolveAsset<graphics::Shader>(
        *value<std::string>(*values, "textures.shader"),
        [&](const std::string& asset) { return assets_->resolveShader(asset); });
    for (const auto* result : {static_cast<const EditorResult<graphics::Texture*>*>(&albedo),
                               static_cast<const EditorResult<graphics::Texture*>*>(&normal),
                               static_cast<const EditorResult<graphics::Texture*>*>(&height)}) {
        if (!result->accepted() || !result->value) {
            EditorResult<void> failed;
            failed.status = result->status;
            failed.diagnostics = result->diagnostics;
            return failed;
        }
    }
    if (!shader.accepted() || !shader.value) {
        EditorResult<void> failed;
        failed.status = shader.status;
        failed.diagnostics = std::move(shader.diagnostics);
        return failed;
    }

    const auto& tint = *value<EditorValue::Array>(*values, "shading.tint");
    renderable_->setTexture(*albedo.value);
    renderable_->setNormalTexture(*normal.value);
    renderable_->setHeightTexture(*height.value);
    renderable_->setShader(*shader.value);
    renderable_->setTint(static_cast<float>(*tint[0].getIf<double>()),
                         static_cast<float>(*tint[1].getIf<double>()),
                         static_cast<float>(*tint[2].getIf<double>()),
                         static_cast<float>(*tint[3].getIf<double>()));
    renderable_->setMetallic(static_cast<float>(*value<double>(*values, "shading.metallic")));
    renderable_->setRoughness(static_cast<float>(*value<double>(*values, "shading.roughness")));
    renderable_->setParallax(static_cast<float>(*value<double>(*values, "parallax.scale")));
    renderable_->setReceiveLight(*value<bool>(*values, "lighting.receive"));
    renderable_->setCastShadow(*value<bool>(*values, "shadow.cast"));
    renderable_->setReceiveShadow(*value<bool>(*values, "shadow.receive"));
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
