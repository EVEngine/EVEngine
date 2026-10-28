#pragma once
#include "common/Export.h"
#include "common/Result.h"

#include <memory>
#include <string>

namespace ssq {
class VM;
class Table;
class Class;
}  // namespace ssq

#define SSQ_REG                            \
    static void expose(ssq::Table& table); \
    static void expose(ssq::Class& vm);

#define Module_REG(ModuleName)                                                                                     \
    SSQ_REG                                                                                                        \
    virtual std::string getName() const override { return name; }                                                  \
    /** @brief Return the manager-owned module instance for this registration.                                     \
     * @ownership Borrowed; ModuleManager retains ownership.                                                       \
     * @nullable No after successful registration; factory failure is an invariant.                                \
     * @lifetime Valid until module shutdown or registry teardown.                                                 \
     * @thread Main/composition thread only.                                                                       \
     * @reentrancy Must not re-enter module registration. */                                                       \
    [[nodiscard("module instance ownership must be retained or explicitly handled")]] static ModuleName* create(); \
    /** @brief Identifies this module's native type generation for script bindings. */                             \
    static auto        nativeModuleFactory() { return &ModuleName::create; }                                       \
    static const char* name

#define Module_IMPL(ModuleName, newExpr)                                                                   \
    ModuleRegister ModuleName##_register(ModuleName::name, (ModuleManager::creator_t)(ModuleName::create), \
                                         ModuleName::expose);                                              \
    ModuleName*    ModuleName::create() {                                                                  \
        auto  factory = reinterpret_cast<ModuleManager::creator_t>(&ModuleName::create);                \
        auto* p       = ModuleManager::find(name, factory);                                             \
        if (p) return static_cast<ModuleName*>(p);                                                      \
        ModuleName* n = newExpr;                                                                        \
        ModuleManager::insert(name, n, factory);                                                        \
        return n;                                                                                       \
    }                                                                                                      \
    const char* ModuleName::name = #ModuleName

#define getModInst(N,T) eve::ModuleManager::getInstance<N::T>(#T)
#define requireModInst(N,T) eve::ModuleManager::requireInstance<N::T>(#T)

namespace eve {

class Runtime;

/** @brief EVENGINE_API_FOUNDATION public API. */
class EVENGINE_API_FOUNDATION Module {
public:
    /** @brief Module. */
    virtual ~Module() {}
    /** @brief Returns the name. */
    virtual std::string getName() const = 0;
};

/** @brief EVENGINE_API_FOUNDATION public API. */
class EVENGINE_API_FOUNDATION ModuleManager {
public:
    typedef Module* (*creator_t)();
    typedef void    (*exposer_t)(ssq::Table&);

    /** @brief Singleton registry holding every registered engine module. */
    static ModuleManager& inst();

    /**
     * @brief Finds a live module instance by name, or nullptr.
     * @ownership Borrowed; ModuleManager owns the returned instance.
     * @nullable Yes when the name is unknown or the module is not created.
     * @lifetime Valid until module shutdown, unregister, or registry teardown;
     *           callers must not retain it across those boundaries.
     * @thread Main/composition thread; the registry is not concurrently mutable.
     * @reentrancy No module registration or shutdown may be re-entered by this call.
     */
    [[nodiscard("module lookup result must be checked before use")]] static Module* find(const char* name);
    /** @brief Looks up a factory's own generation, including retired plugin generations.
     * @ownership Borrowed; the registry owns the instance.
     * @nullable Yes before this factory creates its instance.
     * @lifetime Until process teardown; retired plugin instances remain resident.
     * @thread Main/composition thread only; does not invoke callbacks.
     */
    [[nodiscard]] static Module* find(const char* name, creator_t factory);
    /** @brief Stores a module instance under a name (used by Module_IMPL). */
    static void    insert(const char* name, Module* inst);
    /** @brief Stores an instance in its factory generation; transfers ownership to the registry.
     * @param name Registered name, borrowed for this call.
     * @param instance Owned instance, non-null, retained until registry teardown.
     * @param factory Identifies the active or retired generation.
     * @note Main/composition thread only; invokes no callbacks.
     */
    static void insert(const char* name, Module* instance, creator_t factory);

