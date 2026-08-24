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
    for (auto& kv : loaded_) {
#if defined(_WIN32)
        if (kv.second.native)
            FreeLibrary(static_cast<HMODULE>(kv.second.native));
#else
        if (kv.second.native)
            dlclose(kv.second.native);
#endif
    }
    loaded_.clear();
}

bool Plugins::load(const std::string& path) {
    if (path.empty())
        throw Exception("plugins.load: empty path");
    if (loaded_.count(path))
        return true;

#if defined(_WIN32)
    HMODULE mod = LoadLibraryA(path.c_str());
    if (!mod)
        throw Exception("plugins.load: LoadLibrary failed for '%s' (err=%lu)", path.c_str(),
                        GetLastError());
    auto init = reinterpret_cast<PluginInitFn>(GetProcAddress(mod, "eve_plugin_init"));
    if (!init) {
        FreeLibrary(mod);
        throw Exception("plugins.load: missing eve_plugin_init in '%s'", path.c_str());
    }
    int rc = init();
    if (rc != 0) {
        FreeLibrary(mod);
        throw Exception("plugins.load: eve_plugin_init returned %d for '%s'", rc, path.c_str());
    }
    loaded_[path] = Handle{mod, path};
#else
    void* mod = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!mod)
        throw Exception("plugins.load: dlopen failed for '%s': %s", path.c_str(), dlerror());
    dlerror();
    auto init = reinterpret_cast<PluginInitFn>(dlsym(mod, "eve_plugin_init"));
    const char* err = dlerror();
    if (err != nullptr || !init) {
        const std::string error = err ? err : "";
        dlclose(mod);
        throw Exception("plugins.load: missing eve_plugin_init in '%s'%s%s", path.c_str(),
                        error.empty() ? "" : ": ", error.c_str());
    }
    int rc = init();
    if (rc != 0) {
        dlclose(mod);
        throw Exception("plugins.load: eve_plugin_init returned %d for '%s'", rc, path.c_str());
    }
    loaded_[path] = Handle{mod, path};
#endif

    if (ModuleManager::expose_pending() < 0)
        throw Exception("plugins.load: expose_pending failed after loading '%s'", path.c_str());
    return true;
}

bool Plugins::unload(const std::string& path) {
    auto it = loaded_.find(path);
    if (it == loaded_.end())
        return false;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(it->second.native));
#else
    dlclose(it->second.native);
#endif
    loaded_.erase(it);
    return true;
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
