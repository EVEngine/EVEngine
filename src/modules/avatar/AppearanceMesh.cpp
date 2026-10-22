#include "avatar/Appearance.h"
#include "graphics/Mesh.h"

namespace eve::avatar {
Result<void> Appearance::applyMorphWeights(graphics::Mesh& mesh, std::string_view prefix) const {
    const auto  evaluated = evaluate();
    const auto& channels  = *evaluated.find("channels")->getIf<Value::Object>();
    for (const auto& [name, value] : channels) {
        (void)value;
        if (name.starts_with(prefix) && !mesh.hasMorph(name.substr(prefix.size())))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Appearance morph target is absent from mesh", name));
    }
    for (const auto& [name, value] : channels)
        if (name.starts_with(prefix))
            mesh.setMorphWeight(name.substr(prefix.size()), static_cast<float>(value.asDouble()));
    return Result<void>::success();
}
}  // namespace eve::avatar