    /** @brief Registers a module factory (creator + script exposer) by name. */
    static void register_module(const char* name, creator_t c, exposer_t e);
    /** @brief Begins an atomic native-plugin module registration transaction. */
    [[nodiscard("plugin registration outcome must be checked")]] static eve::Result<void> beginPluginRegistration();
    /** @brief Begins registration/replacement of modules owned by a canonical plugin path.
     * @param owner Owning copy of the plugin identity; other owners cannot be replaced.
     * @return Conflict for nested registration. Rollback preserves the previous generation.
     * @note Main/script thread only; initializer must only register modules.
     * @cost Copies registration metadata once per library load/reload.
     */
    [[nodiscard]] static eve::Result<void> beginPluginRegistration(std::string owner);
    /** @brief Commits or rolls back the active plugin registration transaction.
     * @param commit Whether to keep newly registered modules.
     * @return Applied when committed, NoOp when explicitly rolled back, or a
     *         structured registration conflict/failure.
     */
    [[nodiscard("plugin registration outcome must be checked")]] static eve::Result<void> finishPluginRegistration(
        bool commit);
    /**
     * @brief Installs the `eve` table and defers native class method bindings.
     *
     * Result helpers and script ECS are bound immediately. Each module's
     * `expose(ssq::Table&)` (class + methods + nested types) runs on the first
     * script get of that class name, or of a nested class it owns. The default
     * unfiltered startup path eagerly completes all bindings while preserving
     * the canonical script ECS object identities.
     */
    static void expose(Runtime& runtime);
    // Compatibility for embedders that still own their ssq::VM directly.
    /** @brief Expose. */
    static void expose(ssq::VM& vm);
    // Expose modules registered after the initial expose() (e.g. plugins).
    /** @brief Expose pending. */
    static int  expose_pending();
    /**
     * @brief Active runtime associated with the last expose() call, or nullptr.
     * @ownership Borrowed; the embedding application owns the Runtime.
     * @nullable Yes before expose() and after detach().
     * @lifetime Valid only until the runtime is detached or destroyed.
     * @thread Main/composition thread; do not query while detach is running.
     * @reentrancy The result must not be used to re-enter module lifecycle callbacks.
     */
    static Runtime* runtime();
    /**
     * @brief Script VM of the last expose call, including raw-VM embedders, or nullptr.
     * @ownership Borrowed; Runtime or the embedding host owns the VM.
     * @nullable Yes before expose and after detach.
     * @lifetime Until owner destruction or detach; raw-VM embedders call detach(nullptr) before destruction.
     * @thread Main/script thread only.
     * @reentrancy Do not unload modules or detach the runtime while using the VM.
     */
    static ssq::VM* vm();
    /** @brief Clears the active runtime if it matches; called during shutdown. */
    static void detach(Runtime* runtime);

    /**
     * @brief Casts the registered instance of a module to T (may be null).
     * @ownership Borrowed; the manager owns the module instance.
     * @nullable Yes when no live instance is registered under `name`.
     * @lifetime Valid until module shutdown or registry teardown; do not retain across either.
     * @thread Main/composition thread only.
     * @reentrancy The returned instance must not be used to re-enter registry mutation.
     */
    template <typename T>
    [[nodiscard("module lookup result must be checked before use")]] static T* getInstance(const char* name) {
        return static_cast<T*>(getInstanceRaw(name));
    }

    /**
     * @brief Returns the live instance, creating it lazily through the registered factory.
     * @ownership Borrowed; ModuleManager owns the lazily-created instance.
     * @nullable No when the registration has a valid factory; invalid registration is an invariant failure.
     * @lifetime Valid until module shutdown or registry teardown; do not retain across either.
     * @thread Main/composition thread only.
     * @reentrancy Factory code must not recursively request the same module.
     */
    template <typename T>
    [[nodiscard("module instance ownership must be retained or explicitly handled")]] static T* requireInstance(
        const char* name) {
        return static_cast<T*>(requireInstanceRaw(name));
    }

    /** @brief Instantiates every registered module through its factory.
     *
     * Mirrors what load.nut's module-binding loop does for `eve run`, so
     * embedders that never run load.nut (e.g. the headless `eve mcp` host)
     * still get the capability providers (IEditorHost, IRenderCapture, ...).
     */
    static void requireAll();

protected:
    /** @brief Expose vm. */
    static void exposeVM(ssq::VM& vm);

private:
    static eve::Result<void> publishPluginBindings();
    static int ensureScriptEcs();
    static int exposeAllForCompatibility();

    // Keep unordered_map lookup and lazy-creation code out of the two public
    // templates. Every module instantiates only the pointer cast; the container
    // implementation is emitted once in Module.cpp.
    /** @brief Returns the manager-owned module stored under `name`.
     * @ownership Borrowed; ModuleManager retains ownership.
     * @nullable Yes when the module has no live instance.
     * @lifetime Valid until module shutdown or registry teardown.
     */
    static Module* getInstanceRaw(const char* name);
    /** @brief Returns or lazily creates the manager-owned module under `name`.
     * @ownership Borrowed; ModuleManager retains ownership.
     * @nullable No when the registered factory satisfies its invariant.
     * @lifetime Valid until module shutdown or registry teardown.
     */
    static Module* requireInstanceRaw(const char* name);

private:
    // The singleton owns its registry; only its implementation sees container types.
    struct State;
    std::unique_ptr<State> state_;

    ModuleManager();
    ~ModuleManager();
    ModuleManager(const ModuleManager&)            = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;
    ModuleManager(ModuleManager&&)                 = delete;
    ModuleManager& operator=(ModuleManager&&)      = delete;

    friend struct ModuleBindAccess;
};

/** @brief EVENGINE_API_FOUNDATION public API. */
struct EVENGINE_API_FOUNDATION ModuleRegister {
    /** @brief Module register. */
    ModuleRegister(const char* name, ModuleManager::creator_t c, ModuleManager::exposer_t e);
};


}  // namespace eve
