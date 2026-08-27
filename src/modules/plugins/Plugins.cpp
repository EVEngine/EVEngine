#include "plugins/Plugins.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <string_view>

namespace eve::plugins {

namespace {

void rollbackPluginRegistration(std::string_view reason) {
    auto rollback = ModuleManager::finishPluginRegistration(false);
    rollback.ignore(reason);
}

}  // namespace

using PluginInitFn = int (*)();

Module_IMPL(Plugins, new Plugins());

Plugins::Plugins() = default;

Plugins::~Plugins() {
    // Module factories and Squirrel closures point into plugin code. Keep every
    // successfully loaded library resident until process teardown.
    loaded_.clear();
}

bool Plugins::load(const std::string& path) {
    if (path.empty())
        throw Exception("plugins.load: empty path");
    if (loaded_.count(path))
        return true;
    auto registration = ModuleManager::beginPluginRegistration();
    if (!registration.ok())
        throw Exception("plugins.load: nested plugin loading is not supported");

#if defined(_WIN32)
    HMODULE mod = LoadLibraryA(path.c_str());
    if (!mod) {
        const DWORD error = GetLastError();
        rollbackPluginRegistration("plugin library load failed");
        throw Exception("plugins.load: LoadLibrary failed for '%s' (err=%lu)", path.c_str(),
                        error);
    }
    auto init = reinterpret_cast<PluginInitFn>(GetProcAddress(mod, "eve_plugin_init"));
    if (!init) {
        rollbackPluginRegistration("plugin entry point is missing");
        FreeLibrary(mod);
        throw Exception("plugins.load: missing eve_plugin_init in '%s'", path.c_str());
    }
    int rc = -1;
    try {
        rc = init();
    } catch (...) {
        rollbackPluginRegistration("plugin initializer threw");
        FreeLibrary(mod);
        throw Exception("plugins.load: eve_plugin_init threw for '%s'", path.c_str());
    }
    if (rc != 0) {
        rollbackPluginRegistration("plugin initializer returned failure");
        FreeLibrary(mod);
        throw Exception("plugins.load: eve_plugin_init returned %d for '%s'", rc, path.c_str());
    }
#else
    void* mod = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!mod) {
        rollbackPluginRegistration("plugin library load failed");
        throw Exception("plugins.load: dlopen failed for '%s': %s", path.c_str(), dlerror());
    }
    dlerror();
    auto init = reinterpret_cast<PluginInitFn>(dlsym(mod, "eve_plugin_init"));
    const char* err = dlerror();
    if (err != nullptr || !init) {
        const std::string error = err ? err : "";
        rollbackPluginRegistration("plugin entry point is missing");
        dlclose(mod);
        throw Exception("plugins.load: missing eve_plugin_init in '%s'%s%s", path.c_str(),
                        error.empty() ? "" : ": ", error.c_str());
    }
    int rc = -1;
    try {
        rc = init();
    } catch (...) {
        rollbackPluginRegistration("plugin initializer threw");
        dlclose(mod);
        throw Exception("plugins.load: eve_plugin_init threw for '%s'", path.c_str());
    }
    if (rc != 0) {
        rollbackPluginRegistration("plugin initializer returned failure");
        dlclose(mod);
        throw Exception("plugins.load: eve_plugin_init returned %d for '%s'", rc, path.c_str());
    }
#endif

    auto registrationResult = ModuleManager::finishPluginRegistration(true);
    if (!registrationResult.ok()) {
        const std::string registrationError = registrationResult.status().describe();
#if defined(_WIN32)
        FreeLibrary(mod);
#else
        dlclose(mod);
#endif
        throw Exception("plugins.load: %s in '%s'", registrationError.c_str(), path.c_str());
    }
    loaded_[path] = Handle{mod, path};

    if (ModuleManager::expose_pending() < 0)
        throw Exception("plugins.load: expose_pending failed after loading '%s'; plugin retained",
                        path.c_str());
    return true;
}

bool Plugins::unload(const std::string& path) {
    (void) path;
    return false;
}

bool Plugins::isLoaded(const std::string& path) const {
    return loaded_.count(path) > 0;
}

void Plugins::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Plugins::create, false);
    expose(cls);
}

void Plugins::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Plugins::getName);
    cls.addFunc("load", &Plugins::load);
    cls.addFunc("unload", &Plugins::unload);
    cls.addFunc("isLoaded", &Plugins::isLoaded);
}

}  // namespace eve::plugins
