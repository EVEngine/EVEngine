#include "common/Runtime.h"

#include "common/Assert.h"
#include "common/Module.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace eve {
namespace {

thread_local std::vector<Runtime*> runtime_stack;

const char* stageName(ScriptStage stage) {
    switch (stage) {
        case ScriptStage::Compile: return "compile";
        case ScriptStage::Execute: return "execute";
        case ScriptStage::Reflect: return "reflect";
        case ScriptStage::Unload: return "unload";
        case ScriptStage::Shutdown: return "shutdown";
    }
    return "unknown";
}

std::string valueString(HSQUIRRELVM vm, SQInteger index) {
    switch (sq_gettype(vm, index)) {
        case OT_NULL: return "null";
        case OT_BOOL: {
            SQBool value = SQFalse;
            return SQ_SUCCEEDED(sq_getbool(vm, index, &value)) && value ? "true" : "false";
        }
        case OT_INTEGER: {
            SQInteger value = 0;
            if (SQ_SUCCEEDED(sq_getinteger(vm, index, &value))) return std::to_string(value);
            break;
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            if (SQ_SUCCEEDED(sq_getfloat(vm, index, &value))) {
                // Shortest round-trip representation (0.1 stays "0.1", not
                // "0.100000001"): attribute metadata round-trips exactly.
                char buf[32];
                const std::to_chars_result res =
                    std::to_chars(buf, buf + sizeof(buf), value);
                if (res.ec == std::errc()) return std::string(buf, res.ptr);
                std::ostringstream out;
                out << value;
                return out.str();
            }
            break;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            if (SQ_SUCCEEDED(sq_getstring(vm, index, &value)) && value) return value;
            break;
        }
        default: break;
    }
    return {};
}

ReflectedValue valueFromStack(HSQUIRRELVM vm, SQInteger index) {
    ReflectedValue out;
    switch (sq_gettype(vm, index)) {
        case OT_BOOL: {
            SQBool value = SQFalse;
            if (SQ_SUCCEEDED(sq_getbool(vm, index, &value))) {
                out.kind = ReflectedValueKind::Bool;
                out.boolean = value != SQFalse;
            }
            break;
        }
        case OT_INTEGER: {
            SQInteger value = 0;
            if (SQ_SUCCEEDED(sq_getinteger(vm, index, &value))) {
                out.kind = ReflectedValueKind::Integer;
                out.integer = value;
            }
            break;
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            if (SQ_SUCCEEDED(sq_getfloat(vm, index, &value))) {
                out.kind = ReflectedValueKind::Float;
                out.floating = value;
            }
            break;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            if (SQ_SUCCEEDED(sq_getstring(vm, index, &value)) && value) {
                out.kind = ReflectedValueKind::String;
                out.text = value;
            }
            break;
        }
        case OT_ARRAY: out.kind = ReflectedValueKind::Array; break;
        case OT_TABLE: out.kind = ReflectedValueKind::Table; break;
        case OT_INSTANCE: out.kind = ReflectedValueKind::Instance; break;
        case OT_NULL: break;
        default: out.kind = ReflectedValueKind::Other; break;
    }
    return out;
}

/** Converts a reflected value to a 64-bit integer (used for integer slots). */
int64_t valueToInteger(const ReflectedValue& value) {
    switch (value.kind) {
        case ReflectedValueKind::Integer: return value.integer;
        case ReflectedValueKind::Float: return static_cast<int64_t>(value.floating);
        case ReflectedValueKind::Bool: return value.boolean ? 1 : 0;
        case ReflectedValueKind::String: {
            char* end = nullptr;
            const long long parsed = std::strtoll(value.text.c_str(), &end, 10);
            return (end && end != value.text.c_str()) ? static_cast<int64_t>(parsed) : 0;
        }
        default: return 0;
    }
}

/** Converts a reflected value to a double (used for float slots). */
double valueToDouble(const ReflectedValue& value) {
    switch (value.kind) {
        case ReflectedValueKind::Integer: return static_cast<double>(value.integer);
        case ReflectedValueKind::Float: return value.floating;
        case ReflectedValueKind::Bool: return value.boolean ? 1.0 : 0.0;
        case ReflectedValueKind::String: {
            char* end = nullptr;
            const double parsed = std::strtod(value.text.c_str(), &end);
            return (end && end != value.text.c_str()) ? parsed : 0.0;
        }
        default: return 0.0;
    }
}

/** Converts a reflected value to a boolean (used for bool slots). */
bool valueToBool(const ReflectedValue& value) {
    switch (value.kind) {
        case ReflectedValueKind::Bool: return value.boolean;
        case ReflectedValueKind::Integer: return value.integer != 0;
        case ReflectedValueKind::Float: return value.floating != 0.0;
        case ReflectedValueKind::String:
            return value.text == "true" || value.text == "1";
        default: return false;
    }
}

/** Converts a reflected value to text (used for string slots). */
std::string valueToText(const ReflectedValue& value) {
    switch (value.kind) {
        case ReflectedValueKind::String: return value.text;
        case ReflectedValueKind::Bool: return value.boolean ? "true" : "false";
        case ReflectedValueKind::Integer: return std::to_string(value.integer);
        case ReflectedValueKind::Float: {
            char buf[32];
            const std::to_chars_result res =
                std::to_chars(buf, buf + sizeof(buf), value.floating);
            if (res.ec == std::errc()) return std::string(buf, res.ptr);
            return std::to_string(value.floating);
        }
        default: return {};
    }
}

/**
 * Pushes `value` converted to `currentType` (keeps the slot's own type);
 * falls back to the value's own kind for null/unsupported slots.
 */
void pushConvertedValue(HSQUIRRELVM v, SQObjectType currentType,
                        const ReflectedValue& value) {
    switch (currentType) {
        case OT_BOOL:
            sq_pushbool(v, valueToBool(value) ? SQTrue : SQFalse);
            return;
        case OT_INTEGER:
            sq_pushinteger(v, static_cast<SQInteger>(valueToInteger(value)));
            return;
        case OT_FLOAT:
            sq_pushfloat(v, static_cast<SQFloat>(valueToDouble(value)));
            return;
        case OT_STRING: {
            const std::string text = valueToText(value);
            sq_pushstring(v, text.c_str(), static_cast<SQInteger>(text.size()));
            return;
        }
        default: break;
    }
    switch (value.kind) {
        case ReflectedValueKind::Bool:
            sq_pushbool(v, value.asBool() ? SQTrue : SQFalse);
            return;
        case ReflectedValueKind::Integer:
            sq_pushinteger(v, SQInteger(value.asInt()));
            return;
        case ReflectedValueKind::Float:
            sq_pushfloat(v, SQFloat(value.asFloat()));
            return;
        case ReflectedValueKind::String:
            sq_pushstring(v, value.asString().c_str(), -1);
            return;
        default:
            sq_pushnull(v);
            return;
    }
}

/** Member metadata of the class at `classIndex` (own slots only, no values). */
std::vector<ReflectedMember> collectClassMembersFromStack(HSQUIRRELVM squirrel,
                                                          SQInteger classIndex) {
    std::vector<ReflectedMember> members;
    const SQInteger top = sq_gettop(squirrel);
    sq_push(squirrel, classIndex);  // duplicate the class for iteration
    sq_pushnull(squirrel);
    while (SQ_SUCCEEDED(sq_next(squirrel, -2))) {
        if (sq_gettype(squirrel, -2) != OT_STRING) {
            sq_pop(squirrel, 2);
            continue;
        }
        const SQChar* memberName = nullptr;
        if (!SQ_SUCCEEDED(sq_getstring(squirrel, -2, &memberName)) || !memberName) {
            sq_pop(squirrel, 2);
            continue;
        }
        ReflectedMember member;
        member.name = memberName;
        member.type = static_cast<ssq::Type>(sq_gettype(squirrel, -1));
        member.method = member.type == ssq::Type::CLOSURE ||
                        member.type == ssq::Type::NATIVECLOSURE;

        // sq_getattributes consumes the key at the stack top and replaces it
        // with the attribute table (or null).
        const SQInteger memberTop = sq_gettop(squirrel);
        sq_push(squirrel, -2);
        if (SQ_SUCCEEDED(sq_getattributes(squirrel, -5)) &&
            sq_gettype(squirrel, -1) == OT_TABLE) {
            sq_pushnull(squirrel);
            while (SQ_SUCCEEDED(sq_next(squirrel, -2))) {
                if (sq_gettype(squirrel, -2) == OT_STRING) {
                    const SQChar* attributeName = nullptr;
                    if (SQ_SUCCEEDED(sq_getstring(squirrel, -2, &attributeName)) &&
                        attributeName) {
                        ReflectedAttribute attribute;
                        attribute.name = attributeName;
                        attribute.type =
                            static_cast<ssq::Type>(sq_gettype(squirrel, -1));
                        attribute.value = valueString(squirrel, -1);
                        member.attributes.push_back(std::move(attribute));
                    }
                }
                sq_pop(squirrel, 2);
            }
        }
        sq_settop(squirrel, memberTop);
        std::sort(member.attributes.begin(), member.attributes.end(),
                  [](const ReflectedAttribute& a, const ReflectedAttribute& b) {
                      return a.name < b.name;
                  });
        members.push_back(std::move(member));
        sq_pop(squirrel, 2);
    }
    sq_settop(squirrel, top);
    std::sort(members.begin(), members.end(),
              [](const ReflectedMember& a, const ReflectedMember& b) {
                  return a.name < b.name;
              });
    return members;
}

SQUserPointer objectIdentity(const HSQOBJECT& object) {
    return reinterpret_cast<SQUserPointer>(object._unVal.pRefCounted);
}

std::unique_ptr<ssq::VM> createVm(size_t stackSize, ssq::Libs::Flag libraries) {
    // Validate before constructing the VM: with assertions compiled out the
    // check must not be the only thing standing between a bad stack size and a
    // half-constructed runtime.
    EV_PARAM_CHECK(stackSize > 0, "Runtime stack size must be positive");
    return std::make_unique<ssq::VM>(stackSize, libraries);
}

/** Runtime error hook: captures message + full stack before it unwinds. */
SQInteger scriptErrorHook(HSQUIRRELVM vm) {
    script::ScriptErrorContext ctx = script::captureScriptError(vm);
    script::setLastScriptError(vm, std::move(ctx));
    return 0;
}

}  // namespace

