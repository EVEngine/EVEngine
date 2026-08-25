#include "dialogue/Dialogue.h"
#include "dialogue/DnutParser.h"

#include "avatar/AvatarInstance.h"
#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/utf8.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "i18n/I18n.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

namespace eve::dialogue {

Module_IMPL(Dialogue, new Dialogue());

namespace {

std::string floatToString(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

bool squirrelToVarValueAt(HSQUIRRELVM vm, SQInteger idx, Dialogue::VarValue &out) {
    switch (sq_gettype(vm, idx)) {
        case OT_INTEGER: {
            SQInteger v = 0;
            if (SQ_FAILED(sq_getinteger(vm, idx, &v))) return false;
            out = Dialogue::VarValue::integer(v);
            return true;
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            if (SQ_FAILED(sq_getfloat(vm, idx, &v))) return false;
            out = Dialogue::VarValue::number(v);
            return true;
        }
        case OT_BOOL: {
            SQBool v = SQFalse;
            if (SQ_FAILED(sq_getbool(vm, idx, &v))) return false;
            out = Dialogue::VarValue::boolean(v != 0);
            return true;
        }
        case OT_STRING: {
            const SQChar *v = nullptr;
            if (SQ_FAILED(sq_getstring(vm, idx, &v))) return false;
            out = Dialogue::VarValue::string(v ? v : "");
            return true;
        }
        default:
            return false;
    }
}

/** Convert the Squirrel value at stack index `idx` into a DataValue tree. */
bool squirrelToDataValue(HSQUIRRELVM vm, SQInteger idx, DataValue &out) {
    switch (sq_gettype(vm, idx)) {
        case OT_NULL:
            out = DataValue::null();
            return true;
        case OT_INTEGER: {
            SQInteger v = 0;
            if (SQ_FAILED(sq_getinteger(vm, idx, &v))) return false;
            out = DataValue::integer(v);
            return true;
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            if (SQ_FAILED(sq_getfloat(vm, idx, &v))) return false;
            out = DataValue::number(v);
            return true;
        }
        case OT_BOOL: {
            SQBool v = SQFalse;
            if (SQ_FAILED(sq_getbool(vm, idx, &v))) return false;
            out = DataValue::boolean(v != 0);
            return true;
        }
        case OT_STRING: {
            const SQChar *s = nullptr;
            if (SQ_FAILED(sq_getstring(vm, idx, &s))) return false;
            out = DataValue::string(s ? s : "");
            return true;
        }
        case OT_ARRAY: {
            const SQInteger size = sq_getsize(vm, idx);
            const SQInteger absIdx = idx > 0 ? idx : sq_gettop(vm) + idx + 1;
            std::vector<DataValue> items;
            for (SQInteger i = 0; i < size; ++i) {
                sq_pushinteger(vm, i);
                if (SQ_FAILED(sq_get(vm, absIdx))) return false;
                DataValue item;
                const bool ok = squirrelToDataValue(vm, -1, item);
                sq_pop(vm, 1);
                if (!ok) return false;
                items.push_back(std::move(item));
            }
            out = DataValue::array(std::move(items));
            return true;
        }
        case OT_TABLE: {
            DataValue::Object fields;
            sq_pushnull(vm);  // iterator
            while (SQ_SUCCEEDED(sq_next(vm, -2))) {
                std::string key;
                if (sq_gettype(vm, -2) == OT_STRING) {
                    const SQChar *k = nullptr;
                    if (SQ_SUCCEEDED(sq_getstring(vm, -2, &k)) && k) key = k;
                }
                DataValue value;
                const bool ok = !key.empty() && squirrelToDataValue(vm, -1, value);
                sq_pop(vm, 2);
                if (!ok) {
                    sq_pop(vm, 1);  // iterator
                    return false;
                }
                fields.emplace_back(std::move(key), std::move(value));
            }
            sq_pop(vm, 1);  // iterator
            out = DataValue::object(std::move(fields));
            return true;
        }
        default:
            return false;
    }
}

bool objectToDataValue(HSQUIRRELVM vm, const ssq::Object &obj, DataValue &out) {
    if (!vm) return false;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, obj.getRaw());
    const bool ok = squirrelToDataValue(vm, -1, out);
    sq_settop(vm, top);
    return ok;
}

bool objectToVarParams(HSQUIRRELVM vm, const ssq::Object &obj,
                       std::unordered_map<std::string, Dialogue::VarValue> &out) {
    out.clear();
    if (!vm) return true;  // no VM -> treat as empty params
    const HSQOBJECT raw = obj.getRaw();
    if (raw._type != OT_TABLE) return true;  // null / other -> empty params
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, raw);
    sq_pushnull(vm);  // iterator
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        const SQChar *k = nullptr;
        if (sq_gettype(vm, -2) == OT_STRING && SQ_SUCCEEDED(sq_getstring(vm, -2, &k)) && k) {
            Dialogue::VarValue v;
            if (squirrelToVarValueAt(vm, -1, v)) out[k] = std::move(v);
        }
        sq_pop(vm, 2);
    }
    sq_settop(vm, top);
    return true;
}

void pushVarValue(HSQUIRRELVM vm, const Dialogue::VarValue &v) {
    switch (v.type) {
        case Dialogue::VarValue::Type::Int:
            sq_pushinteger(vm, SQInteger(v.i));
            break;
        case Dialogue::VarValue::Type::Float:
            sq_pushfloat(vm, SQFloat(v.f));
            break;
        case Dialogue::VarValue::Type::Bool:
            sq_pushbool(vm, v.b ? SQTrue : SQFalse);
            break;
        case Dialogue::VarValue::Type::String:
            sq_pushstring(vm, v.s.c_str(), v.s.size());
            break;
    }
}

void pushVarTable(HSQUIRRELVM vm, const std::unordered_map<std::string, Dialogue::VarValue> &vars) {
    sq_newtable(vm);
    for (const auto &kv : vars) {
        sq_pushstring(vm, kv.first.c_str(), kv.first.size());
        pushVarValue(vm, kv.second);
        sq_newslot(vm, -3, SQFalse);
    }
}

std::vector<Dialogue*>& liveDialogues() {
    static std::vector<Dialogue*> instances;
    return instances;
}

StateValue varValueToState(const Dialogue::VarValue& v) {
    switch (v.type) {
        case Dialogue::VarValue::Type::Int: return StateValue::integer(v.i);
        case Dialogue::VarValue::Type::Float: return StateValue::number(v.f);
        case Dialogue::VarValue::Type::Bool: return StateValue::boolean(v.b);
        case Dialogue::VarValue::Type::String: return StateValue::string(v.s);
    }
    return StateValue::null();
}

bool stateToVarValue(const StateValue& v, Dialogue::VarValue& out) {
    switch (v.kind()) {
        case StateValue::Kind::Int: out = Dialogue::VarValue::integer(v.asInt()); return true;
        case StateValue::Kind::Float: out = Dialogue::VarValue::number(v.asDouble()); return true;
        case StateValue::Kind::Bool: out = Dialogue::VarValue::boolean(v.asBool()); return true;
        case StateValue::Kind::String: out = Dialogue::VarValue::string(v.asString()); return true;
        default: return false;
    }
}

