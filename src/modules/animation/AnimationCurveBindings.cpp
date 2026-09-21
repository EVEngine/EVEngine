#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include "animation/AnimCurveLibrary.h"
#include "animation/AnimationBindings.h"
#include "common/SquirrelOwnership.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

namespace eve::animation {
// A provider read hands over a freshly allocated FileData that this call owns.
using eve::script::Owned;
void exposeAnimCurveLibraryBindings(ssq::Table& table) {
    auto cls = table.addClass<AnimCurveLibrary>(
        "AnimCurveLibrary", std::function<AnimCurveLibrary*()>([] { return new AnimCurveLibrary(); }), true);
    cls.addFunc("loadBinary", [vm = table.getHandle()](AnimCurveLibrary* self, std::string path) {
        ssq::Table result(vm);
        try {
            auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
            if (!fs) throw std::runtime_error("filesystem unavailable");
            auto* raw = fs->read(path);
            if (!raw) throw std::runtime_error("animation curve file unavailable");
            Owned<eve::filesystem::FileData> data(raw);
            auto loaded = self->load({static_cast<const std::byte*>(data->getData()), data->getSize()});
            result.set("ok", loaded.ok());
            result.set("message", loaded.status().describe());
        } catch (const std::exception& error) {
            result.set("ok", false);
            result.set("message", std::string(error.what()));
        }
        return result;
    });
    cls.addFunc("contains", [](AnimCurveLibrary* self, std::string source) { return self->contains(source); });
    cls.addFunc("getSourceCount", [](AnimCurveLibrary* self) { return static_cast<int>(self->size()); });
    cls.addFunc("sample", [vm = table.getHandle()](AnimCurveLibrary* self, std::string source, std::string channel,
                                                   float time, bool loop) {
        auto       sampled = self->sample(source, channel, time, loop);
        ssq::Table result(vm);
        result.set("ok", sampled.ok());
        result.set("message", sampled.status().describe());
        result.set("present", sampled.ok() && sampled.value().has_value());
        result.set("value", sampled.ok() ? sampled.value().value_or(0.f) : 0.f);
        return result;
    });
}
}  // namespace eve::animation
