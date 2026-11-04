#include "devtools/Snapshot.hpp"

#include "common/Capability.h"
#include "common/IStateProvider.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <squirrel.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::dev {
namespace {

bool isEngineName(const std::string& name) {
    static const std::unordered_set<std::string> kSkip = {
        "eve",
        "win",
        "gfx",
        "event",
        "timer",
        "system",
        "math",
        "tf",
        "ui",
        "scene",
        "particles",
        "map",
        "gpgpu",
        "physics",
        "keyboard",
        "mouse",
        "touch",
        "sound",
        "audio",
        "model3d",
        "font",
        "thread",
        "fs",
        "hot",
        "config",
        "require",
        "path",
        "exports",
        "module",
        "console",
        "process",
        "stdin",
        "stdout",
        "stderr",
        "_version_",
        "ARGV",
        "eve_init",
        "eve_update",
        "eve_render",
        "eve_quit",
        "eve_reload",
        "eve_asset_reload",
        "watched_scripts",
        "track_script",
        "soft_reload_scripts",
        "poll_hot_reload",
        "file_exists",
        "format_script_error",
        "emit_loop_error",
        "clear_loop_error",
        "_loop_errors",
        "path_endswith",
        "normalize_path",
        "async_pump",
        "async_dispatch_event",
        "Promise",
        "setTimeout",
        "clearTimeout",
        "nextTick",
        "setImmediate",
        "has_dev",
        "dev_poll",
        "dev_should_update",
        "dev_notify_frame_done",
        "handle_dev_key",
    };
    return kSkip.count(name) > 0;
}

constexpr size_t kMaxGraphObjects = 65536;

StateValue objectReference(size_t id) {
    StateValue ref = StateValue::object();
    ref.set("$ref", StateValue::integer(static_cast<int64_t>(id)));
    return ref;
}

struct CaptureGraph {
    std::unordered_map<const void*, size_t> ids;
    StateValue                              objects = StateValue::array();
};

struct StackGuard {
    HSQUIRRELVM vm;
    SQInteger   top;
    explicit StackGuard(HSQUIRRELVM handle) : vm(handle), top(sq_gettop(handle)) {}
    ~StackGuard() { sq_settop(vm, top); }
};

const void* objectId(HSQUIRRELVM vm, SQInteger idx) {
    HSQOBJECT obj;
    sq_resetobject(&obj);
    sq_getstackobj(vm, idx, &obj);
    return reinterpret_cast<const void*>(obj._unVal.pRefCounted);
}

/** @brief True for the Squirrel kinds a state capture can represent without coercion. */
bool isPlainDataType(SQObjectType t) {
    return t == OT_NULL || t == OT_INTEGER || t == OT_FLOAT || t == OT_BOOL || t == OT_STRING || t == OT_TABLE ||
           t == OT_ARRAY;
}

const char* sqTypeName(SQObjectType t) {
    switch (t) {
        case OT_CLOSURE: return "function";
        case OT_NATIVECLOSURE: return "native function";
        case OT_CLASS: return "class";
        case OT_INSTANCE: return "class instance";
        case OT_USERDATA: return "userdata";
        case OT_USERPOINTER: return "userpointer";
        case OT_THREAD: return "thread";
        case OT_GENERATOR: return "generator";
        case OT_WEAKREF: return "weakref";
        default: return "value";
    }
}

/**
 * @brief Collects the values a capture had to leave out, with their path.
 *
 * A closure or class instance inside a container is not state: the reloaded script
 * recreates it. It used to abort the whole capture instead, so moving an entity from a
 * global (`hero <- Hero()`, silently filtered) into a list (`squad <- [Hero()]`) flipped
 * a working reload into a refused one -- and keeping an entity list is exactly what the
 * script ECS encourages. The recorded paths make the omission visible instead.
 *
 * @note The list is capped so one huge table cannot grow the snapshot without bound.
 */
struct SkippedValues {
    std::vector<std::string>* out = nullptr;

    static constexpr std::size_t kMaxRecorded = 64;