struct Runtime::ScriptRecord {
    ScriptInfo info;
    std::string source_text;
    bool from_file = false;
    std::unique_ptr<ssq::Script> compiled;
    std::unordered_map<std::string, ssq::Class> class_objects;
};

ScriptException::ScriptException(ScriptStage stage, std::string source, uint64_t scriptId,
                                 const std::string& message)
    : std::runtime_error([&] {
          std::ostringstream out;
          out << "Script " << stageName(stage);
          if (!source.empty()) out << " failed in '" << source << "'";
          if (scriptId != 0) out << " [id=" << scriptId << "]";
          out << ": " << message;
          return out.str();
      }()),
      stage_(stage), source_(std::move(source)), script_id_(scriptId) {}

ScriptException::ScriptException(ScriptStage stage, std::string source, uint64_t scriptId,
                                 const script::ScriptErrorContext& context)
    : std::runtime_error([&] {
          std::ostringstream out;
          out << "Script " << stageName(stage);
          if (!source.empty()) out << " failed in '" << source << "'";
          if (scriptId != 0) out << " [id=" << scriptId << "]";
          out << ": " << script::formatScriptError(context);
          return out.str();
      }()),
      stage_(stage),
      source_(std::move(source)),
      script_id_(scriptId),
      line_(context.line),
      column_(context.column),
      function_(context.function),
      stack_trace_(script::formatStackTrace(context.stack)),
      reported_(context.reported) {
    if (!stack_trace_.empty() && stack_trace_.back() == '\n') stack_trace_.pop_back();
}

