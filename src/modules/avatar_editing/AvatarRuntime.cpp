#include "avatar_editing/AvatarTarget.h"

#include "avatar/AvatarInstance.h"

#include <sstream>
#include <utility>
#include <vector>

namespace eve::avatar_editing {
namespace {
template <class T>
EditorResult<T> fail(EditorStatus s, const char* r, std::string m) {
    return eve::editing::failed<T>(s, RuleId(r), std::move(m));
}
}  // namespace
AvatarDocumentRuntime::AvatarDocumentRuntime()  = default;
AvatarDocumentRuntime::~AvatarDocumentRuntime() = default;
EditorResult<void> AvatarDocumentRuntime::publish(const AvatarDocumentTarget&   document,
                                                  const IAvatarTextureResolver* textures) {
    const auto diagnostics = document.validate();
    for (const auto& d : diagnostics)
        if (d.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    std::vector<graphics::Texture*> resolved;
    resolved.reserve(document.layers().size());
    for (const auto& layer : document.layers()) {
        if (!textures)
            return fail<void>(EditorStatus::Rejected, "editor.avatar.textures",
                              "Image layers require an Avatar texture resolver");
        auto texture = textures->texture(layer.textureAsset);
        if (!texture.ok()) return EditorResult<void>::failure(texture.status());
        if (!texture.value())
            return fail<void>(EditorStatus::NotFound, "editor.avatar.texture",
                              "Avatar texture resolver returned no texture");
        resolved.push_back(texture.value());
    }
    auto candidate = std::make_unique<avatar::AvatarInstance>(document.kind());
    if (document.kind() == "live2d" && !candidate->loadLive2DModel(document.sourceAsset()))
        return fail<void>(EditorStatus::Failed, "editor.avatar.live2d", "Live2D backend rejected the Avatar model");
    if (document.kind() == "vroid" && !candidate->loadVroidModelPath(document.sourceAsset()))
        return fail<void>(EditorStatus::Failed, "editor.avatar.vroid", "VRoid backend rejected the Avatar model path");
    for (std::size_t i = 0; i < document.layers().size(); ++i) {
        const auto& layer = document.layers()[i];
        if (!candidate->addLayer(layer.name, resolved[i], layer.zIndex) ||
            !candidate->setLayerVisible(layer.name, layer.visible) ||
            !candidate->setLayerOffset(layer.name, layer.offset[0], layer.offset[1]) ||
            !candidate->setLayerSize(layer.name, layer.size[0], layer.size[1]) ||
            !candidate->setLayerColor(layer.name, layer.color[0], layer.color[1], layer.color[2], layer.color[3]))
            return fail<void>(EditorStatus::Failed, "editor.avatar.layer",
                              "Avatar runtime rejected a validated image layer");
    }
    for (const auto& parameter : document.parameters()) {
        if (!candidate->defineParameter(parameter.name, parameter.defaultValue, parameter.minimum, parameter.maximum))
            return fail<void>(EditorStatus::Failed, "editor.avatar.parameter",
                              "Avatar runtime rejected validated parameter metadata");
        candidate->setParameter(parameter.name, parameter.value);
    }
    for (const auto& expression : document.expressions()) {
        std::ostringstream spec;
        bool               first = true;
        for (const auto& [name, value] : expression.channels) {
            if (!first) spec << ';';
            first = false;
            spec << name << '=' << value;
        }
        if (!candidate->defineExpression(expression.name, spec.str()))
            return fail<void>(EditorStatus::Failed, "editor.avatar.expression",
                              "Avatar runtime rejected a validated expression");
    }
    instance_                 = std::move(candidate);
    revision_                 = document.revision();
    return eve::editing::applied<void>(diagnostics);
}
}  // namespace eve::avatar_editing
