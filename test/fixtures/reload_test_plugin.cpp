#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <array>
#include <stdexcept>

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION 1
#endif
#ifndef PLUGIN_FAILURE
#define PLUGIN_FAILURE 0
#endif

namespace eve::plugins::reload_test {

class ReloadTestPlugin : public Module {
public:
    Module_REG(ReloadTestPlugin);
    int               version() const { return PLUGIN_VERSION; }
    int               increment() { return ++count_; }
    int               count() const { return count_; }
    ReloadTestPlugin* self() { return this; }

private:
#if PLUGIN_VERSION != 1
    // Deliberately change the ABI: a cached old constructor must never return
    // the current generation's instance and reinterpret its layout.
    std::array<int, 64> padding_{};
#endif
    int count_ = 0;
};

Module_IMPL(ReloadTestPlugin, new ReloadTestPlugin());

void ReloadTestPlugin::expose(ssq::Table& table) {
    auto cls = table.addClass(name, ReloadTestPlugin::create, false);
    expose(cls);
#if PLUGIN_FAILURE == 3
    table.set("CandidateOnly", 42);
    throw std::runtime_error("intentional plugin binding failure");
#elif PLUGIN_FAILURE == 4
    table.set("Plugins", 42);
#endif
}

void ReloadTestPlugin::expose(ssq::Class& cls) {
    cls.addFunc("version", &ReloadTestPlugin::version);
    cls.addFunc("increment", &ReloadTestPlugin::increment);
    cls.addFunc("count", &ReloadTestPlugin::count);
    cls.addFunc("self", &ReloadTestPlugin::self);
}

#if PLUGIN_VERSION == 1
class RemovedTestPlugin : public Module {
public:
    Module_REG(RemovedTestPlugin);
    int version() const { return 1; }
};
Module_IMPL(RemovedTestPlugin, new RemovedTestPlugin());
void RemovedTestPlugin::expose(ssq::Table& table) {
    auto cls = table.addClass(name, RemovedTestPlugin::create, false);
    expose(cls);
}
void RemovedTestPlugin::expose(ssq::Class& cls) { cls.addFunc("version", &RemovedTestPlugin::version); }
#endif

}  // namespace eve::plugins::reload_test

#if PLUGIN_FAILURE == 5
EVE_PLUGIN_EXPORT int different_entrypoint() { return 0; }
#else
EVE_PLUGIN_EXPORT int eve_plugin_init() {
#if PLUGIN_FAILURE == 1
    return 17;
#elif PLUGIN_FAILURE == 2
    throw std::runtime_error("intentional plugin initializer failure");
#else
    return 0;
#endif
}
#endif