const ReflectedAttribute* ReflectedMember::findAttribute(const std::string& name) const noexcept {
    for (const auto& attribute : attributes) {
        if (attribute.name == name) return &attribute;
    }
    return nullptr;
}

float ReflectedMember::attrFloat(const std::string& name, float def) const noexcept {
    const ReflectedAttribute* attribute = findAttribute(name);
    if (!attribute) return def;
    char* end = nullptr;
    const double value = std::strtod(attribute->value.c_str(), &end);
    if (end && end != attribute->value.c_str() && *end == '\0')
        return static_cast<float>(value);
    return def;
}

bool ReflectedMember::attrBool(const std::string& name, bool def) const noexcept {
    const ReflectedAttribute* attribute = findAttribute(name);
    if (!attribute) return def;
    if (attribute->value == "true" || attribute->value == "1" ||
        attribute->value == "yes")
        return true;
    if (attribute->value == "false" || attribute->value == "0" ||
        attribute->value == "no")
        return false;
    return def;
}

std::string ReflectedMember::attrString(const std::string& name,
                                        const std::string& def) const {
    const ReflectedAttribute* attribute = findAttribute(name);
    return attribute ? attribute->value : def;
}

std::vector<std::string> ReflectedMember::attrOptions(const std::string& name) const {
    std::vector<std::string> options;
    const ReflectedAttribute* attribute = findAttribute(name);
    if (!attribute) return options;
    std::string rest = attribute->value;
    while (true) {
        const size_t comma = rest.find(',');
        std::string piece = comma == std::string::npos ? rest : rest.substr(0, comma);
        const size_t first = piece.find_first_not_of(" \t\r\n");
        const size_t last = piece.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
            options.push_back(piece.substr(first, last - first + 1));
        if (comma == std::string::npos) break;
        rest = rest.substr(comma + 1);
    }
    return options;
}

Runtime::StackGuard::StackGuard(Runtime& runtime) noexcept
    : vm_(runtime.handle()), top_(vm_ ? sq_gettop(vm_) : 0) {}

Runtime::StackGuard::~StackGuard() {
    if (vm_) sq_settop(vm_, top_);
}

Runtime::StackGuard::StackGuard(StackGuard&& other) noexcept
    : vm_(std::exchange(other.vm_, nullptr)), top_(other.top_) {}

Runtime::StackGuard& Runtime::StackGuard::operator=(StackGuard&& other) noexcept {
    if (this == &other) return *this;
    if (vm_) sq_settop(vm_, top_);
    vm_ = std::exchange(other.vm_, nullptr);
    top_ = other.top_;
    return *this;
}

Runtime::Scope::Scope(Runtime& runtime) : runtime_(&runtime) { runtime_stack.push_back(&runtime); }

Runtime::Scope::~Scope() {
    if (!runtime_) return;
    auto it = std::find(runtime_stack.rbegin(), runtime_stack.rend(), runtime_);
    if (it != runtime_stack.rend()) runtime_stack.erase(std::next(it).base());
}

Runtime::Scope::Scope(Scope&& other) noexcept
    : runtime_(std::exchange(other.runtime_, nullptr)) {}

Runtime::Runtime(size_t stackSize, ssq::Libs::Flag libraries)
    : vm_(createVm(stackSize, libraries)) {
    installErrorHandler();
}

Runtime::~Runtime() { shutdown(); }

void Runtime::initialize() {
    if (initialized_) return;
    auto scope = enter();
    auto stack = guard();
    try {
        ModuleManager::expose(*this);
        initialized_ = true;
    } catch (const std::exception& error) {
        ModuleManager::detach(this);
        fail(ScriptStage::Execute, "<module reflection>", 0, error);
    }
}

void Runtime::installErrorHandler() {
    if (!vm_) return;
    HSQUIRRELVM vm = handle();
    sq_newclosure(vm, &scriptErrorHook, 0);  // pushes the closure
    sq_seterrorhandler(vm);  // pops the closure
}

void Runtime::shutdown() noexcept {
    if (shutting_down_ || stopped_) return;
    shutting_down_ = true;
    unloadAll();
    ModuleManager::detach(this);
    script::clearLastScriptError(handle());
    while (true) {
        auto it = std::find(runtime_stack.begin(), runtime_stack.end(), this);
        if (it == runtime_stack.end()) break;
        runtime_stack.erase(it);
    }
    classes_.clear();
    class_owners_.clear();
    class_identities_.clear();
    initialized_ = false;
    vm_.reset();
    stopped_ = true;
    shutting_down_ = false;
}

bool Runtime::initialized() const noexcept { return initialized_; }
ssq::VM& Runtime::vm() noexcept { return *vm_; }
const ssq::VM& Runtime::vm() const noexcept { return *vm_; }
HSQUIRRELVM Runtime::handle() const noexcept { return vm_ ? vm_->getHandle() : nullptr; }
ssq::Table Runtime::root() const { return ssq::Table(static_cast<const ssq::Object&>(*vm_)); }
ssq::Table Runtime::table(const char* name) const {
    const bool validName = name != nullptr && name[0] != '\0';
    EV_PARAM_CHECK(validName, "table name must not be null or empty");
    return ssq::Table(vm_->find(name));
}

Runtime* Runtime::current() noexcept {
    return runtime_stack.empty() ? nullptr : runtime_stack.back();
}

size_t Runtime::stackDepth() noexcept { return runtime_stack.size(); }

