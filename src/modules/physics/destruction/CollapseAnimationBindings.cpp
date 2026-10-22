#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "physics/destruction/CollapseAnimation.h"
namespace eve::physics {
void exposeCollapseAnimation(ssq::Table& table) {
    auto       clip = table.addClass<CollapseAnimation>("CollapseAnimation");
    const auto vm   = table.getHandle();
    clip.addFunc("contentKey",
                 [](CollapseAnimation*, const std::string& source) { return CollapseAnimation::contentKey(source); });
    clip.addFunc("capture", [vm](CollapseAnimation* self, GeometryCollectionInstance* source, float seconds) {
        if (!source)
            return script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "capture requires an instance", "source")));
        return script::projectResult(vm, self->capture(*source, seconds));
    });
    clip.addFunc("save", [vm](CollapseAnimation* self, const std::string& path) {
        return script::projectResult(vm, self->save(path));
    });
    clip.addFunc("load", [vm](CollapseAnimation* self, const std::string& path) {
        auto loaded = CollapseAnimation::load(path);
        if (!loaded) return script::projectResult(vm, Result<void>::failure(loaded.status()));
        *self = std::move(loaded.value());
        return script::projectResult(vm, Result<void>::success());
    });
    clip.addFunc("duration", [](CollapseAnimation* self) { return self->duration(); });
    clip.addFunc("boneCount", [](CollapseAnimation* self) { return self->boneCount(); });
    clip.addFunc("frameCount", [](CollapseAnimation* self) { return self->frameCount(); });
}
}  // namespace eve::physics
