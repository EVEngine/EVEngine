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
    inst().active_vm_ = &vm;
    auto table = vm.addTable("eve");
    for (auto& D : inst().registered_modules) {
        D.second.exposer(table);
        D.second.exposed = true;
    }
}

void ModuleManager::set_vm(ssq::VM* vm) { inst().active_vm_ = vm; }

ssq::VM* ModuleManager::vm() { return inst().active_vm_; }

int ModuleManager::expose_pending() {
    ssq::VM* v = inst().active_vm_;
    if (!v)
        return -1;
    int count = 0;
    try {
        ssq::Table table(v->find("eve"));
        for (auto& D : inst().registered_modules) {
            if (D.second.exposed || !D.second.exposer)
                continue;
            D.second.exposer(table);
            D.second.exposed = true;
            ++count;
        }
    } catch (...) {
        return -1;
    }
    return count;
}

ModuleRegister::ModuleRegister(const char* name,
    ModuleManager::creator_t c, ModuleManager::exposer_t e) {
    ModuleManager::register_module(name, c, e);
}


} // namespace eve