Runtime::ScriptId Runtime::compileSource(std::string source, std::string sourceName) {
    if (stopped_) throw ScriptException(ScriptStage::Compile, sourceName, 0, "runtime is shut down");
    auto scope = enter();
    auto stack = guard();
    const ScriptId id = next_script_id_++;
    auto record = std::make_unique<ScriptRecord>();
    record->info.id = id;
    record->info.source = sourceName;
    record->source_text = std::move(source);
    try {
        record->compiled =
            std::make_unique<ssq::Script>(vm_->compileSource(record->source_text.c_str(), sourceName.c_str()));
        auto [it, inserted] = scripts_.emplace(id, std::move(record));
        (void)inserted;
        notifyLifecycle(it->second->info);
        return id;
    } catch (const std::exception& error) {
        script::ScriptErrorContext ctx = compileErrorContext(error.what(), record->source_text);
        fail(ScriptStage::Compile, sourceName, id, std::move(ctx));
    }
}

Runtime::ScriptId Runtime::compileFile(const std::string& path) {
    const bool validPath = !path.empty();
    EV_PARAM_CHECK(validPath, "script file path must not be empty");
    if (stopped_) throw ScriptException(ScriptStage::Compile, path, 0, "runtime is shut down");
    auto scope = enter();
    auto stack = guard();
    const ScriptId id = next_script_id_++;
    auto record = std::make_unique<ScriptRecord>();
    record->info.id = id;
    record->info.source = path;
    record->source_text = path;
    record->from_file = true;
    try {
        record->compiled = std::make_unique<ssq::Script>(vm_->compileFile(path.c_str()));
        auto [it, inserted] = scripts_.emplace(id, std::move(record));
        (void)inserted;
        notifyLifecycle(it->second->info);
        return id;
    } catch (const std::exception& error) {
        script::ScriptErrorContext ctx = compileErrorContext(error.what(), {});
        fail(ScriptStage::Compile, path, id, std::move(ctx));
    }
}

const ScriptInfo& Runtime::execute(ScriptId id) {
    auto found = scripts_.find(id);
    if (found == scripts_.end())
        throw ScriptException(ScriptStage::Execute, {}, id, "unknown script");
    ScriptRecord& record = *found->second;
    if (!record.compiled || record.info.state == ScriptState::Unloaded)
        throw ScriptException(ScriptStage::Execute, record.info.source, id, "script is unloaded");

    auto scope = enter();
    auto stack = guard();
    const auto before = rootClasses();
    record.info.state = ScriptState::Running;
    record.info.error.clear();
    notifyLifecycle(record.info);
    try {
        // Drive the Squirrel call directly instead of ssq::VM::run(): when a
        // DevTool hook replaces the VM's error handler, ssq never populates its
        // stored RuntimeException and would dereference a null unique_ptr on
        // failure. The raw call reports through the installed handler (which
        // captures the live stack) and lets us throw an enriched ScriptException.
        HSQUIRRELVM vm = handle();
        sq_pushobject(vm, record.compiled->getRaw());
        sq_pushroottable(vm);
        const SQRESULT result = sq_call(vm, 1, SQFalse, SQTrue);
        if (SQ_FAILED(result)) {
            script::ScriptErrorContext ctx = script::takeLastScriptError(vm);
            if (ctx.empty()) ctx.message = "script error";
            try {
                discoverClasses(record, before);
            } catch (...) {
            }
            record.info.state = ScriptState::Failed;
            record.info.error = script::formatScriptError(ctx);
            notifyLifecycle(record.info);
            fail(ScriptStage::Execute, record.info.source, id, std::move(ctx));
        }
        discoverClasses(record, before);
        record.info.state = ScriptState::Loaded;
        notifyLifecycle(record.info);
        return record.info;
    } catch (const ScriptException&) {
        throw;
    } catch (const std::exception& error) {
        try {
            discoverClasses(record, before);
        } catch (...) {
        }
        record.info.state = ScriptState::Failed;
        record.info.error = error.what();
        notifyLifecycle(record.info);
        fail(ScriptStage::Execute, record.info.source, id, error);
    }
}

Runtime::ScriptId Runtime::runSource(std::string source, std::string sourceName) {
    const ScriptId id = compileSource(std::move(source), std::move(sourceName));
    execute(id);
    return id;
}

Runtime::ScriptId Runtime::runFile(const std::string& path) {
    const ScriptId id = compileFile(path);
    execute(id);
    return id;
}

const ReflectedClass& Runtime::reflectClass(const std::string& name, const std::string& source) {
    const bool validName = !name.empty();
    EV_PARAM_CHECK(validName, "reflected class name must not be empty");
    auto scope = enter();
    auto stack = guard();
    try {
        ssq::Class cls = vm_->findClass(name.c_str());
        classes_[name] = inspectClass(name, cls, source);
        return classes_.at(name);
    } catch (const std::exception& error) {
        fail(ScriptStage::Reflect, source.empty() ? name : source, 0, error);
    }
}

