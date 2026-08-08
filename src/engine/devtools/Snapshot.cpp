#include "devtools/Snapshot.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <squirrel.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace eve::dev {
namespace {

bool isEngineName(const std::string& name) {
    static const std::unordered_set<std::string> kSkip = {
        "eve",       "win",      "gfx",        "event",     "timer",     "system",
        "math",      "tf",       "ui",         "scene",     "particles", "map",
        "gpgpu",     "physics",  "keyboard",   "mouse",     "touch",     "sound",
        "audio",     "model3d",  "font",       "thread",    "fs",        "hot",
        "config",    "require",  "path",       "exports",   "module",    "console",
        "process",   "stdin",    "stdout",     "stderr",    "_version_", "ARGV",
        "eve_init",  "eve_update", "eve_render", "eve_quit", "eve_reload",
        "eve_asset_reload", "watched_scripts", "track_script", "soft_reload_scripts",
        "poll_hot_reload", "file_exists", "path_endswith", "normalize_path",
        "async_pump", "async_dispatch_event", "Promise", "setTimeout", "clearTimeout",
        "nextTick",  "setImmediate",
    };
    return kSkip.count(name) > 0;
}

using SeenMap = std::unordered_map<const void*, int>;

const void* objectId(HSQUIRRELVM vm, SQInteger idx) {
    // Squirrel HSQOBJECT pointer identity via stack object.
    HSQOBJECT obj;
    sq_resetobject(&obj);
    sq_getstackobj(vm, idx, &obj);
    return reinterpret_cast<const void*>(obj._unVal.pUserPointer);
}

Poco::Dynamic::Var sqToVar(HSQUIRRELVM vm, SQInteger idx, SeenMap& seen, int depth) {
    if (depth > 32) return Poco::Dynamic::Var("/*max-depth*/");
    if (idx < 0) idx = sq_gettop(vm) + idx + 1;

    const SQObjectType t = sq_gettype(vm, idx);
    switch (t) {
        case OT_NULL:
            return Poco::Dynamic::Var();
        case OT_INTEGER: {
            SQInteger v = 0;
            sq_getinteger(vm, idx, &v);
            return Poco::Dynamic::Var(static_cast<Poco::Int64>(v));
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            sq_getfloat(vm, idx, &v);
            return Poco::Dynamic::Var(static_cast<double>(v));
        }
        case OT_BOOL: {
            SQBool v = SQFalse;
            sq_getbool(vm, idx, &v);
            return Poco::Dynamic::Var(v != SQFalse);
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, idx, &s);
            return Poco::Dynamic::Var(s ? std::string(s) : std::string{});
        }
        case OT_ARRAY: {
            const void* id = objectId(vm, idx);
            if (id && seen.count(id)) {
                auto o = new Poco::JSON::Object();
                o->set("$ref", seen[id]);
                return Poco::Dynamic::Var(o);
            }
            const int refId = static_cast<int>(seen.size()) + 1;
            if (id) seen[id] = refId;
            auto arr = new Poco::JSON::Array();
            const SQInteger top = sq_gettop(vm);
            const SQInteger size = sq_getsize(vm, idx);
            for (SQInteger i = 0; i < size; ++i) {
                sq_pushinteger(vm, i);
                if (SQ_SUCCEEDED(sq_get(vm, idx))) {
                    arr->add(sqToVar(vm, -1, seen, depth + 1));
                    sq_poptop(vm);
                } else {
                    arr->add(Poco::Dynamic::Var());
                }
            }
            sq_settop(vm, top);
            auto wrap = new Poco::JSON::Object();
            wrap->set("$type", "array");
            wrap->set("$id", refId);
            wrap->set("value", arr);
            return Poco::Dynamic::Var(wrap);
        }
        case OT_TABLE:
        case OT_INSTANCE: {
            const void* id = objectId(vm, idx);
            if (id && seen.count(id)) {
                auto o = new Poco::JSON::Object();
                o->set("$ref", seen[id]);
                return Poco::Dynamic::Var(o);
            }
            const int refId = static_cast<int>(seen.size()) + 1;
            if (id) seen[id] = refId;
            auto obj = new Poco::JSON::Object();
            const SQInteger top = sq_gettop(vm);
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, idx))) {
                // stack: … null key val
                if (sq_gettype(vm, -2) == OT_STRING) {
                    const SQChar* key = nullptr;
                    sq_getstring(vm, -2, &key);
                    if (key) {
                        const SQObjectType vt = sq_gettype(vm, -1);
                        if (vt != OT_CLOSURE && vt != OT_NATIVECLOSURE && vt != OT_CLASS &&
                            vt != OT_USERDATA && vt != OT_USERPOINTER && vt != OT_THREAD) {
                            obj->set(std::string(key), sqToVar(vm, -1, seen, depth + 1));
                        }
                    }
                }
                sq_pop(vm, 2);
            }
            sq_settop(vm, top);
            auto wrap = new Poco::JSON::Object();
            wrap->set("$type", t == OT_INSTANCE ? "instance" : "table");
            wrap->set("$id", refId);
            wrap->set("value", obj);
            return Poco::Dynamic::Var(wrap);
        }
        default:
            return Poco::Dynamic::Var("/*skipped*/");
    }
}

