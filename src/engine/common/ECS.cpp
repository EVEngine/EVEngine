#include "common/ECS.h"
#include "common/ScriptComponentStore.h"
#include "common/ScriptError.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

// clang-format off
// Squirrel internals: recovering a component schema's *declaration order* needs the class
// member table and the field-index encoding (`_isfield` / `_member_idx`), which the public
// API cannot express (it only exposes the hash-ordered member iteration of §10.17).
// The order is load-bearing -- `sqpcheader.h` must come first and clang-format would sort
// `sqclass.h` ahead of it -- and these have to follow `<simplesquirrel/simplesquirrel.hpp>`,
// which brings in the public Squirrel types.
#include "sqpcheader.h"
#include "sqstring.h"
#include "sqtable.h"
#include "sqclass.h"
#include "sqvm.h"  // `_ss(vm)->_instance_default_delegate`（把默认委托装成类成员，见下）
// clang-format on

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve {
namespace {

// ---------------------------------------------------------------------------
// Script component registry (name + version → Squirrel class, with generations)
// ---------------------------------------------------------------------------

struct ScriptComponentEntry {
    HSQUIRRELVM vm = nullptr;
    HSQOBJECT   cls;
    Generation  generation;
    /** 声明顺序的字段名：schema，也是第 2 步摘字段槽位时的清单。 */
    std::vector<std::string> fields;
    /**
     * 声明顺序的**字段默认值**，与 `fields` 一一对应。
     *
     * 必须在摘槽位**之前**抄下来：槽位一摘，类和实例上都再也读不到声明值，而"每个字段的默认值"
     * 是 schema 的一部分（建引擎侧数据、热重载迁移换代时都要用）。
     */
    std::vector<Value> defaults;
    ScriptComponentEntry() { sq_resetobject(&cls); }
    ScriptComponentEntry(HSQUIRRELVM v, HSQOBJECT o, Generation g, std::vector<std::string> f, std::vector<Value> d)
        : vm(v), cls(o), generation(g), fields(std::move(f)), defaults(std::move(d)) {}
};

/** Keyed by (name, version) rather than by class object: hot reload replaces the class
 *  but keeps the identity, and identity is what migration and the engine-side store
 *  address. Ordered so enumeration is deterministic. */
using ScriptComponentKey = std::pair<std::string, std::uint64_t>;
using ScriptComponentMap = std::map<ScriptComponentKey, ScriptComponentEntry>;

ScriptComponentMap& currentScriptComponents() {
    static ScriptComponentMap m;
    return m;
}

/** Retired generations, metadata only: the replaced class object is kept alive by the
 *  instances that still reference it, so the registry does not hold a second reference. */
std::vector<ScriptComponentRecord>& retiredScriptComponents() {
    static std::vector<ScriptComponentRecord> v;
    return v;
}

int& nextEntityId() {
    static int id = 1;
    return id;
}

int allocEntityId() { return nextEntityId()++; }

// 当前 VM 的 Squirrel 类对象 → 脚本 view() 收集函数。
std::unordered_map<const void*, CppEntityViewFn>& cppEntityViews() {
    static std::unordered_map<const void*, CppEntityViewFn> views;
    return views;
}

std::vector<std::function<void(ssq::Table&)>>& postEcsHooks() {
    static std::vector<std::function<void(ssq::Table&)>> hooks;
    return hooks;
}

bool& ecsScriptInjected() {
    static bool injected = false;
    return injected;
}

size_t& postEcsHooksFlushed() {
    static size_t n = 0;
    return n;
}

// ---------------------------------------------------------------------------
// Class helpers
// ---------------------------------------------------------------------------

bool isSubclassOf(HSQUIRRELVM vm, HSQOBJECT child, HSQOBJECT base) {
    if (child._type != OT_CLASS || base._type != OT_CLASS) return false;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, child);
    for (int guard = 0; guard < 64; ++guard) {
        if (sq_gettype(vm, -1) != OT_CLASS) break;
        HSQOBJECT cur;
        sq_getstackobj(vm, -1, &cur);
        if (cur._unVal.pClass == base._unVal.pClass) {
            sq_settop(vm, top);
            return true;
        }
        if (SQ_FAILED(sq_getbase(vm, -1))) break;
        sq_remove(vm, -2);  // drop current, keep base
        if (sq_gettype(vm, -1) != OT_CLASS) break;
    }
    sq_settop(vm, top);
    return false;
}

/** Collect non-method members of a class into `out` (field defaults / statics). */
void collectClassFields(HSQUIRRELVM vm, HSQOBJECT clsObj, HSQOBJECT outTable) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, outTable);  // out
    sq_pushobject(vm, clsObj);    // out, cls
    sq_pushnull(vm);              // out, cls, iter
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        // out, cls, iter, key, value
        if (sq_gettype(vm, -2) == OT_STRING) {
            const SQChar* key = nullptr;
            sq_getstring(vm, -2, &key);
            if (key && key[0] != '_') {
                const SQObjectType vt = sq_gettype(vm, -1);
                if (vt != OT_CLOSURE && vt != OT_NATIVECLOSURE && vt != OT_GENERATOR) {
                    sq_push(vm, -5);  // out
                    sq_push(vm, -3);  // key
                    sq_push(vm, -3);  // value
                    sq_rawset(vm, -3);
                    sq_pop(vm, 1);  // pop out
                }
            }
        }
        sq_pop(vm, 2);  // key, value
    }
    sq_settop(vm, top);
}

/** 一个声明字段：名字 + 类成员表里的**键对象**（后者用于精确摘除槽位）。 */
struct DeclaredField {
    std::string name;
    SQObjectPtr key;
};

/**
 * @brief 类声明的**实例字段**，按声明顺序。
 *
 * 不用 `sq_next` 遍历取值：那是成员表的哈希顺序，增删一个字段就会让无关字段换位置（§10.17）。
 * Squirrel 内部把字段序号编进 `_members` 的值（`_isfield` / `_member_idx`），而 `_defaultvalues`
 * 是按声明顺序追加的，因此"按字段序号排序"就还原了声明顺序。方法成员带的是方法序号，被
 * `_isfield` 过滤掉；`_` 前缀的成员（`_get`/`_set`/构造器等）不算字段。
 *
 * @remarks **不需要**沿基类链递归：Squirrel 建立派生类时会把基类的成员一并放进派生类自己的
 *          `_members`（实测 `class EnemyStats extends BaseStats` 会同时列出 `hp` 与 `speed`），
 *          递归反而会把基类字段重复计一次。
 */
std::vector<DeclaredField> declaredFields(HSQOBJECT clsObj) {
    std::vector<std::pair<SQInteger, DeclaredField>> ordered;
    SQClass*                                         klass = _class(clsObj);
    if (klass == nullptr || klass->_members == nullptr) return {};

    SQObjectPtr key;
    SQObjectPtr value;
    SQObjectPtr refpos;  // `Next` 的游标是它自己返回的整数下标，不是 key（传 key 会命中 TranslateIndex 的断言）
    SQInteger   index = 0;
    while ((index = klass->_members->Next(false, refpos, key, value)) != -1) {
        refpos = SQObjectPtr(index);
        if (sq_type(key) != OT_STRING || !_isfield(value)) continue;
        const SQChar* name = _stringval(key);
        if (name == nullptr || name[0] == '_') continue;
        ordered.emplace_back(static_cast<SQInteger>(_member_idx(value)), DeclaredField{name, key});
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<DeclaredField> out;
    out.reserve(ordered.size());
    for (auto& entry : ordered) out.push_back(std::move(entry.second));
    return out;
}

/** 字段名列表（schema 用）。 */
std::vector<std::string> fieldNames(const std::vector<DeclaredField>& fields) {
    std::vector<std::string> names;
    names.reserve(fields.size());
    for (const DeclaredField& field : fields) names.push_back(field.name);
    return names;
}

ssq::Table inspectClassFields(ssq::Object clsObj) {
    HSQUIRRELVM vm = clsObj.getHandle();
    ssq::Table out(vm);
    if (clsObj.getType() != ssq::Type::CLASS) return out;
    collectClassFields(vm, clsObj.getRaw(), out.getRaw());
    return out;
}

/** 读类里某个字段的默认值（声明时写的那个值就是 schema 默认值）。 */
Result<Value> classFieldDefault(HSQUIRRELVM vm, HSQOBJECT clsObj, const std::string& field) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, clsObj);
    sq_pushstring(vm, field.c_str(), static_cast<SQInteger>(field.size()));
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "component field default is not readable", field, {}, "ecs.component"));
    }
    const SQInteger valueIndex = sq_gettop(vm);
    Result<Value>   value      = script::valueFromSquirrel(vm, valueIndex);
    sq_settop(vm, top);
    return value;
}

/** 一个已登记组件的 schema：公开身份记录 + 就是建数据条目要用的**声明默认值**。 */
struct ScriptComponentSchema {
    ScriptComponentRecord record;
    /** 与 `record.fields` 同序、同长。 */
    const std::vector<Value>* defaults = nullptr;
};

