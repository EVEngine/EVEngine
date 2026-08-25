#pragma once

#include "common/Export.h"
#include "common/ScriptError.h"
#include "common/ScriptModule.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdio>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve {

/**
 * @brief Shortest round-trip float formatting (portable).
 *
 * std::to_chars(float/double) is unavailable on Apple platforms with an iOS
 * deployment target below 16.3, so reflection/editor code must not use it.
 * snprintf with max_digits10 guarantees round-tripping at the cost of not
 * being the shortest spelling.
 */
inline std::string reflectedFloatString(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g",
                  std::numeric_limits<double>::max_digits10, value);
    return buf;
}

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

/** @brief Runtime value kind read from a live script instance slot. */
enum class EVENGINE_API ReflectedValueKind : uint8_t {
    None = 0,     /**< @brief Missing / null slot. */
    Bool = 1,     /**< @brief OT_BOOL. */
    Integer = 2,  /**< @brief OT_INTEGER. */
    Float = 3,    /**< @brief OT_FLOAT. */
    String = 4,   /**< @brief OT_STRING. */
    Array = 5,    /**< @brief OT_ARRAY (not yet editable). */
    Table = 6,    /**< @brief OT_TABLE (not yet editable). */
    Instance = 7, /**< @brief Nested script instance (not yet editable). */
    Other = 8,    /**< @brief Any other slot kind. */
};

/**
 * @brief Typed snapshot of one live instance property.
 *
 * `writeProperty()` keeps the script slot's own type, so the numeric member
 * used to build the value only matters when the slot is null.
 */
struct EVENGINE_API ReflectedValue {
    ReflectedValueKind kind = ReflectedValueKind::None;
    bool boolean = false;
    int64_t integer = 0;
    double floating = 0.0;
    std::string text;

    /** @brief True when the slot exists and is non-null. */
    bool empty() const noexcept { return kind == ReflectedValueKind::None; }
    bool asBool() const noexcept { return boolean; }
    int64_t asInt() const noexcept { return integer; }
    double asFloat() const noexcept { return floating; }
    const std::string& asString() const noexcept { return text; }
};

struct EVENGINE_API ReflectedMember {
    std::string name;
    ssq::Type type = ssq::Type::NULLPTR;
    bool method = false;
    std::vector<ReflectedAttribute> attributes;
    ReflectedValue value;  /**< @brief Live value; empty for class-level reflection. */

    /** @brief Looks up an attribute by name; nullptr when absent. */
    const ReflectedAttribute* findAttribute(const std::string& name) const noexcept;
    /** @brief Numeric attribute value; `def` when missing or not numeric. */
    float attrFloat(const std::string& name, float def = 0.f) const noexcept;
    /** @brief Boolean attribute value; `def` when missing or not boolean. */
    bool attrBool(const std::string& name, bool def = false) const noexcept;
    /** @brief Raw string attribute value; `def` when missing. */
    std::string attrString(const std::string& name, const std::string& def = {}) const;
    /** @brief Comma-separated string attribute split into trimmed options. */
    std::vector<std::string> attrOptions(const std::string& name) const;
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

        int top() const noexcept { return static_cast<int>(top_); }
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
    /** @brief Script module provider registry and dependency resolver for this VM. */
    script::ScriptModuleResolver& scriptModules() noexcept;
    /** @brief Const script module resolver for diagnostics and graph inspection. */
    const script::ScriptModuleResolver& scriptModules() const noexcept;
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
    /**
     * @brief Creates a live instance of a script class (default constructor).
     * @param name   Class name (must be non-empty).
     * @param source Optional source label used in errors.
     * @return A rooted instance; the caller's ssq::Object releases it on destruction.
     * @throws ScriptException when the class is missing or the constructor fails.
     */
    ssq::Object createInstance(const std::string& name, const std::string& source = {});
    /**
     * @brief Inspects a live instance: own + inherited members with current values.
     * @param instance Live script instance.
     */
    std::vector<ReflectedMember> reflectInstance(const ssq::Object& instance) const;
    /**
     * @brief Reads one property of a live instance.
     * @return Value snapshot; empty when the slot is missing.
     */
    ReflectedValue readProperty(const ssq::Object& instance,
                                const std::string& name) const;
    /**
     * @brief Writes one property of a live instance.
     *
     * The existing slot type is preserved (bool stays bool, integer stays
     * integer, float stays float, string stays string); null slots take the
     * type of the incoming value.
     * @return False when the member does not exist or is a method.
     */
    bool writeProperty(const ssq::Object& instance, const std::string& name,
                       const ReflectedValue& value) const;
    /**
     * @brief Reads a live object-valued property (nested instance).
     * @return The rooted nested instance; an empty object when the slot is
     *         missing or not an instance.
     */
    ssq::Object readObjectProperty(const ssq::Object& instance,
                                   const std::string& name) const;

