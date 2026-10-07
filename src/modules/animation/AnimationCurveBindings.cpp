#include <functional>
#include <optional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>

#include "animation/AnimCurveLibrary.h"
#include "animation/AnimationBindings.h"
#include "common/SquirrelBindContext.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

namespace eve::animation {
namespace {

constexpr const char* kSource = "animation.bindings";

}  // namespace

// A provider read hands over a freshly allocated FileData that this call owns.
using eve::script::Owned;

void exposeAnimCurveLibraryBindings(ssq::Table& table) {
    const script::BindContext bind{table.getHandle(), kSource};
    auto                      cls = table.addClass<AnimCurveLibrary>(
        "AnimCurveLibrary", std::function<AnimCurveLibrary*()>([] { return new AnimCurveLibrary(); }), true);
    script::bindMethod(cls, "loadBinary", [bind](AnimCurveLibrary* self, std::string path) {
        try {
            auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
            if (!fs) throw std::runtime_error("filesystem unavailable");
            auto* raw = fs->read(path);
            if (!raw) throw std::runtime_error("animation curve file unavailable");
            Owned<eve::filesystem::FileData> data(raw);
            return script::projectResult(bind.vm(),
                                         self->load({static_cast<const std::byte*>(data->getData()), data->getSize()}));
        } catch (const std::exception& error) {
            return bind.failInvalid(error.what());
        }
    });
    script::bindMethod(cls, "contains",
                       [](AnimCurveLibrary* self, std::string source) { return self->contains(source); });
    script::bindMethod(cls, "getSourceCount", [](AnimCurveLibrary* self) { return static_cast<int>(self->size()); });
    script::bindMethod(cls, "sample",
                       [bind](AnimCurveLibrary* self, std::string source, std::string channel, float time, bool loop) {
                           return script::projectResult(bind.vm(), self->sample(source, channel, time, loop),
                                                        [](std::optional<float> sample) {
                                                            return Value(Value::Object{
                                                                {"present", Value(sample.has_value())},
                                                                {"value", Value(sample.value_or(0.f))},
                                                            });
                                                        });
                       });
}
}  // namespace eve::animation
