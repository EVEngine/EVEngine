#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <string>

namespace eve::plugins {

/** @brief 加载导出 eve_plugin_init 的原生动态库（dll / so / dylib）。 */
class EVENGINE_API_FOUNDATION Plugins : public Module {
public:
    Module_REG(Plugins);
    Plugins();
    ~Plugins() override;

    /** @brief Compatibility wrapper for loadPlugin; throws on failure, returns true on success. */
    bool load(const std::string& path);
    /** @brief Loads a plugin once; aliases of a canonical path identify the same plugin.
     * @param path Dynamic library path, copied only for this operation.
     * @return Applied, NoOp if already loaded, or a diagnostic; failure preserves current bindings.
     * @note Main/script thread only; initializer may register modules, not re-enter loading.
     * @cost Copies and links one binary and binds its classes per first load.
     */
    [[nodiscard]] Result<void> loadPlugin(const std::string& path);
    /** @brief Publishes a new plugin generation without restarting the script VM.
     * @param path Previously loaded library path, copied only for this operation.
     * @return Applied or a diagnostic. Failure retains the previous generation.
     * @note Main/script thread only. New constructors use fresh instances; old objects,
     * classes and closures keep their original generation. No automatic state migration.
     * Libraries and retired instances remain resident until process teardown.
     * @cost Copies and links the binary and rebinds its classes on every call;
     * retained code and instances consume memory per generation. Call at a reload boundary.
     */
    [[nodiscard]] Result<void> reload(const std::string& path);
    /** @brief 插件为进程期驻留；该兼容方法始终返回 false。 */
    bool unload(const std::string& path);
    /** @brief 插件是否已加载。 */
    bool isLoaded(const std::string& path) const;

private:
    static Result<void> install(const std::string& path, bool replace);
};

}  // namespace eve::plugins