/** @brief IStateProvider over every live Dialogue instance (module registry). */
class DialogueStateProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "dialogue"; }

    bool captureState(StateValue& out) override {
        StateValue arr = StateValue::array();
        for (Dialogue* d : liveDialogues()) {
            StateValue sv;
            if (d->captureState(sv)) arr.pushBack(std::move(sv));
        }
        out = std::move(arr);
        return true;
    }

    bool restoreState(const StateValue& in, std::string* err) override {
        if (!in.isArray()) {
            if (err) *err = "dialogue: expected array of instances";
            return false;
        }
        const size_t n = std::min(in.arraySize(), liveDialogues().size());
        for (size_t i = 0; i < n; ++i) {
            if (!liveDialogues()[i]->restoreState(in.at(i), err)) return false;
        }
        return true;
    }

    bool resetToDefaults() override {
        for (Dialogue* d : liveDialogues()) d->reset();
        return true;
    }
};

struct Register {
    Register() {
        static DialogueStateProvider provider;
        eve::cap::addListener<eve::caps::IStateProvider>(&provider);
    }
} g_register;

}  // namespace

Dialogue::VarValue Dialogue::VarValue::integer(long long v) {
    VarValue x;
    x.type = Type::Int;
    x.i = v;
    return x;
}

Dialogue::VarValue Dialogue::VarValue::number(double v) {
    VarValue x;
    x.type = Type::Float;
    x.f = v;
    return x;
}

Dialogue::VarValue Dialogue::VarValue::boolean(bool v) {
    VarValue x;
    x.type = Type::Bool;
    x.b = v;
    return x;
}

Dialogue::VarValue Dialogue::VarValue::string(std::string v) {
    VarValue x;
    x.type = Type::String;
    x.s = std::move(v);
    return x;
}

std::string Dialogue::VarValue::typeName() const {
    switch (type) {
        case Type::Int:
            return "int";
        case Type::Float:
            return "float";
        case Type::Bool:
            return "bool";
        case Type::String:
            return "string";
    }
    return "string";
}

std::string Dialogue::VarValue::toString() const {
    switch (type) {
        case Type::Int:
            return std::to_string(i);
        case Type::Float:
            return floatToString(f);
        case Type::Bool:
            return b ? "true" : "false";
        case Type::String:
            return s;
    }
    return {};
}

Dialogue::Dialogue() { liveDialogues().push_back(this); }

Dialogue::~Dialogue() {
    liveDialogues().erase(std::remove(liveDialogues().begin(), liveDialogues().end(), this), liveDialogues().end());
    if (vm_) {
        for (auto &kv : predicates_) sq_release(vm_, &kv.second);
        predicates_.clear();
    }
}

// ---------------------------------------------------------------------------
// Variables (global / scene)
// ---------------------------------------------------------------------------

std::unordered_map<std::string, Dialogue::VarValue> *Dialogue::varsForScope(
    const std::string &scope) {
    if (scope == "global") return &globalVars_;
    if (scope == "scene") return &sceneVars_;
    return nullptr;
}

const std::unordered_map<std::string, Dialogue::VarValue> *Dialogue::varsForScope(
    const std::string &scope) const {
    if (scope == "global") return &globalVars_;
    if (scope == "scene") return &sceneVars_;
    return nullptr;
}

bool Dialogue::setVarValue(const std::string &name, const VarValue &value,
                           const std::string &scope) {
    auto *m = varsForScope(scope);
    if (!m || name.empty()) return false;
    (*m)[name] = value;
    return true;
}

bool Dialogue::setVar(const std::string &name, ssq::Object value, const std::string &scope) {
    if (!vm_) return false;
    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, value.getRaw());
    VarValue v;
    const bool ok = squirrelToVarValueAt(vm_, -1, v);
    sq_settop(vm_, top);
    if (!ok) return false;
    return setVarValue(name, v, scope);
}

Dialogue::VarValue Dialogue::getVarValue(const std::string &name, const std::string &scope) const {
    const auto *m = varsForScope(scope);
    if (!m) return VarValue::string("");
    const auto it = m->find(name);
    return it == m->end() ? VarValue::string("") : it->second;
}

std::string Dialogue::getVarType(const std::string &name, const std::string &scope) const {
    return hasVar(name, scope) ? getVarValue(name, scope).typeName() : "";
}

int Dialogue::getVarInt(const std::string &name, int defaultValue, const std::string &scope) const {
    const VarValue v = getVarValue(name, scope);
    if (v.type == VarValue::Type::Int) return int(v.i);
    if (v.type == VarValue::Type::Float) return int(v.f);
    if (v.type == VarValue::Type::Bool) return v.b ? 1 : 0;
    return defaultValue;
}

float Dialogue::getVarFloat(const std::string &name, float defaultValue,
                            const std::string &scope) const {
    const VarValue v = getVarValue(name, scope);
    if (v.type == VarValue::Type::Int) return float(v.i);
    if (v.type == VarValue::Type::Float) return float(v.f);
    if (v.type == VarValue::Type::Bool) return v.b ? 1.f : 0.f;
    return defaultValue;
}

bool Dialogue::getVarBool(const std::string &name, bool defaultValue,
                          const std::string &scope) const {
    const VarValue v = getVarValue(name, scope);
    if (v.type == VarValue::Type::Int) return v.i != 0;
    if (v.type == VarValue::Type::Float) return v.f != 0.0;
    if (v.type == VarValue::Type::Bool) return v.b;
    return defaultValue;
}

std::string Dialogue::getVarString(const std::string &name, const std::string &defaultValue,
                                   const std::string &scope) const {
    const VarValue v = getVarValue(name, scope);
    return v.type == VarValue::Type::String ? v.s : defaultValue;
}

bool Dialogue::hasVar(const std::string &name, const std::string &scope) const {
    const auto *m = varsForScope(scope);
    return m && m->find(name) != m->end();
}

bool Dialogue::clearVar(const std::string &name, const std::string &scope) {
    auto *m = varsForScope(scope);
    return m && m->erase(name) > 0;
}

void Dialogue::clearVars(const std::string &scope) {
    if (scope == "global") {
        globalVars_.clear();
    } else if (scope == "scene") {
        sceneVars_.clear();
    } else if (scope == "all") {
        globalVars_.clear();
        sceneVars_.clear();
    }
}

// ---------------------------------------------------------------------------
// Conditions (structured tables + script predicates)
// ---------------------------------------------------------------------------

bool Dialogue::registerCondition(const std::string &name, ssq::Object fn) {
    if (!vm_ || name.empty()) return false;
    HSQOBJECT raw = fn.getRaw();
    if (raw._type == OT_NULL) return unregisterCondition(name);
    if (raw._type != OT_CLOSURE && raw._type != OT_NATIVECLOSURE) return false;
    const auto it = predicates_.find(name);
    if (it != predicates_.end()) {
        sq_release(vm_, &it->second);
        predicates_.erase(it);
    }
    sq_addref(vm_, &raw);
    predicates_[name] = raw;
    return true;
}

bool Dialogue::unregisterCondition(const std::string &name) {
    const auto it = predicates_.find(name);
    if (it == predicates_.end()) return false;
    if (vm_) sq_release(vm_, &it->second);
    predicates_.erase(it);
    return true;
}