    void add(const std::string& path, SQObjectType type) {
        if (!out || out->size() >= kMaxRecorded) return;
        out->push_back(path + " (" + sqTypeName(type) + ")");
    }
};

StateValue sqToStateValue(HSQUIRRELVM vm, SQInteger idx, CaptureGraph& graph, int depth, SkippedValues& skipped,
                          const std::string& path) {
    if (idx < 0) idx = sq_gettop(vm) + idx + 1;

    const SQObjectType t = sq_gettype(vm, idx);
    size_t             nodeId = 0;
    if (t == OT_TABLE || t == OT_ARRAY) {
        const void* id = objectId(vm, idx);
        if (auto found = graph.ids.find(id); found != graph.ids.end()) return objectReference(found->second);
        if (graph.objects.arraySize() >= kMaxGraphObjects)
            throw std::runtime_error("snapshot object limit exceeded at " + path);
        nodeId = graph.objects.arraySize();
        graph.ids.emplace(id, nodeId);
        // Reserve identity before descending so back edges resolve immediately.
        graph.objects.pushBack(StateValue::null());
    }
    if (depth > 32) throw std::runtime_error("snapshot depth limit exceeded at " + path);
    switch (t) {
        // Not state at any depth. Capturing a class instance's members as a plain object
        // looked harmless, but restore pushes a table, so the value came back with a
        // different type than the reloaded script declared (`squad[0]` was an instance
        // before and a table after). Recorded here as well as at the callers, so no future
        // path can quietly coerce one.
        case OT_CLOSURE:
        case OT_NATIVECLOSURE:
        case OT_CLASS:
        case OT_INSTANCE:
        case OT_USERDATA:
        case OT_USERPOINTER:
        case OT_THREAD:
        case OT_GENERATOR:
        case OT_WEAKREF: {
            skipped.add(path.empty() ? std::string("<value>") : path, t);
            return StateValue::null();
        }
        case OT_NULL: return StateValue::null();
        case OT_INTEGER: {
            SQInteger v = 0;
            sq_getinteger(vm, idx, &v);
            return StateValue::integer(static_cast<int64_t>(v));
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            sq_getfloat(vm, idx, &v);
            return StateValue::number(static_cast<double>(v));
        }
        case OT_BOOL: {
            SQBool v = SQFalse;
            sq_getbool(vm, idx, &v);
            return StateValue::boolean(v != SQFalse);
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, idx, &s);
            return StateValue::string(s ? std::string(s) : std::string{});
        }
        case OT_ARRAY: {
            StateValue      arr  = StateValue::array();
            const SQInteger top  = sq_gettop(vm);
            const SQInteger size = sq_getsize(vm, idx);
            for (SQInteger i = 0; i < size; ++i) {
                sq_pushinteger(vm, i);
                if (SQ_SUCCEEDED(sq_get(vm, idx))) {
                    const SQObjectType et        = sq_gettype(vm, -1);
                    const std::string  childPath = path + "[" + std::to_string(i) + "]";
                    if (isPlainDataType(et)) {
                        arr.pushBack(sqToStateValue(vm, -1, graph, depth + 1, skipped, childPath));
                    } else {
                        // Recorded and replaced by null so the remaining indices keep their
                        // position (the same reason a failed sq_get pushes null below).
                        skipped.add(childPath, et);
                        arr.pushBack(StateValue::null());
                    }
                    sq_poptop(vm);
                } else {
                    arr.pushBack(StateValue::null());
                }
            }
            sq_settop(vm, top);
            StateValue node = StateValue::object();
            node.set("type", StateValue::string("array"));
            node.set("value", std::move(arr));
            graph.objects.at(nodeId) = std::move(node);
            return objectReference(nodeId);
        }
        case OT_TABLE: {
            StateValue      obj = StateValue::object();
            const SQInteger top = sq_gettop(vm);
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, idx))) {
                if (sq_gettype(vm, -2) == OT_STRING) {
                    const SQChar* key = nullptr;
                    sq_getstring(vm, -2, &key);
                    if (key) {
                        const SQObjectType vt = sq_gettype(vm, -1);
                        if (vt == OT_CLOSURE || vt == OT_NATIVECLOSURE || vt == OT_CLASS || vt == OT_INSTANCE ||
                            vt == OT_USERDATA || vt == OT_USERPOINTER || vt == OT_THREAD || vt == OT_GENERATOR ||
                            vt == OT_WEAKREF) {
                            // Left out on purpose (the reloaded script recreates it), but
                            // recorded so the omission is inspectable instead of silent.
                            skipped.add(path.empty() ? std::string(key) : path + "." + key, vt);
                        } else {
                            obj.set(std::string(key),
                                    sqToStateValue(vm, -1, graph, depth + 1, skipped,
                                                   path.empty() ? std::string(key) : path + "." + key));
                        }
                    }
                } else {
                    throw std::runtime_error("unsupported non-string table key at " + path);
                }
                sq_pop(vm, 2);
            }
            sq_settop(vm, top);
            StateValue node = StateValue::object();
            node.set("type", StateValue::string("table"));
            node.set("value", std::move(obj));
            graph.objects.at(nodeId) = std::move(node);
            return objectReference(nodeId);
        }
        default: return StateValue::null();
    }
}

