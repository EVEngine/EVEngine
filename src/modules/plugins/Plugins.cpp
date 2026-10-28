#include "plugins/Plugins.h"

#include "common/Exception.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#include <unistd.h>
#endif

#include <filesystem>
#include <unordered_set>
#include <vector>

namespace eve::plugins {
namespace {

struct Images {
    std::unordered_set<std::string> loaded;
    std::vector<void*>              resident;
    uint64_t                        serial = 0;
};

Images& images() {
    static Images state;
    return state;
}

std::string identity(const std::string& path) {
    if (path.empty()) throw Exception("plugins: empty path");
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path)).string();
}

Result<void> failure(DiagnosticCode code, std::string message, const std::string& path) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), path, {}, "plugins"));
}

// Load a unique sibling image: the original can be rebuilt on Windows, and the
// loader cannot reuse the previous image by filename/inode. Keeping the sibling
// in the same directory preserves $ORIGIN / @loader_path dependency resolution.
std::filesystem::path shadowCopy(const std::filesystem::path& source) {
#if defined(_WIN32)
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    for (;;) {
        const auto name = ".eve-" + std::to_string(process) + "-" + std::to_string(++images().serial) + "-" +
                          source.filename().string();
        const auto      shadow = source.parent_path() / name;
        std::error_code error;
        if (std::filesystem::copy_file(source, shadow, std::filesystem::copy_options::none, error)) return shadow;
        if (error == std::errc::file_exists) continue;
        const auto message = error.message();
        std::filesystem::remove(shadow, error);
        throw Exception("plugins: cannot copy library '%s': %s", source.string().c_str(), message.c_str());
    }
}

}  // namespace

using PluginInitFn = int (*)();

Module_IMPL(Plugins, new Plugins());

Plugins::Plugins() = default;
Plugins::~Plugins() = default;

Result<void> Plugins::install(const std::string& path, bool replace) {
    bool registering = false;
    try {
        const std::string key   = identity(path);
        auto&             state = images();
        if (replace && !state.loaded.count(key)) return failure(DiagnosticCode::NotFound, "plugin is not loaded", key);
        if (!replace && state.loaded.count(key)) return Result<void>::success(Status::success(StatusCode::NoOp));
        // Allocate bookkeeping before native initialization can publish code pointers.
        state.resident.reserve(state.resident.size() + 1);
        auto registration = ModuleManager::beginPluginRegistration(key);
        if (!registration.ok()) return registration;
        registering       = true;
        const auto shadow = shadowCopy(key);
#if defined(_WIN32)
        HMODULE    native    = LoadLibraryExW(shadow.c_str(), nullptr,
                                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        void*      mod       = native;
        const auto loadError = GetLastError();
        if (!mod) {
            std::error_code ignored;
            std::filesystem::remove(shadow, ignored);
            throw Exception("plugins: LoadLibrary failed (err=%lu)", loadError);
        }
        auto init = reinterpret_cast<PluginInitFn>(GetProcAddress(native, "eve_plugin_init"));
#else
        void*             mod       = dlopen(shadow.c_str(), RTLD_NOW | RTLD_LOCAL);
        const std::string loadError = mod ? "" : dlerror();
        std::error_code   ignored;
        std::filesystem::remove(shadow, ignored);
        if (!mod) throw Exception("plugins: dlopen failed: %s", loadError.c_str());
        auto init = reinterpret_cast<PluginInitFn>(dlsym(mod, "eve_plugin_init"));
#endif
        // Even a failed exposer can leave native closures in the VM's type map.
        // Never unmap an initialized image while any VM may still refer to it.
        state.resident.push_back(mod);
        if (!init) throw Exception("plugins: missing eve_plugin_init");
        const int rc = init();
        if (rc != 0) throw Exception("plugins: eve_plugin_init returned %d", rc);
        auto committed = ModuleManager::finishPluginRegistration(true);
        registering    = false;
        if (!committed.ok()) return committed;
        state.loaded.insert(key);
        return Result<void>::success(Status::success(StatusCode::Applied));
    } catch (const std::exception& error) {
        if (registering) {
            auto rollback = ModuleManager::finishPluginRegistration(false);
            rollback.ignore("plugin candidate failed; preserve the previous generation");
        }
        return failure(DiagnosticCode::InvalidArgument, error.what(), path);
    } catch (...) {
        if (registering) {
            auto rollback = ModuleManager::finishPluginRegistration(false);
            rollback.ignore("plugin initializer threw; preserve the previous generation");
        }
        return failure(DiagnosticCode::InvalidArgument, "plugin initializer threw", path);
    }
}

Result<void> Plugins::loadPlugin(const std::string& path) { return install(path, false); }
Result<void> Plugins::reload(const std::string& path) { return install(path, true); }

bool Plugins::load(const std::string& path) {
    auto result = loadPlugin(path);
    if (!result.ok()) throw Exception("plugins.load: %s", result.status().describe().c_str());
    return true;
}

bool Plugins::unload(const std::string& path) {
    (void)path;
    return false;
}

bool Plugins::isLoaded(const std::string& path) const {
    if (path.empty()) return false;
    try {
        return images().loaded.count(identity(path)) > 0;
    } catch (...) {
        return false;
    }
}

void Plugins::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Plugins::create, false);
    expose(cls);
}

void Plugins::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Plugins::getName);
    cls.addFunc("load", &Plugins::load);
    cls.addFunc("loadPlugin", [vm = cls.getHandle()](Plugins* self, const std::string& path) {
        return script::projectResult(vm, self->loadPlugin(path));
    });
    cls.addFunc("reload", [vm = cls.getHandle()](Plugins* self, const std::string& path) {
        return script::projectResult(vm, self->reload(path));
    });
    cls.addFunc("unload", &Plugins::unload);
    cls.addFunc("isLoaded", &Plugins::isLoaded);
}

}  // namespace eve::plugins