/**
 * @brief 类对应的组件 schema（名字 + 版本 + 代 + 字段 + 声明默认值）。
 * @return 找到则为 true；类对象没登记过则为 false（该组件保持今天的槽位语义）。
 * @remarks 类对象按**裸句柄**传入而不是 `ssq::Object`：调用方是从栈上借用的，
 *          而 `ssq::Object` 的析构会 `sq_release`，包一层很容易变成双重释放
 *          （实测会触发 Squirrel `RefTable::Release` 的 `assert(0)`，见 §9.64）。
 */
bool schemaOfComponentClass(HSQOBJECT cls, ScriptComponentSchema* out) {
    const SQClass* klass = _class(cls);
    if (klass == nullptr) return false;
    for (const auto& [key, entry] : currentScriptComponents()) {
        if (_class(entry.cls) != klass) continue;
        if (out != nullptr) {
            out->record.name       = key.first;
            out->record.version    = SchemaVersion(key.second);
            out->record.generation = entry.generation;
            out->record.retired    = false;
            out->record.fields     = entry.fields;
            out->defaults          = &entry.defaults;
        }
        return true;
    }
    return false;
}

bool dropScriptComponentValues(std::uint64_t entityId) { return removeScriptComponentValues(entityId) > 0; }

// ---------------------------------------------------------------------------
// 已登记组件的字段接缝：`_get` / `_set` 把脚本字段读写接到引擎侧存储
// ---------------------------------------------------------------------------

/** 读实例上某个**已声明**槽位（`sq_get` 对已声明成员不会走 `_get`）。 */
bool instanceSlot(HSQUIRRELVM vm, SQInteger index, const char* name, HSQOBJECT* out) {
    const SQInteger top  = sq_gettop(vm);
    const SQInteger self = index < 0 ? top + index + 1 : index;
    sq_pushstring(vm, name, -1);
    if (SQ_FAILED(sq_get(vm, self))) {
        sq_settop(vm, top);
        return false;
    }
    if (out != nullptr) sq_getstackobj(vm, -1, out);
    sq_settop(vm, top);
    return true;
}

/** 组件实例的归属身份 `(实体, 组件名, 版本)`；未绑定时返回 false。 */
bool instanceIdentity(HSQUIRRELVM vm, SQInteger index, ScriptComponentStoreKey* out) {
    HSQOBJECT eid{};
    HSQOBJECT name{};
    HSQOBJECT version{};
    if (!instanceSlot(vm, index, "_ownerEid", &eid) || sq_type(eid) != OT_INTEGER) return false;
    if (!instanceSlot(vm, index, "_componentName", &name) || sq_type(name) != OT_STRING) return false;
    if (!instanceSlot(vm, index, "_componentVersion", &version) || sq_type(version) != OT_INTEGER) return false;
    if (_integer(eid) <= 0 || _stringval(name)[0] == '\0') return false;
    out->entityId = static_cast<std::uint64_t>(_integer(eid));
    out->name     = _stringval(name);
    out->version  = SchemaVersion(static_cast<std::uint64_t>(_integer(version)));
    return true;
}

/** 取栈上某个位置的字段名（必须是字符串；调用方已判类型）。 */
std::string stackFieldName(HSQUIRRELVM vm, SQInteger index) {
    const SQChar* text = nullptr;
    if (SQ_FAILED(sq_getstring(vm, index, &text)) || text == nullptr) return {};
    return text;
}

/** `_get(key)`：从引擎侧存储读字段。 */
SQInteger scriptComponentFieldGet(HSQUIRRELVM vm) {
    const SQInteger         top = sq_gettop(vm);
    ScriptComponentStoreKey key;
    if (!instanceIdentity(vm, 1, &key)) return sq_throwerror(vm, _SC("component instance is not bound to an entity"));
    if (sq_gettype(vm, 2) != OT_STRING) return sq_throwerror(vm, _SC("component field name must be a string"));

    const std::string field = stackFieldName(vm, 2);
    Result<Value>     value = scriptComponentField(key, field);
    if (!value.ok()) {
        sq_settop(vm, top);
        return sq_throwerror(vm, ("'" + field + "' is not a declared field of component '" + key.name + "'").c_str());
    }
    Result<void> pushed = script::pushValue(vm, value.value());
    if (!pushed.ok()) {
        sq_settop(vm, top);
        return sq_throwerror(vm, pushed.status().describe().c_str());
    }
    return 1;
}

/** `_set(key, value)`：写进引擎侧存储；schema 之外的字段名直接报错（不静默收下）。 */
SQInteger scriptComponentFieldSet(HSQUIRRELVM vm) {
    const SQInteger         top = sq_gettop(vm);
    ScriptComponentStoreKey key;
    if (!instanceIdentity(vm, 1, &key)) return sq_throwerror(vm, _SC("component instance is not bound to an entity"));
    if (sq_gettype(vm, 2) != OT_STRING) return sq_throwerror(vm, _SC("component field name must be a string"));

    const std::string field = stackFieldName(vm, 2);
    Result<Value>     value = script::valueFromSquirrel(vm, 3);
    if (!value.ok()) {
        sq_settop(vm, top);
        return sq_throwerror(vm, value.status().describe().c_str());
    }
    Result<void> written = setScriptComponentField(key, field, std::move(value).takeValue());
    if (!written.ok()) {
        sq_settop(vm, top);
        return sq_throwerror(vm, ("'" + field + "' is not a declared field of component '" + key.name + "'").c_str());
    }
    sq_settop(vm, top);
    sq_push(vm, 3);
    return 1;
}

/** 类上是否已装 `_get`/`_set`（用户自己写了就不许我们覆盖）。 */
bool classDefinesFieldSeam(HSQOBJECT clsObj) {
    SQClass* klass = _class(clsObj);
    if (klass == nullptr) return false;
    return sq_type(klass->_metamethods[MT_GET]) != OT_NULL || sq_type(klass->_metamethods[MT_SET]) != OT_NULL;
}

/** 见定义处：把实例默认委托的方法装成类成员，免得 `_get` 把它们遮住。 */
void installInstanceDelegateMembers(HSQUIRRELVM vm, HSQOBJECT clsObj);

/**
 * @brief 装上 `_get`/`_set` 并摘掉声明的字段槽位——这一步让引擎成为字段的**唯一权威**。
 *
 * 摘槽位是必需的：声明过的成员会**遮蔽**元方法（实测 §9.61.1），不摘就永远还是脚本实例自己的
 * 槽位在存数据。摘掉之后同名成员的读写都落到元方法上，于是 `e.pos.x = 1` 的写法不变，
 * 数据却进了引擎侧存储。
 *
 * @return 成功；用户类自己定义了 `_get`/`_set` 时返回 `AlreadyExists`（不覆盖、也不摘槽位）。
 */
Result<void> installComponentFieldSeam(HSQUIRRELVM vm, HSQOBJECT clsObj, const std::vector<DeclaredField>& fields) {
    if (classDefinesFieldSeam(clsObj))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists,
                              "component class defines _get/_set itself; the engine field seam needs those two names",
                              {}, {}, "ecs.component"));

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, clsObj);
    const auto install = [&](const char* name, SQFUNCTION fn) {
        sq_pushstring(vm, name, -1);
        sq_newclosure(vm, fn, 0);
        sq_newslot(vm, -3, SQFalse);
    };
    install("_get", scriptComponentFieldGet);
    install("_set", scriptComponentFieldSet);

    if (_class(clsObj) != nullptr) {
        for (const DeclaredField& field : fields) _class(clsObj)->_members->Remove(field.key);
    }
    installInstanceDelegateMembers(vm, clsObj);
    sq_settop(vm, top);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

/**
 * @brief 把实例**默认委托**里的方法原样装成类的成员。
 *
 * 为什么非做不可：`SQVM::Get` 只在元方法**干净地找不到**时才去问默认委托，而我们的 `_get` 对
 * 非字段名是**抛错**的（元方法回了错误而不是"没这一项"）——于是实例默认委托里的六个方法全被
 * 遮住（`sqbaselib.cpp:1354` 的 `_instance_default_delegate_funcz`：`getclass` / `rawget` /
 * `rawset` / `rawin` / `weakref` / `tostring`）。实测症状：`load.nut` 的 `migrate_instance`
 * 在登记过的组件实例上直接抛 `'getclass' is not a declared field of component 'HotPos'`。
 * 装成**成员**之后实例查找先命中成员，委托不再被遮住；语义与委托里那一份完全相同（同一批
 * 原生闭包）。非字段成员不会被 `collectClassFields`/`declaredFields` 当成字段（§9.64.2）。
 */