bool pushStateValue(HSQUIRRELVM vm, const StateValue& var, int depth) {
    if (depth > 32) return false;
    switch (var.kind()) {
        case StateValue::Kind::Null: sq_pushnull(vm); return true;
        case StateValue::Kind::Bool: sq_pushbool(vm, var.asBool() ? SQTrue : SQFalse); return true;
        case StateValue::Kind::Int: sq_pushinteger(vm, static_cast<SQInteger>(var.asInt())); return true;
        case StateValue::Kind::Float: sq_pushfloat(vm, static_cast<SQFloat>(var.asDouble())); return true;
        case StateValue::Kind::String: {
            const std::string& s = var.asString();
            sq_pushstring(vm, s.c_str(), static_cast<SQInteger>(s.size()));
            return true;
        }
        case StateValue::Kind::Array:
            sq_newarray(vm, 0);
            for (size_t i = 0; i < var.arraySize(); ++i) {
                if (!pushStateValue(vm, var.at(i), depth + 1)) return false;
                sq_arrayappend(vm, -2);
            }
            return true;
        case StateValue::Kind::Object:
            sq_newtable(vm);
            for (const auto& key : var.keys()) {
                sq_pushstring(vm, key.c_str(), static_cast<SQInteger>(key.size()));
                if (!pushStateValue(vm, *var.find(key), depth + 1)) return false;
                sq_newslot(vm, -3, SQFalse);
            }
            return true;
    }
    return false;
}

// Encoded values are scalars or exact {"$ref": index} records. User tables
// live inside node.value, so a user member named "$ref" never becomes metadata.
size_t referenceId(const StateValue& value, size_t count) {
    const StateValue* id = value.find("$ref");
    if (!value.isObject() || value.keys().size() != 1 || !id || !id->isInt() || id->asInt() < 0 ||
        static_cast<uint64_t>(id->asInt()) >= count)
        throw std::runtime_error("invalid snapshot: malformed or dangling object reference");
    return static_cast<size_t>(id->asInt());
}

void validateGraphValue(const StateValue& value, size_t count) {
    if (value.isObject()) {
        referenceId(value, count);
    } else if (value.isArray()) {
        throw std::runtime_error("invalid snapshot: inline graph container");
    }
}

const StateValue& validateGraph(const StateValue& state) {
    const StateValue* objects = state.find("objects");
    const StateValue* roots   = state.find("roots");
    if (!objects || !objects->isArray() || objects->arraySize() > kMaxGraphObjects || !roots || !roots->isObject())
        throw std::runtime_error("invalid snapshot: expected roots and object graph");
    const size_t count = objects->arraySize();
    for (const auto& key : roots->keys()) validateGraphValue(*roots->find(key), count);
    for (size_t i = 0; i < count; ++i) {
        const auto& node  = objects->at(i);
        const auto* type  = node.find("type");
        const auto* value = node.find("value");
        if (!node.isObject() || node.keys().size() != 2 || !type || !type->isString() || !value)
            throw std::runtime_error("invalid snapshot: malformed object node " + std::to_string(i));
        if (type->asString() == "table" && value->isObject()) {
            for (const auto& key : value->keys()) validateGraphValue(*value->find(key), count);
        } else if (type->asString() == "array" && value->isArray()) {
            for (size_t j = 0; j < value->arraySize(); ++j) validateGraphValue(value->at(j), count);
        } else {
            throw std::runtime_error("invalid snapshot: object type/payload mismatch at " + std::to_string(i));
        }
    }
    return *objects;
}