bool pushVar(HSQUIRRELVM vm, const Poco::Dynamic::Var& var,
             std::unordered_map<int, SQInteger>& idToAbsIndex, int depth) {
    if (depth > 32) {
        sq_pushnull(vm);
        return true;
    }
    if (var.isEmpty()) {
        sq_pushnull(vm);
        return true;
    }
    if (var.isBoolean()) {
        sq_pushbool(vm, var.convert<bool>() ? SQTrue : SQFalse);
        return true;
    }
    if (var.isInteger()) {
        sq_pushinteger(vm, static_cast<SQInteger>(var.convert<Poco::Int64>()));
        return true;
    }
    if (var.isNumeric()) {
        sq_pushfloat(vm, static_cast<SQFloat>(var.convert<double>()));
        return true;
    }
    if (var.isString()) {
        const std::string s = var.convert<std::string>();
        sq_pushstring(vm, s.c_str(), -1);
        return true;
    }
    if (var.type() == typeid(Poco::JSON::Object::Ptr)) {
        Poco::JSON::Object::Ptr o = var.extract<Poco::JSON::Object::Ptr>();
        if (!o) {
            sq_pushnull(vm);
            return true;
        }
        if (o->has("$ref")) {
            const int ref = o->getValue<int>("$ref");
            auto it = idToAbsIndex.find(ref);
            if (it == idToAbsIndex.end()) {
                sq_pushnull(vm);
                return true;
            }
            sq_push(vm, it->second);
            return true;
        }
        const std::string typ = o->has("$type") ? o->getValue<std::string>("$type") : "table";
        const int id = o->has("$id") ? o->getValue<int>("$id") : 0;
        if (typ == "array") {
            sq_newarray(vm, 0);
            if (id) idToAbsIndex[id] = sq_gettop(vm);
            Poco::JSON::Array::Ptr arr = o->getArray("value");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (!pushVar(vm, arr->get(i), idToAbsIndex, depth + 1)) return false;
                    sq_arrayappend(vm, -2);
                }
            }
            return true;
        }
        // table / instance → plain table on restore
        sq_newtable(vm);
        if (id) idToAbsIndex[id] = sq_gettop(vm);
        Poco::JSON::Object::Ptr body = o->getObject("value");
        if (!body && !o->has("$type")) body = o;
        if (body) {
            for (const auto& key : body->getNames()) {
                if (!key.empty() && key[0] == '$') continue;
                sq_pushstring(vm, key.c_str(), -1);
                if (!pushVar(vm, body->get(key), idToAbsIndex, depth + 1)) return false;
                sq_newslot(vm, -3, SQFalse);
            }
        }
        return true;
    }
    if (var.type() == typeid(Poco::JSON::Array::Ptr)) {
        Poco::JSON::Array::Ptr arr = var.extract<Poco::JSON::Array::Ptr>();
        sq_newarray(vm, 0);
        if (arr) {
            for (size_t i = 0; i < arr->size(); ++i) {
                if (!pushVar(vm, arr->get(i), idToAbsIndex, depth + 1)) return false;
                sq_arrayappend(vm, -2);
            }
        }
        return true;
    }
    sq_pushnull(vm);
    return true;
}

}  // namespace

