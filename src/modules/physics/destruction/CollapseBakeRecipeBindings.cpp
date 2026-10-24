#include <simplesquirrel/simplesquirrel.hpp>
#include "CollapseBakeBuildIdentity.h"
#include "common/SquirrelBinding.h"
#include "physics/destruction/CollapseAnimationInternal.h"
#include "physics/destruction/CollapseBakeRecipe.h"

namespace eve::physics {
void exposeCollapseBakeRecipe(ssq::Table& table) {
    auto       recipe = table.addClass<CollapseBakeRecipe>("CollapseBakeRecipe");
    const auto vm     = table.getHandle();
    recipe.addFunc("producerBuildRevision",
                   [](CollapseBakeRecipe*) { return std::string(EVENGINE_COLLAPSE_PRODUCER_REVISION); });
    recipe.addFunc("box3DBackendRevision",
                   [](CollapseBakeRecipe*) { return std::string(EVENGINE_COLLAPSE_BOX3D_REVISION); });
    recipe.addFunc("setProducer", [vm](CollapseBakeRecipe* self, const std::string& revision) {
        return script::projectResult(vm, self->setProducer(revision));
    });
    recipe.addFunc("setBackend", [vm](CollapseBakeRecipe* self, const std::string& revision) {
        return script::projectResult(vm, self->setBackend(revision));
    });
    recipe.addFunc("setParameters", [vm](CollapseBakeRecipe* self, const std::string& bytes) {
        return script::projectResult(vm, self->setParameters(bytes));
    });
    recipe.addFunc("setSource", [vm](CollapseBakeRecipe* self, const std::string& name, const std::string& bytes) {
        return script::projectResult(vm, self->setSource(name, bytes));
    });
    recipe.addFunc("setTiming", [vm](CollapseBakeRecipe* self, float seconds, int stride) {
        if (stride <= 0)
            return script::projectResult(
                vm, Result<void>::failure(collapse_detail::invalid("sample stride must be positive")));
        return script::projectResult(vm, self->setTiming(seconds, static_cast<std::uint32_t>(stride)));
    });
    recipe.addFunc("setRandomStream", [vm](CollapseBakeRecipe* self, const std::string& name, int seed) {
        if (seed < 0)
            return script::projectResult(
                vm, Result<void>::failure(collapse_detail::invalid("script RNG seed must be nonnegative")));
        return script::projectResult(vm, self->setRandomStream(name, static_cast<std::uint64_t>(seed)));
    });
    recipe.addFunc("key", [vm](CollapseBakeRecipe* self) {
        return script::projectResult(vm, self->key(), [](const std::string& key) { return Value::string(key); });
    });
}
}  // namespace eve::physics
