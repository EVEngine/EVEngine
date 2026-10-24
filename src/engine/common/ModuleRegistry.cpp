#include "ModuleRegistryState.h"
#include "common/Assert.h"
#include "common/Module.h"

#include <algorithm>

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

Module* ModuleManager::find(const char* name, creator_t factory) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    auto& state   = *inst().state_;
    auto  current = state.registered_modules.find(name);
    if (current != state.registered_modules.end() && current->second.creator == factory)
        return current->second.instance;
    auto previous = state.plugin_registration_previous_.find(name);
    if (previous != state.plugin_registration_previous_.end() && previous->second.creator == factory)
        return previous->second.instance;
    for (auto& retired : state.retired_modules_)
        if (retired.first == name && retired.second.creator == factory) return retired.second.instance;
    return nullptr;
}

void ModuleManager::insert(const char* name, Module* instance, creator_t factory) {
    EV_PARAM_CHECK(name != nullptr, "module name must not be null");
    EV_PARAM_CHECK(instance != nullptr, "module instance must not be null");
    auto& state   = *inst().state_;
    auto  current = state.registered_modules.find(name);
    if (current != state.registered_modules.end() && current->second.creator == factory) {
        current->second.instance = instance;
        return;
    }
    auto previous = state.plugin_registration_previous_.find(name);
    if (previous != state.plugin_registration_previous_.end() && previous->second.creator == factory) {
        previous->second.instance = instance;
        return;
    }
    for (auto& retired : state.retired_modules_) {
        if (retired.first == name && retired.second.creator == factory) {
            retired.second.instance = instance;
            return;
        }
    }
    EV_ASSERT(false, "factory generation must be registered before instance creation");
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
        auto& state = *inst().state_;
        if (state.plugin_registration_owner_.empty() || p->second.owner != state.plugin_registration_owner_ ||
            std::find(state.plugin_registration_added_.begin(), state.plugin_registration_added_.end(), name) !=
                state.plugin_registration_added_.end()) {
            if (state.plugin_registration_error_.empty())
                state.plugin_registration_error_ = std::string("module name already registered: ") + name;
            return;
        }
        state.plugin_registration_previous_.emplace(name, p->second);
        state.plugin_registration_added_.emplace_back(name);
        p->second = {c, e, nullptr, false, state.plugin_registration_owner_};
        return;
    }
    if (p == inst().state_->registered_modules.end()) {
        inst().state_->registered_modules.insert(std::make_pair(
            std::string(name), State::ModuleInfo{c, e, nullptr, false, inst().state_->plugin_registration_owner_}));
        if (inst().state_->plugin_registration_active_) inst().state_->plugin_registration_added_.emplace_back(name);
    } else {
        p->second = {c, e, nullptr, false, {}};
    }
}

eve::Result<void> ModuleManager::beginPluginRegistration() { return beginPluginRegistration({}); }

eve::Result<void> ModuleManager::beginPluginRegistration(std::string owner) {
    auto& manager = inst();
    if (manager.state_->plugin_registration_active_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "a plugin registration transaction is already active", {}, {}, "module"));
    manager.state_->plugin_registration_active_ = true;
    manager.state_->plugin_registration_added_.clear();
    manager.state_->plugin_registration_error_.clear();
    manager.state_->plugin_registration_owner_ = std::move(owner);
    manager.state_->plugin_registration_previous_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ModuleManager::finishPluginRegistration(bool commit) {
    auto& manager = inst();
    if (!manager.state_->plugin_registration_active_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "no plugin registration transaction is active", {}, {}, "module"));

    std::string error = manager.state_->plugin_registration_error_;
    if (commit && error.empty() && !manager.state_->plugin_registration_owner_.empty()) {
        // Reserve retirement slots before publishing script bindings.
        manager.state_->retired_modules_.reserve(manager.state_->retired_modules_.size() +
                                                 manager.state_->registered_modules.size());
        auto published = publishPluginBindings();
        if (!published.ok()) error = published.status().describe();
    }
    if (!commit || !error.empty()) {
        for (const auto& name : manager.state_->plugin_registration_added_) {
            auto it = manager.state_->registered_modules.find(name);
            if (it == manager.state_->registered_modules.end()) continue;
            delete it->second.instance;
            manager.state_->registered_modules.erase(it);
        }
        for (auto& previous : manager.state_->plugin_registration_previous_)
            manager.state_->registered_modules.emplace(previous.first, std::move(previous.second));
    } else if (!manager.state_->plugin_registration_owner_.empty()) {
        for (auto& previous : manager.state_->plugin_registration_previous_)
            manager.state_->retired_modules_.emplace_back(previous.first, std::move(previous.second));
        // A module removed by the new binary disappears from the current registry,
        // while its captured constructors and live instances keep their generation.
        auto& modules = manager.state_->registered_modules;
        for (auto it = modules.begin(); it != modules.end();) {
            if (it->second.owner == manager.state_->plugin_registration_owner_ &&
                std::find(manager.state_->plugin_registration_added_.begin(),
                          manager.state_->plugin_registration_added_.end(),
                          it->first) == manager.state_->plugin_registration_added_.end()) {
                manager.state_->retired_modules_.emplace_back(it->first, std::move(it->second));
                it = modules.erase(it);
            } else {
                ++it;
            }
        }
    }
    manager.state_->plugin_registration_active_ = false;
    manager.state_->plugin_registration_added_.clear();
    manager.state_->plugin_registration_error_.clear();
    manager.state_->plugin_registration_previous_.clear();
    manager.state_->plugin_registration_owner_.clear();
    if (!error.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, error, {}, {}, "module"));
    return eve::Result<void>::success(eve::Status::success(commit ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

Runtime* ModuleManager::runtime() { return inst().state_->active_runtime_; }

void ModuleManager::detach(Runtime* runtime) {
    if (inst().state_->active_runtime_ == runtime) {
        inst().state_->active_runtime_ = nullptr;
        inst().state_->active_vm_      = nullptr;
    }
}

ModuleRegister::ModuleRegister(const char* name, ModuleManager::creator_t c, ModuleManager::exposer_t e) {
    ModuleManager::register_module(name, c, e);
}


}  // namespace eve
