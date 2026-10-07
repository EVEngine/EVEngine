#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "animation/AnimInertializer.h"
#include "animation/AnimPose.h"
#include "animation/AnimationBindings.h"
#include "common/SquirrelBinding.h"

namespace eve::animation {
namespace {

constexpr const char* kSource = "animation.bindings";

ssq::Table bindingFailure(HSQUIRRELVM vm, std::string message) {
    return script::projectResult(vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                             std::move(message), {}, {}, kSource)));
}

}  // namespace

void exposeAnimInertializerBindings(ssq::Table& table) {
    auto cls = table.addClass<AnimInertializer>(
        "AnimInertializer", std::function<AnimInertializer*()>([] { return new AnimInertializer(); }), true);
    cls.addFunc("begin", [vm = table.getHandle()](AnimInertializer* self, AnimPose* source, AnimPose* previousSource,
                                                  AnimPose* target, AnimPose* previousTarget, float history,
                                                  float duration, ssq::Array factors) {
        try {
            if (!source || !previousSource || !target || !previousTarget)
                throw std::runtime_error("transition requires four poses");
            if (factors.size() != 0 && factors.size() != static_cast<std::size_t>(source->getBoneCount()))
                throw std::runtime_error("time factors must match bone count");
            std::vector<float> values;
            for (std::size_t i = 0; i < factors.size(); ++i) values.push_back(factors.get<float>(i));
            return script::projectResult(
                vm, self->begin(*source, *previousSource, *target, *previousTarget, history, duration, values));
        } catch (const std::exception& error) {
            return bindingFailure(vm, error.what());
        }
    });
    cls.addFunc("evaluate", [vm = table.getHandle()](AnimInertializer* self, AnimPose* target, float elapsed) {
        if (!target) return bindingFailure(vm, "target pose is required");
        return script::projectResult(vm, self->evaluate(*target, elapsed));
    });
    cls.addFunc("copyPose", [](AnimInertializer* self, AnimPose* destination) {
        if (!destination) throw std::runtime_error("output pose is required");
        destination->copyFrom(&self->pose());
    });
}
}  // namespace eve::animation
