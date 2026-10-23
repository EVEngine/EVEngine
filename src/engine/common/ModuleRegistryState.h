#pragma once

// Private registry layout shared only by the two implementation translation units.
#include <unordered_map>
#include <vector>
#include "common/Module.h"

namespace eve {
struct ModuleManager::State {
    struct ModuleInfo {
        creator_t creator  = nullptr;
        exposer_t exposer  = nullptr;
        Module*   instance = nullptr;
        bool      exposed  = false;
    };
    std::unordered_map<std::string, ModuleInfo> registered_modules;
    bool                                        plugin_registration_active_ = false;
    std::vector<std::string>                    plugin_registration_added_;
    std::string                                 plugin_registration_error_;
    Runtime*                                    active_runtime_ = nullptr;
};
}  // namespace eve