void installInstanceDelegateMembers(HSQUIRRELVM vm, HSQOBJECT clsObj) {
    SQClass* klass = _class(clsObj);
    if (klass == nullptr) return;
    SQTable* delegates = _table(_ss(vm)->_instance_default_delegate);
    if (delegates == nullptr) return;

    SQObjectPtr key;
    SQObjectPtr value;
    SQObjectPtr refpos;
    SQInteger   index = 0;
    while ((index = delegates->Next(false, refpos, key, value)) != -1) {
        refpos = SQObjectPtr(index);
        if (sq_type(key) != OT_STRING || sq_type(value) != OT_NATIVECLOSURE) continue;
        klass->NewSlot(_ss(vm), key, value, false);
    }
}

/**
 * @brief 给组件实例盖归属、按 schema 建引擎侧数据条目，并把这一代的字段名还给脚本。
 *
 * 两件事都必须发生在脚本写默认值**之前**：槽位已摘、`_set` 已装，所以那些写入本身就进存储，
 * 没有已绑定的条目就会报错。
 *
 * 为什么要把字段名**还回去**：槽位摘掉之后脚本侧 `inspectClassFields(compClass)` 什么都看不到，
 * 而脚本还要按声明顺序把"类型标记"（`x = eve.Number` → `0.0`）解析成真值。字段名由注册表给，
 * 声明默认值已经按 schema 落在存储里，脚本侧 `c[name]` 读到的就是它——标记规则因此仍然只有
 * 脚本里那一处（§9.64）。
 *
 * @return 这一代的字段名数组（声明顺序）；组件没登记过时返回一个空对象（`null`），
 *         调用方据此保持纯槽位语义。
 */
ssq::Object bindComponentValues(std::uint64_t entityId, ssq::Object instance) {
    HSQUIRRELVM vm = instance.getHandle();
    ssq::Object none(vm);
    if (instance.getType() != ssq::Type::INSTANCE) return none;

    // 类对象只**借用**：实例自己持有类引用（`SQInstance::SQInstance` 里 `__ObjAddRef(_class)`），
    // 所以查完注册表、把类弹出栈之后，注册表里那份引用仍然有效。
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    HSQOBJECT cls{};
    if (SQ_FAILED(sq_getclass(vm, -1))) {
        sq_settop(vm, top);
        return none;
    }
    sq_getstackobj(vm, -1, &cls);

    ScriptComponentSchema schema;
    const bool            known = schemaOfComponentClass(cls, &schema);
    sq_settop(vm, top);
    if (!known) return none;

    const ScriptComponentStoreKey key{entityId, schema.record.name, schema.record.version};
    const auto                    stamp = [&](const char* name, const auto& push) {
        const SQInteger inner = sq_gettop(vm);
        sq_pushobject(vm, instance.getRaw());
        sq_pushstring(vm, name, -1);
        push();
        sq_rawset(vm, -3);  // raw：`_set` 是字段接缝，控制槽位不能从那里走
        sq_settop(vm, inner);
    };
    stamp("_ownerEid", [&] { sq_pushinteger(vm, static_cast<SQInteger>(entityId)); });
    stamp("_componentName", [&] { sq_pushstring(vm, schema.record.name.c_str(), -1); });
    stamp("_componentVersion", [&] { sq_pushinteger(vm, static_cast<SQInteger>(schema.record.version.value())); });

    if (findScriptComponentValues(key) == nullptr) {
        Value::Object                   values;
        const std::vector<Value>&       declared = *schema.defaults;
        const std::vector<std::string>& fields   = schema.record.fields;
        for (std::size_t i = 0; i < fields.size(); ++i)
            values.emplace(fields[i], i < declared.size() ? declared[i] : Value::null());
        setScriptComponentValues(key, Value::object(std::move(values)))
            .ignore("a component whose values cannot be stored would silently lose its data if this succeeded");
    }

    ssq::Array names(vm);
    for (const std::string& field : schema.record.fields) names.push(field);
    return names;
}

bool scriptIsSubclass(ssq::Object child, ssq::Object base) {
    if (child.getType() != ssq::Type::CLASS || base.getType() != ssq::Type::CLASS) return false;
    return isSubclassOf(child.getHandle(), child.getRaw(), base.getRaw());
}

ssq::Object getClassBase(ssq::Object cls) {
    HSQUIRRELVM vm = cls.getHandle();
    ssq::Object ret(vm);
    if (cls.getType() != ssq::Type::CLASS) return ret;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, cls.getRaw());
    if (SQ_FAILED(sq_getbase(vm, -1)) || sq_gettype(vm, -1) != OT_CLASS) {
        sq_settop(vm, top);
        return ret;
    }
    sq_getstackobj(vm, -1, &ret.getRaw());
    sq_addref(vm, &ret.getRaw());
    sq_settop(vm, top);
    return ret;
}

// ---------------------------------------------------------------------------
// Injected Squirrel runtime
// ---------------------------------------------------------------------------

const char* kEcsScript = R"SQ(
// Script ECS: define Component / Entity classes, create instances, view & System.
// Mirrors ECS.hpp's class + component model for Squirrel game logic.

if (!("Number" in eve)) eve.Number <- "number"
if (!("Boolean" in eve)) eve.Boolean <- "boolean"
if (!("String" in eve)) eve.String <- "string"

eve._ecsTypes <- {}   // EntityClass -> { cls, instances }

// --- 性能缓存（万级实体稳态零分配） ---
// view 结果缓存：create/destroy 时按类链失效；C++ 桥接类（_cppHasView=true）不缓存。
eve._ecsViewCache <- {}       // EntityClass -> array
eve._ecsSlotsCache <- {}      // EntityClass -> { fieldName = ComponentClass }
eve._ecsFieldSlotsCache <- {} // EntityClass -> { fieldName = true }：只有这些槽位有真实字段
eve._ecsCompDefaults <- {}    // ComponentClass -> { fieldName = resolvedDefault }
eve._ecsRevision <- 0         // increments on script entity structural changes

function eve::_ecsResolveMarker(v) {
    if (v == this.Number || v == "number") return 0.0
    if (v == this.Boolean || v == "boolean") return false
    if (v == this.String || v == "string") return ""
    return v
}

function eve::_ecsIsComponentClass(obj) {
    if (typeof obj != "class") return false
    return this.isSubclass(obj, this.Component)
}

// 解析"类型标记"（`x = eve.Number` → `0.0`）：只处理**当前值本身是标记**的字段，其它字段
// 一律不碰，所以这个函数可以重复跑（实例化、热重载迁移都调它），不会覆盖已有数据。
//
// `declared` 是引擎侧给的字段名数组（登记组件）或 null（未登记组件）。登记组件的声明默认值
// 已经按 schema 落在引擎侧存储里，`c[name]` 走 `_get` 读到的就是它；未登记组件从类声明读。
// 两种组件因此共用同一段解析，标记规则仍然只有这一处实现（§9.64.4）。
function eve::_ecsResolveComponentMarkers(compClass, c, declared) {
    local fields = {}
    if (declared != null) {
        foreach (name in declared) fields[name] <- true
    } else {
        fields = this.inspectClassFields(compClass)
    }
    local resolved = {}
    foreach (name, val in fields) {
        local cur = null
        try { cur = c[name] } catch (e) { cur = null }
        if (cur == this.Number || cur == this.Boolean || cur == this.String
            || cur == "number" || cur == "boolean" || cur == "string") {
            c[name] = this._ecsResolveMarker(cur)
            resolved[name] <- c[name]
        }
    }
    return resolved
}

function eve::_ecsInstantiateComponent(compClass, ownerEid = 0) {
    local c = compClass()
    // 先盖归属、按 schema 建引擎侧数据条目，**再**写默认值：已登记组件的字段槽位在登记时就被摘掉、
    // `_set` 已装好，所以下面那些 `c[name] = val` 实际上是**写进引擎存储**，必须有个已绑定的条目
    // 接着（顺序反了就会报 "not bound to an entity"）。
    // 返回值是这一代的字段名（声明顺序）：槽位摘掉之后 `inspectClassFields` 什么也看不到，
    // 而"类型标记"还得按声明顺序解析。未登记组件返回 null。
    local declared = eve._ecsBindComponentValues(ownerEid, c)
    if (compClass in this._ecsCompDefaults) {
        foreach (name, val in this._ecsCompDefaults[compClass]) c[name] = val
    } else {
        this._ecsCompDefaults[compClass] <- this._ecsResolveComponentMarkers(compClass, c, declared)
    }
    return c
}

function eve::_ecsCollectSlots(cls) {
    if (cls in this._ecsSlotsCache) return this._ecsSlotsCache[cls]
    // { fieldName = ComponentClass } walking base classes (Entity → … → cls).
    local slots = {}
    local cur = cls
    local guard = 0
    while (cur != null && guard < 64) {
        if (!this.isSubclass(cur, this.Entity)) break

        local declared = null
        try { declared = cur.components } catch (e) { declared = null }
        if (typeof declared == "table") {
            foreach (k, v in declared) {
                if (!(k in slots) && this._ecsIsComponentClass(v))
                    slots[k] <- v
            }
        }

        local fields = this.inspectClassFields(cur)
        foreach (k, v in fields) {
            if (k == "components") continue
            if (!(k in slots) && this._ecsIsComponentClass(v))
                slots[k] <- v
        }

        cur = this.getClassBase(cur)
        guard += 1
    }
    this._ecsSlotsCache[cls] <- slots
    return slots
}

