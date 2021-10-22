#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{


ModuleManager& ModuleManager::inst() {
    static ModuleManager instance;
    return instance;
}

Module* ModuleManager::find(const char* name) {
    auto p = inst().registered_modules.find(name);
    if (p == inst().registered_modules.end() || p->second.instance == nullptr) return nullptr;
    return p->second.instance;
}

void ModuleManager::insert(const char* name, Module* instance) {
    inst().registered_modules[name].instance = instance;
}

void ModuleManager::register_module(const char* name, creator_t c, exposer_t e) {
    auto p = inst().registered_modules.find(name);
    if (p == inst().registered_modules.end()) {
        inst().registered_modules.insert(
            std::make_pair(std::string(name), 
            ModuleInfo{c, e, nullptr}));
    } else {
        p->second = {c, e, nullptr};
    }
}

void ModuleManager::expose(ssq::VM& vm) {
    auto table = vm.addTable("eve");
    for (auto& D : inst().registered_modules) {
        D.second.exposer(table);
    }
}

ModuleRegister::ModuleRegister(const char* name,
    ModuleManager::creator_t c, ModuleManager::exposer_t e) {
    ModuleManager::register_module(name, c, e);
}


} // namespace eve