bool Dialogue::evalCondition(ssq::Object table) {
    DataValue v;
    if (!objectToDataValue(vm_, table, v)) return false;
    return evalConditionData(v);
}

bool Dialogue::parseCondition(const DataValue &v, Condition &out, std::string &error) {
    if (v.kind == DataValue::Kind::Array) {
        out.kind = Condition::Kind::All;
        for (const auto &item : v.arr) {
            Condition child;
            if (!parseCondition(item, child, error)) return false;
            out.children.push_back(std::move(child));
        }
        return true;
    }
    if (v.kind != DataValue::Kind::Object) {
        error = "condition must be a table";
        return false;
    }
    if (const DataValue *script = v.find("script")) {
        if (script->kind != DataValue::Kind::String) {
            error = "script condition name must be a string";
            return false;
        }
        out.kind = Condition::Kind::Script;
        out.script = script->s;
        return true;
    }
    if (const DataValue *all = v.find("all")) {
        if (all->kind != DataValue::Kind::Array) {
            error = "all must be an array";
            return false;
        }
        out.kind = Condition::Kind::All;
        for (const auto &item : all->arr) {
            Condition child;
            if (!parseCondition(item, child, error)) return false;
            out.children.push_back(std::move(child));
        }
        return true;
    }
    if (const DataValue *any = v.find("any")) {
        if (any->kind != DataValue::Kind::Array) {
            error = "any must be an array";
            return false;
        }
        out.kind = Condition::Kind::Any;
        for (const auto &item : any->arr) {
            Condition child;
            if (!parseCondition(item, child, error)) return false;
            out.children.push_back(std::move(child));
        }
        return true;
    }
    if (const DataValue *notc = v.find("not")) {
        out.kind = Condition::Kind::Not;
        out.children.emplace_back();
        return parseCondition(*notc, out.children.back(), error);
    }
    if (const DataValue *var = v.find("var")) {
        const DataValue *op = v.find("op");
        if (var->kind != DataValue::Kind::String || !op ||
            op->kind != DataValue::Kind::String) {
            error = "var/op condition requires string var and op";
            return false;
        }
        static const char *kOps[] = {"eq", "ne", "gt", "ge", "lt", "le", "has", "missing"};
        const std::string opStr = op->s;
        if (std::find(std::begin(kOps), std::end(kOps), opStr) == std::end(kOps)) {
            error = "unknown condition op: " + opStr;
            return false;
        }
        out.kind = Condition::Kind::Cmp;
        out.var = var->s;
        out.op = opStr;
        out.value = VarValue::string("");
        if (const DataValue *val = v.find("value")) {
            switch (val->kind) {
                case DataValue::Kind::Int:
                    out.value = VarValue::integer(val->i);
                    break;
                case DataValue::Kind::Float:
                    out.value = VarValue::number(val->f);
                    break;
                case DataValue::Kind::Bool:
                    out.value = VarValue::boolean(val->b);
                    break;
                case DataValue::Kind::String:
                    out.value = VarValue::string(val->s);
                    break;
                default:
                    break;
            }
        }
        return true;
    }
    error = "unrecognized condition table";
    return false;
}

bool Dialogue::compareEq(const VarValue &a, const VarValue &b) const {
    if (a.isNumeric() && b.isNumeric()) {
        const double x = a.type == VarValue::Type::Int ? double(a.i) : a.f;
        const double y = b.type == VarValue::Type::Int ? double(b.i) : b.f;
        return x == y;
    }
    if (a.type == VarValue::Type::Bool && b.type == VarValue::Type::Bool) return a.b == b.b;
    if (a.type == VarValue::Type::String && b.type == VarValue::Type::String)
        return a.s == b.s;
    return false;
}

bool Dialogue::compareOrder(const std::string &op, const VarValue &a, const VarValue &b) const {
    if (!a.isNumeric() || !b.isNumeric()) return false;
    const double x = a.type == VarValue::Type::Int ? double(a.i) : a.f;
    const double y = b.type == VarValue::Type::Int ? double(b.i) : b.f;
    if (op == "gt") return x > y;
    if (op == "ge") return x >= y;
    if (op == "lt") return x < y;
    if (op == "le") return x <= y;
    return false;
}

bool Dialogue::evalConditionInternal(const Condition &c,
                                     const std::unordered_map<std::string, VarValue> &merged,
                                     const std::unordered_map<std::string, VarValue> &params,
                                     const std::string &lineId) const {
    switch (c.kind) {
        case Condition::Kind::Always:
            return true;
        case Condition::Kind::Cmp: {
            const auto it = merged.find(c.var);
            const bool present = it != merged.end();
            if (c.op == "has") return present;
            if (c.op == "missing") return !present;
            if (!present) return false;
            if (c.op == "eq") return compareEq(it->second, c.value);
            if (c.op == "ne") return !compareEq(it->second, c.value);
            return compareOrder(c.op, it->second, c.value);
        }
        case Condition::Kind::All:
            for (const auto &child : c.children)
                if (!evalConditionInternal(child, merged, params, lineId)) return false;
            return true;
        case Condition::Kind::Any:
            for (const auto &child : c.children)
                if (evalConditionInternal(child, merged, params, lineId)) return true;
            return false;
        case Condition::Kind::Not:
            return c.children.empty() ||
                   !evalConditionInternal(c.children.front(), merged, params, lineId);
        case Condition::Kind::Script:
            return evalScriptPredicate(c.script, merged, params, lineId);
    }
    return false;
}

bool Dialogue::evalConditionData(const DataValue &cond) {
    Condition c;
    std::string error;
    if (!parseCondition(cond, c, error)) return false;
    return evalConditionInternal(c, mergedVars({}), {}, "");
}

bool Dialogue::evalScriptPredicate(const std::string &name,
                                   const std::unordered_map<std::string, VarValue> &merged,
                                   const std::unordered_map<std::string, VarValue> &params,
                                   const std::string &lineId) const {
    if (!vm_) return false;
    const auto it = predicates_.find(name);
    if (it == predicates_.end()) return false;

    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, it->second);  // closure first; sq_call expects fn below its args
    sq_newtable(vm_);  // ctx

    sq_pushstring(vm_, "vars", -1);
    pushVarTable(vm_, merged);
    sq_newslot(vm_, -3, SQFalse);  // fn at -4, key at -2, var table at -1 -> slot into ctx(-3)

    sq_pushstring(vm_, "params", -1);
    pushVarTable(vm_, params);
    sq_newslot(vm_, -3, SQFalse);

    sq_pushstring(vm_, "lineId", -1);
    sq_pushstring(vm_, lineId.c_str(), lineId.size());
    sq_newslot(vm_, -3, SQFalse);

    if (SQ_FAILED(sq_call(vm_, 1, SQTrue, SQTrue))) {
        sq_settop(vm_, top);
        return false;
    }
    SQBool result = SQFalse;
    if (SQ_FAILED(sq_getbool(vm_, -1, &result))) {
        sq_settop(vm_, top);
        return false;
    }
    sq_settop(vm_, top);
    return result != 0;
}

// ---------------------------------------------------------------------------
// Content pools
// ---------------------------------------------------------------------------

