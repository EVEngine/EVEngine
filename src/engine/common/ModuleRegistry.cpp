#include "ModuleRegistryState.h"
#include "common/Assert.h"
#include "common/Module.h"

namespace eve {
ModuleManager::ModuleManager() : state_(std::make_unique<State>()) {}
ModuleManager::~ModuleManager() = default;

ModuleManager& ModuleManager::inst() {
    static ModuleManager instance;
    return instance;
}

Module* ModuleManager::find(const char* name) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    auto p = inst().state_->registered_modules.find(name);
    if (p == inst().state_->registered_modules.end() || p->second.instance == nullptr) return nullptr;
    return p->second.instance;
}

Module* ModuleManager::getInstanceRaw(const char* name) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    return inst().state_->registered_modules[name].instance;
}

Module* ModuleManager::requireInstanceRaw(const char* name) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    auto& module = inst().state_->registered_modules[name];
    if (module.instance) return module.instance;
    return module.creator();
}

void ModuleManager::insert(const char* name, Module* instance) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    EV_PARAM_CHECK(instance != nullptr, "module instance must not be null");
    inst().state_->registered_modules[name].instance = instance;
}

void ModuleManager::requireAll() {
    for (auto& entry : inst().state_->registered_modules) {
        if (!entry.second.instance && entry.second.creator) entry.second.instance = entry.second.creator();
    }
}

void ModuleManager::register_module(const char* name, creator_t c, exposer_t e) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    const bool hasCreator = c != nullptr;  // function pointers can't be printed by zeroerr
    EV_PARAM_CHECK(hasCreator, "module creator must not be null");
    auto p = inst().state_->registered_modules.find(name);
    if (inst().state_->plugin_registration_active_ && p != inst().state_->registered_modules.end()) {
        if (inst().state_->plugin_registration_error_.empty())
            inst().state_->plugin_registration_error_ = std::string("module name already registered: ") + name;
        return;
    }
    if (p == inst().state_->registered_modules.end()) {
        inst().state_->registered_modules.insert(std::make_pair(std::string(name), State::ModuleInfo{c, e, nullptr}));
        if (inst().state_->plugin_registration_active_) inst().state_->plugin_registration_added_.emplace_back(name);
    } else {
        p->second = {c, e, nullptr};
    }
}

eve::Result<void> ModuleManager::beginPluginRegistration() {
    auto& manager = inst();
    if (manager.state_->plugin_registration_active_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "a plugin registration transaction is already active", {}, {}, "module"));
    manager.state_->plugin_registration_active_ = true;
    manager.state_->plugin_registration_added_.clear();
    manager.state_->plugin_registration_error_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ModuleManager::finishPluginRegistration(bool commit) {
    auto& manager = inst();
    if (!manager.state_->plugin_registration_active_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "no plugin registration transaction is active", {}, {}, "module"));

    const std::string error = manager.state_->plugin_registration_error_;
    if (!commit || !error.empty()) {
        for (const auto& name : manager.state_->plugin_registration_added_) {
            auto it = manager.state_->registered_modules.find(name);
            if (it == manager.state_->registered_modules.end()) continue;
            delete it->second.instance;
            manager.state_->registered_modules.erase(it);
        }
    }
    manager.state_->plugin_registration_active_ = false;
    manager.state_->plugin_registration_added_.clear();
    manager.state_->plugin_registration_error_.clear();
    if (!error.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, error, {}, {}, "module"));
    return eve::Result<void>::success(eve::Status::success(commit ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

Runtime* ModuleManager::runtime() { return inst().state_->active_runtime_; }

void ModuleManager::detach(Runtime* runtime) {
    if (inst().state_->active_runtime_ == runtime) inst().state_->active_runtime_ = nullptr;
}

ModuleRegister::ModuleRegister(const char* name, ModuleManager::creator_t c, ModuleManager::exposer_t e) {
    ModuleManager::register_module(name, c, e);
}


}  // namespace eve
