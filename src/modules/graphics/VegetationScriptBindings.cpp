#include "graphics/VegetationScriptBindings.h"
#include <functional>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/Graphics.h"
#include "graphics/VegetationDetails.h"
#include "graphics/VegetationField.h"

namespace eve::graphics {
namespace {
Value        vector(glm::vec4 v) { return Value::array({v.x, v.y, v.z, v.w}); }
Result<void> missing() {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation field must not be null"));
}
Result<void> missingDetails() {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation details must not be null"));
}
}  // namespace
void exposeVegetationScriptBindings(ssq::Table& table, ssq::Class& graphicsClass) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<VegetationField>("VegetationField",
                                                     std::function<VegetationField*()>([] { return nullptr; }), false);
    graphicsClass.addFunc("newVegetationField", [vm](Graphics*) {
        auto owned = script::makeOwnedSquirrelInstance(vm, std::make_unique<VegetationField>());
        if (!owned.ok()) return script::projectStatusResult(vm, owned.status());
        auto result = script::projectStatusResult(vm, Status::success(), std::move(owned).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
    cls.addFunc("restore", [vm](VegetationField* self, ssq::Object document) {
        if (!self) return script::projectResult(vm, missing());
        auto input = script::valueFromSquirrel(document);
        if (!input.ok()) return script::projectResult(vm, Result<void>::failure(input.status()));
        return script::projectResult(vm, self->restore(input.value()));
    });
    cls.addFunc("snapshot", [vm](VegetationField* self) {
        if (!self) return script::projectResult(vm, missing());
        return script::projectResult(vm, self->snapshot(), [](Value value) { return value; });
    });
    cls.addFunc("sample", [vm](VegetationField* self, float x, float y, float z, int color, int extras, int motion,
                               int vertex) {
        if (!self) return script::projectResult(vm, missing());
        if (color < 0 || color > 8 || extras < 0 || extras > 8 || motion < 0 || motion > 8 || vertex < 0 || vertex > 8)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                                     "layers must be in [0,8]")));
        return script::projectResult(
            vm, self->sample({x, y, z}, {uint8_t(color), uint8_t(extras), uint8_t(motion), uint8_t(vertex)}),
            [](VegetationSample value) {
                return Value::object({{"color", vector(value.color)},
                                      {"extras", vector(value.extras)},
                                      {"motion", vector(value.motion)},
                                      {"vertex", vector(value.vertex)}});
            });
    });

    auto detailsClass = table.addClass<VegetationDetailSettings>(
        "VegetationDetails", std::function<VegetationDetailSettings*()>([] { return nullptr; }), false);
    graphicsClass.addFunc("newVegetationDetails", [vm](Graphics*) {
        auto owned = script::makeOwnedSquirrelInstance(vm, std::make_unique<VegetationDetailSettings>());
        if (!owned.ok()) return script::projectStatusResult(vm, owned.status());
        auto result = script::projectStatusResult(vm, Status::success(), std::move(owned).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
    detailsClass.addFunc("restore", [vm](VegetationDetailSettings* self, ssq::Object document) {
        if (!self) return script::projectResult(vm, missingDetails());
        auto input = script::valueFromSquirrel(document);
        if (!input.ok()) return script::projectResult(vm, Result<void>::failure(input.status()));
        auto restored = restoreVegetationDetails(input.value());
        if (!restored.ok()) return script::projectResult(vm, Result<void>::failure(restored.status()));
        *self = std::move(restored).takeValue();
        return script::projectResult(vm, Result<void>::success());
    });
    detailsClass.addFunc("snapshot", [vm](VegetationDetailSettings* self) {
        if (!self) return script::projectResult(vm, missingDetails());
        return script::projectResult(vm, snapshotVegetationDetails(*self), [](Value value) { return value; });
    });
}
}  // namespace eve::graphics
