#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <vector>
#include "animation/AnimInertializer.h"
#include "animation/AnimPose.h"
#include "animation/AnimationBindings.h"

namespace eve::animation {
void exposeAnimInertializerBindings(ssq::Table& table) {
    auto cls = table.addClass<AnimInertializer>(
        "AnimInertializer", std::function<AnimInertializer*()>([] { return new AnimInertializer(); }), true);
    cls.addFunc("begin", [vm = table.getHandle()](AnimInertializer* self, AnimPose* source, AnimPose* previousSource,
                                                  AnimPose* target, AnimPose* previousTarget, float history,
                                                  float duration, ssq::Array factors) {
        ssq::Table result(vm);
        try {
            if (!source || !previousSource || !target || !previousTarget)
                throw std::runtime_error("transition requires four poses");
            if (factors.size() != 0 && factors.size() != static_cast<std::size_t>(source->getBoneCount()))
                throw std::runtime_error("time factors must match bone count");
            std::vector<float> values;
            for (std::size_t i = 0; i < factors.size(); ++i) values.push_back(factors.get<float>(i));
            auto applied = self->begin(*source, *previousSource, *target, *previousTarget, history, duration, values);
            result.set("ok", applied.ok());
            result.set("message", applied.status().describe());
        } catch (const std::exception& e) {
            result.set("ok", false);
            result.set("message", std::string(e.what()));
        }
        return result;
    });
    cls.addFunc("evaluate", [vm = table.getHandle()](AnimInertializer* self, AnimPose* target, float elapsed) {
        ssq::Table result(vm);
        if (!target) {
            result.set("ok", false);
            result.set("message", std::string("target pose is required"));
            return result;
        }
        auto applied = self->evaluate(*target, elapsed);
        result.set("ok", applied.ok());
        result.set("message", applied.status().describe());
        return result;
    });
    cls.addFunc("copyPose", [](AnimInertializer* self, AnimPose* destination) {
        if (!destination) throw std::runtime_error("output pose is required");
        destination->copyFrom(&self->pose());
    });
}
}  // namespace eve::animation
