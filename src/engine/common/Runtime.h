#pragma once

#include "common/Export.h"
#include "common/ScriptError.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve {

enum class ScriptState {
    Compiled,
    Running,
    Loaded,
    Unloading,
    Unloaded,
    Failed,
};

enum class ScriptStage {
    Compile,
    Execute,
    Reflect,
    Unload,
    Shutdown,
};

/** @brief Exception raised at the public Runtime boundary. */
class EVENGINE_API ScriptException : public std::runtime_error {
public:
    ScriptException(ScriptStage stage, std::string source, uint64_t scriptId,
                    const std::string& message);
    /**
     * @brief Constructs from a captured script error context.
     * @param stage   Lifecycle stage that failed.
     * @param source  Source name of the failing script.
     * @param scriptId Id of the failing script (0 when unknown).
     * @param context Structured error payload (message, site, stack).
     */
    ScriptException(ScriptStage stage, std::string source, uint64_t scriptId,
                    const script::ScriptErrorContext& context);

    ScriptStage stage() const noexcept { return stage_; }
    const std::string& source() const noexcept { return source_; }
    uint64_t scriptId() const noexcept { return script_id_; }
    /** @brief True when the exception carries a script source line. */
    bool hasLocation() const noexcept { return line_ > 0; }
    /** @brief 1-based throw/compile site line; -1 when unknown. */
    int line() const noexcept { return line_; }
    /** @brief 1-based compile column; -1 when unknown. */
    int column() const noexcept { return column_; }
    /** @brief Function name at the throw site; empty when unknown. */
    const std::string& function() const noexcept { return function_; }
    /** @brief Multi-line call stack captured at the throw site. */
    const std::string& stackTrace() const noexcept { return stack_trace_; }
    /** @brief True when a reporter (e.g. DevTool hook) already handled it. */
    bool reported() const noexcept { return reported_; }

private:
    ScriptStage stage_;
    std::string source_;
    uint64_t script_id_;
    int line_ = -1;
    int column_ = -1;
    std::string function_;
    std::string stack_trace_;
    bool reported_ = false;
};

struct EVENGINE_API ReflectedAttribute {
    std::string name;
    ssq::Type type = ssq::Type::NULLPTR;
    std::string value;
};

struct EVENGINE_API ReflectedMember {
    std::string name;
    ssq::Type type = ssq::Type::NULLPTR;
    bool method = false;
    std::vector<ReflectedAttribute> attributes;
};

struct EVENGINE_API ReflectedClass {
    std::string name;
    std::string source;
    std::string base;
    std::vector<ReflectedMember> members;
};

struct EVENGINE_API ScriptInfo {
    uint64_t id = 0;
    std::string source;
    ScriptState state = ScriptState::Compiled;
    std::vector<std::string> classes;
    std::string error;
};

class EVENGINE_API Runtime {
public:
    using ScriptId = uint64_t;
    using ErrorHandler = std::function<void(const ScriptException&)>;
    using LifecycleHandler = std::function<void(const ScriptInfo&)>;

    /** @brief Restores the native Squirrel stack when a binding operation leaves scope. */
    class EVENGINE_API StackGuard {
    public:
        explicit StackGuard(Runtime& runtime) noexcept;
        ~StackGuard();
        StackGuard(StackGuard&& other) noexcept;
        StackGuard& operator=(StackGuard&& other) noexcept;
        StackGuard(const StackGuard&) = delete;
        StackGuard& operator=(const StackGuard&) = delete;

        int top() const noexcept { return top_; }
        void dismiss() noexcept { vm_ = nullptr; }

    private:
        HSQUIRRELVM vm_ = nullptr;
        SQInteger top_ = 0;
    };

    /** @brief Pushes this Runtime on the current thread's runtime stack. */
    class EVENGINE_API Scope {
    public:
        explicit Scope(Runtime& runtime);
        ~Scope();
        Scope(Scope&& other) noexcept;
        Scope& operator=(Scope&& other) = delete;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Runtime* runtime_ = nullptr;
    };

    /**
     * @brief Creates a script runtime with its own Squirrel VM.
     * @param stackSize  Initial Squirrel stack size in slots (must be > 0).
     * @param libraries  Squirrel standard libraries to register (default: all).
     */
    explicit Runtime(size_t stackSize = 2048, ssq::Libs::Flag libraries = ssq::Libs::ALL);
    ~Runtime();
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /** @brief Exposes registered engine modules into the script root table. Safe to call more than once. */
    void initialize();
    /** @brief Tears down the VM and detaches the runtime; idempotent and noexcept. */
    void shutdown() noexcept;
    /** @brief True once initialize() has completed successfully. */
    bool initialized() const noexcept;

