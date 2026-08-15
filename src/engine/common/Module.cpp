#include "common/Module.h"
#include "common/ECS.h"
#include "common/Runtime.h"

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

void ModuleManager::exposeVM(ssq::VM& vm) {
    ssq::Table table;
    // Runtime::initialize() guarantees expose() runs once, so addTable never
    // sees an existing "eve" slot. Avoid find()+catch here: ASYNCIFY-wrapped
    // frames can drop the catch and leak NotFoundException to the JS boundary.
    table = vm.addTable("eve");
    for (auto& D : inst().registered_modules) {
        if (D.second.exposer) D.second.exposer(table);
        D.second.exposed = true;
    }
    // After modules: script ECS owns eve.Component / Entity / System / view.
    exposeECS(table);
}

void ModuleManager::expose(Runtime& runtime) {
    inst().active_runtime_ = &runtime;
    exposeVM(runtime.vm());
}

void ModuleManager::expose(ssq::VM& vm) {
    inst().active_runtime_ = nullptr;
    exposeVM(vm);
}

Runtime* ModuleManager::runtime() { return inst().active_runtime_; }

ssq::VM* ModuleManager::vm() {
    Runtime* active = runtime();
    return active ? &active->vm() : nullptr;
}

void ModuleManager::detach(Runtime* runtime) {
    if (inst().active_runtime_ == runtime) inst().active_runtime_ = nullptr;
}

int ModuleManager::expose_pending() {
    Runtime* active = inst().active_runtime_;
    if (!active)
        return -1;
    int count = 0;
    try {
        auto scope = active->enter();
        auto stack = active->guard();
        ssq::Table table = active->table("eve");
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
