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
#include <sstream>
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

using SeenSet = std::unordered_set<const void*>;

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

StateValue sqToStateValue(HSQUIRRELVM vm, SQInteger idx, SeenSet& seen, int depth, SkippedValues& skipped,
                          const std::string& path) {
    if (depth > 32) return StateValue::null();
    if (idx < 0) idx = sq_gettop(vm) + idx + 1;

    const SQObjectType t = sq_gettype(vm, idx);
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
            const void* id = objectId(vm, idx);
            if (id && !seen.insert(id).second) return StateValue::null();  // cycle -> null
            StateValue      arr  = StateValue::array();
            const SQInteger top  = sq_gettop(vm);
            const SQInteger size = sq_getsize(vm, idx);
            for (SQInteger i = 0; i < size; ++i) {
                sq_pushinteger(vm, i);
                if (SQ_SUCCEEDED(sq_get(vm, idx))) {
                    const SQObjectType et        = sq_gettype(vm, -1);
                    const std::string  childPath = path + "[" + std::to_string(i) + "]";
                    if (isPlainDataType(et)) {
                        arr.pushBack(sqToStateValue(vm, -1, seen, depth + 1, skipped, childPath));
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
            return arr;
        }
        case OT_TABLE: {
            const void* id = objectId(vm, idx);
            if (id && !seen.insert(id).second) return StateValue::null();
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
                                    sqToStateValue(vm, -1, seen, depth + 1, skipped,
                                                   path.empty() ? std::string(key) : path + "." + key));
                        }
                    }
                }
                sq_pop(vm, 2);
            }
            sq_settop(vm, top);
            return obj;
        }
        default: return StateValue::null();
    }
}

bool pushStateValue(HSQUIRRELVM vm, const StateValue& var, int depth) {
    if (depth > 32) {
        sq_pushnull(vm);
        return true;
    }
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
        for (const auto& name : rootNames) {
            SeenSet         seen;
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
                    roots.set(name, sqToStateValue(vm, -1, seen, 0, skip, name));
                }
            }
            sq_settop(vm, top);
        }

        StateValue native = StateValue::object();
        eve::cap::forEach<eve::caps::IStateProvider>([&](eve::caps::IStateProvider* p) {
            if (p->reloadPolicy() == eve::caps::StateReloadPolicy::Reset) return;
            StateValue captured;
            if (p->captureState(captured)) native.set(p->stateKind(), std::move(captured));
        });

        out = StateValue::object();
        out.set("version", StateValue::integer(2));
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

bool Snapshot::restoreState(HSQUIRRELVM vm, const StateValue& state, std::string* error) const {
    if (!vm) {
        if (error) *error = "no vm";
        return false;
    }
    try {
        const StateValue* roots = state.find("roots");
        if (!roots || !roots->isObject()) {
            if (error) *error = "invalid snapshot: missing roots";
            return false;
        }
        for (const auto& name : roots->keys()) {
            const SQInteger top = sq_gettop(vm);
            sq_pushroottable(vm);
            sq_pushstring(vm, name.c_str(), static_cast<SQInteger>(name.size()));
            if (!pushStateValue(vm, *roots->find(name), 0)) {
                sq_settop(vm, top);
                if (error) *error = "failed to restore " + name;
                return false;
            }
            sq_newslot(vm, -3, SQFalse);
            sq_settop(vm, top);
        }

        const StateValue* native = state.find("native");
        bool              failed = false;
        std::string       nativeErr;
        eve::cap::forEach<eve::caps::IStateProvider>([&](eve::caps::IStateProvider* p) {
            if (p->reloadPolicy() == eve::caps::StateReloadPolicy::Reset) {
                if (!p->resetToDefaults()) {
                    failed = true;
                    nativeErr += std::string(p->stateKind()) + ": reset failed; ";
                }
                return;
            }
            if (native && native->isObject()) {
                const StateValue* v = native->find(p->stateKind());
                if (!v) return;  // provider not present in the snapshot
                std::string perr;
                if (!p->restoreState(*v, &perr)) {
                    failed = true;
                    nativeErr += std::string(p->stateKind()) + ": " + perr + "; ";
                    p->resetToDefaults();
                }
            }
        });
        if (failed) {
            if (error) *error = nativeErr;
            return false;
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