// 哪些槽位在实体类里有**真实字段**（`pos = Pos` 或 `pos = null`），哪些只是登记在
// `static components` 表里。判据是"这个类有没有声明同名的非函数成员"，而不是"它的值
// 是不是组件类"——`static components = { pos = Pos }` + `pos = null` 这种写法里字段值
// 是 null，但字段确实存在、可以赋值。
//
// 只有有字段的槽位能用 `e.pos` 访问：Squirrel 拒绝对 class 实例新建槽位，所以
// `e[name] = c` 对未声明的键会抛 "the index 'name' does not exist"（`=` 编译成
// _OP_SET；只有 `<-` 才是 new-slot 操作码，报的是另一句
// "class instances do not support the new slot operator"）。
function eve::_ecsCollectFieldSlots(cls) {
    if (cls in this._ecsFieldSlotsCache) return this._ecsFieldSlotsCache[cls]
    local fields = {}
    local cur = cls
    local guard = 0
    while (cur != null && guard < 64) {
        if (!this.isSubclass(cur, this.Entity)) break
        foreach (k, v in this.inspectClassFields(cur)) {
            if (k == "components") continue
            if (!(k in fields)) fields[k] <- true
        }
        cur = this.getClassBase(cur)
        guard += 1
    }
    this._ecsFieldSlotsCache[cls] <- fields
    return fields
}

function eve::_ecsInvalidateViews(cls) {
    local cur = cls
    local guard = 0
    while (cur != null && guard < 64) {
        if (cur in this._ecsViewCache) delete this._ecsViewCache[cur]
        if (!this.isSubclass(cur, this.Entity)) break
        cur = this.getClassBase(cur)
        guard += 1
    }
}

function eve::_ecsEnsureType(cls) {
    if (cls in this._ecsTypes) return this._ecsTypes[cls]
    local info = { cls = cls, instances = [] }
    this._ecsTypes[cls] <- info
    return info
}

function eve::_ecsRegisterInstance(entity, cls) {
    this._ecsEnsureType(cls).instances.push(entity)
    this._ecsRevision += 1
    this._ecsInvalidateViews(cls)
}

function eve::_ecsUnregisterInstance(entity, cls) {
    if (!(cls in this._ecsTypes)) return
    local arr = this._ecsTypes[cls].instances
    for (local i = arr.len() - 1; i >= 0; --i) {
        if (arr[i] == entity) {
            // O(1) swap-remove（顺序不保证）；视图缓存随后失效重建
            local last = arr.len() - 1
            if (i != last) arr[i] = arr[last]
            arr.pop()
            this._ecsRevision += 1
            break
        }
    }
    this._ecsInvalidateViews(cls)
}

function eve::_ecsCollectInstances(cls, out) {
    foreach (key, info in this._ecsTypes) {
        if (info.cls == cls || this.isSubclass(info.cls, cls)) {
            foreach (e in info.instances) {
                if (e != null && e.isAlive()) out.push(e)
            }
        }
    }
    // 稳定顺序：按实体 id 升序（= 创建顺序）。
    //
    // `_ecsUnregisterInstance` 用 swap-remove 保持 O(1) 销毁，所以 instances 数组本身的
    // 顺序在删除后会变——实测 [1,2,3,4] 删掉 2 得到 [1,4,3]。而 GPU System 按**视图顺序**
    // 把实体打包成 buffer 的行，回读时又按同一顺序写回（`unpackEcsFloats(ents, …)`），
    // `requestUploadRange/requestReadbackRange(first, count)` 更是直接按行号寻址：顺序一变，
    // "第 i 行是哪个实体"就变了，回读的值会落到另一个实体上。
    //
    // id 由 `eve.allocEntityId()` 单调分配且**不复用**，所以按 id 排序给出一个跨子类也
    // 成立的全序；`_ecsTypes` 的散列遍历顺序因此不再影响结果。排序只发生在缓存重建时
    // （结构性变更后第一次取视图），每帧取视图仍然是 O(n) 拷贝。
    out.sort(function(a, b) { return a._eid - b._eid })
}

// ---------------------------------------------------------------------------
// 热重载迁移（方案文档实施顺序第 3 步）
// ---------------------------------------------------------------------------

// 未登记组件的字段只存在实例槽位里，换实例就会丢：把旧实例**声明过的**字段值搬到新实例上。
// 枚举类而不是实例——实例不可枚举（`foreach (k, v in instance)` 抛 "_nexti failed"），类可枚举；
// 而 `fresh[name] = old[name]` 对新类声明过的成员成功、对没声明的抛错，正是这里要的过滤
// （与 `load.nut` 的 `migrate_instance` 同一套理由）。
//
// 已登记组件的字段槽位在登记时就被摘掉了，所以 `inspectClassFields(旧类)` 是空表、这里什么也
// 不搬——那些数据本来就在引擎侧存储里，按 (实体, 名字, 版本) 寻址，换代时由 C++ 侧对齐 schema。
function eve::_ecsCopyDeclaredFields(old, fresh) {
    if (typeof old != "instance") return 0
    local declared = null
    try { declared = this.inspectClassFields(old.getclass()) } catch (e) { return 0 }
    local copied = 0
    foreach (name, value in declared) {
        try { fresh[name] = old[name]; copied += 1 } catch (e) { }
    }
    return copied
}

// 把一个实体的一个组件槽位换到**新代类**的实例上。返回 false 表示换不了（调用方据此报出来）。
function eve::_ecsRepointComponent(e, slotName, oldComp, compClass, fieldSlots) {
    local fresh = compClass()
    local declared = eve._ecsBindComponentValues(e._eid, fresh)
    if (declared == null && typeof oldComp == "instance" && oldComp._componentName != "") {
        // 旧实例是**已登记**组件的实例，而新类不是同一身份的登记（登记没跟着换代）：
        // 接管过去只会得到一个读不了字段的实例，宁可留着旧的并报出来。
        return false
    }
    if (!(compClass in this._ecsCompDefaults)) {
        this._ecsCompDefaults[compClass] <- this._ecsResolveComponentMarkers(compClass, fresh, declared)
    } else if (declared != null) {
        // 已登记组件：默认值在引擎侧存储里，这里**不能**把缓存默认值写回去——那会把迁移
        // 保留下来的旧值冲掉。只补新字段的类型标记。
        this._ecsResolveComponentMarkers(compClass, fresh, declared)
    } else {
        foreach (name, val in this._ecsCompDefaults[compClass]) fresh[name] = val
    }
    if (declared == null) this._ecsCopyDeclaredFields(oldComp, fresh)
    e._comps[slotName] = fresh
    if (slotName in fieldSlots) e[slotName] = fresh
    return true
}

/**
 * 热重载之后把脚本 ECS 迁移到重定义后的类上（决定①：同名重定义 = 同一类型的新代）。
 *
 * `oldBindings` 是重载**之前**抓的根表快照、`rootTable` 是重载之后的根表（`load.nut` 的
 * `capture_reload_bindings()` 与 `getroottable()`）：同一个全局名字前后都是类 = 同一个定义被
 * 重写，这就是"同名"的判据，不需要新机制。两张根表都由调用方传进来——注入的 ECS chunk 以 `eve`
 * 为 `this`，在里面取不到 `getroottable()`（实测报 "the index 'getroottable' does not exist"）。
 *
 * 三件事：
 *   1. 把以**类对象**为键的表（`_ecsTypes`、视图/槽位/字段槽位缓存）从旧类搬到新类名下。
 *      §9.42.5 实测旧项不会被回收，每重载一次就多一组孤儿（`oldClassView=2, newClassView=0`）。
 *   2. 每个活实体的组件实例换成新代类的实例，于是 `getComponent(新类)` 拿得到东西。
 *   3. 返回计数：`unresolved > 0` 表示有槽位迁移不了（典型是"实体类没被重写、还指向旧组件类"
 *      ——脚本侧给 class 赋值会抛错，只有原生 `sq_rawset` 能改类的字段默认值），调用方必须
 *      **报出来**，不许静默留孤儿。
 */