int Dialogue::loadPoolsFromTable(ssq::Object table) {
    DataValue root;
    if (!objectToDataValue(vm_, table, root)) {
        lastPoolsError_ = "pools root must be a table";
        return 0;
    }
    return loadPoolsFromData(root);
}

bool Dialogue::parseLineData(const std::string &poolId, const DataValue &v, int index, Line &line,
                             std::string &error) {
    if (v.kind != DataValue::Kind::Object) {
        error = "line must be a table";
        return false;
    }
    if (const DataValue *id = v.find("id"))
        if (id->kind == DataValue::Kind::String) line.id = id->s;
    if (line.id.empty()) line.id = poolId + "." + std::to_string(index);

    if (const DataValue *sp = v.find("speaker"))
        if (sp->kind == DataValue::Kind::String) line.speaker = sp->s;
    if (const DataValue *tx = v.find("text"))
        if (tx->kind == DataValue::Kind::String) line.text = tx->s;
    if (const DataValue *ik = v.find("i18n"))
        if (ik->kind == DataValue::Kind::String) line.i18nKey = ik->s;
    if (line.text.empty() && line.i18nKey.empty()) {
        error = "line '" + line.id + "': text or i18n required";
        return false;
    }

    if (const DataValue *w = v.find("weight")) {
        if (w->kind == DataValue::Kind::Int) line.weight = double(w->i);
        else if (w->kind == DataValue::Kind::Float) line.weight = w->f;
        else {
            error = "line '" + line.id + "': weight must be a number";
            return false;
        }
    }

    if (const DataValue *when = v.find("when")) {
        if (!parseCondition(*when, line.when, error)) {
            error = "line '" + line.id + "': " + error;
            return false;
        }
    }

    if (const DataValue *meta = v.find("meta")) {
        if (meta->kind != DataValue::Kind::Object) {
            error = "line '" + line.id + "': meta must be a table";
            return false;
        }
        for (const auto &mkv : meta->obj) {
            std::string val;
            switch (mkv.second.kind) {
                case DataValue::Kind::String:
                    val = mkv.second.s;
                    break;
                case DataValue::Kind::Int:
                    val = std::to_string(mkv.second.i);
                    break;
                case DataValue::Kind::Float:
                    val = floatToString(mkv.second.f);
                    break;
                case DataValue::Kind::Bool:
                    val = mkv.second.b ? "true" : "false";
                    break;
                default:
                    break;
            }
            line.meta[mkv.first] = val;
        }
    }

    if (const DataValue *tags = v.find("tags")) {
        if (tags->kind != DataValue::Kind::Array) {
            error = "line '" + line.id + "': tags must be an array";
            return false;
        }
        for (const auto &t : tags->arr)
            if (t.kind == DataValue::Kind::String) line.tags.push_back(t.s);
    }
    return true;
}

int Dialogue::loadPoolsFromData(const DataValue &root) {
    lastPoolsError_.clear();
    if (root.kind != DataValue::Kind::Object) {
        lastPoolsError_ = "pools root must be an object";
        return 0;
    }
    const DataValue *pools = root.find("pools");
    if (!pools || pools->kind != DataValue::Kind::Object) {
        lastPoolsError_ = "missing pools object";
        return 0;
    }

    int registered = 0;
    for (const auto &kv : pools->obj) {
        Pool pool;
        pool.id = kv.first;
        if (kv.second.kind != DataValue::Kind::Object) {
            lastPoolsError_ = "pool '" + kv.first + "': expected object";
            continue;
        }
        if (const DataValue *nr = kv.second.find("noRepeat")) {
            if (nr->kind == DataValue::Kind::Int) pool.noRepeat = int(nr->i);
            else if (nr->kind == DataValue::Kind::Float) pool.noRepeat = int(nr->f);
            if (pool.noRepeat < 0) pool.noRepeat = 0;
        }
        const DataValue *lines = kv.second.find("lines");
        if (!lines || lines->kind != DataValue::Kind::Array) {
            lastPoolsError_ = "pool '" + kv.first + "': missing lines array";
            continue;
        }
        int lineIndex = 1;
        for (const auto &lv : lines->arr) {
            Line line;
            std::string error;
            if (!parseLineData(pool.id, lv, lineIndex, line, error)) {
                if (lastPoolsError_.empty()) lastPoolsError_ = error;
                continue;
            }
            pool.lines.push_back(std::move(line));
            ++lineIndex;
        }
        if (Pool *existing = findPool(pool.id)) *existing = std::move(pool);
        else pools_.push_back(std::move(pool));
        ++registered;
    }
    return registered;
}

int Dialogue::loadPoolsFromDnut(const std::string &source, const std::string &path) {
    DataValue root;
    std::string error;
    if (!parseDnut(source, path.empty() ? "<dnut>" : path, root, error)) {
        lastPoolsError_ = error;
        return 0;
    }
    return loadPoolsFromData(root);
}

int Dialogue::loadPoolsFromDnutFile(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::FileData *fd = nullptr;
    try {
        fd = fs->read(path);
    } catch (...) {
        delete fd;
        lastPoolsError_ = path + ": 读取失败";
        return 0;
    }
    if (fd == nullptr || fd->getData() == nullptr || fd->getSize() == 0) {
        delete fd;
        lastPoolsError_ = path + ": 读取失败";
        return 0;
    }
    const std::string text(static_cast<const char *>(fd->getData()), fd->getSize());
    delete fd;
    return loadPoolsFromDnut(text, path);
}

void Dialogue::clearPools() { pools_.clear(); }

int Dialogue::getPoolCount() const { return int(pools_.size()); }

std::string Dialogue::getPoolId(int index) const {
    if (index < 0 || size_t(index) >= pools_.size()) return {};
    return pools_[size_t(index)].id;
}

bool Dialogue::hasPool(const std::string &id) const { return findPool(id) != nullptr; }

Dialogue::Pool *Dialogue::findPool(const std::string &id) {
    for (auto &p : pools_)
        if (p.id == id) return &p;
    return nullptr;
}

const Dialogue::Pool *Dialogue::findPool(const std::string &id) const {
    for (const auto &p : pools_)
        if (p.id == id) return &p;
    return nullptr;
}

Dialogue::Line *Dialogue::findLine(const std::string &id) {
    for (auto &p : pools_)
        for (auto &l : p.lines)
            if (l.id == id) return &l;
    return nullptr;
}

const Dialogue::Line *Dialogue::findLine(const std::string &id) const {
    for (const auto &p : pools_)
        for (const auto &l : p.lines)
            if (l.id == id) return &l;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Merged variable context / interpolation
// ---------------------------------------------------------------------------

std::unordered_map<std::string, Dialogue::VarValue> Dialogue::mergedVars(
    const std::unordered_map<std::string, VarValue> &params) const {
    std::unordered_map<std::string, VarValue> m = globalVars_;
    for (const auto &kv : sceneVars_) m[kv.first] = kv.second;
    for (const auto &kv : params) m[kv.first] = kv.second;
    return m;
}

std::unordered_map<std::string, std::string> Dialogue::stringParams(
    const std::unordered_map<std::string, VarValue> &vars) const {
    std::unordered_map<std::string, std::string> out;
    for (const auto &kv : vars) out[kv.first] = kv.second.toString();
    return out;
}

std::string Dialogue::interpolate(const std::string &tpl,
                                  const std::unordered_map<std::string, VarValue> &vars) const {
    std::string out;
    out.reserve(tpl.size());
    for (size_t i = 0; i < tpl.size();) {
        if (tpl[i] == '{') {
            const size_t close = tpl.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string name = tpl.substr(i + 1, close - i - 1);
                const auto it = vars.find(name);
                if (it != vars.end()) {
                    out += it->second.toString();
                    i = close + 1;
                    continue;
                }
            }
        }
        out += tpl[i++];
    }
    return out;
}