ssq::Object Runtime::createInstance(const std::string& name, const std::string& source) {
    const bool validName = !name.empty();
    EV_PARAM_CHECK(validName, "instance class name must not be empty");
    auto scope = enter();
    auto stack = guard();
    const std::string label = source.empty() ? name : source;
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);                          // [root]
    sq_pushstring(squirrel, name.c_str(), -1);           // [root, name]
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_CLASS) {
        sq_settop(squirrel, top);
        script::ScriptErrorContext ctx;
        ctx.message = "class not found: " + name;
        fail(ScriptStage::Reflect, label, 0, std::move(ctx));
    }
    sq_createinstance(squirrel, -1);                     // [root, class, instance]
    ssq::Object instance(squirrel);
    sq_getstackobj(squirrel, -1, &instance.getRaw());
    sq_addref(squirrel, &instance.getRaw());             // root the instance
    // Call the default constructor with `this` = the new instance.
    sq_pushobject(squirrel, instance.getRaw());          // [.., instance, instance]
    sq_pushstring(squirrel, "constructor", -1);          // [.., instance, instance, "constructor"]
    if (SQ_SUCCEEDED(sq_get(squirrel, -2)) &&
        (sq_gettype(squirrel, -1) == OT_CLOSURE ||
         sq_gettype(squirrel, -1) == OT_NATIVECLOSURE)) {
        // sq_call expects [closure, this, args...] with params including `this`.
        sq_pushobject(squirrel, instance.getRaw());      // [.., instance, ctor, this]
        if (SQ_FAILED(sq_call(squirrel, 1, SQTrue, SQTrue))) {
            script::ScriptErrorContext ctx = script::takeLastScriptError(squirrel);
            if (ctx.empty()) ctx.message = "constructor failed";
            sq_settop(squirrel, top);
            fail(ScriptStage::Reflect, label, 0, std::move(ctx));
        }
    }
    sq_settop(squirrel, top);
    return instance;
}

std::vector<ReflectedMember> Runtime::reflectInstance(const ssq::Object& instance) const {
    std::vector<ReflectedMember> members;
    if (instance.getType() != ssq::Type::INSTANCE) return members;
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);

    // Walk the instance's class chain (derived → base) and merge member
    // metadata; derived members win when a name is overridden.
    sq_pushobject(squirrel, instance.getRaw());
    if (SQ_FAILED(sq_getclass(squirrel, -1)) || sq_gettype(squirrel, -1) != OT_CLASS) {
        sq_settop(squirrel, top);
        return members;
    }
    while (sq_gettype(squirrel, -1) == OT_CLASS) {
        const SQInteger classTop = sq_gettop(squirrel);
        for (ReflectedMember& member : collectClassMembersFromStack(squirrel, -1)) {
            if (!member.method) {
                const SQInteger readTop = sq_gettop(squirrel);
                sq_pushobject(squirrel, instance.getRaw());
                sq_pushstring(squirrel, member.name.c_str(), -1);
                if (SQ_SUCCEEDED(sq_get(squirrel, -2)))
                    member.value = valueFromStack(squirrel, -1);
                sq_settop(squirrel, readTop);
            }
            auto existing =
                std::find_if(members.begin(), members.end(),
                             [&](const ReflectedMember& m) { return m.name == member.name; });
            if (existing != members.end())
                *existing = std::move(member);
            else
                members.push_back(std::move(member));
        }
        sq_settop(squirrel, classTop);
        // Move to the base class; a missing base ends the chain.
        if (!SQ_SUCCEEDED(sq_getbase(squirrel, -1)) ||
            sq_gettype(squirrel, -1) != OT_CLASS) {
            sq_settop(squirrel, top);
            break;
        }
        sq_remove(squirrel, -2);  // drop the derived class, keep the base on top
    }
    return members;
}

ReflectedValue Runtime::readProperty(const ssq::Object& instance,
                                     const std::string& name) const {
    if (name.empty()) return {};
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2))) {
        sq_settop(squirrel, top);
        return {};
    }
    ReflectedValue value = valueFromStack(squirrel, -1);
    sq_settop(squirrel, top);
    return value;
}

bool Runtime::writeProperty(const ssq::Object& instance, const std::string& name,
                            const ReflectedValue& value) const {
    if (name.empty() || value.empty()) return false;
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());   // [instance]
    sq_pushstring(squirrel, name.c_str(), -1);    // [instance, name]
    if (SQ_FAILED(sq_get(squirrel, -2))) {        // [instance, current]
        sq_settop(squirrel, top);
        return false;
    }
    const SQObjectType currentType = sq_gettype(squirrel, -1);
    sq_pop(squirrel, 1);                          // [instance]
    if (currentType == OT_CLOSURE || currentType == OT_NATIVECLOSURE ||
        currentType == OT_CLASS) {
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushstring(squirrel, name.c_str(), -1);    // [instance, name]
    switch (currentType) {
        case OT_BOOL:
            sq_pushbool(squirrel, valueToBool(value) ? SQTrue : SQFalse);
            break;
        case OT_INTEGER:
            sq_pushinteger(squirrel, static_cast<SQInteger>(valueToInteger(value)));
            break;
        case OT_FLOAT:
            sq_pushfloat(squirrel, static_cast<SQFloat>(valueToDouble(value)));
            break;
        case OT_STRING: {
            const std::string text = valueToText(value);
            sq_pushstring(squirrel, text.c_str(), static_cast<SQInteger>(text.size()));
            break;
        }
        default: {
            // Null/unsupported slot: take the type of the incoming value.
            switch (value.kind) {
                case ReflectedValueKind::Bool:
                    sq_pushbool(squirrel, value.asBool() ? SQTrue : SQFalse);
                    break;
                case ReflectedValueKind::Integer:
                    sq_pushinteger(squirrel, SQInteger(value.asInt()));
                    break;
                case ReflectedValueKind::Float:
                    sq_pushfloat(squirrel, SQFloat(value.asFloat()));
                    break;
                case ReflectedValueKind::String:
                    sq_pushstring(squirrel, value.asString().c_str(), -1);
                    break;
                default: sq_pushnull(squirrel); break;
            }
            break;
        }
    }
    // [instance, name, value] — sq_set pops the value and assigns the slot.
    const bool ok = SQ_SUCCEEDED(sq_set(squirrel, -3));
    sq_settop(squirrel, top);
    return ok;
}

ssq::Object Runtime::readObjectProperty(const ssq::Object& instance,
                                        const std::string& name) const {
    if (name.empty()) return {};
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_INSTANCE) {
        sq_settop(squirrel, top);
        return {};
    }
    ssq::Object out(squirrel);
    sq_getstackobj(squirrel, -1, &out.getRaw());
    sq_addref(squirrel, &out.getRaw());
    sq_settop(squirrel, top);
    return out;
}