function eve::_ecsMigrateAfterReload(oldBindings, rootTable) {
    local replacements = {};
    if (rootTable == null) return { entities = 0, components = 0, unresolved = 0 };
    foreach (name, oldClass in oldBindings) {
        if (typeof oldClass != "class" || !(name in rootTable)) continue;
        local newClass = rootTable[name];
        if (typeof newClass != "class" || newClass == oldClass) continue;
        if (!this.isSubclass(newClass, this.Component) && !this.isSubclass(newClass, this.Entity)) continue;
        replacements[oldClass] <- newClass;
    }

    // 第 1 步：换键。旧项一律删掉，否则热重载每来一次就多一组看不见的实例（§9.42.5 第 5 点）。
    foreach (oldClass, newClass in replacements) {
        if (oldClass in this._ecsTypes) {
            this._ecsTypes[newClass] <- this._ecsTypes[oldClass];
            this._ecsTypes[newClass].cls = newClass;
            delete this._ecsTypes[oldClass];
        }
        if (oldClass in this._ecsViewCache) delete this._ecsViewCache[oldClass];
        if (oldClass in this._ecsSlotsCache) delete this._ecsSlotsCache[oldClass];
        if (oldClass in this._ecsFieldSlotsCache) delete this._ecsFieldSlotsCache[oldClass];
        if (oldClass in this._ecsCompDefaults) delete this._ecsCompDefaults[oldClass];
    }

    // 第 2 步：接实例。
    local entities = 0;
    local components = 0;
    local unresolved = 0;
    foreach (etype, info in this._ecsTypes) {
        local slots = this._ecsCollectSlots(etype);  // 按新类重算：它引用的是新组件类
        local fieldSlots = this._ecsCollectFieldSlots(etype);
        foreach (e in info.instances) {
            if (e == null || !e.isAlive()) continue;
            e._etype = etype;
            e._slots = slots;
            foreach (slotName, compClass in slots) {
                if (!(slotName in e._comps)) continue;
                local old = e._comps[slotName];
                if (old == null) continue;
                if (compClass in replacements) {
                    // 实体类没被重写：槽位还指向**旧**组件类，引擎改不了类的字段默认值。
                    unresolved += 1;
                    continue;
                }
                if (typeof old == "instance" && old.getclass() == compClass) continue;
                if (this._ecsRepointComponent(e, slotName, old, compClass, fieldSlots)) components += 1;
                else unresolved += 1;
            }
            entities += 1;
        }
        this._ecsInvalidateViews(etype);
    }
    if (entities > 0) this._ecsRevision += 1;
    return { entities = entities, components = components, unresolved = unresolved };
}

eve.Component <- class {
    // 引擎侧数据条目的归属。必须是**已声明**成员：Squirrel 的实例只能写已声明槽位，而
    // `_get`/`_set` 要靠它们找到"这份数据属于哪个实体的哪个组件"。`_` 前缀保证它们
    // 不会被当成组件字段收进 schema（见 declaredInstanceFields 的过滤）。
    _ownerEid = 0
    _componentName = ""
    _componentVersion = 0

    constructor() {
        _ownerEid = 0
        _componentName = ""
        _componentVersion = 0
    }
}

eve.Entity <- class {
    _eid = 0
    _alive = false
    _etype = null
    _slots = null
    _comps = null

    constructor() {
        _eid = 0
        _alive = false
        _etype = null
        _slots = null
        _comps = null
    }

    static function create() {
        local e = this()
        e._etype = this
        e._eid = eve.allocEntityId()
        e._alive = true
        e._slots = eve._ecsCollectSlots(this)
        e._comps = {}
        local fieldSlots = eve._ecsCollectFieldSlots(this)
        foreach (name, compClass in e._slots) {
            // 传实体 id：已登记组件据此盖归属、建引擎侧数据条目（见 `_ecsInstantiateComponent`）。
            local c = eve._ecsInstantiateComponent(compClass, e._eid)
            e._comps[name] <- c
            // 已声明的槽位（`pos = Pos`）同时写进实体字段，于是 `e.pos` 与
            // `e.getComponent(Pos)` 是**同一个对象**，两条路径读写一致（实测，见 §9.38）。
            // 只登记在 `static components` 表里、没有同名字段的槽位没有字段可写，它只
            // 能经 getComponent()/hasComponent() 访问。这里因此按"是否真实字段"分流，
            // 而不是用 try/catch 把真正的失败吞掉。
            if (name in fieldSlots) e[name] = c
        }
        eve._ecsRegisterInstance(e, this)
        return e
    }

    // Declare a component slot on this entity class (optional sugar).
    static function component(name, compClass) {
        if (!("components" in this) || typeof this.components != "table")
            this.components <- {}
        this.components[name] <- compClass
        return this
    }

    function getId() { return _eid }
    function isAlive() { return _alive }

    function getComponent(compClass) {
        if (_slots == null || _comps == null) return null
        foreach (name, cls in _slots) {
            if (cls == compClass) {
                if (name in _comps) return _comps[name]
                return null
            }
        }
        return null
    }

    function hasComponent(compClass) {
        return getComponent(compClass) != null
    }

    function destroy() {
        if (!_alive) return
        _alive = false
        if (_etype != null) eve._ecsUnregisterInstance(this, _etype)
        // 组件状态一并释放。留着它的话，销毁后的实体仍然可读可写、却对任何 view 无效
        // （静默 no-op），而且每个组件对象都被这个已销毁的实体继续引用着。
        // 两条引用都要清：`_comps` 是槽位表，已声明字段槽位是 `e.pos` 那条路。只清前者
        // 会让 `e.pos` 继续指向一个活着的组件实例，于是对已销毁实体写 `e.pos.x = 1`
        // 会静默改一个孤儿——正是上面要避免的状态。`this[name] = null` 对**已声明**成员
        // 是普通赋值，实测不抛（"会抛"的旧结论是把 `<-` 的行为记到了 `=` 上，见 §9.38）。
        if (_slots != null && _etype != null) {
            local fieldSlots = eve._ecsCollectFieldSlots(_etype)
            foreach (name, compClass in _slots) {
                if (name in fieldSlots) this[name] = null
            }
        }
        _comps = null
        // 引擎侧的组件数据也要清：它是权威存储，留着就是"销毁后仍可读"的孤儿数据。
        eve._ecsDropComponentValues(_eid)
    }
}

// Historical name from early game examples
eve.EntityContainer <- eve.Entity

eve.System <- class {
    _query = null

    constructor(query = null) {
        _query = query
    }

    function setQuery(query) { _query = query }

    function entities() {
        if (_query == null) return []
        if (typeof _query == "array") {
            local out = []
            foreach (q in _query) {
                foreach (e in eve.view(q)) out.push(e)
            }
            return out
        }
        return eve.view(_query)
    }

    function update(dt) {}
}

