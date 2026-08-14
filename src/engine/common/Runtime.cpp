#include "common/Runtime.h"

#include "common/Module.h"

#include <algorithm>
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

SQUserPointer objectIdentity(const HSQOBJECT& object) {
    return reinterpret_cast<SQUserPointer>(object._unVal.pRefCounted);
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
    : vm_(std::make_unique<ssq::VM>(stackSize, libraries)) {}

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

void Runtime::shutdown() noexcept {
    if (shutting_down_ || stopped_) return;
    shutting_down_ = true;
    unloadAll();
    ModuleManager::detach(this);
    while (true) {
        auto it = std::find(runtime_stack.begin(), runtime_stack.end(), this);
        if (it == runtime_stack.end()) break;
        runtime_stack.erase(it);
    }
    classes_.clear();
    class_owners_.clear();
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
ssq::Table Runtime::table(const char* name) const { return ssq::Table(vm_->find(name)); }

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
        fail(ScriptStage::Compile, sourceName, id, error);
    }
}

Runtime::ScriptId Runtime::compileFile(const std::string& path) {
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
        fail(ScriptStage::Compile, path, id, error);
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
        vm_->run(*record.compiled);
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

ssq::Class Runtime::findClass(const std::string& name) const { return vm_->findClass(name.c_str()); }

void Runtime::notifyLifecycle(const ScriptInfo& info) noexcept {
    if (!lifecycle_handler_) return;
    try {
        lifecycle_handler_(info);
    } catch (...) {
    }
}

[[noreturn]] void Runtime::fail(ScriptStage stage, const std::string& source, ScriptId id,
                                const std::exception& error) {
    ScriptException wrapped(stage, source, id, error.what());
    if (error_handler_) {
        try {
            error_handler_(wrapped);
        } catch (...) {
        }
    }
    throw wrapped;
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
    StackGuard stack(*const_cast<Runtime*>(this));
    HSQUIRRELVM squirrel = handle();
    sq_pushobject(squirrel, cls.getRaw());
    sq_pushnull(squirrel);
    while (SQ_SUCCEEDED(sq_next(squirrel, -2))) {
        if (sq_gettype(squirrel, -2) == OT_STRING) {
            const SQChar* memberName = nullptr;
            if (SQ_SUCCEEDED(sq_getstring(squirrel, -2, &memberName)) && memberName) {
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
                info.members.push_back(std::move(member));
            }
        }
        sq_pop(squirrel, 2);
    }
    std::sort(info.members.begin(), info.members.end(),
              [](const ReflectedMember& a, const ReflectedMember& b) { return a.name < b.name; });
    return info;
}

}  // namespace eve