size_t Runtime::arraySize(const ssq::Object& instance,
                          const std::string& name) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_ARRAY) {
        sq_settop(squirrel, top);
        return 0;
    }
    const SQInteger len = sq_getsize(squirrel, -1);
    sq_settop(squirrel, top);
    return len > 0 ? size_t(len) : 0;
}

ReflectedValue Runtime::arrayGet(const ssq::Object& instance,
                                 const std::string& name, size_t index) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_ARRAY) {
        sq_settop(squirrel, top);
        return {};
    }
    sq_pushinteger(squirrel, static_cast<SQInteger>(index));
    if (SQ_FAILED(sq_get(squirrel, -2))) {
        sq_settop(squirrel, top);
        return {};
    }
    const ReflectedValue value = valueFromStack(squirrel, -1);
    sq_settop(squirrel, top);
    return value;
}

bool Runtime::arraySet(const ssq::Object& instance, const std::string& name,
                       size_t index, const ReflectedValue& value) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());   // [instance]
    sq_pushstring(squirrel, name.c_str(), -1);    // [instance, name]
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_ARRAY) {   // [instance, array]
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushinteger(squirrel, static_cast<SQInteger>(index));  // [.., array, idx]
    if (SQ_FAILED(sq_get(squirrel, -2))) {        // [.., array, current]
        sq_settop(squirrel, top);
        return false;
    }
    const SQObjectType currentType = sq_gettype(squirrel, -1);
    sq_pop(squirrel, 1);                          // [.., array]
    sq_pushinteger(squirrel, static_cast<SQInteger>(index));  // [.., array, idx]
    pushConvertedValue(squirrel, currentType, value);          // [.., array, idx, val]
    const bool ok = SQ_SUCCEEDED(sq_set(squirrel, -3));
    sq_settop(squirrel, top);
    return ok;
}

bool Runtime::arrayAppend(const ssq::Object& instance, const std::string& name,
                          const ReflectedValue& value) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_ARRAY) {
        sq_settop(squirrel, top);
        return false;
    }
    pushConvertedValue(squirrel, OT_NULL, value);  // value on top
    const bool ok = SQ_SUCCEEDED(sq_arrayappend(squirrel, -2));
    sq_settop(squirrel, top);
    return ok;
}

bool Runtime::arrayRemove(const ssq::Object& instance, const std::string& name,
                          size_t index) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_ARRAY) {
        sq_settop(squirrel, top);
        return false;
    }
    // arr.remove(index) via the Squirrel array method.
    sq_pushstring(squirrel, "remove", -1);
    if (SQ_FAILED(sq_get(squirrel, -2))) {         // [.., array, remove]
        sq_settop(squirrel, top);
        return false;
    }
    sq_push(squirrel, -2);                          // [.., array, remove, this]
    sq_pushinteger(squirrel, static_cast<SQInteger>(index));  // [.., this, idx]
    const bool ok = SQ_SUCCEEDED(sq_call(squirrel, 2, SQFalse, SQTrue));
    sq_settop(squirrel, top);
    return ok;
}

std::vector<std::string> Runtime::tableKeys(const ssq::Object& instance,
                                            const std::string& name) const {
    std::vector<std::string> keys;
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return keys;
    }
    sq_pushnull(squirrel);
    while (SQ_SUCCEEDED(sq_next(squirrel, -2))) {
        if (sq_gettype(squirrel, -2) == OT_STRING) {
            const SQChar* key = nullptr;
            if (SQ_SUCCEEDED(sq_getstring(squirrel, -2, &key)) && key)
                keys.push_back(key);
        }
        sq_pop(squirrel, 2);
    }
    sq_settop(squirrel, top);
    std::sort(keys.begin(), keys.end());
    return keys;
}

ReflectedValue Runtime::tableGet(const ssq::Object& instance,
                                 const std::string& name,
                                 const std::string& key) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return {};
    }
    sq_pushstring(squirrel, key.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2))) {
        sq_settop(squirrel, top);
        return {};
    }
    const ReflectedValue value = valueFromStack(squirrel, -1);
    sq_settop(squirrel, top);
    return value;
}

bool Runtime::tableSet(const ssq::Object& instance, const std::string& name,
                       const std::string& key, const ReflectedValue& value) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushstring(squirrel, key.c_str(), -1);
    SQObjectType currentType = OT_NULL;
    if (SQ_SUCCEEDED(sq_get(squirrel, -2))) {   // [.., table, current]
        currentType = sq_gettype(squirrel, -1);
        sq_pop(squirrel, 1);                    // [.., table]
    }
    sq_pushstring(squirrel, key.c_str(), -1);   // [.., table, key]
    pushConvertedValue(squirrel, currentType, value);  // [.., table, key, val]
    bool ok = SQ_SUCCEEDED(sq_set(squirrel, -3));
    if (!ok) {
        // Squirrel 3.1's sq_set only updates existing keys; a missing key
        // needs sq_newslot to insert (v->Set raises "index does not exist").
        sq_settop(squirrel, top);
        sq_pushobject(squirrel, instance.getRaw());
        sq_pushstring(squirrel, name.c_str(), -1);
        if (SQ_FAILED(sq_get(squirrel, -2)) ||
            sq_gettype(squirrel, -1) != OT_TABLE) {
            sq_settop(squirrel, top);
            return false;
        }
        sq_pushstring(squirrel, key.c_str(), -1);
        pushConvertedValue(squirrel, OT_NULL, value);
        ok = SQ_SUCCEEDED(sq_newslot(squirrel, -3, SQFalse));
    }
    sq_settop(squirrel, top);
    return ok;
}

