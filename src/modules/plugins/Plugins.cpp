#include "plugins/Plugins.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace eve::plugins {

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
    if (!ModuleManager::beginPluginRegistration())
        throw Exception("plugins.load: nested plugin loading is not supported");

#if defined(_WIN32)
    HMODULE mod = LoadLibraryA(path.c_str());
    if (!mod) {
        const DWORD error = GetLastError();
        ModuleManager::finishPluginRegistration(false);
        throw Exception("plugins.load: LoadLibrary failed for '%s' (err=%lu)", path.c_str(),
                        error);
    }
    auto init = reinterpret_cast<PluginInitFn>(GetProcAddress(mod, "eve_plugin_init"));
    if (!init) {
        ModuleManager::finishPluginRegistration(false);
        FreeLibrary(mod);
        throw Exception("plugins.load: missing eve_plugin_init in '%s'", path.c_str());
    }
    int rc = -1;
    try {
        rc = init();
    } catch (...) {
        ModuleManager::finishPluginRegistration(false);
        FreeLibrary(mod);
        throw Exception("plugins.load: eve_plugin_init threw for '%s'", path.c_str());
    }
    if (rc != 0) {
        ModuleManager::finishPluginRegistration(false);
        FreeLibrary(mod);
        throw Exception("plugins.load: eve_plugin_init returned %d for '%s'", rc, path.c_str());
    }
#else
    void* mod = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!mod) {
        ModuleManager::finishPluginRegistration(false);
        throw Exception("plugins.load: dlopen failed for '%s': %s", path.c_str(), dlerror());
    }
    dlerror();
    auto init = reinterpret_cast<PluginInitFn>(dlsym(mod, "eve_plugin_init"));
    const char* err = dlerror();
    if (err != nullptr || !init) {
        const std::string error = err ? err : "";
        ModuleManager::finishPluginRegistration(false);
        dlclose(mod);
        throw Exception("plugins.load: missing eve_plugin_init in '%s'%s%s", path.c_str(),
                        error.empty() ? "" : ": ", error.c_str());
    }
    int rc = -1;
    try {
        rc = init();
    } catch (...) {
        ModuleManager::finishPluginRegistration(false);
        dlclose(mod);
        throw Exception("plugins.load: eve_plugin_init threw for '%s'", path.c_str());
    }
    if (rc != 0) {
        ModuleManager::finishPluginRegistration(false);
        dlclose(mod);
        throw Exception("plugins.load: eve_plugin_init returned %d for '%s'", rc, path.c_str());
    }
#endif

    const std::string registrationError = ModuleManager::finishPluginRegistration(true);
    if (!registrationError.empty()) {
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
