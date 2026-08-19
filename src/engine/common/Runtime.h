#pragma once

#include "common/Export.h"

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

/** Exception raised at the public Runtime boundary. */
class EVENGINE_API ScriptException : public std::runtime_error {
public:
    ScriptException(ScriptStage stage, std::string source, uint64_t scriptId,
                    const std::string& message);

    ScriptStage stage() const noexcept { return stage_; }
    const std::string& source() const noexcept { return source_; }
    uint64_t scriptId() const noexcept { return script_id_; }

private:
    ScriptStage stage_;
    std::string source_;
    uint64_t script_id_;
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

    /** Restores the native Squirrel stack when a binding operation leaves scope. */
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

    /** Pushes this Runtime on the current thread's runtime stack. */
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

    explicit Runtime(size_t stackSize = 2048, ssq::Libs::Flag libraries = ssq::Libs::ALL);
    ~Runtime();
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /** Exposes registered engine modules. Safe to call more than once. */
    void initialize();
    void shutdown() noexcept;
    bool initialized() const noexcept;

    ssq::VM& vm() noexcept;
    const ssq::VM& vm() const noexcept;
    HSQUIRRELVM handle() const noexcept;
    ssq::Table root() const;
    ssq::Table table(const char* name) const;

    StackGuard guard() noexcept { return StackGuard(*this); }
    Scope enter() { return Scope(*this); }
    static Runtime* current() noexcept;
    static size_t stackDepth() noexcept;

    ScriptId compileSource(std::string source, std::string sourceName = "buffer");
    ScriptId compileFile(const std::string& path);
    const ScriptInfo& execute(ScriptId id);
    ScriptId runSource(std::string source, std::string sourceName = "buffer");
    ScriptId runFile(const std::string& path);
    ScriptId reflectFile(const std::string& path) { return runFile(path); }
    const ReflectedClass& reflectClass(const std::string& name,
                                       const std::string& source = {});
    ScriptId reload(ScriptId id);
    bool unload(ScriptId id);
    void unloadAll() noexcept;

    bool contains(ScriptId id) const noexcept;
    const ScriptInfo* script(ScriptId id) const noexcept;
    std::vector<ScriptInfo> scripts() const;
    const ReflectedClass* reflectedClass(const std::string& name) const noexcept;
    std::vector<ReflectedClass> reflectedClasses() const;
    ssq::Class findClass(const std::string& name) const;

    void setErrorHandler(ErrorHandler handler) { error_handler_ = std::move(handler); }
    void setLifecycleHandler(LifecycleHandler handler) { lifecycle_handler_ = std::move(handler); }

private:
    struct ScriptRecord;

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
    [[noreturn]] void fail(ScriptStage stage, const std::string& source, ScriptId id,
                           const std::exception& error);
    void discoverClasses(ScriptRecord& record,
                         const std::unordered_map<std::string, SQUserPointer>& before);
    std::unordered_map<std::string, SQUserPointer> rootClasses() const;
    ReflectedClass inspectClass(const std::string& name, const ssq::Class& cls,
                                const std::string& source) const;
};

}  // namespace eve