// ---------------------------------------------------------------------------
// RNG / weighted selection / play
// ---------------------------------------------------------------------------

uint32_t Dialogue::nextRandom() {
    uint32_t x = rngState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState_ = x;
    return x;
}

double Dialogue::nextUnit() { return (nextRandom() & 0xFFFFFFu) / 16777216.0; }

void Dialogue::setRandomSeed(int seed) {
    rngState_ = seed ? uint32_t(seed) : 1u;
    for (auto &p : pools_) p.recent.clear();
}

std::string Dialogue::pickLine(const std::string &poolId, ssq::Object params) {
    std::unordered_map<std::string, VarValue> p;
    objectToVarParams(vm_, params, p);
    return pickLineWithParams(poolId, p);
}

std::string Dialogue::pickLineWithParams(
    const std::string &poolId, const std::unordered_map<std::string, VarValue> &params) {
    Pool *pool = findPool(poolId);
    if (!pool) return "";
    const auto merged = mergedVars(params);

    auto collect = [&](std::vector<size_t> &out, bool allowRecent) {
        for (size_t i = 0; i < pool->lines.size(); ++i) {
            const Line &line = pool->lines[i];
            if (line.weight <= 0.0) continue;
            if (!evalConditionInternal(line.when, merged, params, line.id)) continue;
            if (!allowRecent && pool->noRepeat > 0 &&
                std::find(pool->recent.begin(), pool->recent.end(), i) != pool->recent.end())
                continue;
            out.push_back(i);
        }
    };

    std::vector<size_t> candidates;
    collect(candidates, false);
    if (candidates.empty()) collect(candidates, true);  // noRepeat must not deadlock
    if (candidates.empty()) return "";

    double total = 0.0;
    for (const size_t i : candidates) total += pool->lines[i].weight;
    double roll = nextUnit() * total;
    size_t picked = candidates.front();
    double acc = 0.0;
    for (const size_t i : candidates) {
        acc += pool->lines[i].weight;
        if (roll < acc) {
            picked = i;
            break;
        }
    }
    if (pool->noRepeat > 0) {
        pool->recent.push_back(picked);
        if (pool->recent.size() > size_t(pool->noRepeat)) pool->recent.erase(pool->recent.begin());
    }
    return pool->lines[picked].id;
}

bool Dialogue::playLine(const std::string &lineId, ssq::Object params) {
    std::unordered_map<std::string, VarValue> p;
    objectToVarParams(vm_, params, p);
    return playLineWithParams(lineId, p);
}

bool Dialogue::playLineWithParams(const std::string &lineId,
                                  const std::unordered_map<std::string, VarValue> &params) {
    Line *line = findLine(lineId);
    if (!line) return false;
    const auto merged = mergedVars(params);

    std::string text;
    if (!line->text.empty()) {
        text = interpolate(line->text, merged);
    } else {
        auto *i18n = eve::ModuleManager::getInstance<eve::i18n::I18n>("I18n");
        text = i18n ? i18n->getWithParams(line->i18nKey, stringParams(merged)) : line->i18nKey;
    }

    if (line->speaker.empty()) narrate(text);
    else say(line->speaker, text);

    currentLineId_ = line->id;
    currentLineMeta_ = line->meta;
    currentLineTags_ = line->tags;
    applyLineMeta(*line);
    return true;
}

bool Dialogue::playPool(const std::string &poolId, ssq::Object params) {
    std::unordered_map<std::string, VarValue> p;
    objectToVarParams(vm_, params, p);
    return playPoolWithParams(poolId, p);
}

bool Dialogue::playPoolWithParams(const std::string &poolId,
                                  const std::unordered_map<std::string, VarValue> &params) {
    const std::string lineId = pickLineWithParams(poolId, params);
    return !lineId.empty() && playLineWithParams(lineId, params);
}

std::string Dialogue::getCurrentLineMeta(const std::string &field) const {
    const auto it = currentLineMeta_.find(field);
    return it == currentLineMeta_.end() ? std::string{} : it->second;
}

std::vector<std::string> Dialogue::getCurrentLineTags() const { return currentLineTags_; }

void Dialogue::applyLineMeta(const Line &line) {
    if (line.speaker.empty()) return;
    const auto expr = line.meta.find("expression");
    const auto motion = line.meta.find("motion");
    if (expr == line.meta.end() && motion == line.meta.end()) return;
    Character *c = findCharacter(line.speaker);
    if (!c || !c->avatar) return;
    if (expr != line.meta.end()) c->avatar->setExpression(expr->second);
    if (motion != line.meta.end()) c->avatar->setMotion(motion->second);
}

// ---------------------------------------------------------------------------
// Scene-scoped variables: clear when the selected Scene host changes
// ---------------------------------------------------------------------------

void Dialogue::pollSceneChange() {
    auto *scn = eve::ModuleManager::getInstance<eve::scene::Scene>("Scene");
    if (!scn) return;
    eve::scene::SceneHost *host = scn->current();
    const std::string name = host ? host->getName() : "";
    if (!name.empty() && name != lastSceneName_) sceneVars_.clear();
    if (!name.empty()) lastSceneName_ = name;
}

Dialogue::Character *Dialogue::findCharacter(const std::string &id) {
    for (auto &c : characters_)
        if (c.id == id) return &c;
    return nullptr;
}

const Dialogue::Character *Dialogue::findCharacter(const std::string &id) const {
    for (const auto &c : characters_)
        if (c.id == id) return &c;
    return nullptr;
}

bool Dialogue::registerCharacter(const std::string &id, const std::string &displayName) {
    if (id.empty()) return false;
    if (auto *c = findCharacter(id)) {
        c->displayName = displayName.empty() ? id : displayName;
        return true;
    }
    Character c;
    c.id = id;
    c.displayName = displayName.empty() ? id : displayName;
    characters_.push_back(c);
    return true;
}

bool Dialogue::hasCharacter(const std::string &id) const { return findCharacter(id) != nullptr; }

std::string Dialogue::getDisplayName(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c ? c->displayName : std::string{};
}

bool Dialogue::bindAvatar(const std::string &id, avatar::AvatarInstance *av) {
    Character *c = findCharacter(id);
    if (!c) return false;
    // Drop the previous binding's destroy hook before replacing the pointer,
    // otherwise the old avatar's hook would null the new binding on destroy.
    if (c->avatar && c->avatarHook) {
        c->avatar->removeDestroyHook(*c->avatarHook);
        c->avatarHook.reset();
    }
    c->avatar = av;
    if (av) {
        const std::string key = id;
        c->avatarHook = av->addDestroyHook([this, key](avatar::AvatarInstance *) {
            if (Character *ch = findCharacter(key))
                ch->avatar = nullptr;
        });
    }
    return true;
}

