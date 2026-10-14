#include "common/Module.h"
#include "ModuleRegistryState.h"
#include "common/Assert.h"
#include "common/ECS.h"
#include "common/Runtime.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace eve
{

struct ModuleBindAccess {
    static auto& modules() { return ModuleManager::inst().state_->registered_modules; }
};

namespace {

bool eveRawHas(HSQUIRRELVM vm, SQInteger tableIdx, const char* key) {
    if (tableIdx < 0) tableIdx = sq_gettop(vm) + tableIdx + 1;
    sq_pushstring(vm, key, -1);
    if (SQ_SUCCEEDED(sq_rawget(vm, tableIdx))) {
        sq_pop(vm, 1);
        return true;
    }
    return false;
}

ssq::Table eveTableFromStack(HSQUIRRELVM vm, SQInteger tableIdx) {
    ssq::Object wrapped(vm);
    sq_getstackobj(vm, tableIdx, &wrapped.getRaw());
    sq_addref(vm, &wrapped.getRaw());
    return ssq::Table(wrapped);
}

bool isPascalCaseName(const char* key) {
    return key && key[0] >= 'A' && key[0] <= 'Z';
}

bool isScriptEcsName(const char* key) {
    if (!key) return false;
    return std::strcmp(key, "Component") == 0 || std::strcmp(key, "Entity") == 0 ||
           std::strcmp(key, "EntityContainer") == 0 || std::strcmp(key, "System") == 0 ||
           std::strcmp(key, "ShaderSystem") == 0 || std::strcmp(key, "view") == 0 ||
           std::strcmp(key, "ecsReady") == 0;
}

void ensureScriptEcsInTable(ssq::Table& table) {
    HSQUIRRELVM vm = table.getHandle();
    sq_pushobject(vm, table.getRaw());
    const bool hasSystem = eveRawHas(vm, -1, "System");
    const bool ready = eveRawHas(vm, -1, "Component") && eveRawHas(vm, -1, "Entity") && hasSystem;
    if (!ready && hasSystem) {
        // The host-information module historically owns eve.System until the
        // script ECS is injected. Remove that one namespace collision so the
        // canonical script System class can be created exactly once.
        sq_pushstring(vm, "System", -1);
        sq_deleteslot(vm, -2, SQFalse);
    }
    sq_pop(vm, 1);
    if (!ready) exposeECS(table);
}

template <typename Info>
void runExposer(Info& info, ssq::Table& table, const char* name) {
    if (info.exposed || !info.exposer) {
        info.exposed = true;
        return;
    }
    info.exposed = true;
    const auto t0 = std::chrono::steady_clock::now();
    info.exposer(table);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms >= 20.0)
        std::fprintf(stderr, "[startup] bind %s: %.1f ms\n", name ? name : "?", ms);
    flushPostEcsHooks(table);
}

void bindNamedModule(const std::string& name, ssq::Table& table) {
    auto& modules = ModuleBindAccess::modules();
    auto it       = modules.find(name);
    if (it == modules.end()) return;
    runExposer(it->second, table, name.c_str());
}

void bindUntilKeyExists(HSQUIRRELVM vm, SQInteger tableIdx, const char* key, ssq::Table& table) {
    if (eveRawHas(vm, tableIdx, key)) return;
    auto& modules = ModuleBindAccess::modules();
    std::vector<std::string> pending;
    pending.reserve(modules.size());
    for (auto& entry : modules) {
        if (!entry.second.exposed && entry.second.exposer) pending.push_back(entry.first);
    }
    std::sort(pending.begin(), pending.end());
    for (const std::string& name : pending) {
        auto it = modules.find(name);
        if (it == modules.end() || it->second.exposed) continue;
        runExposer(it->second, table, name.c_str());
        if (eveRawHas(vm, tableIdx, key)) return;
    }
}

void bindOnDemand(HSQUIRRELVM vm, SQInteger tableIdx, const char* key) {
    if (!key || eveRawHas(vm, tableIdx, key)) return;
    ssq::Table table = eveTableFromStack(vm, tableIdx);
    if (isScriptEcsName(key)) {
        ensureScriptEcsInTable(table);
        return;
    }
    bindNamedModule(key, table);
    if (eveRawHas(vm, tableIdx, key)) return;
    // Nested types (WindowSettings, PlacementWorld, …) live on a parent
    // module's exposer. Scan remaining PascalCase misses only — lowercase
    // optional keys such as "dev" must not pull in every native class.
    if (isPascalCaseName(key)) bindUntilKeyExists(vm, tableIdx, key, table);
}

SQInteger eveTableGet(HSQUIRRELVM vm) {
    if (sq_gettop(vm) < 2 || sq_gettype(vm, 2) != OT_STRING)
        return sq_throwerror(vm, _SC("the index doesn't exist"));
    const SQChar* key = nullptr;
    if (SQ_FAILED(sq_getstring(vm, 2, &key)) || !key)
        return sq_throwerror(vm, _SC("the index doesn't exist"));
    const std::string name(key);
    // Functions declared as `function eve::name` execute with the eve table as
    // their environment. Preserve the historical root lookup of `eve` itself
    // instead of treating it as a native-class miss in the lazy delegate.
    if (name == "eve") {
        sq_push(vm, 1);
        return 1;
    }
    try {
        bindOnDemand(vm, 1, name.c_str());
    } catch (const std::exception& error) {
        return sq_throwerror(vm, error.what());
    }
    sq_push(vm, 1);
    sq_pushstring(vm, name.c_str(), static_cast<SQInteger>(name.size()));
    if (SQ_FAILED(sq_rawget(vm, -2))) {
        const std::string message = "the index '" + name + "' does not exist";
        return sq_throwerror(vm, message.c_str());
    }
    return 1;
}

void installLazyClassGet(ssq::Table& eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    sq_pushobject(vm, eveTable.getRaw());
    sq_newtable(vm);
    sq_pushstring(vm, _SC("_get"), -1);
    sq_newclosure(vm, &eveTableGet, 0);
    sq_newslot(vm, -3, SQFalse);
    sq_setdelegate(vm, -2);
    sq_pop(vm, 1);
}

}  // namespace


