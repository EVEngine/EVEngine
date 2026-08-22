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

class Runtime;

class EVENGINE_API Module {
public:
    virtual ~Module() {}
    virtual std::string getName() const = 0;
};

class EVENGINE_API ModuleManager {
public:
    typedef Module* (*creator_t)();
    typedef void    (*exposer_t)(ssq::Table&);

    /** @brief Singleton registry holding every registered engine module. */
    static ModuleManager& inst();

    /** @brief Finds a live module instance by name, or nullptr. */
    static Module* find(const char* name);
    /** @brief Stores a module instance under a name (used by Module_IMPL). */
    static void    insert(const char* name, Module* inst);

    /** @brief Registers a module factory (creator + script exposer) by name. */
    static void register_module(const char* name, creator_t c, exposer_t e);
    /** @brief Exposes every registered module into the given runtime's root table. */
    static void expose(Runtime& runtime);
    // Compatibility for embedders that still own their ssq::VM directly.
    static void expose(ssq::VM& vm);
    // Expose modules registered after the initial expose() (e.g. plugins).
    static int  expose_pending();
    /** @brief Active runtime associated with the last expose() call, or nullptr. */
    static Runtime* runtime();
    /** @brief Script VM of the active runtime, or nullptr. */
    static ssq::VM* vm();
    /** @brief Clears the active runtime if it matches; called during shutdown. */
    static void detach(Runtime* runtime);

    /** @brief Casts the registered instance of a module to T (may be null). */
    template <typename T>
    static T* getInstance(const char* name) {
        return static_cast<T*>(inst().registered_modules[name].instance);
    }

    /** @brief Returns the live instance, creating it lazily through the registered factory. */
    template <typename T>
    static T* requireInstance(const char* name) {
        auto& D = inst().registered_modules[name];
        if (D.instance) return static_cast<T*>(D.instance);
        return static_cast<T*>(D.creator());
    }

    /** @brief Instantiates every registered module through its factory.
     *
     * Mirrors what load.nut's module-binding loop does for `eve run`, so
     * embedders that never run load.nut (e.g. the headless `eve mcp` host)
     * still get the capability providers (IEditorHost, IRenderCapture, ...).
     */
    static void requireAll();

protected:
    static void exposeVM(ssq::VM& vm);

    struct ModuleInfo {
        creator_t creator = nullptr;
        exposer_t exposer = nullptr;
        Module*   instance = nullptr;
        bool      exposed  = false;
    };

    std::unordered_map<std::string, ModuleInfo> registered_modules;
    Runtime* active_runtime_ = nullptr;
};

struct EVENGINE_API ModuleRegister {
    ModuleRegister(const char* name, ModuleManager::creator_t c, ModuleManager::exposer_t e);
};



}  // namespace eve