avatar::AvatarInstance *Dialogue::getAvatar(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c ? c->avatar : nullptr;
}

int Dialogue::getCharacterCount() const { return int(characters_.size()); }

std::string Dialogue::getCharacterId(int index) const {
    if (index < 0 || size_t(index) >= characters_.size()) return {};
    return characters_[size_t(index)].id;
}

bool Dialogue::show(const std::string &id, const std::string &slot) {
    Character *c = findCharacter(id);
    if (!c) return false;
    c->shown = true;
    c->slot = slot.empty() ? "center" : slot;
    if (c->avatar) c->avatar->setVisible(true);
    return true;
}

bool Dialogue::hide(const std::string &id) {
    Character *c = findCharacter(id);
    if (!c) return false;
    c->shown = false;
    if (c->avatar) c->avatar->setVisible(false);
    return true;
}

bool Dialogue::isShown(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c && c->shown;
}

std::string Dialogue::getSlot(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c ? c->slot : std::string{};
}

void Dialogue::setSlotX(const std::string &slot, float xNorm) {
    if (slot.empty()) return;
    slotX_[slot] = xNorm;
}

float Dialogue::getSlotX(const std::string &slot) const {
    auto it = slotX_.find(slot);
    if (it != slotX_.end()) return it->second;
    if (slot == "left") return 0.25f;
    if (slot == "right") return 0.75f;
    if (slot == "center") return 0.5f;
    return 0.5f;
}

bool Dialogue::setExpression(const std::string &id, const std::string &expression) {
    Character *c = findCharacter(id);
    if (!c || !c->avatar) return false;
    c->avatar->setExpression(expression);
    return true;
}

bool Dialogue::setMotion(const std::string &id, const std::string &motion) {
    Character *c = findCharacter(id);
    if (!c || !c->avatar) return false;
    c->avatar->setMotion(motion);
    return true;
}

void Dialogue::syncStage(float stageWidth, float stageHeight) {
    (void)stageHeight;
    for (Character &c : characters_) {
        if (!c.avatar) continue;
        c.avatar->setVisible(c.shown);
        if (!c.shown) {
            c.avatar->sync();
            continue;
        }
        const float xn = getSlotX(c.slot);
        const float x = xn * stageWidth;
        // Keep current Y; only drive X from slot.
        c.avatar->setPosition(x, c.avatar->getY());
        c.avatar->sync();
    }
}

void Dialogue::beginLine(const std::string &speakerId, const std::string &text) {
    speakerId_ = speakerId;
    fullText_ = text;
    typed_ = 0.f;
    selectedChoiceId_.clear();
    lipSyncTime_ = 0.f;
    if (typeSpeed_ <= 0.f) {
        typed_ = float(utf8_codepoint_count(fullText_));
        phase_ = Phase::WaitingAdvance;
        lipSyncValue_ = 0.f;
    } else {
        phase_ = Phase::Typing;
    }
}

void Dialogue::say(const std::string &speakerId, const std::string &text) {
    beginLine(speakerId, text);
}

void Dialogue::narrate(const std::string &text) { beginLine("", text); }

void Dialogue::setTypeSpeed(float charsPerSecond) { typeSpeed_ = charsPerSecond; }

void Dialogue::skipTyping() {
    if (phase_ == Phase::Typing) {
        typed_ = float(utf8_codepoint_count(fullText_));
        phase_ = Phase::WaitingAdvance;
    }
}

bool Dialogue::isTyping() const { return phase_ == Phase::Typing; }

bool Dialogue::isWaitingAdvance() const { return phase_ == Phase::WaitingAdvance; }

bool Dialogue::isIdle() const { return phase_ == Phase::Idle; }

void Dialogue::advance() {
    if (phase_ == Phase::Typing) {
        skipTyping();
        return;
    }
    if (phase_ == Phase::WaitingAdvance) phase_ = Phase::Idle;
}

std::string Dialogue::getSpeakerName() const {
    if (speakerId_.empty()) return {};
    return getDisplayName(speakerId_);
}

std::string Dialogue::getVisibleText() const {
    if (fullText_.empty()) return {};
    const size_t total = utf8_codepoint_count(fullText_);
    size_t n = size_t(std::floor(typed_ + 1e-4f));
    if (n == 0) return {};
    if (n >= total) return fullText_;
    return fullText_.substr(0, utf8_byte_offset_for_codepoints(fullText_, n));
}

std::string Dialogue::getPhase() const {
    switch (phase_) {
        case Phase::Idle:
            return "idle";
        case Phase::Typing:
            return "typing";
        case Phase::WaitingAdvance:
            return "waiting_advance";
        case Phase::WaitingChoice:
            return "waiting_choice";
    }
    return "idle";
}

void Dialogue::clearChoices() {
    choices_.clear();
    selectedChoiceId_.clear();
}

bool Dialogue::addChoice(const std::string &id, const std::string &label) {
    if (id.empty()) return false;
    for (auto &ch : choices_) {
        if (ch.id == id) {
            ch.label = label;
            return true;
        }
    }
    choices_.push_back(Choice{id, label});
    return true;
}

void Dialogue::presentChoices() {
    if (choices_.empty()) {
        phase_ = Phase::Idle;
        return;
    }
    if (phase_ == Phase::Typing) skipTyping();
    phase_ = Phase::WaitingChoice;
}

bool Dialogue::isWaitingChoice() const { return phase_ == Phase::WaitingChoice; }

int Dialogue::getChoiceCount() const { return int(choices_.size()); }

std::string Dialogue::getChoiceId(int index) const {
    if (index < 0 || size_t(index) >= choices_.size()) return {};
    return choices_[size_t(index)].id;
}

std::string Dialogue::getChoiceLabel(int index) const {
    if (index < 0 || size_t(index) >= choices_.size()) return {};
    return choices_[size_t(index)].label;
}

bool Dialogue::selectChoice(int index) {
    if (phase_ != Phase::WaitingChoice) return false;
    if (index < 0 || size_t(index) >= choices_.size()) return false;
    selectedChoiceId_ = choices_[size_t(index)].id;
    phase_ = Phase::Idle;
    return true;
}

void Dialogue::setLipSyncEnabled(bool enabled) { lipSyncEnabled_ = enabled; }

void Dialogue::setLipSyncParameter(const std::string &name) {
    if (!name.empty()) lipSyncParameter_ = name;
}

void Dialogue::setLipSyncAmplitude(float amplitude) {
    if (amplitude < 0.f) amplitude = 0.f;
    if (amplitude > 2.f) amplitude = 2.f;
    lipSyncAmplitude_ = amplitude;
}

void Dialogue::updateLipSync(float dt) {
    if (dt < 0.f) dt = 0.f;
    if (lipSyncEnabled_ && phase_ == Phase::Typing && !speakerId_.empty()) {
        lipSyncTime_ += dt;
        // Simple mouth envelope while characters appear (no audio dependency).
        const float wave = std::fabs(std::sin(lipSyncTime_ * 14.f));
        lipSyncValue_ = lipSyncAmplitude_ * (0.25f + 0.75f * wave);
    } else {
        // Ease shut when not typing.
        lipSyncValue_ *= std::max(0.f, 1.f - dt * 8.f);
        if (lipSyncValue_ < 0.01f) lipSyncValue_ = 0.f;
    }
    applyLipSyncToSpeaker();
}

