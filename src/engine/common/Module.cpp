#include "common/Module.h"
#include "common/Assert.h"
#include "common/ECS.h"
#include "common/Runtime.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{


ModuleManager& ModuleManager::inst() {
    static ModuleManager instance;
    return instance;
}

Module* ModuleManager::find(const char* name) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    auto p = inst().registered_modules.find(name);
    if (p == inst().registered_modules.end() || p->second.instance == nullptr) return nullptr;
    return p->second.instance;
}

void ModuleManager::insert(const char* name, Module* instance) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    EV_PARAM_CHECK(instance != nullptr, "module instance must not be null");
    inst().registered_modules[name].instance = instance;
}

void ModuleManager::requireAll() {
    for (auto& entry : inst().registered_modules) {
        if (!entry.second.instance && entry.second.creator)
            entry.second.instance = entry.second.creator();
    }
}

void ModuleManager::register_module(const char* name, creator_t c, exposer_t e) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    const bool hasCreator = c != nullptr;  // function pointers can't be printed by zeroerr
    EV_PARAM_CHECK(hasCreator, "module creator must not be null");
    auto p = inst().registered_modules.find(name);
    if (inst().plugin_registration_active_ && p != inst().registered_modules.end()) {
        if (inst().plugin_registration_error_.empty())
            inst().plugin_registration_error_ =
                std::string("module name already registered: ") + name;
        return;
    }
    if (p == inst().registered_modules.end()) {
        inst().registered_modules.insert(
            std::make_pair(std::string(name), ModuleInfo{c, e, nullptr}));
        if (inst().plugin_registration_active_)
            inst().plugin_registration_added_.emplace_back(name);
    } else {
        p->second = {c, e, nullptr};
    }
}

eve::Result<void> ModuleManager::beginPluginRegistration() {
    auto& manager = inst();
    if (manager.plugin_registration_active_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "a plugin registration transaction is already active", {}, {}, "module"));
    manager.plugin_registration_active_ = true;
    manager.plugin_registration_added_.clear();
    manager.plugin_registration_error_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ModuleManager::finishPluginRegistration(bool commit) {
    auto& manager = inst();
    if (!manager.plugin_registration_active_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "no plugin registration transaction is active", {}, {}, "module"));

    const std::string error = manager.plugin_registration_error_;
    if (!commit || !error.empty()) {
        for (const auto& name : manager.plugin_registration_added_) {
            auto it = manager.registered_modules.find(name);
            if (it == manager.registered_modules.end())
                continue;
            delete it->second.instance;
            manager.registered_modules.erase(it);
        }
    }
    manager.plugin_registration_active_ = false;
    manager.plugin_registration_added_.clear();
    manager.plugin_registration_error_.clear();
    if (!error.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, error, {}, {}, "module"));
    return eve::Result<void>::success(eve::Status::success(commit ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

void ModuleManager::exposeVM(ssq::VM& vm) {
    ssq::Table table;
    // Runtime::initialize() guarantees expose() runs once, so addTable never
    // sees an existing "eve" slot. Avoid find()+catch here: ASYNCIFY-wrapped
    // frames can drop the catch and leak NotFoundException to the JS boundary.
    table = vm.addTable("eve");
    // Result/Status/Diagnostic projection is a common script contract, not a
    // domain-module convenience. Expose it before module constructors run so
    // every checked binding observes the same `eve.result` helper.
    script::exposeResultBindings(table);
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
