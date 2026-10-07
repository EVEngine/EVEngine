#include <functional>
#include <optional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>

#include "animation/AnimCurveLibrary.h"
#include "animation/AnimationBindings.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

namespace eve::animation {
namespace {

constexpr const char* kSource = "animation.bindings";

ssq::Table bindingFailure(HSQUIRRELVM vm, std::string message) {
    return script::projectResult(vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                             std::move(message), {}, {}, kSource)));
}

}  // namespace

// A provider read hands over a freshly allocated FileData that this call owns.
using eve::script::Owned;

void exposeAnimCurveLibraryBindings(ssq::Table& table) {
    auto cls = table.addClass<AnimCurveLibrary>(
        "AnimCurveLibrary", std::function<AnimCurveLibrary*()>([] { return new AnimCurveLibrary(); }), true);
    cls.addFunc("loadBinary", [vm = table.getHandle()](AnimCurveLibrary* self, std::string path) {
        try {
            auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
            if (!fs) throw std::runtime_error("filesystem unavailable");
            auto* raw = fs->read(path);
            if (!raw) throw std::runtime_error("animation curve file unavailable");
            Owned<eve::filesystem::FileData> data(raw);
            return script::projectResult(vm,
                                         self->load({static_cast<const std::byte*>(data->getData()), data->getSize()}));
        } catch (const std::exception& error) {
            return bindingFailure(vm, error.what());
        }
    });
    cls.addFunc("contains", [](AnimCurveLibrary* self, std::string source) { return self->contains(source); });
    cls.addFunc("getSourceCount", [](AnimCurveLibrary* self) { return static_cast<int>(self->size()); });
    cls.addFunc("sample", [vm = table.getHandle()](AnimCurveLibrary* self, std::string source, std::string channel,
                                                   float time, bool loop) {
        return script::projectResult(vm, self->sample(source, channel, time, loop), [](std::optional<float> sample) {
            return Value(Value::Object{
                {"present", Value(sample.has_value())},
                {"value", Value(sample.value_or(0.f))},
            });
        });
    });
}
}  // namespace eve::animation
