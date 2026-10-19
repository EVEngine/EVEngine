#include "avatar/Appearance.h"
#include "common/SquirrelBinding.h"
#include "graphics/Mesh.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::avatar {
void exposeAppearance(ssq::Table& table) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<Appearance>("AvatarAppearance",
                                                std::function<Appearance*()>([] { return new Appearance(); }), true);
    cls.addFunc("configure", [vm](Appearance* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value.ok()) return script::projectStatusResult(vm, value.status());
        return script::projectResult(vm, self->configure(value.value()));
    });
    cls.addFunc("apply", [vm](Appearance* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value.ok()) return script::projectStatusResult(vm, value.status());
        return script::projectResult(vm, self->apply(value.value()));
    });
    cls.addFunc("restore", [vm](Appearance* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value.ok()) return script::projectStatusResult(vm, value.status());
        return script::projectResult(vm, self->restore(value.value()));
    });
    cls.addFunc("snapshot", [vm](Appearance* self) {
        return script::projectStatusResult(vm, Status::success(), self->snapshot());
    });
    cls.addFunc("evaluate", [vm](Appearance* self) {
        return script::projectStatusResult(vm, Status::success(), self->evaluate());
    });
    cls.addFunc("undo", [vm](Appearance* self) { return script::projectResult(vm, self->undo()); });
    cls.addFunc("redo", [vm](Appearance* self) { return script::projectResult(vm, self->redo()); });
    cls.addFunc("canUndo", &Appearance::canUndo);
    cls.addFunc("canRedo", &Appearance::canRedo);
    cls.addFunc("applyMorphWeights", [vm](Appearance* self, graphics::Mesh* mesh, const std::string& prefix) {
        if (!mesh)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected a live mesh")));
        return script::projectResult(vm, self->applyMorphWeights(*mesh, prefix));
    });
    cls.addFunc("snapshotJson", [vm](Appearance* self) {
        return script::projectResult(vm, self->snapshot().toJson(),
                                     [](std::string value) { return Value(std::move(value)); });
    });
    cls.addFunc("restoreJson", [vm](Appearance* self, const std::string& json) {
        auto value = Value::fromJson(json);
        if (!value.ok()) return script::projectStatusResult(vm, value.status());
        return script::projectResult(vm, self->restore(value.value()));
    });
}
}  // namespace eve::avatar
