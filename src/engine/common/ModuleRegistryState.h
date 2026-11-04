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
        std::string owner;
    };
    std::unordered_map<std::string, ModuleInfo> registered_modules;
    bool                                        plugin_registration_active_ = false;
    std::vector<std::string>                    plugin_registration_added_;
    std::string                                 plugin_registration_error_;
    std::string                                               plugin_registration_owner_;
    std::unordered_map<std::string, ModuleInfo>               plugin_registration_previous_;
    std::vector<std::pair<std::string, ModuleInfo>>           retired_modules_;
    std::unordered_map<std::string, std::vector<std::string>> plugin_binding_keys_;
    ssq::VM*                                                  active_vm_      = nullptr;
    Runtime*                                    active_runtime_ = nullptr;
};
}  // namespace eve