// GPU-backed System: pack float component fields → SSBO → compute shader → unpack.
// Requires eve.Gpgpu / eve.EcsShaderSystem (gpgpu module). Push constants:
//   pc.data[0] = dt, pc.data[1] = entityCount. Each bindFields() maps one binding.
eve.ShaderSystem <- class extends eve.System {
    _gpu = null
    _backend = null
    _bindings = null
    _readback = true
    _localSize = 64

    constructor(query = null, gpu = null, glsl = null, localSize = 64) {
        base.constructor(query)
        _bindings = []
        _readback = true
        _localSize = localSize
        if (gpu != null) setGpgpu(gpu)
        if (gpu != null && glsl != null) setShaderSource(glsl)
    }

    function setGpgpu(gpu) {
        _gpu = gpu
        if (_backend == null) {
            if (!("EcsShaderSystem" in eve))
                throw "eve.ShaderSystem requires gpgpu module (eve.EcsShaderSystem)"
            _backend = eve.EcsShaderSystem()
        }
        _backend.setGpgpu(gpu)
        _backend.setLocalSize(_localSize)
        return this
    }

    function setShaderSource(glsl) {
        if (_backend == null) throw "eve.ShaderSystem.setShaderSource: call setGpgpu first"
        _backend.setShaderSource(glsl)
        return this
    }

    function setLocalSize(n) {
        _localSize = n
        if (_backend != null) _backend.setLocalSize(n)
        return this
    }

    function setReadback(enabled) {
        _readback = enabled
        return this
    }

    // binding: SSBO index; slot: entity component field name; fields: ["x","y",...]
    // uploadEachUpdate=false keeps the buffer GPU-resident after its initial upload.
    // readback=false leaves results resident until requestReadback() is called.
    function bindFields(binding, slot, fields, uploadEachUpdate = true, readback = null) {
        _bindings.push({ binding = binding, slot = slot, fields = fields,
                         uploadEachUpdate = uploadEachUpdate,
                         readback = readback, uploadedRevision = -1,
                         uploadRequested = false, readbackRequested = false,
                         uploadFirst = 0, uploadCount = 0,
                         readbackFirst = 0, readbackCount = 0,
                         source = null, sourceBinding = -1,
                         initializeFromCpu = true, schema = null })
        return this
    }

    // Typed binding schema: { slot, fields, scalar="f32", access="read_write" }.
    // write-only bindings allocate storage without an unnecessary CPU upload.
    function bindSchema(binding, schema, uploadEachUpdate = false, readback = false) {
        if (typeof schema != "table" || !("slot" in schema) || !("fields" in schema))
            throw "eve.ShaderSystem.bindSchema: schema requires slot and fields"
        if (typeof schema.slot != "string" || typeof schema.fields != "array" ||
            schema.fields.len() <= 0)
            throw "eve.ShaderSystem.bindSchema: invalid slot or fields"
        foreach (field in schema.fields)
            if (typeof field != "string")
                throw "eve.ShaderSystem.bindSchema: every field must be a string"
        local scalar = "scalar" in schema ? schema.scalar : "f32"
        if (scalar != "f32")
            throw "eve.ShaderSystem.bindSchema: only f32 is currently supported"
        local access = "access" in schema ? schema.access : "read_write"
        if (access != "read" && access != "write" && access != "read_write")
            throw "eve.ShaderSystem.bindSchema: access must be read, write, or read_write"
        local normalized = { slot = schema.slot, fields = schema.fields,
                             scalar = scalar, access = access,
                             strideFloats = schema.fields.len() }
        _bindings.push({ binding = binding, slot = schema.slot, fields = schema.fields,
                         uploadEachUpdate = access == "write" ? false : uploadEachUpdate,
                         readback = readback, uploadedRevision = -1,
                         uploadRequested = false, readbackRequested = false,
                         uploadFirst = 0, uploadCount = 0,
                         readbackFirst = 0, readbackCount = 0,
                         source = null, sourceBinding = -1,
                         initializeFromCpu = access != "write", schema = normalized })
        return this
    }

    function getBindingSchema(binding) {
        foreach (b in _bindings) if (b.binding == binding) return b.schema
        return null
    }

    // Generate backend declaration boilerplate; the kernel body remains explicit.
    function shaderDeclarations(language = "glsl") {
        local out = ""
        foreach (b in _bindings) {
            if (b.schema == null) continue
            if (language == "glsl") {
                local qualifier = b.schema.access == "read" ? "readonly " :
                                  b.schema.access == "write" ? "writeonly " : ""
                out += "layout(set = 0, binding = " + b.binding + ") " + qualifier +
                       "buffer EcsBinding" + b.binding + " { float data[]; } ecs" +
                       b.binding + ";\n"
            } else if (language == "wgsl") {
                local access = b.schema.access == "read" ? "read" : "read_write"
                out += "@group(0) @binding(" + b.binding + ") var<storage, " + access +
                       "> ecs" + b.binding + " : array<f32>;\n"
            } else {
                throw "eve.ShaderSystem.shaderDeclarations: language must be glsl or wgsl"
            }
        }
        return out
    }

    // Reuse another ShaderSystem's resident buffer without a CPU round trip.
    // The source system is retained and must be prepared before this consumer.
    function bindSharedFields(binding, sourceSystem, sourceBinding, slot, fields,
                              readback = false) {
        if (sourceSystem == null)
            throw "eve.ShaderSystem.bindSharedFields: source system is null"
        _bindings.push({ binding = binding, slot = slot, fields = fields,
                         uploadEachUpdate = false, readback = readback,
                         uploadedRevision = -1, uploadRequested = false,
                         readbackRequested = false, uploadFirst = 0, uploadCount = 0,
                         readbackFirst = 0, readbackCount = 0,
                         source = sourceSystem, sourceBinding = sourceBinding,
                         initializeFromCpu = false, schema = null })
        return this
    }

    function setBindingUpload(binding, enabled) {
        foreach (b in _bindings) if (b.binding == binding) b.uploadEachUpdate = enabled
        return this
    }

    function setBindingReadback(binding, enabled) {
        foreach (b in _bindings) if (b.binding == binding) b.readback = enabled
        return this
    }

    // Re-upload CPU component values on the next update. binding < 0 selects all.
    function requestUpload(binding = -1) {
        foreach (b in _bindings) {
            if ((binding < 0 || b.binding == binding) && b.source == null) {
                b.uploadRequested = true
                b.uploadFirst = 0
                b.uploadCount = -1
            }
        }
        return this
    }

    // Re-upload a stable contiguous entity range. Repeated requests are coalesced.
    function requestUploadRange(binding, firstEntity, entityCount) {
        if (firstEntity < 0 || entityCount <= 0)
            throw "eve.ShaderSystem.requestUploadRange: invalid entity range"
        foreach (b in _bindings) {
            if (b.binding != binding) continue
            if (b.source != null)
                throw "eve.ShaderSystem.requestUploadRange: shared binding is GPU-owned"
            if (!b.uploadRequested || b.uploadCount == 0) {
                b.uploadFirst = firstEntity
                b.uploadCount = entityCount
            } else if (b.uploadCount >= 0) {
                local oldEnd = b.uploadFirst + b.uploadCount
                local newEnd = firstEntity + entityCount
                local end = oldEnd > newEnd ? oldEnd : newEnd
                b.uploadFirst = b.uploadFirst < firstEntity ? b.uploadFirst : firstEntity
                b.uploadCount = end - b.uploadFirst
            }
            b.uploadRequested = true
        }
        return this
    }

    // Read GPU values back after the next update. binding < 0 selects all.
    function requestReadback(binding = -1) {
        foreach (b in _bindings) {
            if (binding < 0 || b.binding == binding) {
                b.readbackRequested = true
                b.readbackFirst = 0
                b.readbackCount = -1
            }
        }
        return this
    }

    // Read back a stable contiguous entity range. Repeated requests are coalesced.
    function requestReadbackRange(binding, firstEntity, entityCount) {
        if (firstEntity < 0 || entityCount <= 0)
            throw "eve.ShaderSystem.requestReadbackRange: invalid entity range"
        foreach (b in _bindings) {
            if (b.binding != binding) continue
            if (!b.readbackRequested || b.readbackCount == 0) {
                b.readbackFirst = firstEntity
                b.readbackCount = entityCount
            } else if (b.readbackCount >= 0) {
                local oldEnd = b.readbackFirst + b.readbackCount
                local newEnd = firstEntity + entityCount
                local end = oldEnd > newEnd ? oldEnd : newEnd
                b.readbackFirst = b.readbackFirst < firstEntity ? b.readbackFirst : firstEntity
                b.readbackCount = end - b.readbackFirst
            }
            b.readbackRequested = true
        }
        return this
    }

    function setFloat(index, value) {
        if (_backend != null) _backend.setFloat(index, value)
        return this
    }

    function getBackend() { return _backend }

    // Register renderer resources and submit a standard 80-byte GpuInstance binding
    // without copying the instance stream back through the CPU.
    function setGpuDrivenEnabled(enabled) {
        if (_gpu == null) throw "eve.ShaderSystem.setGpuDrivenEnabled: call setGpgpu first"
        _gpu.setGpuDrivenEnabled(enabled)
        return this
    }

    function gpuDrivenMeshRecord(mesh) {
        if (_gpu == null) throw "eve.ShaderSystem.gpuDrivenMeshRecord: call setGpgpu first"
        return _gpu.gpuDrivenMeshRecord(mesh)
    }

    function gpuDrivenMaterialRecord(material) {
        if (_gpu == null)
            throw "eve.ShaderSystem.gpuDrivenMaterialRecord: call setGpgpu first"
        return _gpu.gpuDrivenMaterialRecord(material)
    }

    function submitResidentInstances(binding, buckets, instanceCount = -1,
                                     offsetBytes = 0) {
        if (_gpu == null || _backend == null)
            throw "eve.ShaderSystem.submitResidentInstances: call setGpgpu first"
        local buffer = _backend.getBuffer(binding)
        if (buffer == null)
            throw "eve.ShaderSystem.submitResidentInstances: binding has no buffer"
        if (instanceCount < 0) instanceCount = entities().len()
        return _gpu.submitResidentInstances(buffer, buckets, instanceCount, offsetBytes)
    }

    function _prepareGpuBatch() {
        if (_backend == null || _gpu == null) return
        if (!("packEcsFloats" in eve) || !("unpackEcsFloats" in eve)) return

        local ents = entities()
        local n = ents.len()
        if (n <= 0) return null

        foreach (b in _bindings) {
            local floatsPer = b.fields.len()
            if (floatsPer <= 0) continue
            if (b.source != null) {
                local sourceBackend = b.source.getBackend()
                local sourceBuffer = sourceBackend == null ? null :
                                     sourceBackend.getBuffer(b.sourceBinding)
                if (sourceBuffer == null)
                    throw "eve.ShaderSystem shared source must be prepared before consumer"
                if (sourceBuffer.getSize() < n * floatsPer * 4)
                    throw "eve.ShaderSystem shared buffer is smaller than consumer view"
                _backend.attachBuffer(b.binding, sourceBuffer)
                continue
            }
            local buf = _backend.ensureBuffer(b.binding, n * floatsPer)
            local structureChanged = b.uploadedRevision != eve._ecsRevision
            local fullUpload = (structureChanged && (b.initializeFromCpu || b.uploadRequested)) ||
                               b.uploadEachUpdate ||
                               (b.uploadRequested && b.uploadCount < 0)
            if (fullUpload) {
                eve.packEcsFloats(ents, b.slot, b.fields, buf)
                b.uploadedRevision = eve._ecsRevision
                b.uploadRequested = false
                b.uploadCount = 0
            } else if (structureChanged) {
                // Output-only schema: capacity/order changed, but the shader is
                // authoritative and promises to initialize every accessed record.
                b.uploadedRevision = eve._ecsRevision
            } else if (b.uploadRequested) {
                if (!("packEcsFloatsRange" in eve))
                    throw "eve.ShaderSystem.requestUploadRange requires gpgpu range packing"
                eve.packEcsFloatsRange(ents, b.slot, b.fields, buf,
                                       b.uploadFirst, b.uploadCount)
                b.uploadRequested = false
                b.uploadCount = 0
            }
        }
        return { entities = ents, count = n }
    }

    function _completeGpuBatch(batch) {
        if (batch == null) return
        local ents = batch.entities
        local n = batch.count
        foreach (b in _bindings) {
            local shouldReadback = b.readback == null ? _readback : b.readback
            if (shouldReadback || (b.readbackRequested && b.readbackCount < 0)) {
                local floatsPer = b.fields.len()
                if (floatsPer <= 0) continue
                local buf = _backend.getBuffer(b.binding)
                eve.unpackEcsFloats(ents, b.slot, b.fields, buf, n)
                b.readbackRequested = false
                b.readbackCount = 0
            } else if (b.readbackRequested) {
                if (!("unpackEcsFloatsRange" in eve))
                    throw "eve.ShaderSystem.requestReadbackRange requires gpgpu range unpacking"
                local buf = _backend.getBuffer(b.binding)
                eve.unpackEcsFloatsRange(ents, b.slot, b.fields, buf,
                                         b.readbackFirst, b.readbackCount)
                b.readbackRequested = false
                b.readbackCount = 0
            }
        }
    }

    // Record into a shared GpuSequence. Call completeRecorded(batch) after submit().
    function record(sequence, dt) {
        if (sequence == null) throw "eve.ShaderSystem.record: sequence is null"
        local batch = _prepareGpuBatch()
        if (batch != null) _backend.recordDispatch(sequence, batch.count, dt)
        return batch
    }

    function completeRecorded(batch) {
        _completeGpuBatch(batch)
        return this
    }

    function update(dt) {
        local batch = _prepareGpuBatch()
        if (batch == null) return
        _backend.dispatch(batch.count, dt)
        _completeGpuBatch(batch)
    }
}