    // ---- array member editing ---------------------------------------------
    /** @brief Element count of an array member (0 when missing/not an array). */
    size_t arraySize(const ssq::Object& instance, const std::string& name) const;
    /** @brief Element value of an array member; empty when out of range. */
    ReflectedValue arrayGet(const ssq::Object& instance, const std::string& name,
                            size_t index) const;
    /** @brief Replaces one element of an array member. */
    bool arraySet(const ssq::Object& instance, const std::string& name, size_t index,
                  const ReflectedValue& value) const;
    /** @brief Appends an element to an array member. */
    bool arrayAppend(const ssq::Object& instance, const std::string& name,
                     const ReflectedValue& value) const;
    /** @brief Removes one element of an array member (shifts the rest). */
    bool arrayRemove(const ssq::Object& instance, const std::string& name,
                     size_t index) const;

    // ---- table member editing ---------------------------------------------
    /** @brief String keys of a table member. */
    std::vector<std::string> tableKeys(const ssq::Object& instance,
                                       const std::string& name) const;
    /** @brief Value of a table member key; empty when missing. */
    ReflectedValue tableGet(const ssq::Object& instance, const std::string& name,
                            const std::string& key) const;
    /** @brief Sets a key of a table member (creates the key when missing). */
    bool tableSet(const ssq::Object& instance, const std::string& name,
                  const std::string& key, const ReflectedValue& value) const;
    /** @brief Removes a key from a table member. */
    bool tableRemove(const ssq::Object& instance, const std::string& name,
                     const std::string& key) const;
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
    /**
     * @brief Scans the root table for script classes and refreshes reflection.
     *
     * Picks up classes defined through dofile()/compilestring() outside the
     * Runtime API and re-inspects classes replaced by hot reload. Returns the
     * number of classes that were (re)scanned.
     */
    size_t scanClasses();
    /** @brief Finds a script class by name (throws if it does not exist). */
    ssq::Class findClass(const std::string& name) const;
    /** @brief Name of the script class of a live instance ("" when unknown). */
    std::string classNameOf(const ssq::Object& instance) const;

    /** @brief Replaces the handler invoked for script errors at the Runtime boundary. */
    void setErrorHandler(ErrorHandler handler) { error_handler_ = std::move(handler); }
    /** @brief Replaces the handler notified on every script lifecycle change. */
    void setLifecycleHandler(LifecycleHandler handler) { lifecycle_handler_ = std::move(handler); }

private:
    struct ScriptRecord;

    /** @brief Installs the runtime error hook so failures capture the live stack. */
    void installErrorHandler();

    std::unique_ptr<ssq::VM> vm_;
    std::unique_ptr<script::ScriptModuleResolver> script_modules_;
    std::unordered_map<ScriptId, std::unique_ptr<ScriptRecord>> scripts_;
    std::unordered_map<std::string, ReflectedClass> classes_;
    std::unordered_map<std::string, ScriptId> class_owners_;
    std::unordered_map<std::string, SQUserPointer> class_identities_;
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
    /** @brief Member metadata of one class (own slots only, no instance values). */
    std::vector<ReflectedMember> collectClassMembers(const ssq::Class& cls) const;
};

}  // namespace eve