bool Runtime::tableRemove(const ssq::Object& instance, const std::string& name,
                          const std::string& key) const {
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    sq_pushstring(squirrel, name.c_str(), -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushstring(squirrel, key.c_str(), -1);
    const bool ok = SQ_SUCCEEDED(sq_deleteslot(squirrel, -2, SQFalse));
    sq_settop(squirrel, top);
    return ok;
}

Runtime::ScriptId Runtime::reload(ScriptId id) {
    auto found = scripts_.find(id);
    if (found == scripts_.end())
        throw ScriptException(ScriptStage::Compile, {}, id, "unknown script");
    const bool fromFile = found->second->from_file;
    const std::string input = found->second->source_text;
    const std::string source = found->second->info.source;
    unload(id);
    return fromFile ? runFile(input) : runSource(input, source);
}

bool Runtime::unload(ScriptId id) {
    auto found = scripts_.find(id);
    if (found == scripts_.end()) return false;
    ScriptRecord& record = *found->second;
    if (record.info.state == ScriptState::Unloaded) return false;
    auto scope = enter();
    auto stack = guard();
    record.info.state = ScriptState::Unloading;
    notifyLifecycle(record.info);
    try {
        for (const auto& pair : record.class_objects) {
            auto owner = class_owners_.find(pair.first);
            if (owner == class_owners_.end() || owner->second != id) continue;
            ssq::Object currentObject = vm_->find(pair.first.c_str());
            if (currentObject.getType() == ssq::Type::CLASS &&
                objectIdentity(currentObject.getRaw()) == objectIdentity(pair.second.getRaw())) {
                sq_pushroottable(handle());
                sq_pushstring(handle(), pair.first.c_str(), static_cast<SQInteger>(pair.first.size()));
                sq_rawdeleteslot(handle(), -2, SQFalse);
                sq_pop(handle(), 1);
            }
            classes_.erase(pair.first);
            class_owners_.erase(owner);
            class_identities_.erase(pair.first);
        }
        record.class_objects.clear();
        record.compiled.reset();
        record.info.state = ScriptState::Unloaded;
        notifyLifecycle(record.info);
        return true;
    } catch (const std::exception& error) {
        record.info.state = ScriptState::Failed;
        record.info.error = error.what();
        notifyLifecycle(record.info);
        fail(ScriptStage::Unload, record.info.source, id, error);
    }
}

void Runtime::unloadAll() noexcept {
    std::vector<ScriptId> ids;
    ids.reserve(scripts_.size());
    for (const auto& pair : scripts_) ids.push_back(pair.first);
    std::reverse(ids.begin(), ids.end());
    for (ScriptId id : ids) {
        try {
            unload(id);
        } catch (...) {
        }
    }
    scripts_.clear();
}

bool Runtime::contains(ScriptId id) const noexcept { return scripts_.count(id) != 0; }

const ScriptInfo* Runtime::script(ScriptId id) const noexcept {
    auto found = scripts_.find(id);
    return found == scripts_.end() ? nullptr : &found->second->info;
}

std::vector<ScriptInfo> Runtime::scripts() const {
    std::vector<ScriptInfo> result;
    result.reserve(scripts_.size());
    for (const auto& pair : scripts_) result.push_back(pair.second->info);
    std::sort(result.begin(), result.end(), [](const ScriptInfo& a, const ScriptInfo& b) {
        return a.id < b.id;
    });
    return result;
}

const ReflectedClass* Runtime::reflectedClass(const std::string& name) const noexcept {
    auto found = classes_.find(name);
    return found == classes_.end() ? nullptr : &found->second;
}

std::vector<ReflectedClass> Runtime::reflectedClasses() const {
    std::vector<ReflectedClass> result;
    result.reserve(classes_.size());
    for (const auto& pair : classes_) result.push_back(pair.second);
    std::sort(result.begin(), result.end(), [](const ReflectedClass& a, const ReflectedClass& b) {
        return a.name < b.name;
    });
    return result;
}

size_t Runtime::scanClasses() {
    auto scope = enter();
    auto stack = guard();
    const auto after = rootClasses();
    size_t scanned = 0;
    for (const auto& pair : after) {
        auto old = class_identities_.find(pair.first);
        if (old != class_identities_.end() && old->second == pair.second) continue;
        try {
            ssq::Class cls = vm_->findClass(pair.first.c_str());
            std::string source;
            auto clsInfo = classes_.find(pair.first);
            if (clsInfo != classes_.end()) source = clsInfo->second.source;
            if (source.empty()) source = "<runtime scan>";
            classes_[pair.first] = inspectClass(pair.first, cls, source);
            class_identities_[pair.first] = pair.second;
            if (!class_owners_.count(pair.first)) class_owners_[pair.first] = 0;
            ++scanned;
        } catch (...) {
            // Not a script class (e.g. native class): skip.
        }
    }
    // Resolve base names against the current root set (also covers classes
    // replaced by hot reload whose base relationship changed).
    for (const auto& pair : after) {
        auto info = classes_.find(pair.first);
        if (info == classes_.end()) continue;
        try {
            const ssq::Class cls = vm_->findClass(pair.first.c_str());
            StackGuard stackGuard(*this);
            sq_pushobject(handle(), cls.getRaw());
            if (SQ_SUCCEEDED(sq_getbase(handle(), -1)) &&
                sq_gettype(handle(), -1) == OT_CLASS) {
                HSQOBJECT baseObject;
                if (SQ_SUCCEEDED(sq_getstackobj(handle(), -1, &baseObject))) {
                    const SQUserPointer identity = objectIdentity(baseObject);
                    for (const auto& candidate : after) {
                        if (candidate.second == identity) {
                            info->second.base = candidate.first;
                            break;
                        }
                    }
                }
            }
        } catch (...) {
        }
    }
    return scanned;
}

ssq::Class Runtime::findClass(const std::string& name) const {
    const bool validName = !name.empty();
    EV_PARAM_CHECK(validName, "class name must not be empty");
    return vm_->findClass(name.c_str());
}

std::string Runtime::classNameOf(const ssq::Object& instance) const {
    if (instance.getType() != ssq::Type::INSTANCE) return {};
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, instance.getRaw());
    if (SQ_FAILED(sq_getclass(squirrel, -1)) ||
        sq_gettype(squirrel, -1) != OT_CLASS) {
        sq_settop(squirrel, top);
        return {};
    }
    HSQOBJECT classObject;
    sq_getstackobj(squirrel, -1, &classObject);
    sq_settop(squirrel, top);
    const SQUserPointer identity = objectIdentity(classObject);
    for (const auto& pair : rootClasses()) {
        if (pair.second == identity) return pair.first;
    }
    return {};
}