void pushGraphValue(HSQUIRRELVM vm, const StateValue& value, SQInteger registry, size_t count) {
    if (value.isObject()) {
        sq_pushinteger(vm, static_cast<SQInteger>(referenceId(value, count)));
        if (SQ_FAILED(sq_rawget(vm, registry))) throw std::runtime_error("snapshot object lookup failed");
    } else if (!pushStateValue(vm, value, 0)) {
        throw std::runtime_error("invalid snapshot graph value");
    }
}

// The registry is a VM-owned array: it keeps every allocated container alive
// throughout both passes without borrowing raw object pointers across allocations.
SQInteger stageGraph(HSQUIRRELVM vm, const StateValue& state) {
    const auto& objects = validateGraph(state);
    sq_newarray(vm, 0);
    const SQInteger registry = sq_gettop(vm);
    for (size_t i = 0; i < objects.arraySize(); ++i) {
        if (objects.at(i).find("type")->asString() == "table")
            sq_newtable(vm);
        else
            sq_newarray(vm, 0);
        if (SQ_FAILED(sq_arrayappend(vm, registry))) throw std::runtime_error("snapshot object allocation failed");
    }
    for (size_t i = 0; i < objects.arraySize(); ++i) {
        sq_pushinteger(vm, static_cast<SQInteger>(i));
        if (SQ_FAILED(sq_rawget(vm, registry))) throw std::runtime_error("snapshot object lookup failed");
        const SQInteger container = sq_gettop(vm);
        const auto&     value     = *objects.at(i).find("value");
        if (value.isObject()) {
            for (const auto& key : value.keys()) {
                sq_pushstring(vm, key.c_str(), static_cast<SQInteger>(key.size()));
                pushGraphValue(vm, *value.find(key), registry, objects.arraySize());
                if (SQ_FAILED(sq_rawset(vm, container))) throw std::runtime_error("snapshot table population failed");
            }
        } else {
            for (size_t j = 0; j < value.arraySize(); ++j) {
                pushGraphValue(vm, value.at(j), registry, objects.arraySize());
                if (SQ_FAILED(sq_arrayappend(vm, container)))
                    throw std::runtime_error("snapshot array population failed");
            }
        }
        sq_poptop(vm);
    }
    return registry;
}

Poco::Dynamic::Var stateToVar(const StateValue& var) {
    switch (var.kind()) {
        case StateValue::Kind::Null: return Poco::Dynamic::Var();
        case StateValue::Kind::Bool: return Poco::Dynamic::Var(var.asBool());
        case StateValue::Kind::Int: return Poco::Dynamic::Var(static_cast<Poco::Int64>(var.asInt()));
        case StateValue::Kind::Float: return Poco::Dynamic::Var(var.asDouble());
        case StateValue::Kind::String: return Poco::Dynamic::Var(var.asString());
        case StateValue::Kind::Array: {
            Poco::JSON::Array::Ptr arr(new Poco::JSON::Array());
            for (size_t i = 0; i < var.arraySize(); ++i) arr->add(stateToVar(var.at(i)));
            return Poco::Dynamic::Var(arr);
        }
        case StateValue::Kind::Object: {
            Poco::JSON::Object::Ptr obj(new Poco::JSON::Object());
            for (const auto& key : var.keys()) obj->set(key, stateToVar(*var.find(key)));
            return Poco::Dynamic::Var(obj);
        }
    }
    return Poco::Dynamic::Var();
}

StateValue varToState(const Poco::Dynamic::Var& var) {
    if (var.isEmpty()) return StateValue::null();
    if (var.isBoolean()) return StateValue::boolean(var.convert<bool>());
    if (var.isInteger()) return StateValue::integer(static_cast<int64_t>(var.convert<Poco::Int64>()));
    if (var.isNumeric()) return StateValue::number(var.convert<double>());
    if (var.isString()) return StateValue::string(var.convert<std::string>());

    try {
        if (var.type() == typeid(Poco::JSON::Array::Ptr)) {
            StateValue             arr = StateValue::array();
            Poco::JSON::Array::Ptr a   = var.extract<Poco::JSON::Array::Ptr>();
            if (a)
                for (size_t i = 0; i < a->size(); ++i)
                    arr.pushBack(varToState(a->get(static_cast<unsigned int>(i))));
            return arr;
        }
    } catch (const Poco::BadCastException&) {
        // fall through
    }

    try {
        if (var.type() == typeid(Poco::JSON::Object::Ptr)) {
            StateValue              obj = StateValue::object();
            Poco::JSON::Object::Ptr o   = var.extract<Poco::JSON::Object::Ptr>();
            if (o)
                for (const auto& name : o->getNames()) obj.set(name, varToState(o->get(name)));
            return obj;
        }
    } catch (const Poco::BadCastException&) {
        // fall through
    }
    return StateValue::null();
}

}  // namespace

