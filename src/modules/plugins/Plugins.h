#pragma once

#include "common/Module.h"

#include <string>
#include <unordered_map>

namespace eve::plugins {

/** @brief 加载导出 eve_plugin_init 的原生动态库（dll / so / dylib）。 */
class Plugins : public Module {
public:
    Module_REG(Plugins);
    Plugins();
    ~Plugins() override;

    /** @brief 加载插件；路径可为绝对路径或游戏相对路径。 */
    bool load(const std::string& path);
    /** @brief 卸载插件。 */
    bool unload(const std::string& path);
    /** @brief 插件是否已加载。 */
    bool isLoaded(const std::string& path) const;

private:
    struct Handle {
        void*       native = nullptr;
        std::string path;
    };

    std::unordered_map<std::string, Handle> loaded_;
};

}  // namespace eve::plugins