    /** @brief Underlying SimpleSquirrel VM (requires a live, initialized runtime). */
    ssq::VM& vm() noexcept;
    const ssq::VM& vm() const noexcept;
    /** @brief Raw Squirrel VM handle; nullptr after shutdown. */
    HSQUIRRELVM handle() const noexcept;
    /** @brief Root script table of the VM. */
    ssq::Table root() const;
    /**
     * @brief Looks up a named global table.
     * @param name Name of the global table (must be non-null and non-empty).
     */
    ssq::Table table(const char* name) const;

    /** @brief RAII guard that restores the Squirrel stack top on scope exit. */
    StackGuard guard() noexcept { return StackGuard(*this); }
    /** @brief Pushes this runtime on the thread-local runtime stack (RAII pop). */
    Scope enter() { return Scope(*this); }
    /** @brief The runtime currently at the top of this thread's runtime stack, or nullptr. */
    static Runtime* current() noexcept;
    /** @brief Depth of the thread-local runtime stack. */
    static size_t stackDepth() noexcept;

    /**
     * @brief Compiles script source without running it.
     * @param source     Script source text.
     * @param sourceName Name used in errors and lifecycle events ("buffer" if omitted).
     * @return New script id.
     * @throws ScriptException on compile failure.
     */
    ScriptId compileSource(std::string source, std::string sourceName = "buffer");
    /**
     * @brief Compiles a script file without running it.
     * @param path File path (must be non-empty).
     * @return New script id.
     * @throws ScriptException on compile failure.
     */
    ScriptId compileFile(const std::string& path);
    /** @brief Runs a previously compiled script; throws ScriptException on failure. */
    const ScriptInfo& execute(ScriptId id);
    /** @brief Convenience: compileSource() then execute(). */
    ScriptId runSource(std::string source, std::string sourceName = "buffer");
    /** @brief Convenience: compileFile() then execute(). */
    ScriptId runFile(const std::string& path);
    /** @brief Alias of runFile(); reflects classes declared by the file. */
    ScriptId reflectFile(const std::string& path) { return runFile(path); }
    /**
     * @brief Reflects a class by name, storing its inspected members.
     * @param name   Class name (must be non-empty).
     * @param source Optional source label used in errors.
     */
    const ReflectedClass& reflectClass(const std::string& name,
                                       const std::string& source = {});
    /** @brief Recompiles and re-runs a script from its original source. */
    ScriptId reload(ScriptId id);
    /** @brief Unloads a script and removes its declared classes; false if unknown/unloaded. */
    bool unload(ScriptId id);
    /** @brief Unloads every script. */
    void unloadAll() noexcept;

    /** @brief True if a script with the given id is tracked. */
    bool contains(ScriptId id) const noexcept;
    /** @brief Script metadata for an id, or nullptr if unknown. */
    const ScriptInfo* script(ScriptId id) const noexcept;
    /** @brief Metadata for all tracked scripts, sorted by id. */
    std::vector<ScriptInfo> scripts() const;
    /** @brief Reflected class by name, or nullptr. */
    const ReflectedClass* reflectedClass(const std::string& name) const noexcept;
    /** @brief All reflected classes, sorted by name. */
    std::vector<ReflectedClass> reflectedClasses() const;
    /** @brief Finds a script class by name (throws if it does not exist). */
    ssq::Class findClass(const std::string& name) const;

    /** @brief Replaces the handler invoked for script errors at the Runtime boundary. */
    void setErrorHandler(ErrorHandler handler) { error_handler_ = std::move(handler); }
    /** @brief Replaces the handler notified on every script lifecycle change. */
    void setLifecycleHandler(LifecycleHandler handler) { lifecycle_handler_ = std::move(handler); }

private:
    struct ScriptRecord;

    /** @brief Installs the runtime error hook so failures capture the live stack. */
    void installErrorHandler();

    std::unique_ptr<ssq::VM> vm_;
    std::unordered_map<ScriptId, std::unique_ptr<ScriptRecord>> scripts_;
    std::unordered_map<std::string, ReflectedClass> classes_;
    std::unordered_map<std::string, ScriptId> class_owners_;
    ScriptId next_script_id_ = 1;
    bool initialized_ = false;
    bool shutting_down_ = false;
    bool stopped_ = false;
    ErrorHandler error_handler_;
    LifecycleHandler lifecycle_handler_;

    void notifyLifecycle(const ScriptInfo& info) noexcept;
    script::ScriptErrorContext compileErrorContext(const std::string& what,
                                                   const std::string& sourceText);
    [[noreturn]] void fail(ScriptStage stage, const std::string& source, ScriptId id,
                           const std::exception& error);
    [[noreturn]] void fail(ScriptStage stage, const std::string& source, ScriptId id,
                           script::ScriptErrorContext context);
    void discoverClasses(ScriptRecord& record,
                         const std::unordered_map<std::string, SQUserPointer>& before);
    std::unordered_map<std::string, SQUserPointer> rootClasses() const;
    ReflectedClass inspectClass(const std::string& name, const ssq::Class& cls,
                                const std::string& source) const;
};

}  // namespace eve