Snapshot& Snapshot::instance() {
    static Snapshot inst;
    return inst;
}

bool Snapshot::isEngineBinding(const std::string& name) { return isEngineName(name); }

void Snapshot::markRoot(std::string name) {
    if (name.empty()) return;
    unmarkTransientRoot(name);
    for (const auto& r : marked_) {
        if (r == name) return;
    }
    marked_.push_back(std::move(name));
}

void Snapshot::unmarkRoot(const std::string& name) {
    marked_.erase(std::remove(marked_.begin(), marked_.end(), name), marked_.end());
}

void Snapshot::markTransientRoot(std::string name) {
    if (name.empty()) return;
    unmarkRoot(name);
    for (const auto& r : transient_) {
        if (r == name) return;
    }
    transient_.push_back(std::move(name));
}

void Snapshot::unmarkTransientRoot(const std::string& name) {
    transient_.erase(std::remove(transient_.begin(), transient_.end(), name), transient_.end());
}

void Snapshot::clearRoots() {
    marked_.clear();
    transient_.clear();
}

std::vector<std::string> Snapshot::roots() const { return marked_; }

std::vector<std::string> Snapshot::transientRoots() const { return transient_; }

void Snapshot::setRootPolicies(std::vector<std::string> persistent, std::vector<std::string> transient) {
    marked_.clear();
    transient_.clear();
    for (auto& name : persistent) markRoot(std::move(name));
    for (auto& name : transient) markTransientRoot(std::move(name));
}

std::vector<std::string> Snapshot::resolveRoots(HSQUIRRELVM vm) const {
    if (!marked_.empty()) return marked_;
    std::vector<std::string> out;
    if (!vm) return out;

    const auto isTransient = [this](const std::string& name) {
        return std::find(transient_.begin(), transient_.end(), name) != transient_.end();
    };

    for (const char* pref : {"eve_state", "gameState", "state"}) {
        if (isTransient(pref)) continue;
        const SQInteger top = sq_gettop(vm);
        sq_pushroottable(vm);
        sq_pushstring(vm, pref, -1);
        if (SQ_SUCCEEDED(sq_get(vm, -2))) {
            const SQObjectType t = sq_gettype(vm, -1);
            if (t == OT_TABLE || t == OT_INSTANCE || t == OT_ARRAY) out.emplace_back(pref);
        }
        sq_settop(vm, top);
    }
    if (!out.empty()) return out;

    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    const SQInteger rootIdx = sq_gettop(vm);
    sq_pushnull(vm);
    while (SQ_SUCCEEDED(sq_next(vm, rootIdx))) {
        if (sq_gettype(vm, -2) == OT_STRING) {
            const SQChar* key = nullptr;
            sq_getstring(vm, -2, &key);
            if (key && !isEngineName(key) && !isTransient(key)) {
                const SQObjectType vt = sq_gettype(vm, -1);
                if (vt == OT_INTEGER || vt == OT_FLOAT || vt == OT_BOOL || vt == OT_STRING || vt == OT_TABLE ||
                    vt == OT_ARRAY) {
                    out.emplace_back(key);
                }
            }
        }
        sq_pop(vm, 2);
    }
    sq_settop(vm, top);
    return out;
}