Snapshot& Snapshot::instance() {
    static Snapshot inst;
    return inst;
}

bool Snapshot::isEngineBinding(const std::string& name) { return isEngineName(name); }

void Snapshot::markRoot(std::string name) {
    if (name.empty()) return;
    for (const auto& r : marked_) {
        if (r == name) return;
    }
    marked_.push_back(std::move(name));
}

void Snapshot::unmarkRoot(const std::string& name) {
    marked_.erase(std::remove(marked_.begin(), marked_.end(), name), marked_.end());
}

void Snapshot::clearRoots() { marked_.clear(); }

std::vector<std::string> Snapshot::roots() const { return marked_; }

std::vector<std::string> Snapshot::resolveRoots(HSQUIRRELVM vm) const {
    if (!marked_.empty()) return marked_;
    std::vector<std::string> out;
    if (!vm) return out;

    // Prefer conventional state tables when present.
    for (const char* pref : {"eve_state", "gameState", "state"}) {
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

    // Heuristic: all serializable non-engine root slots.
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    const SQInteger rootIdx = sq_gettop(vm);
    sq_pushnull(vm);
    while (SQ_SUCCEEDED(sq_next(vm, rootIdx))) {
        if (sq_gettype(vm, -2) == OT_STRING) {
            const SQChar* key = nullptr;
            sq_getstring(vm, -2, &key);
            if (key && !isEngineName(key)) {
                const SQObjectType vt = sq_gettype(vm, -1);
                if (vt == OT_INTEGER || vt == OT_FLOAT || vt == OT_BOOL || vt == OT_STRING ||
                    vt == OT_TABLE || vt == OT_ARRAY || vt == OT_INSTANCE) {
                    out.emplace_back(key);
                }
            }
        }
        sq_pop(vm, 2);
    }
    sq_settop(vm, top);
    return out;
}

std::string Snapshot::capture(HSQUIRRELVM vm, std::string* error) const {
    if (!vm) {
        if (error) *error = "no vm";
        return {};
    }
    auto rootNames = resolveRoots(vm);
    auto rootsObj  = new Poco::JSON::Object();
    SeenMap seen;
    for (const auto& name : rootNames) {
        const SQInteger top = sq_gettop(vm);
        sq_pushroottable(vm);
        sq_pushstring(vm, name.c_str(), -1);
        if (SQ_SUCCEEDED(sq_get(vm, -2))) {
            rootsObj->set(name, sqToVar(vm, -1, seen, 0));
        }
        sq_settop(vm, top);
    }
    auto doc = new Poco::JSON::Object();
    doc->set("version", 1);
    doc->set("roots", rootsObj);
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(doc), oss);
    return oss.str();
}

bool Snapshot::restore(HSQUIRRELVM vm, const std::string& json, std::string* error) const {
    if (!vm) {
        if (error) *error = "no vm";
        return false;
    }
    try {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(json);
        Poco::JSON::Object::Ptr doc = result.extract<Poco::JSON::Object::Ptr>();
        if (!doc || !doc->has("roots")) {
            if (error) *error = "invalid snapshot: missing roots";
            return false;
        }
        Poco::JSON::Object::Ptr roots = doc->getObject("roots");
        if (!roots) {
            if (error) *error = "invalid snapshot: roots not object";
            return false;
        }
        std::unordered_map<int, SQInteger> idMap;
        for (const auto& name : roots->getNames()) {
            const SQInteger top = sq_gettop(vm);
            idMap.clear();
            sq_pushroottable(vm);
            sq_pushstring(vm, name.c_str(), -1);
            if (!pushVar(vm, roots->get(name), idMap, 0)) {
                sq_settop(vm, top);
                if (error) *error = "failed to restore " + name;
                return false;
            }
            sq_newslot(vm, -3, SQFalse);
            sq_settop(vm, top);
        }
        return true;
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