void Dialogue::applyLipSyncToSpeaker() {
    if (!lipSyncEnabled_ || speakerId_.empty() || lipSyncParameter_.empty()) return;
    Character *c = findCharacter(speakerId_);
    if (!c || !c->avatar) return;
    c->avatar->setParameter(lipSyncParameter_, lipSyncValue_);
}

void Dialogue::update(float dt) {
    if (dt < 0.f) dt = 0.f;
    if (phase_ == Phase::Typing) {
        const float total = float(utf8_codepoint_count(fullText_));
        typed_ += typeSpeed_ * dt;
        if (typed_ >= total) {
            typed_ = total;
            phase_ = Phase::WaitingAdvance;
        }
    }
    updateLipSync(dt);
    pollSceneChange();
}

void Dialogue::reset() {
    phase_ = Phase::Idle;
    speakerId_.clear();
    fullText_.clear();
    typed_ = 0.f;
    choices_.clear();
    selectedChoiceId_.clear();
    lipSyncValue_ = 0.f;
    lipSyncTime_ = 0.f;
    currentLineId_.clear();
    currentLineMeta_.clear();
    currentLineTags_.clear();
    for (Character &c : characters_) {
        c.shown = false;
        if (c.avatar) c.avatar->setVisible(false);
    }
}

bool Dialogue::captureState(StateValue& out) const {
    out = StateValue::object();
    out.set("phase", StateValue::string(getPhase()));
    out.set("speakerId", StateValue::string(speakerId_));
    out.set("fullText", StateValue::string(fullText_));
    out.set("typed", StateValue::number(typed_));
    out.set("typeSpeed", StateValue::number(typeSpeed_));
    out.set("lipSyncEnabled", StateValue::boolean(lipSyncEnabled_));
    out.set("lipSyncParameter", StateValue::string(lipSyncParameter_));
    out.set("lipSyncAmplitude", StateValue::number(lipSyncAmplitude_));
    out.set("rngState", StateValue::integer(static_cast<int64_t>(rngState_)));
    out.set("currentLineId", StateValue::string(currentLineId_));
    out.set("selectedChoiceId", StateValue::string(selectedChoiceId_));

    StateValue global = StateValue::object();
    for (const auto& kv : globalVars_) global.set(kv.first, varValueToState(kv.second));
    out.set("globalVars", std::move(global));

    StateValue scene = StateValue::object();
    for (const auto& kv : sceneVars_) scene.set(kv.first, varValueToState(kv.second));
    out.set("sceneVars", std::move(scene));

    StateValue choices = StateValue::array();
    for (const auto& c : choices_) {
        StateValue item = StateValue::object();
        item.set("id", StateValue::string(c.id));
        item.set("label", StateValue::string(c.label));
        choices.pushBack(std::move(item));
    }
    out.set("choices", std::move(choices));

    StateValue chars = StateValue::array();
    for (const auto& c : characters_) {
        StateValue item = StateValue::object();
        item.set("id", StateValue::string(c.id));
        item.set("displayName", StateValue::string(c.displayName));
        item.set("slot", StateValue::string(c.slot));
        item.set("shown", StateValue::boolean(c.shown));
        chars.pushBack(std::move(item));
    }
    out.set("characters", std::move(chars));

    StateValue slotX = StateValue::object();
    for (const auto& kv : slotX_) slotX.set(kv.first, StateValue::number(kv.second));
    out.set("slotX", std::move(slotX));
    return true;
}

bool Dialogue::restoreState(const StateValue& in, std::string* err) {
    if (!in.isObject()) {
        if (err) *err = "dialogue: state is not an object";
        return false;
    }
    const StateValue* phase = in.find("phase");
    if (!phase || !phase->isString()) {
        if (err) *err = "dialogue: missing phase";
        return false;
    }
    const std::string phaseName = phase->asString();
    if (phaseName == "idle")
        phase_ = Phase::Idle;
    else if (phaseName == "typing")
        phase_ = Phase::Typing;
    else if (phaseName == "waiting_advance")
        phase_ = Phase::WaitingAdvance;
    else if (phaseName == "waiting_choice")
        phase_ = Phase::WaitingChoice;
    else {
        if (err) *err = "dialogue: unknown phase '" + phaseName + "'";
        return false;
    }

    if (const StateValue* v = in.find("speakerId"); v && v->isString()) speakerId_ = v->asString();
    if (const StateValue* v = in.find("fullText"); v && v->isString()) fullText_ = v->asString();
    if (const StateValue* v = in.find("typed"); v && (v->isInt() || v->isFloat()))
        typed_ = static_cast<float>(v->isInt() ? double(v->asInt()) : v->asDouble());
    if (const StateValue* v = in.find("typeSpeed"); v && (v->isInt() || v->isFloat()))
        typeSpeed_ = static_cast<float>(v->isInt() ? double(v->asInt()) : v->asDouble());
    if (const StateValue* v = in.find("lipSyncEnabled"); v && v->isBool()) lipSyncEnabled_ = v->asBool();
    if (const StateValue* v = in.find("lipSyncParameter"); v && v->isString()) lipSyncParameter_ = v->asString();
    if (const StateValue* v = in.find("lipSyncAmplitude"); v && (v->isInt() || v->isFloat()))
        lipSyncAmplitude_ = static_cast<float>(v->isInt() ? double(v->asInt()) : v->asDouble());
    if (const StateValue* v = in.find("rngState"); v && v->isInt()) rngState_ = static_cast<uint32_t>(v->asInt());
    if (const StateValue* v = in.find("currentLineId"); v && v->isString()) currentLineId_ = v->asString();
    if (const StateValue* v = in.find("selectedChoiceId"); v && v->isString()) selectedChoiceId_ = v->asString();

    globalVars_.clear();
    if (const StateValue* vars = in.find("globalVars"); vars && vars->isObject()) {
        for (const auto& key : vars->keys()) {
            Dialogue::VarValue val;
            if (stateToVarValue(*vars->find(key), val)) globalVars_[key] = val;
        }
    }
    sceneVars_.clear();
    if (const StateValue* vars = in.find("sceneVars"); vars && vars->isObject()) {
        for (const auto& key : vars->keys()) {
            Dialogue::VarValue val;
            if (stateToVarValue(*vars->find(key), val)) sceneVars_[key] = val;
        }
    }

    choices_.clear();
    if (const StateValue *choices = in.find("choices"); choices && choices->isArray()) {
        for (size_t i = 0; i < choices->arraySize(); ++i) {
            const StateValue &item = choices->at(i);
            const StateValue *id = item.find("id");
            const StateValue *label = item.find("label");
            if (id && id->isString() && label && label->isString()) {
                choices_.push_back(Choice{id->asString(), label->asString()});
            }
        }
    }

    if (const StateValue *chars = in.find("characters"); chars && chars->isArray()) {
        for (size_t i = 0; i < chars->arraySize(); ++i) {
            const StateValue &item = chars->at(i);
            const StateValue *id = item.find("id");
            if (!id || !id->isString()) continue;
            Character *c = findCharacter(id->asString());
            if (!c) {
                characters_.push_back(Character{});
                c = &characters_.back();
                c->id = id->asString();
                if (const StateValue *name = item.find("displayName"); name && name->isString())
                    c->displayName = name->asString();
            }
            if (const StateValue *slot = item.find("slot"); slot && slot->isString())
                c->slot = slot->asString();
            if (const StateValue *shown = item.find("shown"); shown && shown->isBool()) {
                c->shown = shown->asBool();
                if (c->avatar) c->avatar->setVisible(c->shown);
            }
        }
    }

    slotX_.clear();
    if (const StateValue *slots = in.find("slotX"); slots && slots->isObject()) {
        for (const auto &key : slots->keys()) {
            const StateValue *v = slots->find(key);
            if (v && (v->isInt() || v->isFloat()))
                slotX_[key] = static_cast<float>(v->isInt() ? double(v->asInt()) : v->asDouble());
        }
    }
    return true;
}