bool Snapshot::captureState(HSQUIRRELVM vm, StateValue& out, std::string* error) const {
    if (!vm) {
        if (error) *error = "no vm";
        return false;
    }
    StackGuard stack(vm);
    try {
        // The omissions travel inside the captured state (`skipped`), so callers and the
        // persisted file see them without a second out-parameter.
        std::vector<std::string> skippedList;
        SkippedValues            skip{&skippedList};
        auto                     rootNames = resolveRoots(vm);

        // Live objects at the root (`hero <- Hero()`, an ECS world) look like state but
        // cannot be captured. The heuristic only *returns* plain-data roots, so without
        // this pass they would vanish without a trace. Function and class definitions are
        // ordinary script bindings -- naming every one of them would drown the list -- so
        // only instances/userdata are reported, and only when the heuristic picked the
        // roots (a caller who marked roots asked about those specific names).
        if (marked_.empty()) {
            const SQInteger scanTop = sq_gettop(vm);
            sq_pushroottable(vm);
            const SQInteger rootIdx = sq_gettop(vm);
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, rootIdx))) {
                if (sq_gettype(vm, -2) == OT_STRING) {
                    const SQChar* key = nullptr;
                    sq_getstring(vm, -2, &key);
                    const SQObjectType vt = sq_gettype(vm, -1);
                    if (key && (vt == OT_INSTANCE || vt == OT_USERDATA || vt == OT_USERPOINTER) && !isEngineName(key) &&
                        std::find(transient_.begin(), transient_.end(), key) == transient_.end()) {
                        skip.add(key, vt);
                    }
                }
                sq_pop(vm, 2);
            }
            sq_settop(vm, scanTop);
        }

        StateValue roots = StateValue::object();
        CaptureGraph graph;
        for (const auto& name : rootNames) {
            const SQInteger top = sq_gettop(vm);
            sq_pushroottable(vm);
            sq_pushstring(vm, name.c_str(), -1);
            if (SQ_SUCCEEDED(sq_get(vm, -2))) {
                const SQObjectType rt = sq_gettype(vm, -1);
                if (rt == OT_CLOSURE || rt == OT_NATIVECLOSURE || rt == OT_CLASS || rt == OT_INSTANCE ||
                    rt == OT_USERDATA || rt == OT_USERPOINTER || rt == OT_THREAD || rt == OT_GENERATOR ||
                    rt == OT_WEAKREF) {
                    // Reachable for an explicitly marked root: left out and reported, the
                    // same as an unserializable member. Failing here made a marked entity
                    // root refuse the whole reload.
                    skip.add(name, rt);
                } else {
                    roots.set(name, sqToStateValue(vm, -1, graph, 0, skip, name));
                }
            }
            sq_settop(vm, top);
        }

        StateValue native = StateValue::object();
        std::string nativeCaptureError;
        eve::cap::forEach<eve::caps::IStateProvider>([&](eve::caps::IStateProvider* p) {
            if (p->reloadPolicy() == eve::caps::StateReloadPolicy::Reset) return;
            auto captured = p->capture();
            if (captured.ok()) {
                native.set(p->stateKind(), toLegacyStateValue(std::move(captured).takeValue()));
            } else {
                nativeCaptureError +=
                    std::string(p->stateKind()) + ": " + captured.diagnostics().front().message() + "; ";
            }
        });
        if (!nativeCaptureError.empty()) {
            if (error) *error = nativeCaptureError;
            return false;
        }

        out = StateValue::object();
        out.set("version", StateValue::integer(3));
        out.set("objects", std::move(graph.objects));
        out.set("roots", std::move(roots));
        out.set("native", std::move(native));
        // Kept in the snapshot so the omission is visible to whoever reads it (the tool
        // response, the persisted eve_snapshot.json, ReloadSession's notice).
        // restoreState() ignores this key.
        if (!skippedList.empty()) {
            StateValue list = StateValue::array();
            for (const auto& entry : skippedList) list.pushBack(StateValue::string(entry));
            out.set("skipped", std::move(list));
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::string Snapshot::capture(HSQUIRRELVM vm, std::string* error) const {
    StateValue state;
    if (!captureState(vm, state, error)) return {};
    try {
        Poco::JSON::Object::Ptr doc = stateToVar(state).extract<Poco::JSON::Object::Ptr>();
        std::ostringstream      oss;
        doc->stringify(oss);
        return oss.str();
    } catch (const Poco::Exception& e) {
        if (error) *error = e.displayText();
        return {};
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return {};
    }
}

Result<StateValue> Snapshot::mergeDefaultsState(const StateValue& captured, const StateValue& defaults) const {
    try {
        const auto* oldVersion = captured.find("version");
        const auto* newVersion = defaults.find("version");
        if (!oldVersion || !oldVersion->isInt() || oldVersion->asInt() != 3 || !newVersion || !newVersion->isInt() ||
            newVersion->asInt() != 3)
            throw std::runtime_error("snapshot defaults merge requires version 3");
        const auto& oldObjects = validateGraph(captured);
        const auto& newObjects = validateGraph(defaults);
        const auto& oldRoots   = *captured.find("roots");
        const auto& newRoots   = *defaults.find("roots");
        using Pair             = std::tuple<size_t, size_t, bool>;
        std::vector<Pair>                  pairs;
        std::set<Pair>                     visited;
        std::unordered_map<size_t, size_t> mapping;
        auto pairValues = [&](const StateValue& oldValue, const StateValue& newValue, bool mergeFields) {
            if (!oldValue.isObject() || !newValue.isObject()) return;
            const size_t oldId = referenceId(oldValue, oldObjects.arraySize());
            const size_t newId = referenceId(newValue, newObjects.arraySize());
            if (oldObjects.at(oldId).find("type")->asString() != newObjects.at(newId).find("type")->asString()) return;
            // Captured identities win when new definitions have a different alias
            // topology. New fields referring to an existing node use its first match.
            mapping.emplace(newId, oldId);
            if (visited.emplace(oldId, newId, mergeFields).second) {
                if (pairs.size() >= kMaxGraphObjects)
                    throw std::runtime_error("snapshot correspondence limit exceeded during merge");
                pairs.emplace_back(oldId, newId, mergeFields);
            }
        };
        for (const auto& name : oldRoots.keys()) {
            if (const auto* value = newRoots.find(name)) pairValues(*oldRoots.find(name), *value, true);
        }
        // Establish correspondences before importing missing fields. Walk array
        // items for identity matching too, but retain their entire captured content.
        for (size_t i = 0; i < pairs.size(); ++i) {
            const auto [oldId, newId, mergeFields] = pairs[i];
            const auto& oldValue                   = *oldObjects.at(oldId).find("value");
            const auto& newValue                   = *newObjects.at(newId).find("value");
            if (oldValue.isObject()) {
                for (const auto& key : oldValue.keys()) {
                    if (const auto* value = newValue.find(key)) pairValues(*oldValue.find(key), *value, mergeFields);
                }
            } else {
                for (size_t j = 0; j < std::min(oldValue.arraySize(), newValue.arraySize()); ++j)
                    pairValues(oldValue.at(j), newValue.at(j), false);
            }
        }
        StateValue          result  = captured;
        StateValue&         objects = *result.find("objects");
        std::vector<size_t> imported;
        auto                remap = [&](const StateValue& value) {
            if (!value.isObject()) return value;
            const size_t id    = referenceId(value, newObjects.arraySize());
            auto         found = mapping.find(id);
            if (found == mapping.end()) {
                if (objects.arraySize() >= kMaxGraphObjects)
                    throw std::runtime_error("snapshot object limit exceeded during merge");
                found = mapping.emplace(id, objects.arraySize()).first;
                objects.pushBack(StateValue::null());
                imported.push_back(id);
            }
            return objectReference(found->second);
        };
        for (const auto& [oldId, newId, mergeFields] : pairs) {
            const auto& newValue = *newObjects.at(newId).find("value");
            if (!mergeFields || !newValue.isObject()) continue;
            for (const auto& key : newValue.keys()) {
                if (!objects.at(oldId).find("value")->find(key)) {
                    auto value = remap(*newValue.find(key));
                    // remap can grow objects; reacquire the owning node afterwards.
                    objects.at(oldId).find("value")->set(key, std::move(value));
                }
            }
        }
        // Import only nodes reachable from added fields. Reserve each identity
        // before following its edges, preserving forward links and cycles.
        for (size_t i = 0; i < imported.size(); ++i) {
            const size_t id    = imported[i];
            StateValue   node  = newObjects.at(id);
            auto&        value = *node.find("value");
            if (value.isObject()) {
                for (const auto& key : value.keys()) value.set(key, remap(*value.find(key)));
            } else {
                for (size_t j = 0; j < value.arraySize(); ++j) value.at(j) = remap(value.at(j));
            }
            objects.at(mapping.at(id)) = std::move(node);
        }
        return Result<StateValue>::success(std::move(result));
    } catch (const std::exception& error) {
        return Result<StateValue>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error.what()));
    }
}