function eve::view(entityClass) {
    if (entityClass == null) return []
    // C++ 桥接类的实体在脚本外增删（无法通知脚本），不缓存，每次新鲜收集
    local cppBacked = false
    if ("_cppHasView" in this) cppBacked = this._cppHasView(entityClass)

    // 每次调用都返回**新数组**：缓存只保存脚本侧收集结果（遍历 _ecsTypes 才是贵的
    // 那一步），绝不外借。否则调用方 sort/pop/append 自己的视图就会污染其他使用者。
    local out = []
    if (cppBacked) {
        this._ecsCollectInstances(entityClass, out)
    } else {
        if (!(entityClass in this._ecsViewCache)) {
            local collected = []
            this._ecsCollectInstances(entityClass, collected)
            this._ecsViewCache[entityClass] <- collected
        }
        local cached = this._ecsViewCache[entityClass]
        local count = cached.len()
        out.resize(count)
        for (local i = 0; i < count; ++i) out[i] = cached[i]
    }
    if ("_cppCollect" in this) this._cppCollect(entityClass, out)
    // No sort here. Entities appended by a registered C++ collector
    // (registerCppEntityView) are not `eve.Entity` instances and have no `_eid`; sorting them
    // threw "the index '_eid' does not exist" (caught by ECS.script.cppCreatedEntitiesVisible).
    // The collector owns their order; the id-ordered contract covers script instances, which
    // `_ecsCollectInstances` already sorted.
    return out
}

function eve::ecsReady() {
    // This function is installed only after the complete ECS script has run.
    // Avoid probing the delegated eve table here: Squirrel's `in` lookup may
    // invoke its lazy _get handler and turn a readiness query into an error.
    return true
}
)SQ";

void injectEcsScript(ssq::Table& eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kEcsScript,
                                   static_cast<SQInteger>(std::strlen(kEcsScript)),
                                   "ecs.nut", SQTrue))) {
        sq_settop(vm, top);
        const script::ScriptErrorContext ctx = script::captureCompileError(vm);
        std::fprintf(stderr, "EVEngine: ECS bootstrap script failed to compile: %s\n",
                     ctx.empty() ? "unknown error" : ctx.message.c_str());
        return;
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
        const script::ScriptErrorContext ctx = script::takeLastScriptError(vm);
        std::fprintf(stderr, "EVEngine: ECS bootstrap script failed to run: %s\n",
                     ctx.empty() ? "unknown error"
                                 : script::formatScriptError(ctx).c_str());
    }
    sq_settop(vm, top);
}

/**
 * Script hook for eve.view(): walk the script class chain (cls → base → …) and,
 * at the first class registered via registerCppEntityView(), invoke its collector
 * to append the matching C++ entities to `out`.
 */
void cppCollect(ssq::Class cls, ssq::Array out) {
    HSQUIRRELVM vm = cls.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, cls.getRaw());
    for (int guard = 0; guard < 64; ++guard) {
        if (sq_gettype(vm, -1) != OT_CLASS)
            break;
        HSQOBJECT cur;
        sq_getstackobj(vm, -1, &cur);
        auto it = cppEntityViews().find(cur._unVal.pClass);
        if (it != cppEntityViews().end()) {
            sq_settop(vm, top);
            it->second(out);
            return;
        }
        if (SQ_FAILED(sq_getbase(vm, -1)))
            break;
        sq_remove(vm, -2);  // drop current class, keep base
    }
    sq_settop(vm, top);
}

/**
 * Script hook for view-cache policy: returns true when any class on the chain
 * (cls → base → …) is registered via registerCppEntityView(). C++ entities can
 * appear/disappear outside script knowledge, so such views must not be cached.
 */
bool cppHasView(ssq::Object cls) {
    if (cls.getType() != ssq::Type::CLASS) return false;
    HSQUIRRELVM vm = cls.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, cls.getRaw());
    for (int guard = 0; guard < 64; ++guard) {
        if (sq_gettype(vm, -1) != OT_CLASS) break;
        HSQOBJECT cur;
        sq_getstackobj(vm, -1, &cur);
        if (cppEntityViews().find(cur._unVal.pClass) != cppEntityViews().end()) {
            sq_settop(vm, top);
            return true;
        }
        if (SQ_FAILED(sq_getbase(vm, -1))) break;
        sq_remove(vm, -2);  // drop current class, keep base
    }
    sq_settop(vm, top);
    return false;
}

}  // namespace

Result<Value> readScriptComponentField(const ssq::Object& instance, const std::string& field) {
    if (instance.getType() != ssq::Type::INSTANCE)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "component field access needs a component instance", field, {},
                                                        "ecs.component"));
    HSQUIRRELVM vm = instance.getHandle();
    if (vm == nullptr)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "component field access needs a live script VM", field, {},
                                                        "ecs.component"));

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());

    ScriptComponentStoreKey key;
    if (instanceIdentity(vm, -1, &key)) {  // 登记过：字段在引擎侧存储里
        Result<Value> stored = scriptComponentField(key, field);
        sq_settop(vm, top);
        return stored;
    }

    // 没登记（或登记了但没绑定实体）：槽位语义，直接读实例成员。这里的 `sq_get` 对实例其实也会
    // 兜底跑 `_get`（`FallBackGet` 的 OT_INSTANCE 分支），但未登记组件的类上根本没装那个元方法；
    // 直读槽位既是最便宜的，也不依赖"谁装了什么"。
    sq_pushstring(vm, field.c_str(), static_cast<SQInteger>(field.size()));
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "script component instance has no such field", field, {}, "ecs.component"));
    }
    Result<Value> slot = script::valueFromSquirrel(vm, -1);
    sq_settop(vm, top);
    return slot;
}

