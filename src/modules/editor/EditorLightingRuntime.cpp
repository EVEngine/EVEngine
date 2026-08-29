#include "editor/EditorLightingTarget.h"

#include "graphics/Light.h"

#include <utility>

namespace eve::editor {
namespace {

EditorResult<void> lightError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<void>::error(status, RuleId(rule), std::move(message));
}

const EditorValue::Array& array(const Light3DDocumentTarget& document, const char* path) {
    return *document.value(path)->getIf<EditorValue::Array>();
}

double component(const EditorValue::Array& values, size_t index) {
    return *values[index].getIf<double>();
}

}  // namespace

EditorResult<void> Light3DRuntimeApplier::apply(const Light3DDocumentTarget& document,
                                                graphics::Light3D* light) const {
    if (!light)
        return lightError(EditorStatus::Rejected, "editor.light.runtime-required",
                          "Runtime Light3D is required");
    const auto diagnostics = document.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            EditorResult<void> failed;
            failed.status = EditorStatus::Rejected;
            failed.diagnostics = diagnostics;
            return failed;
        }
    const auto& position = array(document, "transform.position");
    const auto& direction = array(document, "transform.direction");
    const auto& color = array(document, "light.color");
    light->setType(*document.value("light.type")->getIf<std::string>());
    light->setEnabled(*document.value("light.enabled")->getIf<bool>());
    light->setPosition(static_cast<float>(component(position, 0)), static_cast<float>(component(position, 1)),
                       static_cast<float>(component(position, 2)));
    light->setDirection(static_cast<float>(component(direction, 0)), static_cast<float>(component(direction, 1)),
                        static_cast<float>(component(direction, 2)));
    light->setColor(static_cast<float>(component(color, 0)), static_cast<float>(component(color, 1)),
                    static_cast<float>(component(color, 2)),
                    static_cast<float>(*document.value("light.intensity")->getIf<double>()));
    light->setRadius(static_cast<float>(*document.value("light.radius")->getIf<double>()));
    light->setCastShadow(*document.value("shadow.cast")->getIf<bool>());
    light->setShadowBias(static_cast<float>(*document.value("shadow.bias")->getIf<double>()));
    light->setShadowStrength(static_cast<float>(*document.value("shadow.strength")->getIf<double>()));
    light->setVolumetric(*document.value("volumetric.enabled")->getIf<bool>());
    light->setVolumetricIntensity(
        static_cast<float>(*document.value("volumetric.intensity")->getIf<double>()));
    EditorResult<void> result = EditorResult<void>::applied();
    result.diagnostics = diagnostics;
    return result;
}

}  // namespace eve::editor