bool Snapshot::restoreState(HSQUIRRELVM vm, const StateValue& state, std::string* error) const {
    if (!vm) {
        if (error) *error = "no vm";
        return false;
    }
    StackGuard stack(vm);
    try {
        const StateValue* roots = state.find("roots");
        if (!roots || !roots->isObject()) {
            if (error) *error = "invalid snapshot: missing roots";
            return false;
        }
        const StateValue* version = state.find("version");
        if (version && (!version->isInt() || version->asInt() < 1 || version->asInt() > 3)) {
            if (error) *error = "invalid snapshot: unsupported version";
            return false;
        }
        const bool      graphFormat = version && version->asInt() == 3;
        const SQInteger registry    = graphFormat ? stageGraph(vm, state) : 0;
        // Build every root before touching live state. A late depth/type failure
        // must not replace roots that happened to precede it in the document.
        sq_newtable(vm);
        const SQInteger staged = sq_gettop(vm);
        for (const auto& name : roots->keys()) {
            sq_pushstring(vm, name.c_str(), static_cast<SQInteger>(name.size()));
            if (graphFormat) {
                pushGraphValue(vm, *roots->find(name), registry, state.find("objects")->arraySize());
            } else if (!pushStateValue(vm, *roots->find(name), 0)) {
                if (error) *error = "failed to restore " + name + ": snapshot depth limit exceeded";
                return false;
            }
            if (SQ_FAILED(sq_rawset(vm, staged))) {
                if (error) *error = "failed to stage " + name;
                return false;
            }
        }

        const StateValue* native = state.find("native");
        if (native && !native->isObject()) {
            if (error) *error = "invalid snapshot: native must be an object";
            return false;
        }
        struct ProviderState {
            eve::caps::IStateProvider* provider;  // Borrowed for this synchronous restore.
            Value                      before;
            Value                      after;
            bool                       reset;
        };
        std::vector<ProviderState> providers;
        // Capture all affected providers before any provider mutates. Reset-policy
        // providers need a backup too: reset itself can fail after changing state.
        eve::cap::forEach<eve::caps::IStateProvider>([&](eve::caps::IStateProvider* p) {
            const bool        reset = p->reloadPolicy() == eve::caps::StateReloadPolicy::Reset;
            const StateValue* value = native ? native->find(p->stateKind()) : nullptr;
            if (!reset && !value) return;
            auto before = p->capture();
            if (!before.ok())
                throw std::runtime_error(std::string(p->stateKind()) +
                                         ": backup failed: " + before.diagnostics().front().message());
            providers.push_back(
                {p, std::move(before).takeValue(), value ? toCanonicalStateValue(*value) : Value::null(), reset});
        });
        size_t attempted = 0;
        try {
            for (auto& entry : providers) {
                ++attempted;
                auto result = entry.reset ? entry.provider->resetToDefaults() : entry.provider->restore(entry.after);
                if (!result.ok())
                    throw std::runtime_error(std::string(entry.provider->stateKind()) + ": " +
                                             result.diagnostics().front().message());
            }
        } catch (const std::exception& failure) {
            std::string message = failure.what();
            while (attempted > 0) {
                auto& entry = providers[--attempted];
                try {
                    auto rollback = entry.provider->restore(entry.before);
                    if (!rollback.ok())
                        message += "; rollback failed for " + std::string(entry.provider->stateKind()) + ": " +
                                   rollback.diagnostics().front().message();
                } catch (const std::exception& rollback) {
                    message +=
                        "; rollback failed for " + std::string(entry.provider->stateKind()) + ": " + rollback.what();
                }
            }
            if (error) *error = message;
            return false;
        }
        sq_pushroottable(vm);
        const SQInteger root = sq_gettop(vm);
        sq_pushnull(vm);
        while (SQ_SUCCEEDED(sq_next(vm, staged))) {
            // Raw writes cannot invoke script metamethods during commit.
            sq_rawset(vm, root);
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool Snapshot::restore(HSQUIRRELVM vm, const std::string& json, std::string* error) const {
    try {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(json);
        return restoreState(vm, varToState(result), error);
    } catch (const Poco::Exception& e) {
        if (error) *error = e.displayText();
        return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool Snapshot::saveFile(HSQUIRRELVM vm, const std::string& path, std::string* error) const {
    const std::string json = capture(vm, error);
    if (json.empty()) return false;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    ofs << json;
    return static_cast<bool>(ofs);
}

bool Snapshot::loadFile(HSQUIRRELVM vm, const std::string& path, std::string* error) const {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return restore(vm, oss.str(), error);
}

}  // namespace eve::dev