Result<void> writeScriptComponentField(const ssq::Object& instance, const std::string& field, const Value& value) {
    if (instance.getType() != ssq::Type::INSTANCE)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "component field access needs a component instance", field, {},
                                                       "ecs.component"));
    HSQUIRRELVM vm = instance.getHandle();
    if (vm == nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "component field access needs a live script VM", field, {},
                                                       "ecs.component"));

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());

    ScriptComponentStoreKey key;
    if (instanceIdentity(vm, -1, &key)) {  // 登记过：写引擎侧存储（脚本侧下一次读就看见）
        Result<void> written = setScriptComponentField(key, field, value);
        sq_settop(vm, top);
        return written;
    }

    sq_pushstring(vm, field.c_str(), static_cast<SQInteger>(field.size()));
    Result<void> pushed = script::pushValue(vm, value);
    if (!pushed.ok()) {
        sq_settop(vm, top);
        return pushed;
    }
    // `sq_set`（而非 `sq_rawset`）：失败时只返回 SQ_ERROR，不会顺手把 VM 的 last-error 也设上，
    // 调用方拿到的是一条干净的 `NotFound`。
    if (SQ_FAILED(sq_set(vm, -3))) {
        sq_settop(vm, top);
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "script component instance has no such field", field, {}, "ecs.component"));
    }
    sq_settop(vm, top);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> registerScriptComponent(const std::string& name, SchemaVersion version, const ssq::Object& cls) {
    if (name.empty())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "script component name must not be empty", {}, {}, "ecs.component"));
    if (version.isZero())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "script component version must be a positive integer", name, {},
                                                       "ecs.component"));
    if (cls.getType() != ssq::Type::CLASS)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "eve.component needs a class, not a value or instance", name, {},
                                                       "ecs.component"));

    HSQUIRRELVM vm = cls.getHandle();
    HSQOBJECT   o  = cls.getRaw();
    sq_addref(vm, &o);

    // 登记时读出这一代声明的字段（按声明顺序）：它既是组件的 schema，也是"取走字段槽位、
    // 让引擎成为权威存储"要用的清单——必须先读出来，才能安全地摘掉。
    const std::vector<DeclaredField> fields = declaredFields(o);
    const std::vector<std::string>   names  = fieldNames(fields);

    // 声明默认值也是 schema 的一部分，而且**只能在摘槽位之前**读：摘掉之后类和实例上都没有它了，
    // 而建引擎侧数据条目、以及将来热重载迁移换代都要按这个默认值来。
    std::vector<Value> defaults;
    defaults.reserve(names.size());
    for (const std::string& field : names) {
        Result<Value> value = classFieldDefault(vm, o, field);
        defaults.push_back(value.ok() ? std::move(value).takeValue() : Value::null());
    }

    // 摘槽位 + 装 `_get`/`_set`：从这一刻起这个组件的字段读写都进引擎侧存储。用户类自己
    // 定义了 `_get`/`_set` 时这里返回 AlreadyExists，且**不做任何改动**（不覆盖、不摘槽位）。
    Result<void> seam = installComponentFieldSeam(vm, o, fields);
    if (!seam.ok()) {
        sq_release(vm, &o);
        return seam;
    }

    auto&      current = currentScriptComponents();
    const auto key     = ScriptComponentKey{name, version.value()};
    auto       found   = current.find(key);
    if (found == current.end()) {
        current.emplace(key, ScriptComponentEntry(vm, o, Generation(1), names, std::move(defaults)));
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    // Same name and version again: hot reload redefined the class. Record the retired
    // generation (metadata only) and advance the counter, so a caller can tell that the
    // identity it holds refers to an older generation of the same component. The retired
    // row keeps the *previous* field list: migration needs to know which fields it moves.
    const Generation previous = found->second.generation;
    retiredScriptComponents().push_back(ScriptComponentRecord{name, version, previous, true, found->second.fields});
    const auto next = previous.incremented();
    if (!next)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                       "script component generation counter is exhausted", name, {},
                                                       "ecs.component"));
    // 同名同版本再登记 = 热重载换了一代：已存在的字段数据要按**新 schema** 对齐——两代都有的
    // 字段保留旧值、新代新增的字段补声明默认值、旧代删掉的字段丢弃。少了这一步，脚本改一次
    // 类定义就会出现两种静默错误：新加的字段永远读不到（存储里没有它），被删掉的字段一直留着
    // 并被序列化/编辑器看见。字段数据按 (实体, 名字, 版本) 寻址，所以换代时能就地迁移。
    reconcileScriptComponentValues(name, version, names, defaults);

    // Only release a reference belonging to *this* VM: a stale entry from another VM must
    // not be released, because that VM may already be destroyed and sq_release() would
    // dereference its freed RefTable (measured access violation, plan doc §9.49).
    if (found->second.vm == vm) sq_release(found->second.vm, &found->second.cls);
    found->second = ScriptComponentEntry(vm, o, *next, names, std::move(defaults));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

const ScriptComponentRecord* findScriptComponent(const std::string& name, SchemaVersion version) {
    static ScriptComponentRecord scratch;
    const auto&                  current = currentScriptComponents();
    const auto                   found   = current.find(ScriptComponentKey{name, version.value()});
    if (found == current.end()) return nullptr;
    scratch = ScriptComponentRecord{found->first.first, version, found->second.generation, false, found->second.fields};
    return &scratch;
}

std::vector<ScriptComponentRecord> scriptComponentRecords() {
    std::vector<ScriptComponentRecord> out;
    out.reserve(currentScriptComponents().size() + retiredScriptComponents().size());
    for (const auto& [key, entry] : currentScriptComponents())
        out.push_back(
            ScriptComponentRecord{key.first, SchemaVersion(key.second), entry.generation, false, entry.fields});
    for (const ScriptComponentRecord& retired : retiredScriptComponents()) out.push_back(retired);
    std::sort(out.begin(), out.end(), [](const ScriptComponentRecord& a, const ScriptComponentRecord& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.version != b.version) return a.version < b.version;
        return a.generation < b.generation;
    });
    return out;
}

Result<ScriptComponentDeclaredSchema> scriptComponentDeclaredSchema(const std::string& name, SchemaVersion version) {
    const auto& current = currentScriptComponents();
    const auto  found   = current.find(ScriptComponentKey{name, version.value()});
    if (found == current.end())
        return Result<ScriptComponentDeclaredSchema>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "script component is not registered in this VM", name, {}, "ecs.component"));

    ScriptComponentDeclaredSchema out;
    out.name       = found->first.first;
    out.version    = version;
    out.generation = found->second.generation;
    out.fields     = found->second.fields;
    out.defaults   = found->second.defaults;
    return Result<ScriptComponentDeclaredSchema>::success(std::move(out));
}

void exposeECS(ssq::Table& table) {
    table.set("Number", std::string("number"));
    table.set("Boolean", std::string("boolean"));
    table.set("String", std::string("string"));
    // ecsReady / view / Entity / Component are provided by the injected script.

    table.addFunc("inspectClassFields", inspectClassFields);
    table.addFunc("isSubclass", scriptIsSubclass);
    table.addFunc("getClassBase", getClassBase);
    table.addFunc("allocEntityId", allocEntityId);
    table.addFunc("_cppHasView", cppHasView);
    // `eve.component(name, version, cls)` registers the component's *identity*. It is
    // optional today (classes keep working unregistered), but it is what lets a hot
    // reload match a redefined class to the instances created by the previous one.
    table.addFunc("component", [vm = table.getHandle()](std::string name, int version, ssq::Object cls) {
        return script::projectResult(
            vm, registerScriptComponent(name, SchemaVersion(static_cast<std::uint64_t>(version)), cls));
    });
    table.addFunc("_cppCollect", std::function<void(ssq::Class, ssq::Array)>(cppCollect));
    // 已登记组件在引擎侧的数据：实例化时盖归属、按 schema 建条目（`_ecsBindComponentValues`），
    // 销毁实体时一并清掉。返回这一代的**字段名数组**，未登记时返回 null——脚本侧据此保持
    // 纯槽位语义，并在槽位已被摘掉的情况下仍能按声明顺序解析类型标记。
    table.addFunc("_ecsBindComponentValues",
                  [](std::uint64_t entityId, ssq::Object instance) { return bindComponentValues(entityId, instance); });
    table.addFunc("_ecsDropComponentValues",
                  [](std::uint64_t entityId) { return dropScriptComponentValues(entityId); });

    injectEcsScript(table);

    ecsScriptInjected() = true;
    // A new VM re-runs every hook (SceneEntity class, …) against this table.
    postEcsHooksFlushed() = 0;
    flushPostEcsHooks(table);
}

void prepareEcsScriptInjection() {
    // Class identities belong to one VM. Clear them when a new eve table is
    // prepared, before any lazy/native exposer registers the next VM's classes.
    cppEntityViews().clear();
    // Script component registrations (eve.component) hold a Squirrel reference
    // into the VM that made them. By the time the next table is prepared that VM
    // is already destroyed, so the entries must be dropped **without** releasing:
    // sq_release() on a destroyed VM is a use-after-free (see the plan doc §9.49).
    // The retired-generation history is per VM too: generations count reloads of one
    // VM's script, so a new VM starts with no identity history.
    currentScriptComponents().clear();
    retiredScriptComponents().clear();
    // 引擎侧组件数据同样属于"一个 VM 的脚本世界"：新 VM 从空开始。
    clearScriptComponentStore();
    ecsScriptInjected()  = false;
    postEcsHooksFlushed() = 0;
}

void registerCppEntityView(const ssq::Class& cls, CppEntityViewFn fn) {
    cppEntityViews()[cls.getRaw()._unVal.pClass] = std::move(fn);
}

void registerPostEcsHook(PostEcsHook fn) {
    postEcsHooks().push_back(std::move(fn));
}

void flushPostEcsHooks(ssq::Table& table) {
    if (!ecsScriptInjected()) return;
    auto& hooks = postEcsHooks();
    auto& n     = postEcsHooksFlushed();
    while (n < hooks.size()) {
        try {
            hooks[n](table);
        } catch (...) {
            // A failing module hook must not break VM exposure.
        }
        ++n;
    }
}

void exposeECSToVM(ssq::VM& vm) {
    try {
        ssq::Table eveTable(vm.find("eve"));
        exposeECS(eveTable);
    } catch (...) {
    }
}

}  // namespace eve