void Runtime::notifyLifecycle(const ScriptInfo& info) noexcept {
    if (!lifecycle_handler_) return;
    try {
        lifecycle_handler_(info);
    } catch (...) {
    }
}

script::ScriptErrorContext Runtime::compileErrorContext(const std::string& what,
                                                        const std::string& sourceText) {
    script::ScriptErrorContext ctx;
    ctx.message = what;
    if (!script::parseCompileError(what, &ctx.source, &ctx.line, &ctx.column, &ctx.message))
        return ctx;
    const std::string lineText = script::sourceLineText(sourceText, ctx.line);
    if (lineText.empty()) return ctx;
    std::string hint = std::to_string(ctx.line) + " | " + lineText;
    if (ctx.column > 0) {
        hint += "\n";
        const size_t caret = static_cast<size_t>(ctx.column > 1 ? ctx.column - 1 : 0);
        hint.append(caret, ' ');
        hint += '^';
    }
    ctx.hint = std::move(hint);
    return ctx;
}

[[noreturn]] void Runtime::fail(ScriptStage stage, const std::string& source, ScriptId id,
                                script::ScriptErrorContext context) {
    ScriptException wrapped(stage, source, id, context);
    if (error_handler_) {
        try {
            error_handler_(wrapped);
        } catch (...) {
        }
    }
    throw wrapped;
}

[[noreturn]] void Runtime::fail(ScriptStage stage, const std::string& source, ScriptId id,
                                const std::exception& error) {
    script::ScriptErrorContext ctx = script::takeLastScriptError(handle());
    if (ctx.empty()) ctx.message = error.what();
    fail(stage, source, id, std::move(ctx));
}

std::unordered_map<std::string, SQUserPointer> Runtime::rootClasses() const {
    std::unordered_map<std::string, SQUserPointer> result;
    StackGuard stack(*const_cast<Runtime*>(this));
    HSQUIRRELVM squirrel = handle();
    sq_pushroottable(squirrel);
    sq_pushnull(squirrel);
    while (SQ_SUCCEEDED(sq_next(squirrel, -2))) {
        if (sq_gettype(squirrel, -2) == OT_STRING && sq_gettype(squirrel, -1) == OT_CLASS) {
            const SQChar* name = nullptr;
            HSQOBJECT object;
            if (SQ_SUCCEEDED(sq_getstring(squirrel, -2, &name)) && name &&
                SQ_SUCCEEDED(sq_getstackobj(squirrel, -1, &object)))
                result[name] = objectIdentity(object);
        }
        sq_pop(squirrel, 2);
    }
    return result;
}

void Runtime::discoverClasses(
    ScriptRecord& record, const std::unordered_map<std::string, SQUserPointer>& before) {
    const auto after = rootClasses();
    for (const auto& pair : after) {
        auto old = before.find(pair.first);
        if (old != before.end() && old->second == pair.second) continue;
        ssq::Class cls = vm_->findClass(pair.first.c_str());
        record.class_objects.insert_or_assign(pair.first, cls);
        if (std::find(record.info.classes.begin(), record.info.classes.end(), pair.first) ==
            record.info.classes.end())
            record.info.classes.push_back(pair.first);
        classes_[pair.first] = inspectClass(pair.first, cls, record.info.source);
        class_owners_[pair.first] = record.info.id;
        class_identities_[pair.first] = pair.second;
    }
    std::sort(record.info.classes.begin(), record.info.classes.end());

    // Resolve base class names after every newly defined class is registered.
    for (const std::string& name : record.info.classes) {
        auto info = classes_.find(name);
        auto cls = record.class_objects.find(name);
        if (info == classes_.end() || cls == record.class_objects.end()) continue;
        StackGuard stack(*this);
        sq_pushobject(handle(), cls->second.getRaw());
        if (SQ_SUCCEEDED(sq_getbase(handle(), -1)) && sq_gettype(handle(), -1) == OT_CLASS) {
            HSQOBJECT baseObject;
            if (SQ_SUCCEEDED(sq_getstackobj(handle(), -1, &baseObject))) {
                const SQUserPointer identity = objectIdentity(baseObject);
                for (const auto& candidate : after) {
                    if (candidate.second == identity) {
                        info->second.base = candidate.first;
                        break;
                    }
                }
            }
        }
    }
}

ReflectedClass Runtime::inspectClass(const std::string& name, const ssq::Class& cls,
                                     const std::string& source) const {
    ReflectedClass info;
    info.name = name;
    info.source = source;
    info.members = collectClassMembers(cls);
    return info;
}

std::vector<ReflectedMember> Runtime::collectClassMembers(const ssq::Class& cls) const {
    std::vector<ReflectedMember> members;
    Runtime* self = const_cast<Runtime*>(this);
    auto scope = self->enter();
    auto stack = self->guard();
    HSQUIRRELVM squirrel = handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushobject(squirrel, cls.getRaw());
    members = collectClassMembersFromStack(squirrel, -1);
    sq_settop(squirrel, top);
    return members;
}

}  // namespace eve
