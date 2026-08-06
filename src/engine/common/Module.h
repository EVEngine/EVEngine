#pragma once
#include "common/Export.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ssq {
class VM;
class Table;
class Class;
}  // namespace ssq

#define SSQ_REG                            \
    static void expose(ssq::Table& table); \
    static void expose(ssq::Class& vm);

#define Module_REG(ModuleName) \
    SSQ_REG \
    virtual std::string getName() const { return name; } \
    static ModuleName* create(); \
    static const char* name

#define Module_IMPL(ModuleName, newExpr) \
    ModuleRegister ModuleName##_register(ModuleName::name, \
        (ModuleManager::creator_t)(ModuleName::create), ModuleName::expose); \
    ModuleName* ModuleName::create() { \
        auto* p = ModuleManager::find(name); \
        if (p) return static_cast<ModuleName*>(p); \
        ModuleName* n = newExpr; \
        ModuleManager::insert(name, n); \
        return n; \
    } \
    const char* ModuleName::name = #ModuleName

#define getModInst(N,T) eve::ModuleManager::getInstance<N::T>(#T)
#define requireModInst(N,T) eve::ModuleManager::requireInstance<N::T>(#T)

namespace eve {

class EVENGINE_API Module {
public:
    virtual ~Module() {}
    virtual std::string getName() const = 0;
};

class EVENGINE_API ModuleManager {
public:
    typedef Module* (*creator_t)();
    typedef void    (*exposer_t)(ssq::Table&);

    static ModuleManager& inst();

    static Module* find(const char* name);
    static void    insert(const char* name, Module* inst);

    static void register_module(const char* name, creator_t c, exposer_t e);
    static void expose(ssq::VM& vm);
    // Expose modules registered after the initial expose() (e.g. plugins).
    static int  expose_pending();
    static void set_vm(ssq::VM* vm);
    static ssq::VM* vm();

    template <typename T>
    static T* getInstance(const char* name) {
        return static_cast<T*>(inst().registered_modules[name].instance);
    }

    template <typename T>
    static T* requireInstance(const char* name) {
        auto& D = inst().registered_modules[name];
        if (D.instance) return static_cast<T*>(D.instance);
        return static_cast<T*>(D.creator());
    }

protected:
    struct ModuleInfo {
        creator_t creator = nullptr;
        exposer_t exposer = nullptr;
        Module*   instance = nullptr;
        bool      exposed  = false;
    };

    std::unordered_map<std::string, ModuleInfo> registered_modules;
    ssq::VM* active_vm_ = nullptr;
};

struct EVENGINE_API ModuleRegister {
    ModuleRegister(const char* name, ModuleManager::creator_t c, ModuleManager::exposer_t e);
};



}  // namespace eve
