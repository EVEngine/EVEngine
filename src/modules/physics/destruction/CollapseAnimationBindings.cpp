#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/CollapseBakeRecipe.h"
#include "physics/destruction/CollapsePlayback.h"
#include "physics/destruction/CollapseScriptValue.h"
#include "physics/destruction/GeometryCollectionInstance.h"
namespace eve::physics {
namespace {
ssq::Table ownPlayback(HSQUIRRELVM vm, Result<CollapsePlayback> prepared) {
    if (!prepared) return script::projectStatusResult(vm, prepared.status());
    auto object = script::makeOwnedSquirrelInstance<CollapsePlayback>(
        vm, std::make_unique<CollapsePlayback>(std::move(prepared).takeValue()));
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
}  // namespace
void exposeCollapseAnimation(ssq::Table& table) {
    exposeCollapseBakeRecipe(table);
    auto       clip = table.addClass<CollapseAnimation>("CollapseAnimation");
    const auto vm   = table.getHandle();
    auto       player = table.addClass<CollapsePlayback>(
        "CollapsePlayback", std::function<CollapsePlayback*()>([]() -> CollapsePlayback* { return nullptr; }), true);
    clip.addFunc("createPlayback",
                 [vm](CollapseAnimation* self) { return ownPlayback(vm, CollapsePlayback::prepare(*self)); });
    player.addFunc("copy",
                   [vm](CollapsePlayback* self) { return ownPlayback(vm, Result<CollapsePlayback>::success(*self)); });
    player.addFunc("advance", [vm](CollapsePlayback* self, float seconds) {
        return script::projectResult(vm, self->advance(seconds));
    });
    player.addFunc(
        "seek", [vm](CollapsePlayback* self, float seconds) { return script::projectResult(vm, self->seek(seconds)); });
    player.addFunc("setRate",
                   [vm](CollapsePlayback* self, float rate) { return script::projectResult(vm, self->setRate(rate)); });
    player.addFunc("setLoop", [vm](CollapsePlayback* self, bool loop) {
        return script::projectResult(vm,
                                     self->setWrap(loop ? CollapsePlaybackWrap::Loop : CollapsePlaybackWrap::Clamp));
    });
    player.addFunc("setPaused", [](CollapsePlayback* self, bool paused) { self->setPaused(paused); });
    player.addFunc("paused", [](CollapsePlayback* self) { return self->paused(); });
    player.addFunc("time", [](CollapsePlayback* self) { return static_cast<float>(self->time()); });
    player.addFunc("rate", [](CollapsePlayback* self) { return static_cast<float>(self->rate()); });
    player.addFunc("duration", [](CollapsePlayback* self) { return self->animation()->duration(); });
    player.addFunc("setPlacement",
                   [vm](CollapsePlayback* self, float x, float y, float z, float qx, float qy, float qz, float qw) {
                       return script::projectResult(vm, self->setPlacement({x, y, z, qx, qy, qz, qw}));
                   });
    clip.addFunc("contentKey",
                 [](CollapseAnimation*, const std::string& source) { return CollapseAnimation::contentKey(source); });
    clip.addFunc("capture", [vm](CollapseAnimation* self, GeometryCollectionInstance* source, float seconds) {
        if (!source)
            return script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "capture requires an instance", "source")));
        return script::projectResult(vm, self->capture(*source, seconds));
    });
    clip.addFunc("beginBake", [vm](CollapseAnimation* self, const ssq::Object& recipeObject) {
        auto recipe = collapse_detail::scriptValue<CollapseBakeRecipe>(recipeObject);
        if (!recipe) return script::projectResult(vm, Result<void>::failure(recipe.status()));
        return script::projectResult(vm, self->beginBake(recipe.value().get()));
    });
    clip.addFunc("loadForRecipe",
                 [vm](CollapseAnimation* self, const std::string& path, const ssq::Object& recipeObject) {
                     auto recipe = collapse_detail::scriptValue<CollapseBakeRecipe>(recipeObject);
                     if (!recipe) return script::projectResult(vm, Result<void>::failure(recipe.status()));
                     auto loaded = CollapseAnimation::loadForRecipe(path, recipe.value().get());
                     if (!loaded) return script::projectResult(vm, Result<void>::failure(loaded.status()));
                     *self = std::move(loaded).takeValue();
                     return script::projectResult(vm, Result<void>::success());
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