void ModuleManager::exposeVM(ssq::VM& vm) {
    for (auto& entry : inst().state_->registered_modules) entry.second.exposed = false;
    prepareEcsScriptInjection();

    ssq::Table table;
    // Runtime::initialize() guarantees expose() runs once, so addTable never
    // sees an existing "eve" slot. Avoid find()+catch here: ASYNCIFY-wrapped
    // frames can drop the catch and leak NotFoundException to the JS boundary.
    table = vm.addTable("eve");
    // Result/Status/Diagnostic projection is a common script contract, not a
    // domain-module convenience. Expose it before module constructors run so
    // every checked binding observes the same `eve.result` helper.
    script::exposeResultBindings(table);
    installLazyClassGet(table);
    table.addFunc("_bindAllNativeClasses", []() { return ModuleManager::exposeAllForCompatibility(); });
    table.addFunc("_ensureScriptECS", []() { return ModuleManager::ensureScriptEcs(); });
}

void ModuleManager::expose(Runtime& runtime) {
    inst().state_->active_runtime_ = &runtime;
    exposeVM(runtime.vm());
}

void ModuleManager::expose(ssq::VM& vm) {
    inst().state_->active_runtime_ = nullptr;
    exposeVM(vm);
}

ssq::VM* ModuleManager::vm() {
    Runtime* active = runtime();
    return active ? &active->vm() : nullptr;
}

int ModuleManager::expose_pending() {
    Runtime* active = inst().state_->active_runtime_;
    if (!active)
        return -1;
    int count = 0;
    try {
        auto scope = active->enter();
        auto stack = active->guard();
        ssq::Table table = active->table("eve");
        for (auto& D : inst().state_->registered_modules) {
            if (D.second.exposed || !D.second.exposer)
                continue;
            D.second.exposer(table);
            D.second.exposed = true;
            flushPostEcsHooks(table);
            ++count;
        }
    } catch (...) {
        return -1;
    }
    return count;
}

int ModuleManager::ensureScriptEcs() {
    Runtime* active = inst().state_->active_runtime_;
    if (!active) return -1;
    try {
        auto scope = active->enter();
        auto stack = active->guard();
        ssq::Table table = active->table("eve");
        ensureScriptEcsInTable(table);
    } catch (...) {
        return -1;
    }
    return 0;
}

int ModuleManager::exposeAllForCompatibility() {
    const int count = expose_pending();
    if (count < 0) return count;
    if (ensureScriptEcs() < 0) return -1;
    return count;
}

} // namespace eve