bool Dialogue::resetToDefaults() {
    reset();
    return true;
}

void Dialogue::expose(ssq::Table &table) {
    if (Dialogue *self = Dialogue::create()) self->vm_ = table.getHandle();
    auto cls = table.addClass(name, Dialogue::create, false);
    expose(cls);
}

void Dialogue::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Dialogue::getName);
    cls.addFunc("registerCharacter", &Dialogue::registerCharacter);
    cls.addFunc("hasCharacter", &Dialogue::hasCharacter);
    cls.addFunc("getDisplayName", &Dialogue::getDisplayName);
    cls.addFunc("bindAvatar", &Dialogue::bindAvatar);
    cls.addFunc("getAvatar", &Dialogue::getAvatar);
    cls.addFunc("getCharacterCount", &Dialogue::getCharacterCount);
    cls.addFunc("getCharacterId", &Dialogue::getCharacterId);

    cls.addFunc("show", &Dialogue::show);
    cls.addFunc("hide", &Dialogue::hide);
    cls.addFunc("isShown", &Dialogue::isShown);
    cls.addFunc("getSlot", &Dialogue::getSlot);
    cls.addFunc("setSlotX", &Dialogue::setSlotX);
    cls.addFunc("getSlotX", &Dialogue::getSlotX);
    cls.addFunc("setExpression", &Dialogue::setExpression);
    cls.addFunc("setMotion", &Dialogue::setMotion);
    cls.addFunc("syncStage", &Dialogue::syncStage);

    cls.addFunc("say", &Dialogue::say);
    cls.addFunc("narrate", &Dialogue::narrate);
    cls.addFunc("setTypeSpeed", &Dialogue::setTypeSpeed);
    cls.addFunc("getTypeSpeed", &Dialogue::getTypeSpeed);
    cls.addFunc("skipTyping", &Dialogue::skipTyping);
    cls.addFunc("isTyping", &Dialogue::isTyping);
    cls.addFunc("isWaitingAdvance", &Dialogue::isWaitingAdvance);
    cls.addFunc("isIdle", &Dialogue::isIdle);
    cls.addFunc("advance", &Dialogue::advance);
    cls.addFunc("getSpeakerId", &Dialogue::getSpeakerId);
    cls.addFunc("getSpeakerName", &Dialogue::getSpeakerName);
    cls.addFunc("getFullText", &Dialogue::getFullText);
    cls.addFunc("getVisibleText", &Dialogue::getVisibleText);
    cls.addFunc("getPhase", &Dialogue::getPhase);

    cls.addFunc("setLipSyncEnabled", &Dialogue::setLipSyncEnabled);
    cls.addFunc("isLipSyncEnabled", &Dialogue::isLipSyncEnabled);
    cls.addFunc("setLipSyncParameter", &Dialogue::setLipSyncParameter);
    cls.addFunc("getLipSyncParameter", &Dialogue::getLipSyncParameter);
    cls.addFunc("setLipSyncAmplitude", &Dialogue::setLipSyncAmplitude);
    cls.addFunc("getLipSyncAmplitude", &Dialogue::getLipSyncAmplitude);
    cls.addFunc("getLipSyncValue", &Dialogue::getLipSyncValue);

    cls.addFunc("clearChoices", &Dialogue::clearChoices);
    cls.addFunc("addChoice", &Dialogue::addChoice);
    cls.addFunc("presentChoices", &Dialogue::presentChoices);
    cls.addFunc("isWaitingChoice", &Dialogue::isWaitingChoice);
    cls.addFunc("getChoiceCount", &Dialogue::getChoiceCount);
    cls.addFunc("getChoiceId", &Dialogue::getChoiceId);
    cls.addFunc("getChoiceLabel", &Dialogue::getChoiceLabel);
    cls.addFunc("selectChoice", &Dialogue::selectChoice);
    cls.addFunc("getSelectedChoiceId", &Dialogue::getSelectedChoiceId);

    cls.addFunc("setVar", &Dialogue::setVar);
    cls.addFunc("getVarType", &Dialogue::getVarType);
    cls.addFunc("getVarInt", &Dialogue::getVarInt);
    cls.addFunc("getVarFloat", &Dialogue::getVarFloat);
    cls.addFunc("getVarBool", &Dialogue::getVarBool);
    cls.addFunc("getVarString", &Dialogue::getVarString);
    cls.addFunc("hasVar", &Dialogue::hasVar);
    cls.addFunc("clearVar", &Dialogue::clearVar);
    cls.addFunc("clearVars", &Dialogue::clearVars);

    cls.addFunc("registerCondition", &Dialogue::registerCondition);
    cls.addFunc("unregisterCondition", &Dialogue::unregisterCondition);
    cls.addFunc("evalCondition", &Dialogue::evalCondition);

    cls.addFunc("loadPoolsFromTable", &Dialogue::loadPoolsFromTable);
    cls.addFunc("loadPoolsFromDnut", &Dialogue::loadPoolsFromDnut);
    cls.addFunc("loadPoolsFromDnutFile", &Dialogue::loadPoolsFromDnutFile);
    cls.addFunc("clearPools", &Dialogue::clearPools);
    cls.addFunc("getPoolCount", &Dialogue::getPoolCount);
    cls.addFunc("getPoolId", &Dialogue::getPoolId);
    cls.addFunc("hasPool", &Dialogue::hasPool);
    cls.addFunc("getLastPoolsError", &Dialogue::getLastPoolsError);

    cls.addFunc("setRandomSeed", &Dialogue::setRandomSeed);
    cls.addFunc("getRandomSeed", &Dialogue::getRandomSeed);

    cls.addFunc("pickLine", &Dialogue::pickLine);
    cls.addFunc("playLine", &Dialogue::playLine);
    cls.addFunc("playPool", &Dialogue::playPool);
    cls.addFunc("getCurrentLineId", &Dialogue::getCurrentLineId);
    cls.addFunc("getCurrentLineMeta", &Dialogue::getCurrentLineMeta);
    cls.addFunc("getCurrentLineTags", &Dialogue::getCurrentLineTags);

    cls.addFunc("update", &Dialogue::update);
    cls.addFunc("reset", &Dialogue::reset);
}

}  // namespace eve::dialogue
