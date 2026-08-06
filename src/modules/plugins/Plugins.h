#pragma once

#include "common/Module.h"

#include <string>
#include <unordered_map>

namespace eve::plugins {

// Loads native shared libraries (dll / so / dylib) that export eve_plugin_init.
class Plugins : public Module {
public:
    Module_REG(Plugins);
    Plugins();
    ~Plugins() override;

    // Absolute or game-relative path. Returns true on success.
    bool load(const std::string& path);
    bool unload(const std::string& path);
    bool isLoaded(const std::string& path) const;

private:
    struct Handle {
        void*       native = nullptr;
        std::string path;
    };

    std::unordered_map<std::string, Handle> loaded_;
};

}  // namespace eve::plugins
